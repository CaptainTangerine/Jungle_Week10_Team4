#pragma once
#include <fbxsdk.h>
#include "IAssetLoader.h"
#include "Object/FName.h"
#include "SkeletalMeshTypes.h"

class UMaterial;
class UMaterialInterface;

class FFbxLoader :public IAssetLoader
{
private:
    struct FBoneInfluence
    {
        int BoneIndex;
        float Weight;
    };
    struct FParseContext
    {
        FSkeletalMeshAsset Asset;
        FString SourceFilePath;
        FString SourceDirectory;
        
        TArray<FbxNode*> MeshNodes;
        TSet<FbxNode*> RequiredBoneNodes;
        TMap<FbxNode*, uint32> BoneNodeToIndex;
        TMap<FbxMesh*, TMap<uint32, TArray<FBoneInfluence>>> ControlPointWeights;
        TMap<FbxNode*, int32> MeshNodeToMaterialSlotBaseIndex;
    };
    
public:
    FFbxLoader();
    ~FFbxLoader() override;
    

    //logic Related
    bool ImportFbxFile(const char* FilePath);
    bool ImportFbxFile(const char* FilePath,FSkeletalMeshAsset& OutAsset);
    bool Traverse(FbxNode* Node,FParseContext& Context);
    
    //Override Related
    bool SupportsExtension(const FString& Extension) const override;
    FString GetLoaderName() const override;
private:
    FbxManager* Manager = nullptr;
    
private:
    void ConstructSkeletonRecursive(FbxNode* Node, int32 ParentBoneIndex, FParseContext& Context);
    void CollectMeshNodesAndMaterialSlot(FbxNode* Node,FParseContext& Context);
    void CollectRequiredBoneNodes(FParseContext& Context);
    void CollectRequiredDescendantNodes(FParseContext& Context);
    bool CollectRequiredDescendantNodesRecursive(FbxNode* Node, FParseContext& Context);
    bool ShouldImportNodeAsBone(FbxNode* Node) const;
    UMaterialInterface* ImportFbxMaterial(FbxSurfaceMaterial* Material, const FParseContext& Context) const;
    FString FindFbxTexturePath(FbxSurfaceMaterial* Material, const char* PropertyName, const FParseContext& Context) const;
    FString ResolveFbxTexturePath(const char* TexturePath, const FParseContext& Context) const;
    void ApplyMaterialParams(UMaterial* Material) const;
    void CollectClusterData(FParseContext& Context);
    void RebuildNonClusterBoneGlobals(FParseContext& Context);
    bool ImportMeshGeometry(FParseContext& Context);
    bool TryGetRigidSkinBoneIndex(FbxNode* MeshNode, const FParseContext& Context, int32& OutBoneIndex) const;
    void FillVertexBoneWeights(FbxMesh* Mesh, uint32 ControlPointIndex, const FParseContext& Context, FbxVertex& Vertex) const;
    
    bool PrepareTraverse(FbxScene* Scene);
    bool LoadScene(const char* FilePath,FbxScene*& InOutScene);
    FMatrix ConvertFbxMatrix(FbxAMatrix Matrix);
};
