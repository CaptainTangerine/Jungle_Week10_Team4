#pragma once
#include <fbxsdk.h>
#include "IAssetLoader.h"
#include "Object/FName.h"
#include "SkeletalMeshTypes.h"

class FFbxLoader : IAssetLoader
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
    bool ImportMeshGeometry(FParseContext& Context);
    
    //Override Related
    bool SupportsExtension(const FString& Extension) const override;
    FString GetLoaderName() const override;
private:
    FbxManager* Manager = nullptr;
    
private:
    bool PrepareTraverse(FbxScene* Scene);
    bool LoadScene(const char* FilePath,FbxScene*& InOutScene);
    void ConstructSkeletonRecursive(FbxNode* Node, int32 ParentBoneIndex, FParseContext& Context);
    void CollectMeshNodesAndMaterialSlot(FbxNode* Node,FParseContext& Context);
    void CollectRequiredBoneNodes(FParseContext& Context);
    void CollectClusterData(FParseContext& Context);
    FMatrix ConvertFbxMatrix(FbxAMatrix Matrix);
    void LogImportValidation(const FParseContext& Context) const;
};
