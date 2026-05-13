#pragma once
#include "Asset/FBX/FBXSDKContext.h"
#include "Asset/StaticMeshTypes.h"
#include "Asset/SkeletalMeshTypes.h"
#include "Core/ResourceTypes.h"

// 계층 구조와 Transform 관리
struct FFBXImportNode
{
    FString Name;
    int32 ParentIndex = -1;
    TArray<int32> Children;

    FMatrix LocalTransformMatrix;
    FMatrix GlobalTransformMatrix;

    int32 StaticMeshIndex = -1;
    int32 SkeletalMeshIndex = -1;
    int32 BoneIndex = -1;
};

// Gemotry 정보 관리
struct FFBXStaticMeshImportData
{
    FString Name; // 노드와 실제 메시의 이름이 다를 수 있음
    int32 SourceNodeIndex = -1;

    TArray<FNormalVertex> Vertices;
    TArray<uint32> Indices;
    TArray<FStaticMeshSection> Sections;
    TArray<FStaticMeshMaterialSlot> MaterialSlots;
};

struct FFBXSkeletalMeshImportData
{
    FString Name;
    int32 SourceNodeIndex = -1;
    FMatrix SourceNodeLocalTransformMatrix = FMatrix::Identity;
    FMatrix SourceNodeGlobalTransformMatrix = FMatrix::Identity;

    TArray<FSkeletalVertex> Vertices;
    TArray<uint32> Indices;
    TArray<FSkeletalMeshSection> Sections;
    TArray<FSkeletalMeshMaterialSlot> MaterialSlots;

    TArray<FBoneInfo> Bones; // 본 계층구조
};

// BuildStaticMeshImportData / BuildSkeletalMeshImportData 가 공유하는 중간 Geometry 데이터
// FNormalVertex 기반으로 공통 작업을 처리한 뒤, 각 타입 빌드 함수에서 타입 변환만 수행
struct FFBXMeshRawData
{
    FString Name;
    int32 SourceNodeIndex = -1;

    TArray<FNormalVertex> Vertices;
    TArray<uint32> Indices;
    TArray<FStaticMeshSection> Sections;
    TArray<FStaticMeshMaterialSlot> MaterialSlots;

    // 각 Vertex가 어떤 ControlPoint에서 왔는지 기록 (스킨 본 웨이트 조회용)
    TArray<int32> VertexControlPointIndices;
};

// FBX는 하나의 모델이라기보다 Scene의 개념으로 둠
// 하나의 FBX 모델 -> StaticMesh와 SkeltalMesh가 공존이 가능함
struct FFBXImportScene
{
    FString SourceFilePath;

    TArray<FFBXImportNode> Nodes;
    TArray<FFBXStaticMeshImportData> StaticMeshes;
    TArray<FFBXSkeletalMeshImportData> SkeletalMeshes;
};

struct FFBXImportOptions
{
    bool bImportSkinnedMeshesAsStatic = false;
};

struct FBoneIndexWeight
{
    int32 BoneIdx;
    float BoneWeight;
};

class FFBXImporter
{
public:
    FFBXImporter() = default;
    ~FFBXImporter() = default;

    FFBXImporter(const FFBXImporter&) = delete;
    FFBXImporter& operator=(const FFBXImporter&) = delete;

public:
    FFBXImportScene Import(const FString& Path, const FFBXImportOptions& Options = FFBXImportOptions{});
    FStaticMesh*   CreateStaticMeshFromImportData(const FFBXStaticMeshImportData& ImportData) const;
    FSkeletalMesh* CreateSkeletalMeshFromtImportData(const FFBXSkeletalMeshImportData& ImportData) const;
    void AttachSceneDataToSkeletalMesh(
        const FFBXImportScene& ImportScene,
        int32 SkeletalMeshIndex,
        FSkeletalMesh& OutMesh) const;

private:
    struct FPendingSkinnedMeshNode
    {
        FbxNode* Node = nullptr;
        FbxMesh* Mesh = nullptr;
        FbxNode* SkeletonRoot = nullptr;
        int32 SceneNodeIndex = -1;
    };

    void TraversalNode(FbxNode* Node, int32 ParentIndex, OUT FFBXImportScene& OutImportScene);
    int32 AddImportNode(FbxNode* Node, int32 ParentIndex, OUT FFBXImportScene& OutImportScene);
    void BuildGroupedSkeletalMeshes(OUT FFBXImportScene& OutImportScene);
    void BuildBakedSceneStaticMesh(OUT FFBXImportScene& OutImportScene);
    void BuildGroupedSkinnedStaticMeshes(OUT FFBXImportScene& OutImportScene);
    bool BuildSkeletalMeshGroupImportData(
        const TArray<FPendingSkinnedMeshNode>& MeshNodes,
        OUT FFBXSkeletalMeshImportData& OutMeshData);
    bool MergeSkinnedGroupToStatic(
        const TArray<FPendingSkinnedMeshNode>& MeshNodes,
        OUT FFBXStaticMeshImportData& OutMeshData);
    void AppendRawDataToStaticMesh(
        const FFBXMeshRawData& RawData,
        const FMatrix& LocalToBakedRoot,
        OUT FFBXStaticMeshImportData& OutMeshData) const;

    /* Mesh 구조체를 채우기 위한 중간 데이터 */
    bool BuildMeshRawData(FbxNode* Node, FbxMesh* Mesh, OUT FFBXMeshRawData& OutRawData);
    bool BuildStaticMeshImportData(FbxNode* Node, FbxMesh* Mesh, OUT FFBXStaticMeshImportData& OutMeshData);
    bool BuildSkeletalMeshImportData(FbxNode* Node, FbxMesh* Mesh, OUT FFBXSkeletalMeshImportData& OutMeshData);

    void BuildMaterialSlot(FbxNode* Node, OUT FFBXMeshRawData& OutRawData);
    void BuildSectionsFromSlotIndices(const TArray<TArray<uint32>>& SlotIndices, FFBXMeshRawData& OutRawData);
    void BuildTangentsAndBitangents(FFBXMeshRawData& OutRawData);
    UMaterialInterface* BuildMaterialInterface(FbxSurfaceMaterial* Material);

    void BuildBoneTable(FbxMesh* Mesh , OUT FFBXSkeletalMeshImportData& OutMeshDatas);
    bool IsSkeletonNode(FbxNode* Node) const;
    FbxNode* FindSkeletonRootForMesh(FbxNode* MeshNode, FbxMesh* Mesh) const;
    int32 FindRegisteredBoneParentIndex(FbxNode* Node) const;
    FMatrix CalculateBoneLocalBindTransform(FbxNode* BoneNode, const FMatrix& BoneBindMesh, const FFBXSkeletalMeshImportData& MeshData) const;
    int32 RegisterBone(FbxNode* BoneNode, FbxCluster* Cluster, const FMatrix& MeshBindGlobal, OUT FFBXSkeletalMeshImportData& OutMeshDatas);
    int32 RegisterBoneForGroup(FbxNode* BoneNode, FbxCluster* Cluster, const FMatrix& GroupGlobalInverse, OUT FFBXSkeletalMeshImportData& OutMeshDatas);
    void BuildBoneTableForGroup(const TArray<FPendingSkinnedMeshNode>& MeshNodes, const FMatrix& GroupGlobalInverse, OUT FFBXSkeletalMeshImportData& OutMeshDatas);
    void BuildBoneWeight(FbxMesh* Mesh, const TArray<int32>& VertexControlPointsIndices, OUT FFBXSkeletalMeshImportData& OutMeshDatas);
    void ApplyBoneWeightForMesh(FbxMesh* Mesh, const TArray<int32>& VertexControlPointsIndices, int32 VertexBaseIndex, OUT FFBXSkeletalMeshImportData& OutMeshDatas);
    void BuildBoneHierarchy(FbxMesh* Mesh, OUT FFBXSkeletalMeshImportData& OutMeshDatas);
    void BuildBoneHierarchyForGroup(const TArray<FPendingSkinnedMeshNode>& MeshNodes, OUT FFBXSkeletalMeshImportData& OutMeshDatas);


    FAABB BuildLocalBounds(const FFBXStaticMeshImportData& ImportData) const;
    FAABB BuildLocalBounds(const FFBXSkeletalMeshImportData& ImportData) const;

private:
    FMatrix ConvertFbxMatrix(const FbxAMatrix& fbxMat);
    FString GetFbxMaterialName(FbxSurfaceMaterial* Material) const;
    FVector GetFbxVectorProperty(FbxSurfaceMaterial* Material, const char* PropertyName, const FVector& DefaultValue) const;
    float   GetFbxFloatProperty(FbxSurfaceMaterial* Material, const char* PropertyName, float DefaultValue) const;
    FbxFileTexture* GetFirstFileTexture(FbxSurfaceMaterial* Material, const char* PropertyName) const;
    int32 GetPolygonMaterialIndex(FbxMesh* Mesh, int32 PolygonIndex);

    // TODO : 라이트 맵 고려 시 다른 채널의 UVName도 지원
    FString GetFirstUVName(FbxMesh* Mesh);

private:
    FString ImportDirectory;
    FFBXImportOptions ImportOptions;

    TArray<FPendingSkinnedMeshNode> PendingSkinnedMeshNodes;
    TMap<uint64, int32> FbxNodeIdToImportNodeIndex;
    TMap<uint64, int32> BoneIdToIndex;
    TMap<int32, TArray<FBoneIndexWeight>> IndexWeightMap;

};
