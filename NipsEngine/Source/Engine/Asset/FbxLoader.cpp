#include "Core/EnginePCH.h"
#include "FbxLoader.h"

#include <algorithm>

FFbxLoader::FFbxLoader()
{
    Manager = FbxManager::Create();
    if (!Manager)
    {
        UE_LOG("Fbx Manager failed to create!");
        return;
    }
    
    FbxIOSettings* Ios = FbxIOSettings::Create(Manager,IOSROOT);
    Manager->SetIOSettings(Ios);
}

FFbxLoader::~FFbxLoader()
{
    if (Manager)
    {
        Manager->Destroy();
        Manager = nullptr;
    }
}


bool FFbxLoader::ImportFbxFile(const char* FilePath)
{
    FSkeletalMeshAsset ImportedAsset;
    return ImportFbxFile(FilePath, ImportedAsset);
}

bool FFbxLoader::ImportFbxFile(const char* FilePath, FSkeletalMeshAsset& OutAsset)
{
    if (Manager == nullptr)
    {
        UE_LOG("FBX Manager is not initialized");
        return false;
    }

    FbxScene* Scene= nullptr;
    if (LoadScene(FilePath,Scene))
    {
        if (!PrepareTraverse(Scene))
        {
            Scene->Destroy();
            return false;
        }

        FbxNode* RootNode = Scene->GetRootNode();
        if (RootNode == nullptr)
        {
            UE_LOG("FBX scene has no root node");
            Scene->Destroy();
            return false;
        }

        FParseContext ParseContext;
        
        const bool bTraverseSuccess = Traverse(RootNode,ParseContext);
        if (bTraverseSuccess)
        {
            OutAsset = std::move(ParseContext.Asset);
        }
        Scene->Destroy();
        return bTraverseSuccess;
    }
    return false;
}

bool FFbxLoader::Traverse(FbxNode* Node,FParseContext& Context)
{
    if (Node == nullptr)
    {
        return false;
    }

    CollectMeshNodesAndMaterialSlot(Node, Context);
    CollectRequiredBoneNodes(Context);
    CollectRequiredDescendantNodes(Context);
    ConstructSkeletonRecursive(Node, -1,Context);
    CollectClusterData(Context);
    RebuildDerivedGlobalBindTransforms(Node, Context, FbxAMatrix(), false);
    if (!ImportMeshGeometry(Context))
    {
        return false;
    }
    UE_LOG("[FbxLoader] MeshNodes: %zu, Bones: %zu, Vertices: %zu, Indices: %zu",
    Context.MeshNodes.size(),
    Context.Asset.Bones.size(),
    Context.Asset.Vertices.size(),
    Context.Asset.Indices.size());
    
    return true;
}


//-------------------Override Related--------------------------------Start
bool FFbxLoader::SupportsExtension(const FString& Extension) const
{
    return Extension == FString("fbx") || Extension == FString(".fbx") || Extension == FString("FBX") || Extension == FString(".FBX");
}

FString FFbxLoader::GetLoaderName() const
{
    return FString{ "FFbxLoader" };
}


//-------------------HelperFunction--------------------------------Start

void FFbxLoader::CollectMeshNodesAndMaterialSlot(FbxNode* Node, FParseContext& Context)
{
    if (!Node)
        return;
    
    FbxNodeAttribute* Attrib = Node->GetNodeAttribute();
    if (Attrib && Attrib->GetAttributeType() == FbxNodeAttribute::eMesh)
    {
        //Mesh
        Context.MeshNodes.push_back(Node);
        //Material
        const int32 MaterialSlotBaseIndex = static_cast<int32>(Context.Asset.MaterialSlots.size());
        Context.MeshNodeToMaterialSlotBaseIndex[Node] = MaterialSlotBaseIndex;

        const int MaterialCount = Node->GetMaterialCount();
        if (MaterialCount > 0)
        {
            for (int MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
            {
                FbxSurfaceMaterial* Material = Node->GetMaterial(MaterialIndex);
                FString SlotName = Material ? Material->GetName() : "Default";
                Context.Asset.MaterialSlots.push_back({ SlotName });
            }
        }
        else
        {
            Context.Asset.MaterialSlots.push_back({ "Default" });
        }
    }
    
    for (int i=0;i<Node->GetChildCount();i++)
    {
        CollectMeshNodesAndMaterialSlot(Node->GetChild(i),Context);
    }
}

void FFbxLoader::CollectRequiredBoneNodes(FParseContext& Context)
{
    for (int i=0;i<Context.MeshNodes.size();i++)
    {
        FbxMesh* Mesh = Context.MeshNodes[i]->GetMesh();
        if (Mesh == nullptr)
        {
            continue;
        }

        const int SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
        for (int SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
        {
            FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
            if (Skin == nullptr)
            {
                continue;
            }

            const int ClusterCount = Skin->GetClusterCount();
            for (int ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
            {
                FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
                if (Cluster == nullptr)
                {
                    continue;
                }

                FbxNode* BoneNode = Cluster->GetLink();
                //최대 부모까지 올라가서 전부다 사용할 Bones에 넣는다.
                while (BoneNode != nullptr && BoneNode->GetParent() != nullptr)
                {
                    Context.RequiredBoneNodes.insert(BoneNode);
                    BoneNode = BoneNode->GetParent();
                }
            }
        }
    }
}

void FFbxLoader::CollectRequiredDescendantNodes(FParseContext& Context)
{
    TArray<FbxNode*> RequiredRootNodes;
    RequiredRootNodes.reserve(Context.RequiredBoneNodes.size());

    for (FbxNode* RequiredNode : Context.RequiredBoneNodes)
    {
        RequiredRootNodes.push_back(RequiredNode);
    }

    for (FbxNode* RequiredNode : RequiredRootNodes)
    {
        CollectRequiredDescendantNodesRecursive(RequiredNode, Context);
    }
}

bool FFbxLoader::CollectRequiredDescendantNodesRecursive(FbxNode* Node, FParseContext& Context)
{
    if (Node == nullptr)
    {
        return false;
    }

    bool bHasRequiredDescendant = false;
    for (int ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
    {
        FbxNode* ChildNode = Node->GetChild(ChildIndex);
        const bool bShouldKeepChild = ShouldImportNodeAsBone(ChildNode);
        const bool bChildHasRequiredDescendant = CollectRequiredDescendantNodesRecursive(ChildNode, Context);

        if (bShouldKeepChild || bChildHasRequiredDescendant)
        {
            Context.RequiredBoneNodes.insert(ChildNode);
            bHasRequiredDescendant = true;
        }
    }

    return bHasRequiredDescendant;
}

bool FFbxLoader::ShouldImportNodeAsBone(FbxNode* Node) const
{
    if (Node == nullptr)
    {
        return false;
    }

    FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
    if (Attribute == nullptr)//Attribute가 없지만 Leaf일 수 있으니까 넣는다
    {
        return Node->GetChildCount() == 0;
    }

    switch (Attribute->GetAttributeType())
    {
    case FbxNodeAttribute::eSkeleton://Bone이니까 넣는다
        return true;
    case FbxNodeAttribute::eMesh://Mesh인데 Skin이 없으면 Attach된 Mesh일 수 있으니까 넣는다
    {
        FbxMesh* Mesh = Node->GetMesh();
        return Mesh != nullptr && Mesh->GetDeformerCount(FbxDeformer::eSkin) == 0;
    }
    case FbxNodeAttribute::eNull://Null이지만 Leaf니까 넣는다
        return Node->GetChildCount() == 0;
    default:
        return false;
    }
}

void FFbxLoader::ConstructSkeletonRecursive(FbxNode* Node, int32 ParentBoneIndex, FParseContext& Context)
{
    if (!Node)
        return;
    
    int CurrentBoneIndex = ParentBoneIndex;
    //실제로 사용하는 Bone이면 쓴다.
    const bool bIsRequiredBone = Context.RequiredBoneNodes.find(Node) != Context.RequiredBoneNodes.end();
    if (bIsRequiredBone)
    {
        FBone Bone;
        Bone.Name = Node->GetName();
        Bone.ParentIndex = ParentBoneIndex;
        
        //NodeLocalToParentNodeLocal
        FbxAMatrix NodeLocalToParentNodeLocal = Node->EvaluateLocalTransform();
        FbxAMatrix NodeLocalToGlobal  = Node->EvaluateGlobalTransform();
        
        Bone.BoneToParentBind = ConvertFbxMatrix(NodeLocalToParentNodeLocal);
        Bone.BoneToGlobalBind = ConvertFbxMatrix(NodeLocalToGlobal );
        if (Node->GetMesh() != nullptr && Node->GetMesh()->GetDeformerCount(FbxDeformer::eSkin) == 0)
        {
            Bone.MeshToBoneBind = FMatrix::Identity;
        }
        else
        {
            // Fallback: mesh local과 global이 같다고 가정한 값. Cluster bind pose pass에서 더 정확한 값으로 덮어쓴다.
            Bone.MeshToBoneBind = Bone.BoneToGlobalBind.GetInverse();
        }
        
        CurrentBoneIndex = static_cast<int32>(Context.Asset.Bones.size());
        Context.Asset.Bones.push_back(Bone);
        Context.BoneNodeToIndex[Node] = CurrentBoneIndex;
    }
    
    for (int i=0;i<Node->GetChildCount();i++)
    {
        ConstructSkeletonRecursive(Node->GetChild(i),CurrentBoneIndex,Context);
    }
}

void FFbxLoader::CollectClusterData(FParseContext& Context)
{
    for (int i=0;i<Context.MeshNodes.size();i++)
    {
        FbxMesh* Mesh = Context.MeshNodes[i]->GetMesh();
        if (Mesh == nullptr)
        {
            continue;
        }

        const int SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
        for (int SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
        {
            FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
            if (Skin == nullptr)
            {
                continue;
            }

            const int ClusterCount = Skin->GetClusterCount();
            for (int ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
            {
                FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
                if (Cluster == nullptr)
                {
                    continue;
                }

                FbxNode* BoneNode = Cluster->GetLink();
                auto BoneIndexIter = Context.BoneNodeToIndex.find(BoneNode);
                if (BoneIndexIter == Context.BoneNodeToIndex.end())
                {
                    continue;
                }
                
                FbxAMatrix MeshLocalToGlobalBind ;
                FbxAMatrix BoneLocalToGlobalBind;
                Cluster->GetTransformMatrix(MeshLocalToGlobalBind);
                Cluster->GetTransformLinkMatrix(BoneLocalToGlobalBind);
                
                FbxAMatrix MeshToBoneBind = MeshLocalToGlobalBind *BoneLocalToGlobalBind.Inverse(); 
                
                uint32 iter = BoneIndexIter->second;
                Context.Asset.Bones[iter].BoneToGlobalBind = ConvertFbxMatrix(BoneLocalToGlobalBind);
                Context.Asset.Bones[iter].MeshToBoneBind = ConvertFbxMatrix(MeshToBoneBind);
                //NodeLocalToGlobalBind를 받은 node목록
                Context.ClusterBoneNodes.insert(BoneNode);
                //BoneLocalToGlobalBind
                Context.CorrectedNodeGlobalMatrices[BoneNode] = BoneLocalToGlobalBind;
                

                const int ControlPointIndexCount = Cluster->GetControlPointIndicesCount();
                int* ControlPointIndices = Cluster->GetControlPointIndices();
                double* ControlPointWeights = Cluster->GetControlPointWeights();

                for (int t = 0; t < ControlPointIndexCount; ++t)
                {
                    const uint32 Index = static_cast<uint32>(ControlPointIndices[t]);
                    const float Weight = static_cast<float>(ControlPointWeights[t]);
                    if (Weight <= 0.0f)
                    {
                        continue;
                    }

                    FBoneInfluence Influence;
                    Influence.BoneIndex = static_cast<int>(BoneIndexIter->second);
                    Influence.Weight = Weight;

                    Context.ControlPointWeights[Mesh][Index].push_back(Influence);
                }
            }
        }
    }
}

void FFbxLoader::RebuildDerivedGlobalBindTransforms(
    FbxNode* Node,
    FParseContext& Context,
    const FbxAMatrix& ParentGlobalMatrix,
    bool bHasParentGlobalMatrix)
{
    if (Node == nullptr)
    {
        return;
    }

    FbxAMatrix CurrentGlobalMatrix = bHasParentGlobalMatrix
        ? ParentGlobalMatrix * Node->EvaluateLocalTransform()
        : Node->EvaluateGlobalTransform();

    auto BoneIndexIter = Context.BoneNodeToIndex.find(Node);
    if (BoneIndexIter != Context.BoneNodeToIndex.end())
    {
        const bool bIsClusterBone = Context.ClusterBoneNodes.find(Node) != Context.ClusterBoneNodes.end();
        if (bIsClusterBone)
        {
            auto CorrectedGlobalIter = Context.CorrectedNodeGlobalMatrices.find(Node);
            if (CorrectedGlobalIter != Context.CorrectedNodeGlobalMatrices.end())
            {
                CurrentGlobalMatrix = CorrectedGlobalIter->second;
            }
        }
        else
        {
            const uint32 BoneIndex = BoneIndexIter->second;
            Context.Asset.Bones[BoneIndex].BoneToGlobalBind = ConvertFbxMatrix(CurrentGlobalMatrix);
            if (Node->GetMesh() != nullptr && Node->GetMesh()->GetDeformerCount(FbxDeformer::eSkin) == 0)
            {
                Context.Asset.Bones[BoneIndex].MeshToBoneBind = FMatrix::Identity;
            }
            else
            {
                Context.Asset.Bones[BoneIndex].MeshToBoneBind =
                    Context.Asset.Bones[BoneIndex].BoneToGlobalBind.GetInverse();
            }
        }

        Context.CorrectedNodeGlobalMatrices[Node] = CurrentGlobalMatrix;
    }

    for (int ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
    {
        RebuildDerivedGlobalBindTransforms(Node->GetChild(ChildIndex), Context, CurrentGlobalMatrix, true);
    }
}

bool FFbxLoader::ImportMeshGeometry(FParseContext& Context)
{
    TMap<int32, TArray<uint32>> IndicesByMaterialSlot;
    for (int MeshNodeIndex = 0; MeshNodeIndex < Context.MeshNodes.size(); ++MeshNodeIndex)
    {
        FbxNode* MeshNode = Context.MeshNodes[MeshNodeIndex];
        if (MeshNode == nullptr) continue;
        
        FbxMesh* Mesh = MeshNode->GetMesh();
        if (Mesh == nullptr) continue;

        const int SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
        const bool bShouldBakeMeshNodeTransform = SkinCount == 0;
        FbxAMatrix MeshNodeGlobalMatrix = MeshNode->EvaluateGlobalTransform();
        if (bShouldBakeMeshNodeTransform)
        {
            auto CorrectedGlobalIter = Context.CorrectedNodeGlobalMatrices.find(MeshNode);
            if (CorrectedGlobalIter != Context.CorrectedNodeGlobalMatrices.end())
            {
                MeshNodeGlobalMatrix = CorrectedGlobalIter->second;
            }
        }
        FbxAMatrix GeometryMatrix;
        GeometryMatrix.SetT(MeshNode->GetGeometricTranslation(FbxNode::eSourcePivot));
        GeometryMatrix.SetR(MeshNode->GetGeometricRotation(FbxNode::eSourcePivot));
        GeometryMatrix.SetS(MeshNode->GetGeometricScaling(FbxNode::eSourcePivot));
        const FbxAMatrix MeshLocalToGlobalMatrix = MeshNodeGlobalMatrix * GeometryMatrix;
        FbxAMatrix MeshLocalToGlobalInverseTransposeMatrix = MeshLocalToGlobalMatrix.Inverse();
        MeshLocalToGlobalInverseTransposeMatrix = MeshLocalToGlobalInverseTransposeMatrix.Transpose();
        
        FbxVector4* ControlPoints = Mesh->GetControlPoints();
        const int PolygonCnt = Mesh->GetPolygonCount();

        FbxStringList UVSetNames;
        Mesh->GetUVSetNames(UVSetNames);
        const int UVSetCount = UVSetNames.GetCount();
        const char* UVSetName = UVSetCount > 0 ? UVSetNames.GetStringAt(0) : nullptr;
        
        FbxLayerElementMaterial* MaterialElement = Mesh->GetElementMaterial();
        
        //Mesh의 모든Polygon 순회
        for (int PolygonIndex = 0; PolygonIndex < PolygonCnt; ++PolygonIndex)
        {
            //Material
            int32 PolygonMaterialIndex = 0;
            if (MaterialElement&&MaterialElement->GetMappingMode()==FbxLayerElement::eByPolygon)
            {
                PolygonMaterialIndex = MaterialElement->GetIndexArray().GetAt(PolygonIndex);
            }
            
            int32 MaterialSlotBaseIndex = 0;
            auto BaseIter = Context.MeshNodeToMaterialSlotBaseIndex.find(MeshNode);
            if (BaseIter != Context.MeshNodeToMaterialSlotBaseIndex.end())
            {
                MaterialSlotBaseIndex = BaseIter->second;
            }
            
            //어떤 매터리얼의 Vertex인지 구별하기 위함
            int32 MaterialSlotIndex = MaterialSlotBaseIndex + PolygonMaterialIndex;
            
            
            if (Mesh->GetPolygonSize(PolygonIndex) != 3) continue;
            static constexpr int32 TriangleWinding[3] = { 0, 2, 1 };
            for (int WindingIndex = 0; WindingIndex < 3; ++WindingIndex)
            {
                const int VertexIndex = TriangleWinding[WindingIndex];
                //nth Polygon의 m번쨰 Vertex의 ControlPoint를 읽기
                const int ControlPointIndex = Mesh->GetPolygonVertex(PolygonIndex,VertexIndex);
                //Mesh 좌표계 기준에서의 좌표
                // Mesh Local -> Global -> Bone Local = MeshLocalToBoneLocalAtBindPose
                FbxVector4 ControlPoint = ControlPoints[ControlPointIndex];
                if (bShouldBakeMeshNodeTransform)
                {
                    ControlPoint = MeshLocalToGlobalMatrix.MultT(ControlPoint);
                }
                
                FbxVertex Vertex = {};
                Vertex.Position = FVector(
                    static_cast<float>(ControlPoint[0]),
                    static_cast<float>(ControlPoint[1]),
                    static_cast<float>(ControlPoint[2])
                    );
                
                FbxVector4 Normal = {};
                if (Mesh->GetPolygonVertexNormal(PolygonIndex,VertexIndex,Normal))
                {
                    if (bShouldBakeMeshNodeTransform)
                    {
                        Normal = MeshLocalToGlobalInverseTransposeMatrix.MultT(Normal);
                    }

                    Vertex.Normal = FVector(
                    static_cast<float>(Normal[0]),
                    static_cast<float>(Normal[1]),
                    static_cast<float>(Normal[2])
                    );
                }
                
                FbxVector2 UV;
                bool bUnmapped = false;
                
                if (UVSetName != nullptr && Mesh->GetPolygonVertexUV(PolygonIndex,VertexIndex,UVSetName,UV,bUnmapped))
                {
                    Vertex.UV = FVector2(
                        static_cast<float>(UV[0]),
                        static_cast<float>(UV[1])
                        );
                }
                
                for (int InfluenceIndex = 0; InfluenceIndex<4;++InfluenceIndex)
                {
                    Vertex.BoneIndices[InfluenceIndex] = 0;
                    Vertex.BoneWeights[InfluenceIndex] = 0.f;
                }
                
                auto MeshWeightIter = Context.ControlPointWeights.find(Mesh);
                if (MeshWeightIter != Context.ControlPointWeights.end())
                {
                    auto WeightIter = MeshWeightIter->second.find(static_cast<uint32>(ControlPointIndex));
                    if (WeightIter != MeshWeightIter->second.end())
                    {
                        const TArray<FBoneInfluence>& Influences = WeightIter->second;
                        TArray<FBoneInfluence> SortedInfluences = Influences;
                        std::sort(SortedInfluences.begin(), SortedInfluences.end(),
                            [](const FBoneInfluence& A, const FBoneInfluence& B)
                            {
                                return A.Weight > B.Weight;
                            });
                        
                        const int InfluenceCount = static_cast<int>(SortedInfluences.size() < 4 ? SortedInfluences.size() : 4);
                        float TotalWeight =0.f;
                        
                        for (int InfluenceIndex = 0; InfluenceIndex < InfluenceCount; ++InfluenceIndex)
                        {
                            Vertex.BoneIndices[InfluenceIndex] = SortedInfluences[InfluenceIndex].BoneIndex;
                            Vertex.BoneWeights[InfluenceIndex] = SortedInfluences[InfluenceIndex].Weight;
                            TotalWeight += Vertex.BoneWeights[InfluenceIndex];
                        }
                        
                        if (TotalWeight > 0.0f)
                        {
                            for (int InfluenceIndex = 0; InfluenceIndex < InfluenceCount; ++InfluenceIndex)
                            {
                                Vertex.BoneWeights[InfluenceIndex] /= TotalWeight;
                            }
                        }
                    }
                }
                
                const uint32 NewVertexIndex = static_cast<uint32>(Context.Asset.Vertices.size());
                IndicesByMaterialSlot[MaterialSlotIndex].push_back(NewVertexIndex);
                Context.Asset.Vertices.push_back(Vertex);
                //Context.Asset.Indices.push_back(NewVertexIndex);
            }
        }
    }
    
    TArray<int32> SortedMaterialSlotIndices;
    SortedMaterialSlotIndices.reserve(IndicesByMaterialSlot.size());
    for (const auto& Pair : IndicesByMaterialSlot)
    {
        SortedMaterialSlotIndices.push_back(Pair.first);
    }
    
    std::sort(SortedMaterialSlotIndices.begin(), SortedMaterialSlotIndices.end());
    
    for (int32 MaterialSlotIndex : SortedMaterialSlotIndices)
    {
        auto IndicesIter = IndicesByMaterialSlot.find(MaterialSlotIndex);
        if (IndicesIter == IndicesByMaterialSlot.end() || IndicesIter->second.empty())
        {
            continue;
        }

        FSKeletalMeshSection Section;
        Section.StartIndex = static_cast<uint32>(Context.Asset.Indices.size());
        Section.IndexCount = static_cast<uint32>(IndicesIter->second.size());
        Section.MaterialSlotIndex = MaterialSlotIndex;

        for (uint32 Index : IndicesIter->second)
        {
            Context.Asset.Indices.push_back(Index);
        }

        Context.Asset.Sections.push_back(Section);
    }

    return !Context.Asset.Vertices.empty() && !Context.Asset.Indices.empty();
}


bool FFbxLoader::PrepareTraverse(FbxScene* Scene)
{
    if (Manager == nullptr || Scene == nullptr)
    {
        UE_LOG("FBX scene preparation failed: invalid manager or scene");
        return false;
    }

    FbxAxisSystem EngineAxisSystem(FbxAxisSystem::eZAxis, FbxAxisSystem::eParityOdd, FbxAxisSystem::eLeftHanded);
    EngineAxisSystem.ConvertScene(Scene);
    
    FbxSystemUnit EngineUnit(1.0);
    FbxSystemUnit::ConversionOptions UnitConversionOptions;
    UnitConversionOptions.mConvertRrsNodes = true;
    UnitConversionOptions.mConvertLimits = true;
    UnitConversionOptions.mConvertClusters = true;
    UnitConversionOptions.mConvertLightIntensity = true;
    UnitConversionOptions.mConvertPhotometricLProperties = true;
    UnitConversionOptions.mConvertCameraClipPlanes = true;
    EngineUnit.ConvertScene(Scene, UnitConversionOptions);
    
    FbxGeometryConverter Converter(Manager);
    if (!Converter.Triangulate(Scene,true))
    {
        UE_LOG("FBX scene triangulation failed");
        return false;
    }

    return true;
}

bool FFbxLoader::LoadScene(const char* FilePath, FbxScene*& InOutScene)
{
    if (Manager == nullptr)
    {
        UE_LOG("FBX Manager is not initialized");
        return false;
    }

    FbxImporter* Importer = FbxImporter::Create(Manager,"");
    if (!Importer)
    {
        UE_LOG("Importer failed to create!");
        return false;
    }
    InOutScene = FbxScene::Create(Manager,"ImportedScene");
    if (!InOutScene)
    {
        UE_LOG("Fbx InOutScene failed to create!");
        Importer->Destroy();
        return false;
    }
    
    bool InitSuccess = Importer->Initialize(FilePath,-1,Manager->GetIOSettings());
    if (!InitSuccess)
    {
        Importer->Destroy();
        InOutScene->Destroy();
        InOutScene = nullptr;
        UE_LOG("FBX Importer Initialization failed");
        return false;
    }
    
    bool ImportSucess = Importer->Import(InOutScene);
    
    if (!ImportSucess)
    {
        Importer->Destroy();
        InOutScene->Destroy();
        InOutScene = nullptr;
        UE_LOG("FBX Importer import failed");
        return false;
    }
    Importer->Destroy();
    return true;
}


FMatrix FFbxLoader::ConvertFbxMatrix(FbxAMatrix Matrix)
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
        static_cast<float>(Matrix.Get(3, 3))
    );
}

