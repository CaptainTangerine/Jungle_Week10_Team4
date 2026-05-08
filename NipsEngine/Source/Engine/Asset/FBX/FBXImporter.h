#pragma once
#include "Asset/FBX/FBXSDKContext.h"
#include "Asset/StaticMeshTypes.h"
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
};

// Gemotry 정보 관리
struct FFBXStaticMeshImportData
{
    FString Name; // 노드와 실제 메시의 이름이 다를 수 있음
    int32 SourceNodeIndex = -1;

    TArray<FNormalVertex> Vertices;
    TArray<uint32> Indices;
    TArray<FStaticMeshSection> Sections;
    TArray<FStaticMeshMaterialSlot> MatreialSlots;
};

struct FFBXSkeletalMeshImportData
{
    FString Name;
    int32 SourceNodeIndex = -1;
    int32 SkinDeformerCount = 0;
    int32 ControlPointCount = 0;
    int32 PolygonCount = 0;
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

class FFBXImporter
{
public:
    FFBXImporter() = default;
    ~FFBXImporter() = default;

    FFBXImporter(const FFBXImporter&) = delete;
    FFBXImporter& operator=(const FFBXImporter&) = delete;

public:
    FFBXImportScene Import(const FString& Path);
    FStaticMesh* CreateStaticMeshFromImportData(const FFBXStaticMeshImportData& ImportData) const;

private:
    void TraversalNode(FbxNode* Node, int32 ParentIndex, OUT FFBXImportScene& OutImportScene);
    int32 AddImportNode(FbxNode* Node, int32 ParentIndex, OUT FFBXImportScene& OutImportScene);
    
    bool  BuildStaticMeshImportData(FbxNode* Node, FbxMesh* Mesh, OUT FFBXStaticMeshImportData& OutMeshData);

    // Util
    void BuildMaterialSlot(FbxNode* Node, OUT FFBXStaticMeshImportData& OutMeshData);
    void BuildSectionsFromSlotIndices(const TArray<TArray<uint32>>& SlotIndices, FFBXStaticMeshImportData& OutMesh);
    void BuildTangentsAndBitangents(FFBXStaticMeshImportData& OutMesh);
    UMaterialInterface* BuildMaterialInterface(FbxSurfaceMaterial* Material);
    FString GetFbxMaterialName(FbxSurfaceMaterial* Material) const;
    FVector GetFbxVectorProperty(FbxSurfaceMaterial* Material, const char* PropertyName, const FVector& DefaultValue) const;
    float GetFbxFloatProperty(FbxSurfaceMaterial* Material, const char* PropertyName, float DefaultValue) const;
    FbxFileTexture* GetFirstFileTexture(FbxSurfaceMaterial* Material, const char* PropertyName) const;

    int32 GetPolygonMaterialIndex(FbxMesh* Mesh, int32 PolygonIndex);

    // TODO : 라이트 맵 고려 시 다른 채널의 UVName도 지원
    FString GetFirstUVName(FbxMesh* Mesh);

    FAABB BuildLocalBounds(const FFBXStaticMeshImportData& ImportData) const;

private:
    FMatrix ConvertFbxMatrix(const FbxAMatrix& fbxMat);
    FString ImportDirectory;
};

