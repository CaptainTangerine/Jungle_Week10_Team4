#pragma once
#include <fbxsdk.h>
#include "IAssetLoader.h"
#include "Object/FName.h"
#include "SkeletalMeshTypes.h"

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
        
        TArray<FbxNode*> MeshNodes;
        TSet<FbxNode*> RequiredBoneNodes;
        TMap<FbxNode*, uint32> BoneNodeToIndex;
        TMap<FbxMesh*, TMap<uint32, TArray<FBoneInfluence>>> ControlPointWeights;
        TMap<FbxNode*, int32> MeshNodeToMaterialSlotBaseIndex;
        
        //EvaluateGlobalTransform()도 Local -> global
        //Cluster->GetTransformLinkMatrix()도 Local -> global
        
        //이 node는 Cluster가 준 행렬이 있으니 parent * local로 덮어쓰면 안 됨
        TSet<FbxNode*> ClusterBoneNodes;
        
        //그럼 둘중 어떤 Local->global을 쓸거냐 쓸 Local->global이 Value에 들어있음
        TMap<FbxNode*, FbxAMatrix> CorrectedNodeGlobalMatrices;
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
    void CollectClusterData(FParseContext& Context);
    void RebuildDerivedGlobalBindTransforms(FbxNode* Node, FParseContext& Context, const FbxAMatrix& ParentGlobalMatrix, bool bHasParentGlobalMatrix);
    bool ImportMeshGeometry(FParseContext& Context);
    
    bool PrepareTraverse(FbxScene* Scene);
    bool LoadScene(const char* FilePath,FbxScene*& InOutScene);
    FMatrix ConvertFbxMatrix(FbxAMatrix Matrix);
};
