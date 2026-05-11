#include "FBXSDKContext.h"
#include "FBXImporter.h"
#include "Engine/Core/ResourceManager.h"
#include "Core/Paths.h"

#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <cstring>

namespace
{
    struct FVertexKey
    {
        int32 ControlPointIndex;
        float Px, Py, Pz;
        float Nx, Ny, Nz;
        float U, V;

        bool operator==(const FVertexKey& O) const
        {
            return ControlPointIndex == O.ControlPointIndex
                && Px == O.Px && Py == O.Py && Pz == O.Pz
                && Nx == O.Nx && Ny == O.Ny && Nz == O.Nz
                && U  == O.U  && V  == O.V;
        }
    };

    struct FVertexKeyHash
    {
        size_t operator()(const FVertexKey& Key) const
        {
            auto HashFloat = [](float F) -> size_t
            {
                uint32 Bits;
                memcpy(&Bits, &F, sizeof(Bits));
                return std::hash<uint32>{}(Bits);
            };

            size_t H = 0;
            auto Combine = [&](float F)
            {
                H ^= HashFloat(F) + 0x9e3779b9 + (H << 6) + (H >> 2);
            };

            H ^= std::hash<int32>{}(Key.ControlPointIndex) + 0x9e3779b9 + (H << 6) + (H >> 2);
            Combine(Key.Px); Combine(Key.Py); Combine(Key.Pz);
            Combine(Key.Nx); Combine(Key.Ny); Combine(Key.Nz);
            Combine(Key.U);  Combine(Key.V);

            return H;
        }
    };

    bool CompareBoneWeightDescending(const FBoneIndexWeight& A, const FBoneIndexWeight& B)
    {
        return A.BoneWeight > B.BoneWeight;
    }
}

FFBXImportScene FFBXImporter::Import(const FString& Path, const FFBXImportOptions& Options)
{
    FFBXSDKContext& Context = FFBXSDKContext::Get();
    FFBXImportScene ImportScene = {};
    ImportScene.SourceFilePath = Path;
    ImportOptions = Options;
    ImportDirectory = FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(Path)).parent_path().generic_wstring());

    if (!Context.Initialize())
    {
        return ImportScene;
    }

    FFBXSceneHandle Scene = Context.LoadScene(Path);
    if (!Scene)
    {
        return ImportScene;
    }

    FbxNode* RootNode = Scene->GetRootNode();
    if (!RootNode)
    {
        return ImportScene;
    }

    FbxGeometryConverter Converter(Context.GetManager());
    Converter.Triangulate(Scene.Get(), true);

    TraversalNode(RootNode, -1, ImportScene);

    return ImportScene;
}

FStaticMesh* FFBXImporter::CreateStaticMeshFromImportData(const FFBXStaticMeshImportData& ImportData) const
{
    FStaticMesh* Mesh = new FStaticMesh();

    Mesh->PathFileName = ImportData.Name;
    Mesh->Vertices = ImportData.Vertices;
    Mesh->Indices = ImportData.Indices;
    Mesh->Sections = ImportData.Sections;
    Mesh->Slots = ImportData.MaterialSlots;
    Mesh->LocalBounds = BuildLocalBounds(ImportData);

    return Mesh;
}

FSkeletalMesh* FFBXImporter::CreateSkeletalMeshFromtImportData(const FFBXSkeletalMeshImportData& ImportData) const
{
    FSkeletalMesh* Mesh = new FSkeletalMesh;

    Mesh->FilePathName = ImportData.Name;
    Mesh->Vertices = ImportData.Vertices;
    Mesh->Indices = ImportData.Indices;
    Mesh->Sections = ImportData.Sections;
    Mesh->Slots = ImportData.MaterialSlots;
    Mesh->Bones = ImportData.Bones;
    Mesh->LocalBounds = BuildLocalBounds(ImportData);

    return Mesh;
}

void FFBXImporter::TraversalNode(FbxNode* Node, int32 ParentIndex, OUT FFBXImportScene& OutImportScene)
{
    if (!Node)
    {
        return;
    }

    int32 curNodeIndex = AddImportNode(Node, ParentIndex, OutImportScene);

    FbxMesh* Mesh = Node->GetMesh();
    if (Mesh)
    {
        const bool bHasSkin = Mesh->GetDeformerCount(FbxDeformer::eSkin) > 0;

        if (bHasSkin && !ImportOptions.bImportSkinnedMeshesAsStatic)
        {
            FFBXSkeletalMeshImportData SkeletalMeshData = {};
            SkeletalMeshData.SourceNodeIndex = curNodeIndex;

            if (BuildSkeletalMeshImportData(Node, Mesh, SkeletalMeshData))
            {
                const int32 SkeletalMeshIndex = static_cast<int32>(OutImportScene.SkeletalMeshes.size());
                OutImportScene.SkeletalMeshes.push_back(SkeletalMeshData);
                OutImportScene.Nodes[curNodeIndex].SkeletalMeshIndex = SkeletalMeshIndex;
            }
        }

        if (!bHasSkin || ImportOptions.bImportSkinnedMeshesAsStatic)
        {
            FFBXStaticMeshImportData StaticMeshData;
            StaticMeshData.SourceNodeIndex = curNodeIndex;
            if (BuildStaticMeshImportData(Node, Mesh, StaticMeshData))
            {
                const int32 StaticMeshIndex = static_cast<int32>(OutImportScene.StaticMeshes.size());
                OutImportScene.StaticMeshes.push_back(StaticMeshData);
                OutImportScene.Nodes[curNodeIndex].StaticMeshIndex = StaticMeshIndex;
            }
        }
    }

    for (int32 i = 0; i < Node->GetChildCount(); ++i)
    {
        TraversalNode(Node->GetChild(i), curNodeIndex, OutImportScene);
    }
}

int32 FFBXImporter::AddImportNode(FbxNode* Node, int32 ParentIndex, OUT FFBXImportScene& OutImportScene)
{
    FFBXImportNode ImportNode = {};
    int32 curIndex = static_cast<int32>(OutImportScene.Nodes.size());

    ImportNode.Name = Node->GetName();
    ImportNode.ParentIndex = ParentIndex;

    if (ParentIndex >= 0)
    {
        FFBXImportNode& ParentImportNode = OutImportScene.Nodes[ParentIndex];
        ParentImportNode.Children.push_back(curIndex);
    }

    ImportNode.LocalTransformMatrix = ConvertFbxMatrix(Node->EvaluateLocalTransform());
    ImportNode.GlobalTransformMatrix = ConvertFbxMatrix(Node->EvaluateGlobalTransform());
    OutImportScene.Nodes.push_back(ImportNode);

    return curIndex;
}

// Static / Skeletal 공통 Geometry 처리 (UV, Normal, Position, Material, Section, Tangent)
// VertexControlPointIndices에 각 Vertex의 원본 ControlPoint 인덱스를 함께 기록해
// 이후 스켈레탈 메시가 본 웨이트를 조회할 수 있도록 한다.
bool FFBXImporter::BuildMeshRawData(FbxNode* Node, FbxMesh* Mesh, OUT FFBXMeshRawData& OutRawData)
{
    OutRawData.Name = Mesh->GetName();
    if (OutRawData.Name.empty())
    {
        OutRawData.Name = Node->GetName();
    }

    OutRawData.Vertices.clear();
    OutRawData.Indices.clear();
    OutRawData.Sections.clear();
    OutRawData.VertexControlPointIndices.clear();

    // PolyGon을 순회하면서 FStaticMeshSection을 채워야 해서 먼저 Materail 생성
    BuildMaterialSlot(Node, OutRawData);
    FString UVSetName = GetFirstUVName(Mesh);

    FbxAMatrix GeometricTransform;

    FbxVector4 GeometryT = Node->GetGeometricTranslation(FbxNode::eSourcePivot);
    FbxVector4 GeometryR = Node->GetGeometricRotation(FbxNode::eSourcePivot);
    FbxVector4 GeometryS = Node->GetGeometricScaling(FbxNode::eSourcePivot);
    GeometricTransform.SetTRS(GeometryT, GeometryR, GeometryS);

    // 정점은 항상 mesh local 공간으로 유지한다.
    // Node/Component World는 렌더 셰이더에서 한 번만 적용한다.
    FbxAMatrix PositionTransform = GeometricTransform;

    // Normal은 역전치 행렬로 변환해야 비균등 스케일에서도 정확함
    const FbxAMatrix NormalTransform = PositionTransform.Inverse().Transpose();

    FbxVector4* pControlPoints = Mesh->GetControlPoints();

    TArray<TArray<uint32>> SlotIndices;
    SlotIndices.resize(OutRawData.MaterialSlots.size());

    std::unordered_map<FVertexKey, uint32, FVertexKeyHash> VertexCache;

    for (int32 PolygonIndex = 0; PolygonIndex < Mesh->GetPolygonCount(); ++PolygonIndex)
    {
        if (Mesh->GetPolygonSize(PolygonIndex) != 3)
        {
            continue;
        }

        int32 MaterialIndex = GetPolygonMaterialIndex(Mesh, PolygonIndex);
        if (MaterialIndex < 0 || MaterialIndex >= static_cast<int32>(SlotIndices.size()))
        {
            MaterialIndex = 0;
        }

        for (int32 VertexInPolygon = 0; VertexInPolygon < 3; ++VertexInPolygon)
        {
            FNormalVertex Vertex = {};

            // UV
            if (!UVSetName.empty())
            {
                FbxVector2 FbxUV;
                bool bUnmapped = false;

                const bool bHasUV = Mesh->GetPolygonVertexUV(PolygonIndex, VertexInPolygon, UVSetName.c_str(), FbxUV, bUnmapped);

                if (bHasUV && !bUnmapped)
                {
                    Vertex.UVs = FVector2(
                        static_cast<float>(FbxUV[0]),
                        1.0f - static_cast<float>(FbxUV[1]));
                }
            }

            // Normal: 역전치 행렬 적용 후 정규화
            FbxVector4 FbxNormal;
            if (Mesh->GetPolygonVertexNormal(PolygonIndex, VertexInPolygon, FbxNormal))
            {
                FbxNormal[3] = 0.0;
                FbxVector4 TransformedNormal = NormalTransform.MultT(FbxNormal);
                TransformedNormal.Normalize();
                Vertex.Normal = FVector(
                    static_cast<float>(TransformedNormal[0]),
                    static_cast<float>(TransformedNormal[1]),
                    static_cast<float>(TransformedNormal[2])
                );
            }

            // Position: mesh local 공간으로 변환
            const int32 ControlPointIndex = Mesh->GetPolygonVertex(PolygonIndex, VertexInPolygon);
            if (pControlPoints && ControlPointIndex >= 0)
            {
                FbxVector4 TransformedPoint = PositionTransform.MultT(pControlPoints[ControlPointIndex]);
                Vertex.Position = FVector(
                    static_cast<float>(TransformedPoint[0]),
                    static_cast<float>(TransformedPoint[1]),
                    static_cast<float>(TransformedPoint[2])
                );
            }

            const FVertexKey Key = {
                ControlPointIndex,
                Vertex.Position.X, Vertex.Position.Y, Vertex.Position.Z,
                Vertex.Normal.X,   Vertex.Normal.Y,   Vertex.Normal.Z,
                Vertex.UVs.X,      Vertex.UVs.Y
            };

            auto It = VertexCache.find(Key);
            if (It != VertexCache.end())
            {
                SlotIndices[MaterialIndex].push_back(It->second);
            }
            else
            {
                const uint32 NewIndex = static_cast<uint32>(OutRawData.Vertices.size());
                OutRawData.Vertices.push_back(Vertex);
                OutRawData.VertexControlPointIndices.push_back(ControlPointIndex);
                VertexCache[Key] = NewIndex;
                SlotIndices[MaterialIndex].push_back(NewIndex);
            }
        }
    }

    // Section
    BuildSectionsFromSlotIndices(SlotIndices, OutRawData);
    // Tangent
    BuildTangentsAndBitangents(OutRawData);
    return true;
}

bool FFBXImporter::BuildStaticMeshImportData(FbxNode* Node, FbxMesh* Mesh, OUT FFBXStaticMeshImportData& OutMeshData)
{
    FFBXMeshRawData RawData;
    RawData.SourceNodeIndex = OutMeshData.SourceNodeIndex;

    if (!BuildMeshRawData(Node, Mesh, RawData))
    {
        return false;
    }

    OutMeshData.Name = RawData.Name;
    OutMeshData.Vertices = RawData.Vertices;
    OutMeshData.Indices = RawData.Indices;
    OutMeshData.Sections = RawData.Sections;
    OutMeshData.MaterialSlots = RawData.MaterialSlots;

    return true;
}

bool FFBXImporter::BuildSkeletalMeshImportData(FbxNode* Node, FbxMesh* Mesh, OUT FFBXSkeletalMeshImportData& OutMeshData)
{
    FFBXMeshRawData RawData;
    RawData.SourceNodeIndex = OutMeshData.SourceNodeIndex;

    if (!BuildMeshRawData(Node, Mesh, RawData))
    {
        return false;
    }

    OutMeshData.Name = RawData.Name;

    // FNormalVertex -> FSkeletalVertex 변환 (Position, Normal, UV, Tangent, Bitangent 복사)
    // BoneIndices / BoneWeights만 추가

    OutMeshData.Vertices.resize(RawData.Vertices.size());
    for (int32 i = 0; i < static_cast<int32>(RawData.Vertices.size()); ++i)
    {
        const FNormalVertex& Src = RawData.Vertices[i];
        FSkeletalVertex& Dst = OutMeshData.Vertices[i];

        Dst.Position = Src.Position;
        Dst.Color = Src.Color;
        Dst.Normal = Src.Normal;
        Dst.UVs = Src.UVs;
        Dst.Tangent = Src.Tangent;
        Dst.Bitangent = Src.Bitangent;

        for (int32 k = 0; k < 4; ++k)
        {
            Dst.BoneIndices[k] = 0;
            Dst.BoneWeights[k] = 0.f;
        }
    }

    // Indices 복사
    OutMeshData.Indices = RawData.Indices;

    // Sections 
    OutMeshData.Sections.resize(RawData.Sections.size());
    for (int32 i = 0; i < static_cast<int32>(RawData.Sections.size()); ++i)
    {
        OutMeshData.Sections[i].StartIndex = RawData.Sections[i].StartIndex;
        OutMeshData.Sections[i].IndexCount = RawData.Sections[i].IndexCount;
        OutMeshData.Sections[i].MaterialSlotIndex = RawData.Sections[i].MaterialSlotIndex;
    }

    // MaterialSlots 
    OutMeshData.MaterialSlots.resize(RawData.MaterialSlots.size());
    for (int32 i = 0; i < static_cast<int32>(RawData.MaterialSlots.size()); ++i)
    {
        OutMeshData.MaterialSlots[i].SlotName = RawData.MaterialSlots[i].SlotName;
        OutMeshData.MaterialSlots[i].Material = RawData.MaterialSlots[i].Material;
    }

    // boneIndex 매핑 및 Global, Local, BindInv 행렬   
    BuildBoneTable(Mesh, OutMeshData);

    // Vertex의 BoneWeight 계산
    BuildBoneWeight(Mesh, RawData.VertexControlPointIndices, OutMeshData);

    // 최종적으로 Bone의 계층구조 완성
    BuildBoneHierarchy(Mesh, OutMeshData);

    return true;
}

void FFBXImporter::BuildMaterialSlot(FbxNode* Node, OUT FFBXMeshRawData& OutRawData)
{
    int32 MatCnt = static_cast<int32>(Node->GetMaterialCount());

    OutRawData.MaterialSlots.clear();

    if (MatCnt <= 0)
    {
        FStaticMeshMaterialSlot MaterialSlot = {};
        MaterialSlot.SlotName = "DefaultMaterial";
        MaterialSlot.Material = FResourceManager::Get().GetMaterial("DefaultWhite");
        OutRawData.MaterialSlots.push_back(MaterialSlot);
        return;
    }

    for (int32 i = 0; i < MatCnt; ++i)
    {
        FStaticMeshMaterialSlot MaterialSlot = {};
        FbxSurfaceMaterial* Material = Node->GetMaterial(i);

        MaterialSlot.SlotName = GetFbxMaterialName(Material);
        MaterialSlot.Material = BuildMaterialInterface(Material);

        OutRawData.MaterialSlots.push_back(MaterialSlot);
    }
}

UMaterialInterface* FFBXImporter::BuildMaterialInterface(FbxSurfaceMaterial* Material)
{
    const FString MaterialName = GetFbxMaterialName(Material);
    if (UMaterial* ExistingMaterial = FResourceManager::Get().GetMaterial(MaterialName))
    {
        return ExistingMaterial;
    }

    UMaterial* ParentMaterial = FResourceManager::Get().GetMaterial("DefaultWhite");
    if (ParentMaterial == nullptr)
    {
        return nullptr;
    }

    UMaterialInstance* Instance = UMaterialInstance::CreateTransient(ParentMaterial);
    Instance->Name = MaterialName;

    const FVector DiffuseColor = GetFbxVectorProperty(Material, FbxSurfaceMaterial::sDiffuse, FVector(0.8f, 0.8f, 0.8f));
    const FVector SpecularColor = GetFbxVectorProperty(Material, FbxSurfaceMaterial::sSpecular, FVector::ZeroVector);
    const FVector EmissiveColor = GetFbxVectorProperty(Material, FbxSurfaceMaterial::sEmissive, FVector::ZeroVector);
    const float   Shininess = GetFbxFloatProperty(Material, FbxSurfaceMaterial::sShininess, 0.0f);
    const float   Transparency = GetFbxFloatProperty(Material, FbxSurfaceMaterial::sTransparencyFactor, 0.0f);

    Instance->SetParam("BaseColor", FMaterialParamValue(DiffuseColor));
    Instance->SetParam("SpecularColor", FMaterialParamValue(SpecularColor));
    Instance->SetParam("EmissiveColor", FMaterialParamValue(EmissiveColor));
    Instance->SetParam("Shininess", FMaterialParamValue(Shininess));
    Instance->SetParam("Opacity", FMaterialParamValue(1.0f - Transparency));

    UTexture* DefaultWhite = FResourceManager::Get().GetTexture("DefaultWhite");
    UTexture* DefaultNormal = FResourceManager::Get().GetTexture("DefaultNormal");

    UTexture* DiffuseTexture = nullptr;
    if (FbxFileTexture* FbxDiffuseTexture = GetFirstFileTexture(Material, FbxSurfaceMaterial::sDiffuse))
    {
        FString TexturePath = FbxDiffuseTexture->GetFileName();
        if (TexturePath.empty())
        {
            TexturePath = FbxDiffuseTexture->GetRelativeFileName();
        }

        if (!TexturePath.empty())
        {
            std::filesystem::path FsTexturePath(FPaths::ToWide(TexturePath));
            if (!FsTexturePath.is_absolute() && !ImportDirectory.empty())
            {
                FsTexturePath = std::filesystem::path(FPaths::ToWide(ImportDirectory)) / FsTexturePath;
            }

            TexturePath = FPaths::ToUtf8(FsTexturePath.lexically_normal().generic_wstring());
            DiffuseTexture = FResourceManager::Get().LoadTexture(TexturePath);
        }
    }

    Instance->SetParam("DiffuseMap", FMaterialParamValue(DiffuseTexture ? DiffuseTexture : DefaultWhite));
    Instance->SetParam("SpecularMap", FMaterialParamValue(DefaultWhite));
    Instance->SetParam("NormalMap", FMaterialParamValue(DefaultNormal));
    Instance->SetParam("BumpMap", FMaterialParamValue(DefaultWhite));
    Instance->SetParam("bHasDiffuseMap", FMaterialParamValue(DiffuseTexture != nullptr));
    Instance->SetParam("bHasSpecularMap", FMaterialParamValue(false));
    Instance->SetParam("bHasNormalMap", FMaterialParamValue(false));
    Instance->SetParam("bHasBumpMap", FMaterialParamValue(false));
    Instance->SetParam("ScrollUV", FMaterialParamValue(FVector2(0.0f, 0.0f)));

    return Instance;
}

void FFBXImporter::BuildBoneTable(FbxMesh* Mesh, OUT FFBXSkeletalMeshImportData& OutMeshDatas)
{
    BoneIdToIndex.clear();
    IndexWeightMap.clear();
    OutMeshDatas.Bones.clear();

    if (!Mesh)
    {
        return;
    }

    FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(0, FbxDeformer::eSkin));
    if (!Skin)
    {
        return;
    }

    int32 ClusterCnt = Skin->GetClusterCount();
    for (int32 i = 0; i < ClusterCnt; ++i)
    {
        FbxCluster* Cluster = Skin->GetCluster(i);
        if (!Cluster)
        {
            continue;
        }

        FbxAMatrix MeshBindMatrix;
        Cluster->GetTransformMatrix(MeshBindMatrix);

        RegisterBone(Cluster->GetLink(), Cluster, ConvertFbxMatrix(MeshBindMatrix), OutMeshDatas);
    }
}

bool FFBXImporter::IsSkeletonNode(FbxNode* Node) const
{
    FbxNodeAttribute* Attribute = Node ? Node->GetNodeAttribute() : nullptr;
    return Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton;
}

int32 FFBXImporter::FindRegisteredBoneParentIndex(FbxNode* Node) const
{
    FbxNode* ParentNode = Node ? Node->GetParent() : nullptr;
    while (ParentNode)
    {
        const auto ParentIt = BoneIdToIndex.find(ParentNode->GetUniqueID());
        if (ParentIt != BoneIdToIndex.end())
        {
            return ParentIt->second;
        }

        ParentNode = ParentNode->GetParent();
    }

    return -1;
}

FMatrix FFBXImporter::CalculateBoneLocalBindTransform(
    FbxNode* BoneNode,
    const FMatrix& BoneBindMesh,
    const FFBXSkeletalMeshImportData& MeshData) const
{
    const int32 ParentBoneIndex = FindRegisteredBoneParentIndex(BoneNode);
    if (ParentBoneIndex >= 0 && ParentBoneIndex < static_cast<int32>(MeshData.Bones.size()))
    {
        return BoneBindMesh * MeshData.Bones[ParentBoneIndex].GlobalTransform.GetInverse();
    }

    return BoneBindMesh;
}

int32 FFBXImporter::RegisterBone(FbxNode* BoneNode, FbxCluster* Cluster, const FMatrix& MeshBindGlobal, OUT FFBXSkeletalMeshImportData& OutMeshDatas)
{
    if (!BoneNode)
    {
        return -1;
    }

    FbxNode* ParentNode = BoneNode->GetParent();
    if (IsSkeletonNode(ParentNode))
    {
        RegisterBone(ParentNode, nullptr, MeshBindGlobal, OutMeshDatas);
    }

    // 클러스터가 있는 경우엔 해당 노드 바인드 포즈 기준의 Global인 LinkBindMatrix를 써야함
    FMatrix BoneBindGlobal = ConvertFbxMatrix(BoneNode->EvaluateGlobalTransform());
    if (Cluster)
    {
        FbxAMatrix LinkBindMatrix;
        Cluster->GetTransformLinkMatrix(LinkBindMatrix);
        BoneBindGlobal = ConvertFbxMatrix(LinkBindMatrix);
    }

    // Mesh local 기준에서의 LocalTrnasform과 GlobalTrnasform을 만들어서 저장
    const FMatrix BoneBindMesh = BoneBindGlobal * MeshBindGlobal.GetInverse();

    const uint64 BoneNodeKey = BoneNode->GetUniqueID();
    auto Iter = BoneIdToIndex.find(BoneNodeKey);
    if (Iter != BoneIdToIndex.end())
    {
        const int32 BoneIndex = Iter->second;

        if (BoneIndex >= 0 && BoneIndex < static_cast<int32>(OutMeshDatas.Bones.size()))
        {
            FBoneInfo& BoneInfo = OutMeshDatas.Bones[BoneIndex];
            BoneInfo.GlobalTransform = BoneBindMesh;
            BoneInfo.LocalTransform = CalculateBoneLocalBindTransform(BoneNode, BoneBindMesh, OutMeshDatas);
            BoneInfo.InverseBindTransform = BoneBindMesh.GetInverse();
        }

        return BoneIndex;
    }

    FBoneInfo BoneInfo = {};
    BoneInfo.Name = BoneNode->GetName();
    BoneInfo.LocalTransform = CalculateBoneLocalBindTransform(BoneNode, BoneBindMesh, OutMeshDatas);
    BoneInfo.GlobalTransform = BoneBindMesh;
    BoneInfo.InverseBindTransform = BoneBindMesh.GetInverse();

    const int32 BoneIndex = static_cast<int32>(OutMeshDatas.Bones.size());
    BoneIdToIndex[BoneNodeKey] = BoneIndex;
    OutMeshDatas.Bones.push_back(BoneInfo);

    return BoneIndex;
}

void FFBXImporter::BuildBoneHierarchy(FbxMesh* Mesh, OUT FFBXSkeletalMeshImportData& OutMeshDatas)
{
    if (!Mesh)
    {
        return;
    }

    FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(0, FbxDeformer::eSkin));
    if (!Skin)
    {
        return;
    }

    for (FBoneInfo& BoneInfo : OutMeshDatas.Bones)
    {
        BoneInfo.ParentIndex = -1;
    }

    const int32 ClusterCnt = Skin->GetClusterCount();
    for (int32 i = 0; i < ClusterCnt; ++i)
    {
        FbxCluster* Cluster = Skin->GetCluster(i);
        if (!Cluster)
        {
            continue;
        }

        FbxNode* BoneNode = Cluster->GetLink();
        while (BoneNode && IsSkeletonNode(BoneNode))
        {
            const auto BoneIt = BoneIdToIndex.find(BoneNode->GetUniqueID());
            if (BoneIt != BoneIdToIndex.end())
            {
                const int32 BoneIndex = BoneIt->second;
                if (BoneIndex >= 0 && BoneIndex < static_cast<int32>(OutMeshDatas.Bones.size()))
                {
                    OutMeshDatas.Bones[BoneIndex].ParentIndex = FindRegisteredBoneParentIndex(BoneNode);
                }
            }

            BoneNode = BoneNode->GetParent();
        }
    }

    for (FBoneInfo& BoneInfo : OutMeshDatas.Bones)
    {
        if (BoneInfo.ParentIndex >= 0 && BoneInfo.ParentIndex < static_cast<int32>(OutMeshDatas.Bones.size()))
        {
            BoneInfo.LocalTransform =
                BoneInfo.GlobalTransform * OutMeshDatas.Bones[BoneInfo.ParentIndex].GlobalTransform.GetInverse();
        }
        else
        {
            BoneInfo.LocalTransform = BoneInfo.GlobalTransform;
        }
    }
}

void FFBXImporter::BuildBoneWeight(FbxMesh* Mesh, const TArray<int32>& VertexControlPointsIndices, FFBXSkeletalMeshImportData& OutMeshDatas)
{
    IndexWeightMap.clear();

    if (!Mesh)
    {
        return;
    }

    FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(0, FbxDeformer::eSkin));
    if (!Skin)
    {
        return;
    }

    int32 ClusterCnt = Skin->GetClusterCount();
    
    for (int32 i = 0; i < ClusterCnt; ++i)
    {
        FbxCluster* Cluster = Skin->GetCluster(i);
        if (!Cluster)
        {
            continue;
        }

        FbxNode* BoneNode = Cluster->GetLink();
        if (!BoneNode)
        {
            continue;
        }

        const auto BoneIt = BoneIdToIndex.find(BoneNode->GetUniqueID());
        if (BoneIt == BoneIdToIndex.end())
        {
            continue;
        }

        const int32 BoneIndex = BoneIt->second;

        // 클러스터를 순회하면서 ControlPoint 번호를 Key로 삼고 boneIndex와 Weight를 설정
        int32 ControlPointCnt = Cluster->GetControlPointIndicesCount();

        int32* ControlPointIndices = Cluster->GetControlPointIndices();
        double* ControlPointWeight = Cluster->GetControlPointWeights();

        for (int j = 0; j < ControlPointCnt; ++j)
        {
            int32 ControlPointIndexKey = ControlPointIndices[j];
            float Weight = static_cast<float>(ControlPointWeight[j]);
            if (Weight <= 0.0f)
            {
                continue;
            }

            IndexWeightMap[ControlPointIndexKey].push_back({ BoneIndex, Weight });
        }
    }

    for (int32 VertexIndex = 0; VertexIndex < static_cast<int32>(OutMeshDatas.Vertices.size()); ++VertexIndex)
    {
        if (VertexIndex >= static_cast<int32>(VertexControlPointsIndices.size()))
        {
            break;
        }

        FSkeletalVertex& Vertex = OutMeshDatas.Vertices[VertexIndex];
        for (int32 k = 0; k < 4; ++k)
        {
            Vertex.BoneIndices[k] = 0;
            Vertex.BoneWeights[k] = 0.0f;
        }

        const int32 ControlPointIndex = VertexControlPointsIndices[VertexIndex];
        auto WeightIt = IndexWeightMap.find(ControlPointIndex);
        if (WeightIt == IndexWeightMap.end())
        {
            if (!OutMeshDatas.Bones.empty())
            {
                Vertex.BoneWeights[0] = 1.0f;
            }
            continue;
        }

        TArray<FBoneIndexWeight>& Weights = WeightIt->second;
        std::sort(Weights.begin(), Weights.end(), CompareBoneWeightDescending);

        float WeightSum = 0.0f;
        const int32 InfluenceCount = std::min<int32>(4, static_cast<int32>(Weights.size()));
        for (int32 k = 0; k < InfluenceCount; ++k)
        {
            Vertex.BoneIndices[k] = static_cast<uint32>(Weights[k].BoneIdx);
            Vertex.BoneWeights[k] = Weights[k].BoneWeight;
            WeightSum += Weights[k].BoneWeight;
        }

        if (WeightSum > 0.0f)
        {
            for (int32 k = 0; k < InfluenceCount; ++k)
            {
                Vertex.BoneWeights[k] /= WeightSum;
            }
        }
    }
}

FString FFBXImporter::GetFbxMaterialName(FbxSurfaceMaterial* Material) const
{
    return Material ? FString(Material->GetName()) : FString("DefaultMaterial");
}

FVector FFBXImporter::GetFbxVectorProperty(FbxSurfaceMaterial* Material, const char* PropertyName, const FVector& DefaultValue) const
{
    if (Material == nullptr)
    {
        return DefaultValue;
    }

    FbxProperty Property = Material->FindProperty(PropertyName);
    if (!Property.IsValid())
    {
        return DefaultValue;
    }

    const FbxDouble3 Value = Property.Get<FbxDouble3>();
    return FVector(
        static_cast<float>(Value[0]),
        static_cast<float>(Value[1]),
        static_cast<float>(Value[2]));
}

float FFBXImporter::GetFbxFloatProperty(FbxSurfaceMaterial* Material, const char* PropertyName, float DefaultValue) const
{
    if (Material == nullptr)
    {
        return DefaultValue;
    }

    FbxProperty Property = Material->FindProperty(PropertyName);
    if (!Property.IsValid())
    {
        return DefaultValue;
    }

    return static_cast<float>(Property.Get<FbxDouble>());
}

FbxFileTexture* FFBXImporter::GetFirstFileTexture(FbxSurfaceMaterial* Material, const char* PropertyName) const
{
    if (Material == nullptr)
    {
        return nullptr;
    }

    FbxProperty Property = Material->FindProperty(PropertyName);
    if (!Property.IsValid() || Property.GetSrcObjectCount<FbxFileTexture>() <= 0)
    {
        return nullptr;
    }

    return Property.GetSrcObject<FbxFileTexture>(0);
}

void FFBXImporter::BuildSectionsFromSlotIndices(const TArray<TArray<uint32>>& SlotIndices, FFBXMeshRawData& OutRawData)
{
    OutRawData.Indices.clear();
    OutRawData.Sections.clear();

    for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(SlotIndices.size()); ++SlotIndex)
    {
        const TArray<uint32>& IndicesPerSlot = SlotIndices[SlotIndex];
        if (IndicesPerSlot.empty())
        {
            continue;
        }

        FStaticMeshSection Section = {};
        Section.StartIndex = static_cast<uint32>(OutRawData.Indices.size());
        Section.IndexCount = static_cast<uint32>(IndicesPerSlot.size());
        Section.MaterialSlotIndex = SlotIndex;

        OutRawData.Indices.insert(
            OutRawData.Indices.end(),
            IndicesPerSlot.begin(),
            IndicesPerSlot.end());

        OutRawData.Sections.push_back(Section);
    }
}

void FFBXImporter::BuildTangentsAndBitangents(FFBXMeshRawData& OutRawData)
{
    if (OutRawData.Vertices.empty() || OutRawData.Indices.empty())
    {
        return;
    }

    TArray<FVector> AccumulatedTangents(OutRawData.Vertices.size(), FVector::ZeroVector);
    TArray<FVector> AccumulatedBitangents(OutRawData.Vertices.size(), FVector::ZeroVector);

    for (size_t TriangleIndex = 0; TriangleIndex + 2 < OutRawData.Indices.size(); TriangleIndex += 3)
    {
        const uint32 Index0 = OutRawData.Indices[TriangleIndex + 0];
        const uint32 Index1 = OutRawData.Indices[TriangleIndex + 1];
        const uint32 Index2 = OutRawData.Indices[TriangleIndex + 2];

        if (Index0 >= OutRawData.Vertices.size() ||
            Index1 >= OutRawData.Vertices.size() ||
            Index2 >= OutRawData.Vertices.size())
        {
            continue;
        }

        const FNormalVertex& Vertex0 = OutRawData.Vertices[Index0];
        const FNormalVertex& Vertex1 = OutRawData.Vertices[Index1];
        const FNormalVertex& Vertex2 = OutRawData.Vertices[Index2];

        const FVector Edge1 = Vertex1.Position - Vertex0.Position;
        const FVector Edge2 = Vertex2.Position - Vertex0.Position;

        const FVector2 DeltaUV1 = Vertex1.UVs - Vertex0.UVs;
        const FVector2 DeltaUV2 = Vertex2.UVs - Vertex0.UVs;

        const float Denominator = DeltaUV1.X * DeltaUV2.Y - DeltaUV2.X * DeltaUV1.Y;
        if (std::fabs(Denominator) < 1e-8f)
        {
            continue;
        }

        const float InvDenominator = 1.0f / Denominator;

        FVector Tangent = (Edge1 * DeltaUV2.Y - Edge2 * DeltaUV1.Y) * InvDenominator;
        FVector Bitangent = (Edge2 * DeltaUV1.X - Edge1 * DeltaUV2.X) * InvDenominator;

        Tangent.Normalize();
        Bitangent.Normalize();

        AccumulatedTangents[Index0] += Tangent;
        AccumulatedTangents[Index1] += Tangent;
        AccumulatedTangents[Index2] += Tangent;

        AccumulatedBitangents[Index0] += Bitangent;
        AccumulatedBitangents[Index1] += Bitangent;
        AccumulatedBitangents[Index2] += Bitangent;
    }

    for (size_t VertexIndex = 0; VertexIndex < OutRawData.Vertices.size(); ++VertexIndex)
    {
        FNormalVertex& Vertex = OutRawData.Vertices[VertexIndex];

        FVector Tangent = AccumulatedTangents[VertexIndex];
        FVector Bitangent = AccumulatedBitangents[VertexIndex];

        if (Tangent.SizeSquared() > 1e-8f)
        {
            Tangent.Normalize();
        }
        else
        {
            Tangent = FVector(1.0f, 0.0f, 0.0f);
        }

        if (Bitangent.SizeSquared() > 1e-8f)
        {
            Bitangent.Normalize();
        }
        else
        {
            Bitangent = FVector::CrossProduct(Vertex.Normal, Tangent);
            if (Bitangent.SizeSquared() > 1e-8f)
            {
                Bitangent.Normalize();
            }
            else
            {
                Bitangent = FVector(0.0f, 1.0f, 0.0f);
            }
        }

        Vertex.Tangent = Tangent;
        Vertex.Bitangent = Bitangent;
    }
}

FString FFBXImporter::GetFirstUVName(FbxMesh* Mesh)
{
    FbxStringList UVNames;
    Mesh->GetUVSetNames(UVNames);
    if (UVNames.GetCount() <= 0)
    {
        return "";
    }
    // 첫번째 채널 UV 반환 주로 Diffuse Normal 처럼 일반적인 Map 전용
    // LightMap을 쓰는 경우 다른 채널 써야함
    return FString(UVNames[0]);
}

FAABB FFBXImporter::BuildLocalBounds(const FFBXStaticMeshImportData& ImportData) const
{
    FAABB Bounds;
    Bounds.Reset();

    for (const FNormalVertex& Vertex : ImportData.Vertices)
    {
        Bounds.Expand(Vertex.Position);
    }

    return Bounds;
}

FAABB FFBXImporter::BuildLocalBounds(const FFBXSkeletalMeshImportData& ImportData) const
{
    FAABB Bounds;
    Bounds.Reset();

    for (const FSkeletalVertex& Vertex : ImportData.Vertices)
    {
        Bounds.Expand(Vertex.Position);
    }

    return Bounds;
}

int32 FFBXImporter::GetPolygonMaterialIndex(FbxMesh* Mesh, int32 PolygonIndex)
{
    FbxLayerElementMaterial* MaterialElement = Mesh->GetElementMaterial();
    if (!MaterialElement)
    {
        return 0;
    }

    if (MaterialElement->GetMappingMode() == FbxLayerElement::eByPolygon)
    {
        return MaterialElement->GetIndexArray().GetAt(PolygonIndex);
    }

    // eAllSame
    return 0;
}

FMatrix FFBXImporter::ConvertFbxMatrix(const FbxAMatrix& fbxMatrix)
{
    FMatrix Result;
    for (int32 Row = 0; Row < 4; ++Row)
    {
        for (int32 Col = 0; Col < 4; ++Col)
        {
            Result.M[Row][Col] = static_cast<float>(fbxMatrix.Get(Row, Col));
        }
    }
    return Result;
}
