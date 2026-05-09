#include "FbxImporter.h"

#include "Asset/SkeletalMesh.h"
#include "Math/Utils.h"
#include "Object/Object.h"

#include <algorithm>
#include <limits>

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

    FbxAMatrix GetSkeletalMeshBindTransform(FbxNode* Node, FbxMesh* Mesh)
    {
        FbxAMatrix MeshBindTransform;
        MeshBindTransform.SetIdentity();

        bool bFoundSkinBindTransform = false;
        if (Mesh != nullptr)
        {
            const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
            for (int32 SkinIndex = 0; SkinIndex < SkinCount && !bFoundSkinBindTransform; ++SkinIndex)
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
                    if (Cluster == nullptr)
                    {
                        continue;
                    }

                    Cluster->GetTransformMatrix(MeshBindTransform);
                    bFoundSkinBindTransform = true;
                    break;
                }
            }
        }

        if (!bFoundSkinBindTransform && Node != nullptr)
        {
            MeshBindTransform = Node->EvaluateGlobalTransform();
        }

        return MeshBindTransform * GetGeometryTransform(Node);
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

USkeletalMesh* FFbxImporter::ImportSkeletalMeshAsset(FbxScene* Scene)
{
    FSkeletalMesh* SkeletalMeshData = ImportSkeletalMesh(Scene);
    if (SkeletalMeshData == nullptr)
    {
        return nullptr;
    }

    USkeletalMesh* SkeletalMesh = UObjectManager::Get().CreateObject<USkeletalMesh>();
    SkeletalMesh->SetMeshData(SkeletalMeshData);
    return SkeletalMesh;
}

bool FFbxImporter::ImportSkeletalMesh(FbxScene* Scene, FSkeletalMesh& OutSkeletalMesh)
{
    ResetSkeletalMesh(OutSkeletalMesh);
    UniqueSkeletalVertices.clear();
    BoneIndexByNode.clear();
    BoneGlobalBindPoseByNode.clear();
    BoneGlobalBindPoseFbxByNode.clear();

    if (Scene == nullptr || Scene->GetRootNode() == nullptr)
    {
        return false;
    }

    ProcessSkeletonNode(Scene->GetRootNode(), -1, OutSkeletalMesh);
    CollectSkinBindPoses(Scene->GetRootNode(), OutSkeletalMesh);
    if (OutSkeletalMesh.Bones.empty())
    {
        FSkeletonBone RootBone = {};
        RootBone.Name = FString("Root");
        OutSkeletalMesh.Bones.push_back(RootBone);
    }

    ProcessSkeletalNode(Scene->GetRootNode(), OutSkeletalMesh);
    RefreshBoneLocalBindPoses(OutSkeletalMesh);

    if (OutSkeletalMesh.Vertices.empty() || OutSkeletalMesh.Indices.empty())
    {
        return false;
    }

    BuildDefaultSection(OutSkeletalMesh);
    BuildTangentsAndBitangents(OutSkeletalMesh);
    OutSkeletalMesh.LocalBounds = BuildLocalBounds(OutSkeletalMesh);
    OutSkeletalMesh.RebuildDerivedData();

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

void FFbxImporter::CollectSkinBindPoses(FbxNode* Node, FSkeletalMesh& OutSkeletalMesh)
{
    if (Node == nullptr)
    {
        return;
    }

    FbxMesh* Mesh = Node->GetMesh();
    if (Mesh != nullptr)
    {
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

                FbxAMatrix LinkBindMatrix;
                Cluster->GetTransformLinkMatrix(LinkBindMatrix);
                SetBoneGlobalBindPose(Cluster->GetLink(), LinkBindMatrix, OutSkeletalMesh);
            }
        }
    }

    const int32 ChildCount = Node->GetChildCount();
    for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
    {
        CollectSkinBindPoses(Node->GetChild(ChildIndex), OutSkeletalMesh);
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
    Mesh->GetUVSetNames(UVSetNameList); // 메시에서 UV 세트 이름을 가져옴. 메시에는 여러 UV 세트가 있을 수 있으므로, 첫 번째 UV 세트를 사용하기 위해 이름을 가져옴.
    const char* UVSetName = UVSetNameList.GetCount() > 0 ? UVSetNameList.GetStringAt(0) : nullptr; // UV 세트가 하나라도 있으면 첫 번째 UV 세트 이름을 사용하고

    const TArray<FControlPointSkinData> ControlPointSkinData = BuildControlPointSkinData(Mesh, OutSkeletalMesh); // 컨트롤 포인트의 스킨 데이터를 생성
    bool bHasAnySkinInfluence = false;
    for (const FControlPointSkinData& SkinData : ControlPointSkinData)
    {
        if (!SkinData.Influences.empty())
        {
            bHasAnySkinInfluence = true;
            break;
        }
    }

    const FbxAMatrix MeshBindTransform = ResolveSkeletalMeshBindTransform(Node, Mesh, bHasAnySkinInfluence);
    const int32 FallbackBoneIndex = bHasAnySkinInfluence
        ? 0
        : ResolveRigidSkinBoneIndex(Node, Mesh, MeshBindTransform, OutSkeletalMesh);
    const int32 PolygonCount = Mesh->GetPolygonCount(); // 메시를 구성하는 polygon의 개수

    for (int32 PolygonIndex = 0; PolygonIndex < PolygonCount; ++PolygonIndex) // 모든 polygon에 대해서 반복
    {
        const int32 PolygonSize = Mesh->GetPolygonSize(PolygonIndex); // 현재 polygon의 vertex 개수
        if (PolygonSize < 3)
        {
            continue;
        }

        for (int32 TriangleIndex = 0; TriangleIndex < PolygonSize - 2; ++TriangleIndex)
        {
            const uint32 Index0 = AddSkeletalPolygonVertex(Mesh, MeshBindTransform, UVSetName, PolygonIndex, 0, ControlPointSkinData, FallbackBoneIndex, OutSkeletalMesh);
            const uint32 Index1 = AddSkeletalPolygonVertex(Mesh, MeshBindTransform, UVSetName, PolygonIndex, TriangleIndex + 1, ControlPointSkinData, FallbackBoneIndex, OutSkeletalMesh);
            const uint32 Index2 = AddSkeletalPolygonVertex(Mesh, MeshBindTransform, UVSetName, PolygonIndex, TriangleIndex + 2, ControlPointSkinData, FallbackBoneIndex, OutSkeletalMesh);

            OutSkeletalMesh.Indices.push_back(Index0);
            OutSkeletalMesh.Indices.push_back(Index2);
            OutSkeletalMesh.Indices.push_back(Index1);
        }
    }
}

FbxAMatrix FFbxImporter::ResolveSkeletalMeshBindTransform(FbxNode* Node, FbxMesh* Mesh, bool bHasAnySkinInfluence) const
{
    if (Node == nullptr)
    {
        FbxAMatrix Identity;
        Identity.SetIdentity();
        return Identity;
    }

    if (bHasAnySkinInfluence)
    {
        return GetSkeletalMeshBindTransform(Node, Mesh);
    }

    FbxAMatrix MeshBindTransform = Node->EvaluateGlobalTransform();
    FbxNode* ParentBoneNode = FindClosestParentBoneNode(Node);
    auto ParentBindIt = ParentBoneNode != nullptr
        ? BoneGlobalBindPoseFbxByNode.find(ParentBoneNode)
        : BoneGlobalBindPoseFbxByNode.end();
    if (ParentBindIt != BoneGlobalBindPoseFbxByNode.end())
    {
        const FbxAMatrix ParentCurrentTransform = ParentBoneNode->EvaluateGlobalTransform();
        const FbxAMatrix MeshRelativeToParentBone = ParentCurrentTransform.Inverse() * MeshBindTransform;
        MeshBindTransform = ParentBindIt->second * MeshRelativeToParentBone;
    }

    return MeshBindTransform * GetGeometryTransform(Node);
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
    const FbxAMatrix& MeshBindTransform,
    const char* UVSetName,
    int32 PolygonIndex,
    int32 VertexInPolygon,
    const TArray<FControlPointSkinData>& ControlPointSkinData,
    int32 FallbackBoneIndex,
    FSkeletalMesh& OutSkeletalMesh)
{
    FSkeletalVertex Vertex = {};

    const int32 ControlPointIndex = Mesh->GetPolygonVertex(PolygonIndex, VertexInPolygon);
    const FbxVector4 FbxPosition = Mesh->GetControlPointAt(ControlPointIndex);
    Vertex.Position = ToVector(MeshBindTransform.MultT(FbxPosition));

    FbxVector4 FbxNormal(0.0, 0.0, 1.0, 0.0);
    if (Mesh->GetPolygonVertexNormal(PolygonIndex, VertexInPolygon, FbxNormal))
    {
        FbxNormal = MeshBindTransform.MultR(FbxNormal);
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
        Vertex.BoneIndices[0] = FallbackBoneIndex >= 0
            ? static_cast<uint32>(FallbackBoneIndex)
            : 0;
        Vertex.BoneWeights[0] = 1.0f;
    }

    return GetOrCreateSkeletalVertexIndex(Vertex, OutSkeletalMesh);
}

int32 FFbxImporter::ResolveRigidSkinBoneIndex(
    FbxNode* Node,
    FbxMesh* Mesh,
    const FbxAMatrix& MeshBindTransform,
    const FSkeletalMesh& OutSkeletalMesh) const
{
    if (OutSkeletalMesh.Bones.empty())
    {
        return -1;
    }

    for (FbxNode* ParentNode = Node != nullptr ? Node->GetParent() : nullptr;
         ParentNode != nullptr;
         ParentNode = ParentNode->GetParent())
    {
        auto BoneIndexIt = BoneIndexByNode.find(ParentNode);
        if (BoneIndexIt != BoneIndexByNode.end() &&
            BoneIndexIt->second < OutSkeletalMesh.Bones.size())
        {
            return static_cast<int32>(BoneIndexIt->second);
        }
    }

    FVector MeshCenter = ToMatrix(MeshBindTransform).GetOrigin();
    const int32 ControlPointCount = Mesh != nullptr ? Mesh->GetControlPointsCount() : 0;
    if (ControlPointCount > 0)
    {
        MeshCenter = FVector::ZeroVector;
        for (int32 ControlPointIndex = 0; ControlPointIndex < ControlPointCount; ++ControlPointIndex)
        {
            MeshCenter += ToVector(MeshBindTransform.MultT(Mesh->GetControlPointAt(ControlPointIndex)));
        }
        MeshCenter *= 1.0f / static_cast<float>(ControlPointCount);
    }

    int32 BestBoneIndex = 0;
    float BestDistanceSquared = std::numeric_limits<float>::max();
    for (const auto& Pair : BoneIndexByNode)
    {
        if (Pair.first == nullptr || Pair.second >= OutSkeletalMesh.Bones.size())
        {
            continue;
        }

        auto GlobalBindPoseIt = BoneGlobalBindPoseByNode.find(Pair.first);
        const FMatrix GlobalBindPose =
            GlobalBindPoseIt != BoneGlobalBindPoseByNode.end()
            ? GlobalBindPoseIt->second
            : ToMatrix(Pair.first->EvaluateGlobalTransform());

        const float DistanceSquared = FVector::DistSquared(MeshCenter, GlobalBindPose.GetOrigin());
        if (DistanceSquared < BestDistanceSquared)
        {
            BestDistanceSquared = DistanceSquared;
            BestBoneIndex = static_cast<int32>(Pair.second);
        }
    }

    return BestBoneIndex;
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

    int32 ParentIndex = -1;
    for (FbxNode* ParentNode = BoneNode->GetParent(); ParentNode != nullptr; ParentNode = ParentNode->GetParent())
    {
        FbxNodeAttribute* ParentAttribute = ParentNode->GetNodeAttribute();
        const bool bParentIsSkeleton =
            ParentAttribute != nullptr && ParentAttribute->GetAttributeType() == FbxNodeAttribute::eSkeleton;
        if (bParentIsSkeleton || BoneIndexByNode.find(ParentNode) != BoneIndexByNode.end())
        {
            ParentIndex = static_cast<int32>(GetOrCreateBoneIndex(ParentNode, OutSkeletalMesh));
            break;
        }
    }

    FSkeletonBone Bone = {};
    Bone.Name = BoneNode->GetName() != nullptr ? FString(BoneNode->GetName()) : FString();
    Bone.ParentIndex = ParentIndex;

    const FbxAMatrix GlobalBindPoseFbx = BoneNode->EvaluateGlobalTransform();
    const FMatrix GlobalBindPose = ToMatrix(GlobalBindPoseFbx);
    Bone.BindPose = GlobalBindPose;
    if (ParentIndex >= 0 && ParentIndex < static_cast<int32>(OutSkeletalMesh.Bones.size()))
    {
        FbxNode* ParentNode = BoneNode->GetParent();
        while (ParentNode != nullptr && BoneIndexByNode.find(ParentNode) == BoneIndexByNode.end())
        {
            ParentNode = ParentNode->GetParent();
        }

        auto ParentGlobalIt = ParentNode != nullptr ? BoneGlobalBindPoseByNode.find(ParentNode) : BoneGlobalBindPoseByNode.end();
        const FMatrix ParentGlobalBindPose =
            ParentGlobalIt != BoneGlobalBindPoseByNode.end()
            ? ParentGlobalIt->second
            : OutSkeletalMesh.Bones[ParentIndex].BindPose;

        Bone.BindPose = GlobalBindPose * ParentGlobalBindPose.GetInverse();
    }

    if (GlobalBindPose.IsInvertible())
    {
        Bone.InverseBindPose = GlobalBindPose.GetInverse();
    }

    const uint32 NewIndex = static_cast<uint32>(OutSkeletalMesh.Bones.size());
    OutSkeletalMesh.Bones.push_back(Bone);
    BoneIndexByNode.emplace(BoneNode, NewIndex);
    BoneGlobalBindPoseByNode.emplace(BoneNode, GlobalBindPose);
    BoneGlobalBindPoseFbxByNode.emplace(BoneNode, GlobalBindPoseFbx);
    return NewIndex;
}

FbxNode* FFbxImporter::FindClosestParentBoneNode(FbxNode* Node) const
{
    for (FbxNode* ParentNode = Node != nullptr ? Node->GetParent() : nullptr;
         ParentNode != nullptr;
         ParentNode = ParentNode->GetParent())
    {
        if (BoneIndexByNode.find(ParentNode) != BoneIndexByNode.end())
        {
            return ParentNode;
        }
    }

    return nullptr;
}

void FFbxImporter::SetBoneGlobalBindPose(FbxNode* BoneNode, const FbxAMatrix& GlobalBindPose, FSkeletalMesh& OutSkeletalMesh)
{
    if (BoneNode == nullptr)
    {
        return;
    }

    const uint32 BoneIndex = GetOrCreateBoneIndex(BoneNode, OutSkeletalMesh);
    BoneGlobalBindPoseByNode[BoneNode] = ToMatrix(GlobalBindPose);
    BoneGlobalBindPoseFbxByNode[BoneNode] = GlobalBindPose;

    FSkeletonBone& Bone = OutSkeletalMesh.Bones[BoneIndex];
    const FMatrix GlobalBindPoseMatrix = ToMatrix(GlobalBindPose);
    if (GlobalBindPoseMatrix.IsInvertible())
    {
        Bone.InverseBindPose = GlobalBindPoseMatrix.GetInverse();
    }
}

void FFbxImporter::RefreshBoneLocalBindPoses(FSkeletalMesh& OutSkeletalMesh)
{
    for (const auto& Pair : BoneIndexByNode)
    {
        FbxNode* BoneNode = Pair.first;
        const uint32 BoneIndex = Pair.second;
        if (BoneNode == nullptr || BoneIndex >= OutSkeletalMesh.Bones.size())
        {
            continue;
        }

        auto GlobalIt = BoneGlobalBindPoseByNode.find(BoneNode);
        const FMatrix GlobalBindPose =
            GlobalIt != BoneGlobalBindPoseByNode.end()
            ? GlobalIt->second
            : ToMatrix(BoneNode->EvaluateGlobalTransform());

        FSkeletonBone& Bone = OutSkeletalMesh.Bones[BoneIndex];
        if (GlobalBindPose.IsInvertible())
        {
            Bone.InverseBindPose = GlobalBindPose.GetInverse();
        }

        Bone.BindPose = GlobalBindPose;
        if (Bone.ParentIndex < 0 || Bone.ParentIndex >= static_cast<int32>(OutSkeletalMesh.Bones.size()))
        {
            continue;
        }

        FbxNode* ParentNode = BoneNode->GetParent();
        while (ParentNode != nullptr)
        {
            auto ParentIndexIt = BoneIndexByNode.find(ParentNode);
            if (ParentIndexIt != BoneIndexByNode.end() &&
                static_cast<int32>(ParentIndexIt->second) == Bone.ParentIndex)
            {
                break;
            }

            ParentNode = ParentNode->GetParent();
        }

        auto ParentGlobalIt = ParentNode != nullptr ? BoneGlobalBindPoseByNode.find(ParentNode) : BoneGlobalBindPoseByNode.end();
        const FMatrix ParentGlobalBindPose =
            ParentGlobalIt != BoneGlobalBindPoseByNode.end()
            ? ParentGlobalIt->second
            : OutSkeletalMesh.Bones[Bone.ParentIndex].BindPose;

        Bone.BindPose = GlobalBindPose * ParentGlobalBindPose.GetInverse();
    }
}

TArray<FFbxImporter::FControlPointSkinData> FFbxImporter::BuildControlPointSkinData(FbxMesh* Mesh, FSkeletalMesh& OutSkeletalMesh)
{
    const int32 ControlPointCount = Mesh != nullptr ? Mesh->GetControlPointsCount() : 0;
    TArray<FControlPointSkinData> ControlPointSkinData(ControlPointCount);

    if (Mesh == nullptr)
    {
        return ControlPointSkinData;
    }

    const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin); // 스킨 디포머
    for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)       // 스킨 디포머에 대한 반복문
    {
        FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin)); 
        if (Skin == nullptr)
        {
            continue;
        }

        const int32 ClusterCount = Skin->GetClusterCount();
        for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex) // 스킨 디포머에 포함된 클러스터에 대한 반복문
        {
            FbxCluster* Cluster = Skin->GetCluster(ClusterIndex); // 클러스터(본과 그 본에 영향을 미치는 컨트롤 포인트들의 집합)
            if (Cluster == nullptr || Cluster->GetLink() == nullptr)
            {
                continue;
            }

            FbxAMatrix LinkBindMatrix;
            Cluster->GetTransformLinkMatrix(LinkBindMatrix); // 처음 리깅할 때, 바인드 포즈(T-Pose나 A-Pose) 상태에서 애니메이션이 하나도 적용되지 않았을 때, 이 뼈가 월드 공간상에 정확히 어디에 있었는가
            const uint32 BoneIndex = GetOrCreateBoneIndex(Cluster->GetLink(), OutSkeletalMesh); // 뼈를 등록하고 인덱스를 가져옴
            SetBoneGlobalBindPose(Cluster->GetLink(), LinkBindMatrix, OutSkeletalMesh); // 뼈에 inverse 바인드 포즈를 저장(추후에 현재 애니메이션 프레임의 뼈 위치 행렬 * inverse 바인드 포즈 행렬로 뼈가 원래 있던 위치에서 얼만큼 변했는지 변화량을 계산하는데 사용)

            const int32 InfluenceCount = Cluster->GetControlPointIndicesCount(); // 이 뼈가 영향을 미치는 컨트롤 포인트의 수
            int* ControlPointIndices = Cluster->GetControlPointIndices();        // 이 뼈가 영향을 미치는 컨트롤 포인트들의 인덱스 배열
            double* ControlPointWeights = Cluster->GetControlPointWeights();     // 이 뼈가 영향을 미치는 컨트롤 포인트들의 가중치 배열
            for (int32 InfluenceIndex = 0; InfluenceIndex < InfluenceCount; ++InfluenceIndex)
            {
                const int32 ControlPointIndex = ControlPointIndices[InfluenceIndex]; // 영향을 받는 컨트롤 포인트의 인덱스
                if (ControlPointIndex < 0 || ControlPointIndex >= ControlPointCount)
                {
                    continue;
                }

                const float Weight = static_cast<float>(ControlPointWeights[InfluenceIndex]); // 영향을 받는 컨트롤 포인트에 대한 가중치
                if (Weight <= MinBoneWeight)
                {
                    continue;
                }

                TArray<FBoneWeightPair>& Influences = ControlPointSkinData[ControlPointIndex].Influences; // 해당 컨트롤 포인트에 영향을 미치는 뼈들의 리스트를 가져와서
                auto ExistingInfluence = std::find_if(
                    Influences.begin(),
                    Influences.end(),
                    [BoneIndex](const FBoneWeightPair& Influence)
                    {
                        return Influence.BoneIndex == BoneIndex;
                    });

                if (ExistingInfluence != Influences.end())
                {
                    ExistingInfluence->Weight += Weight; // 이미 이 뼈가 영향을 미치고 있다면 가중치를 누적
                }
                else
                {
                    Influences.push_back(FBoneWeightPair{ BoneIndex, Weight }); // 새로운 뼈의 영향을 추가
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
                return A.Weight > B.Weight; // 뼈들을 가중치가 큰 순서대로 줄 세우기
            });

        if (Influences.size() > FSkeletalVertex::MaxBoneInfluences)
        {
            Influences.resize(FSkeletalVertex::MaxBoneInfluences); // 성능과 메모리 한계 때문에 정점이 영향받을 수 있는 뼈의 개수 제한 (지금은 영향력이 큰 뼈 4개로 제한)
        }

        float TotalWeight = 0.0f;
        for (const FBoneWeightPair& Influence : Influences)
        {
            TotalWeight += Influence.Weight; // 모든 뼈의 가중치를 합산
        }

        if (TotalWeight <= MathUtil::Epsilon)
        {
            Influences.clear(); // 가중치 합이 거의 0에 가깝다면 영향을 받는 뼈가 없다고 판단하고 초기화
            continue;
        }

        const float InverseTotalWeight = 1.0f / TotalWeight;
        for (FBoneWeightPair& Influence : Influences)
        {
            Influence.Weight *= InverseTotalWeight; // 모든 뼈의 가중치 합이 1이 되도록 정규화 (1보다 작으면 정점이 원점으로 쪼그라들고, 1보다 크면 정점이 원점에서 멀어지는 현상이 발생할 수 있기 때문에 가중치 합이 1이 되도록 조정)
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
    OutSkeletalMesh.RefSkeleton.Reset();
    OutSkeletalMesh.SkinWeightVertexBuffer.Reset();
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
