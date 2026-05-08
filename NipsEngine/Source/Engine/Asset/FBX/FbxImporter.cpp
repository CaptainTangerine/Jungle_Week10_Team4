#include "FbxImporter.h"

#include "Math/Utils.h"

namespace
{
    FVector ToVector(const FbxVector4& Vector)
    {
        return FVector(
            static_cast<float>(Vector[0]),
            static_cast<float>(Vector[1]),
            static_cast<float>(Vector[2]));
    }

    FVector2 ToVector2(const FbxVector2& Vector)
    {
        return FVector2(
            static_cast<float>(Vector[0]),
            1.0f - static_cast<float>(Vector[1]));
    }

    void HashCombine(size_t& Seed, size_t HashValue)
    {
        Seed ^= HashValue + 0x9e3779b9 + (Seed << 6) + (Seed >> 2);
    }

    void BuildFallbackBasis(const FVector& InNormal, FVector& OutTangent, FVector& OutBitangent)
    {
        FVector Normal = InNormal.GetSafeNormal();
        if (Normal.IsNearlyZero())
        {
            Normal = FVector(0.0f, 0.0f, 1.0f);
        }

        Normal.FindBestAxisVectors(OutTangent, OutBitangent);
        OutTangent = OutTangent.GetSafeNormal();
        OutBitangent = OutBitangent.GetSafeNormal();
    }
}

size_t FFbxImporter::FVertexKeyHasher::operator()(const FVertexKey& Key) const noexcept
{
    size_t Seed = 0;
    HashCombine(Seed, std::hash<FVector>{}(Key.Position));
    HashCombine(Seed, std::hash<FVector>{}(Key.Normal));
    HashCombine(Seed, std::hash<FVector2>{}(Key.UVs));
    return Seed;
}

FStaticMesh* FFbxImporter::ImportStaticMesh(FbxScene* Scene)
{
    FStaticMesh* StaticMesh = new FStaticMesh();
    if (!ImportStaticMesh(Scene, *StaticMesh))
    {
        delete StaticMesh;
        return nullptr;
    }

    return StaticMesh;
}

bool FFbxImporter::ImportStaticMesh(FbxScene* Scene, FStaticMesh& OutStaticMesh)
{
    ResetStaticMesh(OutStaticMesh);
    UniqueVertices.clear();

    if (Scene == nullptr || Scene->GetRootNode() == nullptr)
    {
        return false;
    }

    ProcessNode(Scene->GetRootNode(), OutStaticMesh);

    if (OutStaticMesh.Vertices.empty() || OutStaticMesh.Indices.empty())
    {
        return false;
    }

    BuildDefaultSection(OutStaticMesh);
    BuildTangentsAndBitangents(OutStaticMesh);
    OutStaticMesh.LocalBounds = BuildLocalBounds(OutStaticMesh);

    return true;
}

void FFbxImporter::ProcessNode(FbxNode* Node, FStaticMesh& OutStaticMesh)
{
    if (Node == nullptr)
    {
        return;
    }

    FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
    if (Attribute != nullptr && Attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
    {
        ProcessMesh(Node, Node->GetMesh(), OutStaticMesh);
    }

    const int32 ChildCount = Node->GetChildCount();
    for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
    {
        ProcessNode(Node->GetChild(ChildIndex), OutStaticMesh);
    }
}

void FFbxImporter::ProcessMesh(FbxNode* Node, FbxMesh* Mesh, FStaticMesh& OutStaticMesh)
{
    if (Node == nullptr || Mesh == nullptr)
    {
        return;
    }

    FbxStringList UVSetNameList;
    Mesh->GetUVSetNames(UVSetNameList);
    const char* UVSetName = UVSetNameList.GetCount() > 0 ? UVSetNameList.GetStringAt(0) : nullptr;

    const FbxAMatrix NodeTransform = Node->EvaluateGlobalTransform();
    const int32 PolygonCount = Mesh->GetPolygonCount();

    for (int32 PolygonIndex = 0; PolygonIndex < PolygonCount; ++PolygonIndex)
    {
        const int32 PolygonSize = Mesh->GetPolygonSize(PolygonIndex);
        if (PolygonSize < 3)
        {
            continue;
        }

        for (int32 TriangleIndex = 0; TriangleIndex < PolygonSize - 2; ++TriangleIndex)
        {
            const uint32 Index0 = AddPolygonVertex(Mesh, NodeTransform, UVSetName, PolygonIndex, 0, OutStaticMesh);
            const uint32 Index1 = AddPolygonVertex(Mesh, NodeTransform, UVSetName, PolygonIndex, TriangleIndex + 1, OutStaticMesh);
            const uint32 Index2 = AddPolygonVertex(Mesh, NodeTransform, UVSetName, PolygonIndex, TriangleIndex + 2, OutStaticMesh);

            OutStaticMesh.Indices.push_back(Index0);
            OutStaticMesh.Indices.push_back(Index1);
            OutStaticMesh.Indices.push_back(Index2);
        }
    }
}

uint32 FFbxImporter::AddPolygonVertex(
    FbxMesh* Mesh,
    const FbxAMatrix& NodeTransform,
    const char* UVSetName,
    int32 PolygonIndex,
    int32 VertexInPolygon,
    FStaticMesh& OutStaticMesh)
{
    FNormalVertex Vertex = {};

    const int32 ControlPointIndex = Mesh->GetPolygonVertex(PolygonIndex, VertexInPolygon);
    const FbxVector4 FbxPosition = Mesh->GetControlPointAt(ControlPointIndex);
    Vertex.Position = ToVector(NodeTransform.MultT(FbxPosition));

    FbxVector4 FbxNormal(0.0, 0.0, 1.0, 0.0);
    if (Mesh->GetPolygonVertexNormal(PolygonIndex, VertexInPolygon, FbxNormal))
    {
        FbxNormal = NodeTransform.MultR(FbxNormal);
        FbxNormal.Normalize();
    }
    Vertex.Normal = ToVector(FbxNormal).GetSafeNormal();
    if (Vertex.Normal.IsNearlyZero())
    {
        Vertex.Normal = FVector(0.0f, 0.0f, 1.0f);
    }

    FbxVector2 FbxUV(0.0, 0.0);
    bool bUnmapped = true;
    if (UVSetName != nullptr)
    {
        Mesh->GetPolygonVertexUV(PolygonIndex, VertexInPolygon, UVSetName, FbxUV, bUnmapped);
    }
    Vertex.UVs = bUnmapped ? FVector2(0.0f, 0.0f) : ToVector2(FbxUV);

    Vertex.Color = FColor::White();

    return GetOrCreateVertexIndex(Vertex, OutStaticMesh);
}

uint32 FFbxImporter::GetOrCreateVertexIndex(const FNormalVertex& Vertex, FStaticMesh& OutStaticMesh)
{
    FVertexKey Key = {};
    Key.Position = Vertex.Position;
    Key.Normal = Vertex.Normal;
    Key.UVs = Vertex.UVs;

    auto It = UniqueVertices.find(Key);
    if (It != UniqueVertices.end())
    {
        return It->second;
    }

    const uint32 NewIndex = static_cast<uint32>(OutStaticMesh.Vertices.size());
    OutStaticMesh.Vertices.push_back(Vertex);
    UniqueVertices.emplace(Key, NewIndex);
    return NewIndex;
}

void FFbxImporter::ResetStaticMesh(FStaticMesh& OutStaticMesh) const
{
    OutStaticMesh.Vertices.clear();
    OutStaticMesh.Indices.clear();
    OutStaticMesh.Sections.clear();
    OutStaticMesh.Slots.clear();
    OutStaticMesh.LocalBounds.Reset();
    OutStaticMesh.RenderData.VertexBuffer = nullptr;
    OutStaticMesh.RenderData.IndexBuffer = nullptr;
}

void FFbxImporter::BuildDefaultSection(FStaticMesh& OutStaticMesh) const
{
    FStaticMeshMaterialSlot Slot = {};
    Slot.SlotName = FString("DefaultWhite");
    Slot.Material = nullptr;
    OutStaticMesh.Slots.push_back(Slot);

    FStaticMeshSection Section = {};
    Section.StartIndex = 0;
    Section.IndexCount = static_cast<uint32>(OutStaticMesh.Indices.size());
    Section.MaterialSlotIndex = 0;
    OutStaticMesh.Sections.push_back(Section);
}

void FFbxImporter::BuildTangentsAndBitangents(FStaticMesh& OutStaticMesh) const
{
    if (OutStaticMesh.Vertices.empty())
    {
        return;
    }

    TArray<FVector> AccumulatedTangents(OutStaticMesh.Vertices.size(), FVector::ZeroVector);
    TArray<FVector> AccumulatedBitangents(OutStaticMesh.Vertices.size(), FVector::ZeroVector);

    for (size_t TriangleIndex = 0; TriangleIndex + 2 < OutStaticMesh.Indices.size(); TriangleIndex += 3)
    {
        const uint32 Index0 = OutStaticMesh.Indices[TriangleIndex + 0];
        const uint32 Index1 = OutStaticMesh.Indices[TriangleIndex + 1];
        const uint32 Index2 = OutStaticMesh.Indices[TriangleIndex + 2];

        if (Index0 >= OutStaticMesh.Vertices.size() ||
            Index1 >= OutStaticMesh.Vertices.size() ||
            Index2 >= OutStaticMesh.Vertices.size())
        {
            continue;
        }

        const FNormalVertex& Vertex0 = OutStaticMesh.Vertices[Index0];
        const FNormalVertex& Vertex1 = OutStaticMesh.Vertices[Index1];
        const FNormalVertex& Vertex2 = OutStaticMesh.Vertices[Index2];

        const FVector Edge01 = Vertex1.Position - Vertex0.Position;
        const FVector Edge02 = Vertex2.Position - Vertex0.Position;
        const FVector2 UV01 = Vertex1.UVs - Vertex0.UVs;
        const FVector2 UV02 = Vertex2.UVs - Vertex0.UVs;

        const float Determinant = UV01.X * UV02.Y - UV01.Y * UV02.X;
        if (MathUtil::Abs(Determinant) <= MathUtil::Epsilon)
        {
            continue;
        }

        const float InverseDeterminant = 1.0f / Determinant;
        const FVector TriangleTangent = (Edge01 * UV02.Y - Edge02 * UV01.Y) * InverseDeterminant;
        const FVector TriangleBitangent = (Edge02 * UV01.X - Edge01 * UV02.X) * InverseDeterminant;

        if (TriangleTangent.IsNearlyZero() || TriangleBitangent.IsNearlyZero())
        {
            continue;
        }

        AccumulatedTangents[Index0] += TriangleTangent;
        AccumulatedTangents[Index1] += TriangleTangent;
        AccumulatedTangents[Index2] += TriangleTangent;

        AccumulatedBitangents[Index0] += TriangleBitangent;
        AccumulatedBitangents[Index1] += TriangleBitangent;
        AccumulatedBitangents[Index2] += TriangleBitangent;
    }

    for (size_t VertexIndex = 0; VertexIndex < OutStaticMesh.Vertices.size(); ++VertexIndex)
    {
        FNormalVertex& Vertex = OutStaticMesh.Vertices[VertexIndex];
        FVector Normal = Vertex.Normal.GetSafeNormal();
        if (Normal.IsNearlyZero())
        {
            Normal = FVector(0.0f, 0.0f, 1.0f);
        }

        FVector Tangent = AccumulatedTangents[VertexIndex] - Normal * FVector::DotProduct(Normal, AccumulatedTangents[VertexIndex]);
        if (!Tangent.Normalize())
        {
            BuildFallbackBasis(Normal, Tangent, Vertex.Bitangent);
            Vertex.Tangent = Tangent;
            continue;
        }

        FVector Bitangent = AccumulatedBitangents[VertexIndex];
        Bitangent = Bitangent - Normal * FVector::DotProduct(Normal, Bitangent);
        Bitangent = Bitangent - Tangent * FVector::DotProduct(Tangent, Bitangent);

        if (!Bitangent.Normalize())
        {
            Bitangent = FVector::CrossProduct(Normal, Tangent);
            if (!Bitangent.Normalize())
            {
                BuildFallbackBasis(Normal, Tangent, Bitangent);
            }
        }

        Vertex.Tangent = Tangent;
        Vertex.Bitangent = Bitangent;
    }
}

FAABB FFbxImporter::BuildLocalBounds(const FStaticMesh& StaticMesh) const
{
    FAABB Bounds;
    Bounds.Reset();

    for (const FNormalVertex& Vertex : StaticMesh.Vertices)
    {
        Bounds.Expand(Vertex.Position);
    }

    return Bounds;
}
