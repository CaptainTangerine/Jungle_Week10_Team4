#include "FbxImporter.h"

#include "Math/Utils.h"

#include <algorithm>

namespace
{
    constexpr float MinBoneWeight = 0.0001f;

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

    FMatrix ToMatrix(const FbxAMatrix& Matrix)
    {
        return FMatrix(
            static_cast<float>(Matrix.Get(0, 0)),
            static_cast<float>(Matrix.Get(0, 1)),
            static_cast<float>(Matrix.Get(0, 2)),
            static_cast<float>(Matrix.Get(0, 3)),
            static_cast<float>(Matrix.Get(1, 0)),
            static_cast<float>(Matrix.Get(1, 1)),
            static_cast<float>(Matrix.Get(1, 2)),
            static_cast<float>(Matrix.Get(1, 3)),
            static_cast<float>(Matrix.Get(2, 0)),
            static_cast<float>(Matrix.Get(2, 1)),
            static_cast<float>(Matrix.Get(2, 2)),
            static_cast<float>(Matrix.Get(2, 3)),
            static_cast<float>(Matrix.Get(3, 0)),
            static_cast<float>(Matrix.Get(3, 1)),
            static_cast<float>(Matrix.Get(3, 2)),
            static_cast<float>(Matrix.Get(3, 3)));
    }

    FbxAMatrix GetGeometryTransform(FbxNode* Node)
    {
        if (Node == nullptr)
        {
            FbxAMatrix Identity;
            Identity.SetIdentity();
            return Identity;
        }

        const FbxVector4 Translation = Node->GetGeometricTranslation(FbxNode::eSourcePivot);
        const FbxVector4 Rotation = Node->GetGeometricRotation(FbxNode::eSourcePivot);
        const FbxVector4 Scaling = Node->GetGeometricScaling(FbxNode::eSourcePivot);
        return FbxAMatrix(Translation, Rotation, Scaling);
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

bool FFbxImporter::FSkeletalVertexKey::operator==(const FSkeletalVertexKey& Other) const
{
    if (Position != Other.Position || Normal != Other.Normal || UVs != Other.UVs)
    {
        return false;
    }

    for (uint32 InfluenceIndex = 0; InfluenceIndex < FSkeletalVertex::MaxBoneInfluences; ++InfluenceIndex)
    {
        if (BoneIndices[InfluenceIndex] != Other.BoneIndices[InfluenceIndex] ||
            BoneWeights[InfluenceIndex] != Other.BoneWeights[InfluenceIndex])
        {
            return false;
        }
    }

    return true;
}

size_t FFbxImporter::FSkeletalVertexKeyHasher::operator()(const FSkeletalVertexKey& Key) const noexcept
{
    size_t Seed = 0;
    HashCombine(Seed, std::hash<FVector>{}(Key.Position));
    HashCombine(Seed, std::hash<FVector>{}(Key.Normal));
    HashCombine(Seed, std::hash<FVector2>{}(Key.UVs));

    for (uint32 InfluenceIndex = 0; InfluenceIndex < FSkeletalVertex::MaxBoneInfluences; ++InfluenceIndex)
    {
        HashCombine(Seed, std::hash<uint32>{}(Key.BoneIndices[InfluenceIndex]));
        HashCombine(Seed, std::hash<float>{}(Key.BoneWeights[InfluenceIndex]));
    }

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

FSkeletalMesh* FFbxImporter::ImportSkeletalMesh(FbxScene* Scene)
{
    FSkeletalMesh* SkeletalMesh = new FSkeletalMesh();
    if (!ImportSkeletalMesh(Scene, *SkeletalMesh))
    {
        delete SkeletalMesh;
        return nullptr;
    }

    return SkeletalMesh;
}

bool FFbxImporter::ImportSkeletalMesh(FbxScene* Scene, FSkeletalMesh& OutSkeletalMesh)
{
    ResetSkeletalMesh(OutSkeletalMesh);
    UniqueSkeletalVertices.clear();
    BoneIndexByNode.clear();

    if (Scene == nullptr || Scene->GetRootNode() == nullptr)
    {
        return false;
    }

    ProcessSkeletonNode(Scene->GetRootNode(), -1, OutSkeletalMesh);
    if (OutSkeletalMesh.Bones.empty())
    {
        FSkeletonBone RootBone = {};
        RootBone.Name = FString("Root");
        OutSkeletalMesh.Bones.push_back(RootBone);
    }

    ProcessSkeletalNode(Scene->GetRootNode(), OutSkeletalMesh);

    if (OutSkeletalMesh.Vertices.empty() || OutSkeletalMesh.Indices.empty())
    {
        return false;
    }

    BuildDefaultSection(OutSkeletalMesh);
    BuildTangentsAndBitangents(OutSkeletalMesh);
    OutSkeletalMesh.LocalBounds = BuildLocalBounds(OutSkeletalMesh);

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

void FFbxImporter::ProcessSkeletonNode(FbxNode* Node, int32 ParentIndex, FSkeletalMesh& OutSkeletalMesh)
{
    if (Node == nullptr)
    {
        return;
    }

    int32 CurrentParentIndex = ParentIndex;
    FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
    if (Attribute != nullptr && Attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
    {
        const uint32 BoneIndex = GetOrCreateBoneIndex(Node, OutSkeletalMesh);
        OutSkeletalMesh.Bones[BoneIndex].ParentIndex = ParentIndex;
        CurrentParentIndex = static_cast<int32>(BoneIndex);
    }

    const int32 ChildCount = Node->GetChildCount();
    for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
    {
        ProcessSkeletonNode(Node->GetChild(ChildIndex), CurrentParentIndex, OutSkeletalMesh);
    }
}

void FFbxImporter::ProcessSkeletalNode(FbxNode* Node, FSkeletalMesh& OutSkeletalMesh)
{
    if (Node == nullptr)
    {
        return;
    }

    FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
    if (Attribute != nullptr && Attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
    {
        ProcessSkeletalMesh(Node, Node->GetMesh(), OutSkeletalMesh);
    }

    const int32 ChildCount = Node->GetChildCount();
    for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
    {
        ProcessSkeletalNode(Node->GetChild(ChildIndex), OutSkeletalMesh);
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
    const FbxAMatrix MeshTransform = NodeTransform * GetGeometryTransform(Node);
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
            const uint32 Index0 = AddPolygonVertex(Mesh, MeshTransform, UVSetName, PolygonIndex, 0, OutStaticMesh);
            const uint32 Index1 = AddPolygonVertex(Mesh, MeshTransform, UVSetName, PolygonIndex, TriangleIndex + 1, OutStaticMesh);
            const uint32 Index2 = AddPolygonVertex(Mesh, MeshTransform, UVSetName, PolygonIndex, TriangleIndex + 2, OutStaticMesh);

            OutStaticMesh.Indices.push_back(Index0);
            OutStaticMesh.Indices.push_back(Index1);
            OutStaticMesh.Indices.push_back(Index2);
        }
    }
}

void FFbxImporter::ProcessSkeletalMesh(FbxNode* Node, FbxMesh* Mesh, FSkeletalMesh& OutSkeletalMesh)
{
    if (Node == nullptr || Mesh == nullptr)
    {
        return;
    }

    FbxStringList UVSetNameList;
    Mesh->GetUVSetNames(UVSetNameList);
    const char* UVSetName = UVSetNameList.GetCount() > 0 ? UVSetNameList.GetStringAt(0) : nullptr;

    const FbxAMatrix NodeTransform = Node->EvaluateGlobalTransform();
    const FbxAMatrix MeshTransform = NodeTransform * GetGeometryTransform(Node);
    const TArray<FControlPointSkinData> ControlPointSkinData = BuildControlPointSkinData(Mesh, OutSkeletalMesh);
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
            const uint32 Index0 = AddSkeletalPolygonVertex(Mesh, MeshTransform, UVSetName, PolygonIndex, 0, ControlPointSkinData, OutSkeletalMesh);
            const uint32 Index1 = AddSkeletalPolygonVertex(Mesh, MeshTransform, UVSetName, PolygonIndex, TriangleIndex + 1, ControlPointSkinData, OutSkeletalMesh);
            const uint32 Index2 = AddSkeletalPolygonVertex(Mesh, MeshTransform, UVSetName, PolygonIndex, TriangleIndex + 2, ControlPointSkinData, OutSkeletalMesh);

            OutSkeletalMesh.Indices.push_back(Index0);
            OutSkeletalMesh.Indices.push_back(Index1);
            OutSkeletalMesh.Indices.push_back(Index2);
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

uint32 FFbxImporter::AddSkeletalPolygonVertex(
    FbxMesh* Mesh,
    const FbxAMatrix& NodeTransform,
    const char* UVSetName,
    int32 PolygonIndex,
    int32 VertexInPolygon,
    const TArray<FControlPointSkinData>& ControlPointSkinData,
    FSkeletalMesh& OutSkeletalMesh)
{
    FSkeletalVertex Vertex = {};

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

    if (ControlPointIndex >= 0 && ControlPointIndex < static_cast<int32>(ControlPointSkinData.size()))
    {
        const TArray<FBoneWeightPair>& Influences = ControlPointSkinData[ControlPointIndex].Influences;
        const uint32 InfluenceCount = static_cast<uint32>(std::min<size_t>(Influences.size(), FSkeletalVertex::MaxBoneInfluences));
        for (uint32 InfluenceIndex = 0; InfluenceIndex < InfluenceCount; ++InfluenceIndex)
        {
            Vertex.BoneIndices[InfluenceIndex] = Influences[InfluenceIndex].BoneIndex;
            Vertex.BoneWeights[InfluenceIndex] = Influences[InfluenceIndex].Weight;
        }
    }

    if (Vertex.BoneWeights[0] <= MathUtil::Epsilon && !OutSkeletalMesh.Bones.empty())
    {
        Vertex.BoneIndices[0] = 0;
        Vertex.BoneWeights[0] = 1.0f;
    }

    return GetOrCreateSkeletalVertexIndex(Vertex, OutSkeletalMesh);
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

uint32 FFbxImporter::GetOrCreateSkeletalVertexIndex(const FSkeletalVertex& Vertex, FSkeletalMesh& OutSkeletalMesh)
{
    FSkeletalVertexKey Key = {};
    Key.Position = Vertex.Position;
    Key.Normal = Vertex.Normal;
    Key.UVs = Vertex.UVs;
    for (uint32 InfluenceIndex = 0; InfluenceIndex < FSkeletalVertex::MaxBoneInfluences; ++InfluenceIndex)
    {
        Key.BoneIndices[InfluenceIndex] = Vertex.BoneIndices[InfluenceIndex];
        Key.BoneWeights[InfluenceIndex] = Vertex.BoneWeights[InfluenceIndex];
    }

    auto It = UniqueSkeletalVertices.find(Key);
    if (It != UniqueSkeletalVertices.end())
    {
        return It->second;
    }

    const uint32 NewIndex = static_cast<uint32>(OutSkeletalMesh.Vertices.size());
    OutSkeletalMesh.Vertices.push_back(Vertex);
    UniqueSkeletalVertices.emplace(Key, NewIndex);
    return NewIndex;
}

uint32 FFbxImporter::GetOrCreateBoneIndex(FbxNode* BoneNode, FSkeletalMesh& OutSkeletalMesh)
{
    if (BoneNode == nullptr)
    {
        return 0;
    }

    auto It = BoneIndexByNode.find(BoneNode);
    if (It != BoneIndexByNode.end())
    {
        return It->second;
    }

    FSkeletonBone Bone = {};
    Bone.Name = BoneNode->GetName() != nullptr ? FString(BoneNode->GetName()) : FString();
    Bone.BindPose = ToMatrix(BoneNode->EvaluateGlobalTransform());
    if (Bone.BindPose.IsInvertible())
    {
        Bone.InverseBindPose = Bone.BindPose.GetInverse();
    }

    for (FbxNode* ParentNode = BoneNode->GetParent(); ParentNode != nullptr; ParentNode = ParentNode->GetParent())
    {
        auto ParentIt = BoneIndexByNode.find(ParentNode);
        if (ParentIt != BoneIndexByNode.end())
        {
            Bone.ParentIndex = static_cast<int32>(ParentIt->second);
            break;
        }
    }

    const uint32 NewIndex = static_cast<uint32>(OutSkeletalMesh.Bones.size());
    OutSkeletalMesh.Bones.push_back(Bone);
    BoneIndexByNode.emplace(BoneNode, NewIndex);
    return NewIndex;
}

TArray<FFbxImporter::FControlPointSkinData> FFbxImporter::BuildControlPointSkinData(FbxMesh* Mesh, FSkeletalMesh& OutSkeletalMesh)
{
    const int32 ControlPointCount = Mesh != nullptr ? Mesh->GetControlPointsCount() : 0;
    TArray<FControlPointSkinData> ControlPointSkinData(ControlPointCount);

    if (Mesh == nullptr)
    {
        return ControlPointSkinData;
    }

    const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
    for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
    {
        FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
        if (Skin == nullptr)
        {
            continue;
        }

        const int32 ClusterCount = Skin->GetClusterCount();
        for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
        {
            FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
            if (Cluster == nullptr || Cluster->GetLink() == nullptr)
            {
                continue;
            }

            const uint32 BoneIndex = GetOrCreateBoneIndex(Cluster->GetLink(), OutSkeletalMesh);

            FbxAMatrix MeshBindMatrix;
            FbxAMatrix LinkBindMatrix;
            Cluster->GetTransformMatrix(MeshBindMatrix);
            Cluster->GetTransformLinkMatrix(LinkBindMatrix);
            OutSkeletalMesh.Bones[BoneIndex].BindPose = ToMatrix(LinkBindMatrix);
            OutSkeletalMesh.Bones[BoneIndex].InverseBindPose = ToMatrix(LinkBindMatrix.Inverse() * MeshBindMatrix);

            const int32 InfluenceCount = Cluster->GetControlPointIndicesCount();
            int* ControlPointIndices = Cluster->GetControlPointIndices();
            double* ControlPointWeights = Cluster->GetControlPointWeights();
            for (int32 InfluenceIndex = 0; InfluenceIndex < InfluenceCount; ++InfluenceIndex)
            {
                const int32 ControlPointIndex = ControlPointIndices[InfluenceIndex];
                if (ControlPointIndex < 0 || ControlPointIndex >= ControlPointCount)
                {
                    continue;
                }

                const float Weight = static_cast<float>(ControlPointWeights[InfluenceIndex]);
                if (Weight <= MinBoneWeight)
                {
                    continue;
                }

                TArray<FBoneWeightPair>& Influences = ControlPointSkinData[ControlPointIndex].Influences;
                auto ExistingInfluence = std::find_if(
                    Influences.begin(),
                    Influences.end(),
                    [BoneIndex](const FBoneWeightPair& Influence)
                    {
                        return Influence.BoneIndex == BoneIndex;
                    });

                if (ExistingInfluence != Influences.end())
                {
                    ExistingInfluence->Weight += Weight;
                }
                else
                {
                    Influences.push_back(FBoneWeightPair{ BoneIndex, Weight });
                }
            }
        }
    }

    NormalizeControlPointSkinData(ControlPointSkinData);
    return ControlPointSkinData;
}

void FFbxImporter::NormalizeControlPointSkinData(TArray<FControlPointSkinData>& ControlPointSkinData) const
{
    for (FControlPointSkinData& SkinData : ControlPointSkinData)
    {
        TArray<FBoneWeightPair>& Influences = SkinData.Influences;
        std::sort(
            Influences.begin(),
            Influences.end(),
            [](const FBoneWeightPair& A, const FBoneWeightPair& B)
            {
                return A.Weight > B.Weight;
            });

        if (Influences.size() > FSkeletalVertex::MaxBoneInfluences)
        {
            Influences.resize(FSkeletalVertex::MaxBoneInfluences);
        }

        float TotalWeight = 0.0f;
        for (const FBoneWeightPair& Influence : Influences)
        {
            TotalWeight += Influence.Weight;
        }

        if (TotalWeight <= MathUtil::Epsilon)
        {
            Influences.clear();
            continue;
        }

        const float InverseTotalWeight = 1.0f / TotalWeight;
        for (FBoneWeightPair& Influence : Influences)
        {
            Influence.Weight *= InverseTotalWeight;
        }
    }
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

void FFbxImporter::ResetSkeletalMesh(FSkeletalMesh& OutSkeletalMesh) const
{
    OutSkeletalMesh.Vertices.clear();
    OutSkeletalMesh.Indices.clear();
    OutSkeletalMesh.Sections.clear();
    OutSkeletalMesh.Slots.clear();
    OutSkeletalMesh.Bones.clear();
    OutSkeletalMesh.LocalBounds.Reset();
    OutSkeletalMesh.RenderData.VertexBuffer = nullptr;
    OutSkeletalMesh.RenderData.IndexBuffer = nullptr;
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

void FFbxImporter::BuildDefaultSection(FSkeletalMesh& OutSkeletalMesh) const
{
    FStaticMeshMaterialSlot Slot = {};
    Slot.SlotName = FString("DefaultWhite");
    Slot.Material = nullptr;
    OutSkeletalMesh.Slots.push_back(Slot);

    FStaticMeshSection Section = {};
    Section.StartIndex = 0;
    Section.IndexCount = static_cast<uint32>(OutSkeletalMesh.Indices.size());
    Section.MaterialSlotIndex = 0;
    OutSkeletalMesh.Sections.push_back(Section);
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

void FFbxImporter::BuildTangentsAndBitangents(FSkeletalMesh& OutSkeletalMesh) const
{
    if (OutSkeletalMesh.Vertices.empty())
    {
        return;
    }

    TArray<FVector> AccumulatedTangents(OutSkeletalMesh.Vertices.size(), FVector::ZeroVector);
    TArray<FVector> AccumulatedBitangents(OutSkeletalMesh.Vertices.size(), FVector::ZeroVector);

    for (size_t TriangleIndex = 0; TriangleIndex + 2 < OutSkeletalMesh.Indices.size(); TriangleIndex += 3)
    {
        const uint32 Index0 = OutSkeletalMesh.Indices[TriangleIndex + 0];
        const uint32 Index1 = OutSkeletalMesh.Indices[TriangleIndex + 1];
        const uint32 Index2 = OutSkeletalMesh.Indices[TriangleIndex + 2];

        if (Index0 >= OutSkeletalMesh.Vertices.size() ||
            Index1 >= OutSkeletalMesh.Vertices.size() ||
            Index2 >= OutSkeletalMesh.Vertices.size())
        {
            continue;
        }

        const FSkeletalVertex& Vertex0 = OutSkeletalMesh.Vertices[Index0];
        const FSkeletalVertex& Vertex1 = OutSkeletalMesh.Vertices[Index1];
        const FSkeletalVertex& Vertex2 = OutSkeletalMesh.Vertices[Index2];

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

    for (size_t VertexIndex = 0; VertexIndex < OutSkeletalMesh.Vertices.size(); ++VertexIndex)
    {
        FSkeletalVertex& Vertex = OutSkeletalMesh.Vertices[VertexIndex];
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

FAABB FFbxImporter::BuildLocalBounds(const FSkeletalMesh& SkeletalMesh) const
{
    FAABB Bounds;
    Bounds.Reset();

    for (const FSkeletalVertex& Vertex : SkeletalMesh.Vertices)
    {
        Bounds.Expand(Vertex.Position);
    }

    return Bounds;
}
