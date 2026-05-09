#include "Core/EnginePCH.h"
#include "FbxLoader.h"

#include "Core/Paths.h"
#include "Core/ResourceManager.h"
#include "Render/Resource/Material.h"

#include <algorithm>
#include <filesystem>

namespace
{
    constexpr const char* FbxMaterialPropertyDiffuse = "DiffuseColor";
    constexpr const char* FbxMaterialPropertySpecular = "SpecularColor";
    constexpr const char* FbxMaterialPropertyEmissive = "EmissiveColor";
    constexpr const char* FbxMaterialPropertyShininess = "Shininess";
    constexpr const char* FbxMaterialPropertyTransparencyFactor = "TransparencyFactor";
    constexpr const char* FbxMaterialPropertyNormalMap = "NormalMap";
    constexpr const char* FbxMaterialPropertyBump = "Bump";

    bool TryReadFbxColor(FbxSurfaceMaterial* Material, const char* PropertyName, FVector& OutColor)
    {
        if (Material == nullptr || PropertyName == nullptr)
        {
            return false;
        }

        FbxProperty Property = Material->FindProperty(PropertyName);
        if (!Property.IsValid())
        {
            return false;
        }

        const FbxDouble3 Color = Property.Get<FbxDouble3>();
        OutColor = FVector(
            static_cast<float>(Color[0]),
            static_cast<float>(Color[1]),
            static_cast<float>(Color[2]));
        return true;
    }

    bool TryReadFbxScalar(FbxSurfaceMaterial* Material, const char* PropertyName, float& OutValue)
    {
        if (Material == nullptr || PropertyName == nullptr)
        {
            return false;
        }

        FbxProperty Property = Material->FindProperty(PropertyName);
        if (!Property.IsValid())
        {
            return false;
        }

        OutValue = static_cast<float>(Property.Get<FbxDouble>());
        return true;
    }

    bool FileExists(const std::filesystem::path& Path)
    {
        std::error_code ErrorCode;
        return std::filesystem::exists(Path, ErrorCode) && std::filesystem::is_regular_file(Path, ErrorCode);
    }

    FbxFileTexture* TryGetFbxFileTexture(FbxObject* Object)
    {
        if (Object == nullptr)
        {
            return nullptr;
        }

        const char* ClassName = Object->GetClassId().GetName();
        if (ClassName == nullptr || strcmp(ClassName, "FbxFileTexture") != 0)
        {
            return nullptr;
        }

        return static_cast<FbxFileTexture*>(Object);
    }
}

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
        ParseContext.SourceFilePath = FilePath ? FilePath : "";
        if (!ParseContext.SourceFilePath.empty())
        {
            ParseContext.SourceDirectory = FPaths::ToUtf8(
                std::filesystem::path(FPaths::ToWide(ParseContext.SourceFilePath)).parent_path().generic_wstring());
        }
        
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
    RebuildNonClusterBoneGlobals(Context);
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
                FSkeletalMeshMaterialSlot Slot = {};
                Slot.SlotName = SlotName;
                Slot.Material = ImportFbxMaterial(Material, Context);
                Context.Asset.MaterialSlots.push_back(Slot);
            }
        }
        else
        {
            FSkeletalMeshMaterialSlot Slot = {};
            Slot.SlotName = "Default";
            Slot.Material = FResourceManager::Get().GetMaterial("DefaultWhite");
            Context.Asset.MaterialSlots.push_back(Slot);
        }
    }
    
    for (int i=0;i<Node->GetChildCount();i++)
    {
        CollectMeshNodesAndMaterialSlot(Node->GetChild(i),Context);
    }
}

UMaterialInterface* FFbxLoader::ImportFbxMaterial(FbxSurfaceMaterial* FbxMaterial, const FParseContext& Context) const
{
    if (FbxMaterial == nullptr)
    {
        return FResourceManager::Get().GetMaterial("DefaultWhite");
    }

    const FString SlotName = FbxMaterial->GetName() ? FbxMaterial->GetName() : "Default";
    const FString MaterialName = Context.SourceFilePath + "::" + SlotName;

    UMaterial* Material = FResourceManager::Get().GetOrCreateMaterial(
        MaterialName,
        Context.SourceFilePath,
        "Shaders/UberLit.hlsl");
    if (Material == nullptr)
    {
        return FResourceManager::Get().GetMaterial("DefaultWhite");
    }

    Material->Name = MaterialName;
    Material->MaterialData = FMaterial{};
    Material->MaterialData.Name = SlotName;

    TryReadFbxColor(FbxMaterial, FbxMaterialPropertyDiffuse, Material->MaterialData.BaseColor);
    TryReadFbxColor(FbxMaterial, FbxMaterialPropertySpecular, Material->MaterialData.SpecularColor);
    TryReadFbxColor(FbxMaterial, FbxMaterialPropertyEmissive, Material->MaterialData.EmissiveColor);
    TryReadFbxScalar(FbxMaterial, FbxMaterialPropertyShininess, Material->MaterialData.Shininess);

    float TransparencyFactor = 0.0f;
    if (TryReadFbxScalar(FbxMaterial, FbxMaterialPropertyTransparencyFactor, TransparencyFactor))
    {
        Material->MaterialData.Opacity = std::clamp(1.0f - TransparencyFactor, 0.0f, 1.0f);
    }

    Material->MaterialData.DiffuseTexPath = FindFbxTexturePath(FbxMaterial, FbxMaterialPropertyDiffuse, Context);
    Material->MaterialData.bHasDiffuseTexture = !Material->MaterialData.DiffuseTexPath.empty();

    Material->MaterialData.SpecularTexPath = FindFbxTexturePath(FbxMaterial, FbxMaterialPropertySpecular, Context);
    Material->MaterialData.bHasSpecularTexture = !Material->MaterialData.SpecularTexPath.empty();

    const FString NormalTexPath = FindFbxTexturePath(FbxMaterial, FbxMaterialPropertyNormalMap, Context);
    if (!NormalTexPath.empty())
    {
        Material->MaterialData.NormalTexPath[0] = NormalTexPath;
        Material->MaterialData.NormalTextureCount = 1;
    }

    Material->MaterialData.BumpTexPath = FindFbxTexturePath(FbxMaterial, FbxMaterialPropertyBump, Context);
    Material->MaterialData.bHasBumpTexture = !Material->MaterialData.BumpTexPath.empty();

    ApplyMaterialParams(Material);

    UE_LOG("[FbxLoader][Material] Slot: %s, Diffuse: %s, Normal: %s, Specular: %s",
        SlotName.c_str(),
        Material->MaterialData.DiffuseTexPath.empty() ? "<none>" : Material->MaterialData.DiffuseTexPath.c_str(),
        Material->MaterialData.NormalTextureCount > 0 ? Material->MaterialData.NormalTexPath[0].c_str() : "<none>",
        Material->MaterialData.SpecularTexPath.empty() ? "<none>" : Material->MaterialData.SpecularTexPath.c_str());

    return Material;
}

FString FFbxLoader::FindFbxTexturePath(FbxSurfaceMaterial* Material, const char* PropertyName, const FParseContext& Context) const
{
    if (Material == nullptr || PropertyName == nullptr)
    {
        return {};
    }

    FbxProperty Property = Material->FindProperty(PropertyName);
    if (!Property.IsValid())
    {
        return {};
    }

    const int FileTextureCount = Property.GetSrcObjectCount();
    for (int TextureIndex = 0; TextureIndex < FileTextureCount; ++TextureIndex)
    {
        FbxObject* TextureObject = Property.GetSrcObject(TextureIndex);
        FbxFileTexture* Texture = TryGetFbxFileTexture(TextureObject);
        if (Texture == nullptr)
        {
            continue;
        }

        FString ResolvedPath = ResolveFbxTexturePath(Texture->GetFileName(), Context);
        if (!ResolvedPath.empty())
        {
            return ResolvedPath;
        }

        ResolvedPath = ResolveFbxTexturePath(Texture->GetRelativeFileName(), Context);
        if (!ResolvedPath.empty())
        {
            return ResolvedPath;
        }
    }

    return {};
}

FString FFbxLoader::ResolveFbxTexturePath(const char* TexturePath, const FParseContext& Context) const
{
    if (TexturePath == nullptr || TexturePath[0] == '\0')
    {
        return {};
    }

    const std::filesystem::path RawPath(FPaths::ToWide(TexturePath));
    const std::filesystem::path SourceDirectory(FPaths::ToWide(Context.SourceDirectory));
    const std::filesystem::path RootDirectory(FPaths::RootDir());
    const std::filesystem::path SourceFile(FPaths::ToWide(Context.SourceFilePath));

    TArray<std::filesystem::path> Candidates;
    if (RawPath.is_absolute())
    {
        Candidates.push_back(RawPath);
    }
    else
    {
        Candidates.push_back(SourceDirectory / RawPath);
        Candidates.push_back(RootDirectory / RawPath);
    }

    const std::filesystem::path FileName = RawPath.filename();
    if (!FileName.empty())
    {
        Candidates.push_back(SourceDirectory / FileName);
        Candidates.push_back(SourceDirectory / (SourceFile.stem().wstring() + L".fbm") / FileName);
        Candidates.push_back(RootDirectory / FileName);
    }

    for (const std::filesystem::path& Candidate : Candidates)
    {
        const std::filesystem::path Normalized = Candidate.lexically_normal();
        if (FileExists(Normalized))
        {
            return FPaths::ToUtf8(Normalized.generic_wstring());
        }
    }

    UE_LOG("[FbxLoader][Material] Texture not found: %s", TexturePath);
    return {};
}

void FFbxLoader::ApplyMaterialParams(UMaterial* Material) const
{
    if (Material == nullptr)
    {
        return;
    }

    FResourceManager& ResourceManager = FResourceManager::Get();
    ID3D11Device* Device = ResourceManager.GetCachedDevice();
    UTexture* DefaultWhite = ResourceManager.GetTexture("DefaultWhite");
    UTexture* DefaultNormal = ResourceManager.GetTexture("DefaultNormal");

    Material->MaterialParams["BaseColor"] = FMaterialParamValue(Material->MaterialData.BaseColor);
    Material->MaterialParams["SpecularColor"] = FMaterialParamValue(Material->MaterialData.SpecularColor);
    Material->MaterialParams["EmissiveColor"] = FMaterialParamValue(Material->MaterialData.EmissiveColor);
    Material->MaterialParams["Shininess"] = FMaterialParamValue(Material->MaterialData.Shininess);
    Material->MaterialParams["Opacity"] = FMaterialParamValue(Material->MaterialData.Opacity);

    UTexture* DiffuseTexture = nullptr;
    if (Material->MaterialData.bHasDiffuseTexture)
    {
        DiffuseTexture = ResourceManager.LoadTexture(Material->MaterialData.DiffuseTexPath, Device);
    }
    Material->MaterialParams["DiffuseMap"] = FMaterialParamValue(DiffuseTexture ? DiffuseTexture : DefaultWhite);

    UTexture* SpecularTexture = nullptr;
    if (Material->MaterialData.bHasSpecularTexture)
    {
        SpecularTexture = ResourceManager.LoadTexture(Material->MaterialData.SpecularTexPath, Device);
    }
    Material->MaterialParams["SpecularMap"] = FMaterialParamValue(SpecularTexture ? SpecularTexture : DefaultWhite);

    UTexture* NormalTexture = nullptr;
    if (Material->MaterialData.NormalTextureCount > 0)
    {
        NormalTexture = ResourceManager.LoadTexture(Material->MaterialData.NormalTexPath[0], Device);
    }
    Material->MaterialParams["NormalMap"] = FMaterialParamValue(NormalTexture ? NormalTexture : DefaultNormal);

    UTexture* BumpTexture = nullptr;
    if (Material->MaterialData.bHasBumpTexture)
    {
        BumpTexture = ResourceManager.LoadTexture(Material->MaterialData.BumpTexPath, Device);
    }
    Material->MaterialParams["BumpMap"] = FMaterialParamValue(BumpTexture ? BumpTexture : DefaultWhite);

    Material->MaterialParams["bHasDiffuseMap"] = FMaterialParamValue(DiffuseTexture != nullptr);
    Material->MaterialParams["bHasSpecularMap"] = FMaterialParamValue(SpecularTexture != nullptr);
    Material->MaterialParams["bHasNormalMap"] = FMaterialParamValue(NormalTexture != nullptr);
    Material->MaterialParams["bHasBumpMap"] = FMaterialParamValue(BumpTexture != nullptr);
    Material->MaterialParams["ScrollUV"] = FMaterialParamValue(FVector2(0.0f, 0.0f));
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
                Context.Asset.Bones[iter].bHasClusterBind = true;
                

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

void FFbxLoader::RebuildNonClusterBoneGlobals(FParseContext& Context)
{
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Context.Asset.Bones.size()); ++BoneIndex)
    {
        FBone& Bone = Context.Asset.Bones[BoneIndex];
        if (Bone.bHasClusterBind)
        {
            continue;
        }

        if (Bone.ParentIndex >= 0 && Bone.ParentIndex < static_cast<int32>(Context.Asset.Bones.size()))
        {
            const FBone& ParentBone = Context.Asset.Bones[Bone.ParentIndex];
            Bone.BoneToGlobalBind = Bone.BoneToParentBind * ParentBone.BoneToGlobalBind;
        }

        Bone.MeshToBoneBind = FMatrix::Identity;
    }
}

bool FFbxLoader::TryGetRigidSkinBoneIndex(FbxNode* MeshNode, const FParseContext& Context, int32& OutBoneIndex) const
{
    auto MeshNodeBoneIter = Context.BoneNodeToIndex.find(MeshNode);
    if (MeshNodeBoneIter == Context.BoneNodeToIndex.end())
    {
        return false;
    }

    const uint32 BoneIndex = MeshNodeBoneIter->second;
    if (BoneIndex >= Context.Asset.Bones.size())
    {
        return false;
    }

    OutBoneIndex = static_cast<int32>(BoneIndex);
    return true;
}

void FFbxLoader::FillVertexBoneWeights(
    FbxMesh* Mesh,
    uint32 ControlPointIndex,
    const FParseContext& Context,
    FbxVertex& Vertex) const
{
    for (int InfluenceIndex = 0; InfluenceIndex<4;++InfluenceIndex)
    {
        Vertex.BoneIndices[InfluenceIndex] = 0;
        Vertex.BoneWeights[InfluenceIndex] = 0.f;
    }

    auto MeshWeightIter = Context.ControlPointWeights.find(Mesh);
    if (MeshWeightIter == Context.ControlPointWeights.end())
    {
        return;
    }

    auto WeightIter = MeshWeightIter->second.find(ControlPointIndex);
    if (WeightIter == MeshWeightIter->second.end())
    {
        return;
    }

    TArray<FBoneInfluence> SortedInfluences = WeightIter->second;
    std::sort(SortedInfluences.begin(), SortedInfluences.end(),
        [](const FBoneInfluence& A, const FBoneInfluence& B)
        {
            return A.Weight > B.Weight;
        });

    const int InfluenceCount = static_cast<int>(SortedInfluences.size() < 4 ? SortedInfluences.size() : 4);
    float TotalWeight = 0.f;

    for (int InfluenceIndex = 0; InfluenceIndex < InfluenceCount; ++InfluenceIndex)
    {
        Vertex.BoneIndices[InfluenceIndex] = SortedInfluences[InfluenceIndex].BoneIndex;
        Vertex.BoneWeights[InfluenceIndex] = SortedInfluences[InfluenceIndex].Weight;
        TotalWeight += Vertex.BoneWeights[InfluenceIndex];
    }

    if (TotalWeight <= 0.0f)
    {
        return;
    }

    for (int InfluenceIndex = 0; InfluenceIndex < InfluenceCount; ++InfluenceIndex)
    {
        Vertex.BoneWeights[InfluenceIndex] /= TotalWeight;
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
        
        FbxAMatrix MeshNodeGlobalMatrix = MeshNode->EvaluateGlobalTransform();
        
        FbxAMatrix GeometryMatrix;
        GeometryMatrix.SetT(MeshNode->GetGeometricTranslation(FbxNode::eSourcePivot));
        GeometryMatrix.SetR(MeshNode->GetGeometricRotation(FbxNode::eSourcePivot));
        GeometryMatrix.SetS(MeshNode->GetGeometricScaling(FbxNode::eSourcePivot));
        
        int32 RigidSkinBoneIndex = -1;
        const bool bUseRigidSkinning = SkinCount == 0 && TryGetRigidSkinBoneIndex(MeshNode, Context, RigidSkinBoneIndex);
        const bool bShouldBakeMeshNodeTransform = SkinCount == 0 && !bUseRigidSkinning;
        
        const FbxAMatrix MeshLocalToGlobalMatrix = MeshNodeGlobalMatrix * GeometryMatrix;
        FbxAMatrix MeshLocalToGlobalInverseTransposeMatrix = MeshLocalToGlobalMatrix.Inverse().Transpose();
        FbxAMatrix GeometryInverseTransposeMatrix = GeometryMatrix.Inverse().Transpose();
        const FMatrix MeshNormalMatrix = ConvertFbxMatrix(MeshLocalToGlobalInverseTransposeMatrix);
        const FMatrix GeometryNormalMatrix = ConvertFbxMatrix(GeometryInverseTransposeMatrix);
        
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
            //Mesh에서 몇번째 Material인지 나타내는 Index
            int32 PolygonMaterialIndex = 0;
            if (MaterialElement&&MaterialElement->GetMappingMode()==FbxLayerElement::eByPolygon)
            {
                PolygonMaterialIndex = MaterialElement->GetIndexArray().GetAt(PolygonIndex);
            }
            
            //모든 Material Slot에서 현재 Mesh의 첫 Material이 몇번째인지 나타내는 Index
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
                FbxVector4 ControlPoint = ControlPoints[ControlPointIndex];
                if (bUseRigidSkinning)
                {
                    ControlPoint = GeometryMatrix.MultT(ControlPoint);
                }
                else if (bShouldBakeMeshNodeTransform)
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
                    if (bUseRigidSkinning)
                    {
                        const FVector LocalNormal(
                            static_cast<float>(Normal[0]),
                            static_cast<float>(Normal[1]),
                            static_cast<float>(Normal[2]));
                        const FVector TransformedNormal = GeometryNormalMatrix.TransformVector(LocalNormal);
                        Normal = FbxVector4(TransformedNormal.X, TransformedNormal.Y, TransformedNormal.Z);
                    }
                    else if (bShouldBakeMeshNodeTransform)
                    {
                        const FVector LocalNormal(
                            static_cast<float>(Normal[0]),
                            static_cast<float>(Normal[1]),
                            static_cast<float>(Normal[2]));
                        const FVector TransformedNormal = MeshNormalMatrix.TransformVector(LocalNormal);
                        Normal = FbxVector4(TransformedNormal.X, TransformedNormal.Y, TransformedNormal.Z);
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
                
                //RigidSkinning 해야하면 부모가 
                if (bUseRigidSkinning)
                {
                    Vertex.BoneIndices[0] = RigidSkinBoneIndex;
                    Vertex.BoneWeights[0] = 1.0f;
                    for (int InfluenceIndex = 1; InfluenceIndex < 4; ++InfluenceIndex)
                    {
                        Vertex.BoneIndices[InfluenceIndex] = 0;
                        Vertex.BoneWeights[InfluenceIndex] = 0.0f;
                    }
                }
                else
                {
                    FillVertexBoneWeights(Mesh, static_cast<uint32>(ControlPointIndex), Context, Vertex);
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

