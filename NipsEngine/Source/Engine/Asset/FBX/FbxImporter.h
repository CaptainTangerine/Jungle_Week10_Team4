#pragma once

#include <fbxsdk.h>

#include "Asset/StaticMeshTypes.h"
#include "Core/Containers/Map.h"

class FFbxImporter
{
public:
    FFbxImporter() = default;
    ~FFbxImporter() = default;

    FStaticMesh* ImportStaticMesh(FbxScene* Scene);
    bool ImportStaticMesh(FbxScene* Scene, FStaticMesh& OutStaticMesh);

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

    void ProcessNode(FbxNode* Node, FStaticMesh& OutStaticMesh);
    void ProcessMesh(FbxNode* Node, FbxMesh* Mesh, FStaticMesh& OutStaticMesh);

    uint32 AddPolygonVertex(
        FbxMesh* Mesh,
        const FbxAMatrix& NodeTransform,
        const char* UVSetName,
        int32 PolygonIndex,
        int32 VertexInPolygon,
        FStaticMesh& OutStaticMesh);

    uint32 GetOrCreateVertexIndex(const FNormalVertex& Vertex, FStaticMesh& OutStaticMesh);

    void ResetStaticMesh(FStaticMesh& OutStaticMesh) const;
    void BuildDefaultSection(FStaticMesh& OutStaticMesh) const;
    void BuildTangentsAndBitangents(FStaticMesh& OutStaticMesh) const;
    FAABB BuildLocalBounds(const FStaticMesh& StaticMesh) const;

private:
    TMap<FVertexKey, uint32, FVertexKeyHasher> UniqueVertices;
};
