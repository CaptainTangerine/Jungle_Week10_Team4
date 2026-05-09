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
    ConstructSkeletonRecursive(Node, -1,Context);
    CollectClusterData(Context);
    if (!ImportMeshGeometry(Context))
    {
        return false;
    }
    UE_LOG("[FbxLoader] MeshNodes: %zu, Bones: %zu, Vertices: %zu, Indices: %zu",
    Context.MeshNodes.size(),
    Context.Asset.Bones.size(),
    Context.Asset.Vertices.size(),
    Context.Asset.Indices.size());
    LogImportValidation(Context);
    
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

bool FFbxLoader::ImportMeshGeometry(FParseContext& Context)
{
    TMap<int32, TArray<uint32>> IndicesByMaterialSlot;
    for (int MeshNodeIndex = 0; MeshNodeIndex < Context.MeshNodes.size(); ++MeshNodeIndex)
    {
        FbxNode* MeshNode = Context.MeshNodes[MeshNodeIndex];
        if (MeshNode == nullptr) continue;
        
        FbxMesh* Mesh = MeshNode->GetMesh();
        if (Mesh == nullptr) continue;
        
        FbxVector4* ControlPoints = Mesh->GetControlPoints();
        const int PolygonCnt = Mesh->GetPolygonCount();

        FbxStringList UVSetNames;
        Mesh->GetUVSetNames(UVSetNames);
        const int UVSetCount = UVSetNames.GetCount();
        const char* UVSetName = UVSetCount > 0 ? UVSetNames.GetStringAt(0) : nullptr;
        int32 PolygonVertexCount = 0;
        int32 UVReadSuccessCount = 0;
        int32 UVReadFailCount = 0;
        int32 ZeroUVCount = 0;
        
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
            
            int32 MaterialSlotIndex = MaterialSlotBaseIndex + PolygonMaterialIndex;
            
            
            if (Mesh->GetPolygonSize(PolygonIndex) != 3) continue;
            for (int VertexIndex = 0;VertexIndex<3;VertexIndex++)
            {
                ++PolygonVertexCount;
                //nth Polygon의 m번쨰 Vertex의 ControlPoint를 읽기
                const int ControlPointIndex = Mesh->GetPolygonVertex(PolygonIndex,VertexIndex);
                //Mesh 좌표계 기준에서의 좌표
                // Mesh Local -> Global -> Bone Local = MeshLocalToBoneLocalAtBindPose
                const FbxVector4& ControlPoint = ControlPoints[ControlPointIndex];
                
                FbxVertex Vertex = {};
                Vertex.Position = FVector(
                    static_cast<float>(ControlPoint[0]),
                    static_cast<float>(ControlPoint[1]),
                    static_cast<float>(ControlPoint[2])
                    );
                
                FbxVector4 Normal = {};
                if (Mesh->GetPolygonVertexNormal(PolygonIndex,VertexIndex,Normal))
                {
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
                    ++UVReadSuccessCount;
                    Vertex.UV = FVector2(
                        static_cast<float>(UV[0]),
                        static_cast<float>(UV[1])
                        );
                    if (Vertex.UV.X == 0.0f && Vertex.UV.Y == 0.0f)
                    {
                        ++ZeroUVCount;
                    }
                }
                else
                {
                    ++UVReadFailCount;
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
                                //why?
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
        UE_LOG("[FbxLoader][UV] Mesh: %s, UVSets: %d, PolygonVertices: %d, Success: %d, Failed: %d, ZeroUV: %d",
            MeshNode->GetName(),
            UVSetCount,
            PolygonVertexCount,
            UVReadSuccessCount,
            UVReadFailCount,
            ZeroUVCount);
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
        
        FbxAMatrix BoneLocalToParentLocalAtBindPoseMatrix = Node->EvaluateLocalTransform();
        FbxAMatrix BoneLocalToGlobalAtBindPoseMatrix = Node->EvaluateGlobalTransform();
        
        Bone.BoneLocalToParentLocalAtBindPose = ConvertFbxMatrix(BoneLocalToParentLocalAtBindPoseMatrix);
        Bone.BoneLocalToGlobalAtBindPose = ConvertFbxMatrix(BoneLocalToGlobalAtBindPoseMatrix);
        // Fallback: mesh local과 global이 같다고 가정한 값. Cluster bind pose pass에서 더 정확한 값으로 덮어쓴다.
        Bone.MeshLocalToBoneLocalAtBindPose = Bone.BoneLocalToGlobalAtBindPose.GetInverse();
        
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
                
                FbxAMatrix MeshLocalToGlobalAtBindPose ;
                FbxAMatrix BoneLocalToGlobalAtBindPose;
                Cluster->GetTransformMatrix(MeshLocalToGlobalAtBindPose);
                Cluster->GetTransformLinkMatrix(BoneLocalToGlobalAtBindPose);
                
                FbxAMatrix MeshLocalToBoneLocalAtBindPose = MeshLocalToGlobalAtBindPose *BoneLocalToGlobalAtBindPose.Inverse(); 
                
                uint32 iter = BoneIndexIter->second;
                Context.Asset.Bones[iter].BoneLocalToGlobalAtBindPose = ConvertFbxMatrix(BoneLocalToGlobalAtBindPose);
                Context.Asset.Bones[iter].MeshLocalToBoneLocalAtBindPose = ConvertFbxMatrix(MeshLocalToBoneLocalAtBindPose);
                

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

void FFbxLoader::LogImportValidation(const FParseContext& Context) const
{
    int32 InvalidBoneIndexCount = 0;
    int32 ZeroWeightVertexCount = 0;
    int32 BadWeightSumVertexCount = 0;
    int32 ZeroNormalVertexCount = 0;
    int32 ZeroUVVertexCount = 0;

    for (const FbxVertex& Vertex : Context.Asset.Vertices)
    {
        float WeightSum = 0.0f;
        for (int InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
        {
            const int BoneIndex = Vertex.BoneIndices[InfluenceIndex];
            const float Weight = Vertex.BoneWeights[InfluenceIndex];
            if (Weight > 0.0f)
            {
                WeightSum += Weight;
                if (BoneIndex < 0 || BoneIndex >= static_cast<int>(Context.Asset.Bones.size()))
                {
                    ++InvalidBoneIndexCount;
                }
            }
        }

        if (WeightSum <= 0.0f)
        {
            ++ZeroWeightVertexCount;
        }
        else if (WeightSum < 0.99f || WeightSum > 1.01f)
        {
            ++BadWeightSumVertexCount;
        }

        if (Vertex.Normal.X == 0.0f && Vertex.Normal.Y == 0.0f && Vertex.Normal.Z == 0.0f)
        {
            ++ZeroNormalVertexCount;
        }

        if (Vertex.UV.X == 0.0f && Vertex.UV.Y == 0.0f)
        {
            ++ZeroUVVertexCount;
        }
    }

    int32 RootBoneCount = 0;
    int32 InvalidParentIndexCount = 0;
    for (const FBone& Bone : Context.Asset.Bones)
    {
        if (Bone.ParentIndex == -1)
        {
            ++RootBoneCount;
        }
        else if (Bone.ParentIndex < 0 || Bone.ParentIndex >= static_cast<int>(Context.Asset.Bones.size()))
        {
            ++InvalidParentIndexCount;
        }
    }

    uint32 SectionIndexCountSum = 0;
    int32 InvalidSectionMaterialSlotCount = 0;
    int32 InvalidSectionIndexCount = 0;
    for (const FSKeletalMeshSection& Section : Context.Asset.Sections)
    {
        SectionIndexCountSum += Section.IndexCount;
        if (Section.IndexCount == 0 || Section.IndexCount % 3 != 0)
        {
            ++InvalidSectionIndexCount;
        }
        if (Section.MaterialSlotIndex < 0 ||
            Section.MaterialSlotIndex >= static_cast<int32>(Context.Asset.MaterialSlots.size()))
        {
            ++InvalidSectionMaterialSlotCount;
        }
    }

    UE_LOG("[FbxLoader][Validation] Triangles: %zu, RootBones: %d, InvalidParentIndices: %d",
        Context.Asset.Indices.size() / 3,
        RootBoneCount,
        InvalidParentIndexCount);

    UE_LOG("[FbxLoader][Validation] ZeroWeightVertices: %d, BadWeightSumVertices: %d, InvalidBoneIndices: %d",
        ZeroWeightVertexCount,
        BadWeightSumVertexCount,
        InvalidBoneIndexCount);

    UE_LOG("[FbxLoader][Validation] ZeroNormalVertices: %d, ZeroUVVertices: %d",
        ZeroNormalVertexCount,
        ZeroUVVertexCount);

    UE_LOG("[FbxLoader][Validation] MaterialSlots: %zu, Sections: %zu, SectionIndexSum: %u, Indices: %zu",
        Context.Asset.MaterialSlots.size(),
        Context.Asset.Sections.size(),
        SectionIndexCountSum,
        Context.Asset.Indices.size());

    UE_LOG("[FbxLoader][Validation] InvalidSectionIndexCounts: %d, InvalidSectionMaterialSlots: %d",
        InvalidSectionIndexCount,
        InvalidSectionMaterialSlotCount);

    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < static_cast<int32>(Context.Asset.MaterialSlots.size()); ++MaterialSlotIndex)
    {
        UE_LOG("[FbxLoader][MaterialSlot] %d: %s",
            MaterialSlotIndex,
            Context.Asset.MaterialSlots[MaterialSlotIndex].SlotName.c_str());
    }
}

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
        const int MaterialSlotBaseIndex = Context.Asset.MaterialSlots.size();
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
