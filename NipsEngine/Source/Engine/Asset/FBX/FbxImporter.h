#pragma once

#include <fbxsdk.h>

#include "Asset/SkeletalMeshTypes.h"
#include "Asset/StaticMeshTypes.h"
#include "Core/Containers/Map.h"

class FFbxImporter
{
public:
    FFbxImporter() = default;
    ~FFbxImporter() = default;

    FStaticMesh* ImportStaticMesh(FbxScene* Scene);
    bool ImportStaticMesh(FbxScene* Scene, FStaticMesh& OutStaticMesh);
    FSkeletalMesh* ImportSkeletalMesh(FbxScene* Scene);
    bool ImportSkeletalMesh(FbxScene* Scene, FSkeletalMesh& OutSkeletalMesh);

private:
    struct FVertexKey
    {
        FVector Position;
        FVector Normal;
        FVector2 UVs;

        bool operator==(const FVertexKey& Other) const
        {
            return Position == Other.Position && Normal == Other.Normal && UVs == Other.UVs;
        }
    };

    struct FVertexKeyHasher
    {
        size_t operator()(const FVertexKey& Key) const noexcept;
    };

    struct FSkeletalVertexKey
    {
        FVector Position;
        FVector Normal;
        FVector2 UVs;
        uint32 BoneIndices[FSkeletalVertex::MaxBoneInfluences] = {};
        float BoneWeights[FSkeletalVertex::MaxBoneInfluences] = {};

        bool operator==(const FSkeletalVertexKey& Other) const;
    };

    struct FSkeletalVertexKeyHasher
    {
        size_t operator()(const FSkeletalVertexKey& Key) const noexcept;
    };

    struct FControlPointSkinData
    {
        TArray<FBoneWeightPair> Influences;
    };

    void ProcessNode(FbxNode* Node, FStaticMesh& OutStaticMesh);
    void ProcessMesh(FbxNode* Node, FbxMesh* Mesh, FStaticMesh& OutStaticMesh);
    void ProcessSkeletonNode(FbxNode* Node, int32 ParentIndex, FSkeletalMesh& OutSkeletalMesh);
    void ProcessSkeletalNode(FbxNode* Node, FSkeletalMesh& OutSkeletalMesh);
    void ProcessSkeletalMesh(FbxNode* Node, FbxMesh* Mesh, FSkeletalMesh& OutSkeletalMesh);

    uint32 AddPolygonVertex(
        FbxMesh* Mesh,
        const FbxAMatrix& NodeTransform,
        const char* UVSetName,
        int32 PolygonIndex,
        int32 VertexInPolygon,
        FStaticMesh& OutStaticMesh);

    uint32 AddSkeletalPolygonVertex(
        FbxMesh* Mesh,
        const FbxAMatrix& NodeTransform,
        const char* UVSetName,
        int32 PolygonIndex,
        int32 VertexInPolygon,
        const TArray<FControlPointSkinData>& ControlPointSkinData,
        FSkeletalMesh& OutSkeletalMesh);

    uint32 GetOrCreateVertexIndex(const FNormalVertex& Vertex, FStaticMesh& OutStaticMesh);
    uint32 GetOrCreateSkeletalVertexIndex(const FSkeletalVertex& Vertex, FSkeletalMesh& OutSkeletalMesh);
    uint32 GetOrCreateBoneIndex(FbxNode* BoneNode, FSkeletalMesh& OutSkeletalMesh);
    TArray<FControlPointSkinData> BuildControlPointSkinData(FbxMesh* Mesh, FSkeletalMesh& OutSkeletalMesh);
    void NormalizeControlPointSkinData(TArray<FControlPointSkinData>& ControlPointSkinData) const;

    void ResetStaticMesh(FStaticMesh& OutStaticMesh) const;
    void ResetSkeletalMesh(FSkeletalMesh& OutSkeletalMesh) const;
    void BuildDefaultSection(FStaticMesh& OutStaticMesh) const;
    void BuildDefaultSection(FSkeletalMesh& OutSkeletalMesh) const;
    void BuildTangentsAndBitangents(FStaticMesh& OutStaticMesh) const;
    void BuildTangentsAndBitangents(FSkeletalMesh& OutSkeletalMesh) const;
    FAABB BuildLocalBounds(const FStaticMesh& StaticMesh) const;
    FAABB BuildLocalBounds(const FSkeletalMesh& SkeletalMesh) const;

private:
    TMap<FVertexKey, uint32, FVertexKeyHasher> UniqueVertices;
    TMap<FSkeletalVertexKey, uint32, FSkeletalVertexKeyHasher> UniqueSkeletalVertices;
    TMap<FbxNode*, uint32> BoneIndexByNode;
};
