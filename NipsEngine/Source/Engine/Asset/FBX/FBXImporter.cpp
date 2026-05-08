#include "FBXSDKContext.h"
#include "FBXImporter.h"
#include "Engine/Core/ResourceManager.h"
#include "Core/Paths.h"

#include <filesystem>

FFBXImportScene FFBXImporter::Import(const FString& Path)
{
    FFBXSDKContext Context;
    FFBXImportScene ImportScene = {};
    ImportScene.SourceFilePath = Path;
    ImportDirectory = FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(Path)).parent_path().generic_wstring());

    if (!Context.Initialize())
    {
        return ImportScene;
    }

    FFBXSceneHandle Scene = Context.LoadScene(Path);
    if (!Scene)
    {
        return ImportScene;
    }

    FbxNode* RootNode = Scene->GetRootNode();
    if (!RootNode)
    {
        return ImportScene;
    }

    FbxGeometryConverter Converter(Context.GetManager());
    Converter.Triangulate(Scene.Get(), true);

    TraversalNode(RootNode, -1, ImportScene);

    return ImportScene;
}

FStaticMesh* FFBXImporter::CreateStaticMeshFromImportData(const FFBXStaticMeshImportData& ImportData) const
{
    FStaticMesh* Mesh = new FStaticMesh();

    Mesh->PathFileName = ImportData.Name;
    Mesh->Vertices = ImportData.Vertices;
    Mesh->Indices = ImportData.Indices;
    Mesh->Sections = ImportData.Sections;
    Mesh->Slots = ImportData.MatreialSlots;
    Mesh->LocalBounds = BuildLocalBounds(ImportData);

    return Mesh;
}

void FFBXImporter::TraversalNode(FbxNode* Node, int32 ParentIndex, OUT FFBXImportScene& OutImportScene)
{
    if (!Node)
    {
        return;
    }

    int32 curNodeIndex = AddImportNode(Node, ParentIndex, OutImportScene);

    FbxMesh* Mesh = Node->GetMesh();
    if (Mesh)
    {
        const int32 SkinDeformerCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
        const bool bHasSkin = SkinDeformerCount > 0;

        if (bHasSkin)
        {
            FFBXSkeletalMeshImportData SkeletalMeshData = {};
            SkeletalMeshData.Name = Mesh->GetName();
            if (SkeletalMeshData.Name.empty())
            {
                SkeletalMeshData.Name = Node->GetName();
            }
            SkeletalMeshData.SourceNodeIndex = curNodeIndex;
            SkeletalMeshData.SkinDeformerCount = SkinDeformerCount;
            SkeletalMeshData.ControlPointCount = Mesh->GetControlPointsCount();
            SkeletalMeshData.PolygonCount = Mesh->GetPolygonCount();

            const int32 SkeletalMeshIndex = static_cast<int32>(OutImportScene.SkeletalMeshes.size());
            OutImportScene.SkeletalMeshes.push_back(SkeletalMeshData);
            OutImportScene.Nodes[curNodeIndex].SkeletalMeshIndex = SkeletalMeshIndex;
        }
        else
        {
            FFBXStaticMeshImportData StaticMeshData;
            StaticMeshData.SourceNodeIndex = curNodeIndex;
            if (BuildStaticMeshImportData(Node, Mesh, StaticMeshData))
            {
                int32 StaticMeshIndex = static_cast<int32>(OutImportScene.StaticMeshes.size());

                OutImportScene.StaticMeshes.push_back(StaticMeshData);
                OutImportScene.Nodes[curNodeIndex].StaticMeshIndex = StaticMeshIndex;
            }
        }
    }

    for (int32 i = 0; i < Node->GetChildCount(); ++i)
    {
        TraversalNode(Node->GetChild(i), curNodeIndex, OutImportScene);
    }
}

int32 FFBXImporter::AddImportNode(FbxNode* Node, int32 ParentIndex, OUT FFBXImportScene& OutImportScene)
{
    FFBXImportNode ImportNode = {};
    int32 curIndex = static_cast<int32>(OutImportScene.Nodes.size());

    ImportNode.Name = Node->GetName();
    ImportNode.ParentIndex = ParentIndex;

    if (ParentIndex >= 0)
    {
        FFBXImportNode& ParentImportNode = OutImportScene.Nodes[ParentIndex];
        ParentImportNode.Children.push_back(curIndex);
    }

    ImportNode.LocalTransformMatrix = ConvertFbxMatrix(Node->EvaluateLocalTransform());
    ImportNode.GlobalTransformMatrix = ConvertFbxMatrix(Node->EvaluateGlobalTransform());
    OutImportScene.Nodes.push_back(ImportNode);

    return curIndex;
}

bool FFBXImporter::BuildStaticMeshImportData(FbxNode* Node, FbxMesh* Mesh, OUT FFBXStaticMeshImportData& OutMeshData)
{
    OutMeshData.Name = Mesh->GetName();
    if (OutMeshData.Name.empty())
    {
        OutMeshData.Name = Node->GetName();
    }

    OutMeshData.Vertices.clear();
    OutMeshData.Indices.clear();
    OutMeshData.Sections.clear();

    // PolyGon을 순회하면서 FStaticMeshSection을 채워야 해서 먼저 Materail 생성
    BuildMaterialSlot(Node, OutMeshData);
    FString UVSetName = GetFirstUVName(Mesh);

    TArray<TArray<uint32>> SlotIndices;
    SlotIndices.resize(OutMeshData.MatreialSlots.size());

    for (int32 PolygonIndex = 0; PolygonIndex < Mesh->GetPolygonCount(); ++PolygonIndex)
    {
        if (Mesh->GetPolygonSize(PolygonIndex) != 3)
        {
            continue;
        }

        int32 MaterialIndex = GetPolygonMaterialIndex(Mesh, PolygonIndex);
        if (MaterialIndex < 0 || MaterialIndex >= static_cast<int32>(SlotIndices.size()))
        {
            MaterialIndex = 0;
        }

        for (int32 VertexInPolygon = 0; VertexInPolygon < 3; ++VertexInPolygon)
        {
            FNormalVertex Vertex = {};

            // UV
            if (!UVSetName.empty())
            {
                FbxVector2 FbxUV;
                bool bUnmapped = false;

                const bool bHasUV = Mesh->GetPolygonVertexUV(PolygonIndex, VertexInPolygon,  UVSetName.c_str(), FbxUV, bUnmapped);

                if (bHasUV && !bUnmapped)
                {
                    Vertex.UVs = FVector2(
                        static_cast<float>(FbxUV[0]),
                        1.0f - static_cast<float>(FbxUV[1]));
                }
            }

            // Normal
            FbxVector4 FbxNormal;
            
            if (Mesh->GetPolygonVertexNormal(PolygonIndex, VertexInPolygon, FbxNormal))
            {
                Vertex.Normal = FVector(
                    static_cast<float>(FbxNormal[0]),
                    static_cast<float>(FbxNormal[1]),
                    static_cast<float>(FbxNormal[2])
                );
            }

            // Position -> 인덱스 기반으로 ControlPoint 정점 찾기
            FbxVector4* pControlPoints = Mesh->GetControlPoints();
            int32 ControlPointIndex = Mesh->GetPolygonVertex(PolygonIndex, VertexInPolygon);
            if (pControlPoints && ControlPointIndex >= 0)
            {
                Vertex.Position = FVector(
                    static_cast<float>(pControlPoints[ControlPointIndex][0]),
                    static_cast<float>(pControlPoints[ControlPointIndex][1]),
                    static_cast<float>(pControlPoints[ControlPointIndex][2])
                );
            }

            OutMeshData.Vertices.push_back(Vertex);

            const uint32 VertexIndex = static_cast<uint32>(OutMeshData.Vertices.size() - 1);
            SlotIndices[MaterialIndex].push_back(VertexIndex);
        }
    }

    BuildSectionsFromSlotIndices(SlotIndices, OutMeshData);
    BuildTangentsAndBitangents(OutMeshData);
    return true;
}

void FFBXImporter::BuildMaterialSlot(FbxNode* Node, FFBXStaticMeshImportData& OutMeshData)
{
    int32 MatCnt = static_cast<int32>(Node->GetMaterialCount());

    OutMeshData.MatreialSlots.clear();

    if (MatCnt <= 0)
    {
        FStaticMeshMaterialSlot MaterialSlot = {};
        MaterialSlot.SlotName = "DefaultMaterial";
        MaterialSlot.Material = FResourceManager::Get().GetMaterial("DefaultWhite");
        OutMeshData.MatreialSlots.push_back(MaterialSlot);
        return;
    }

    for (int32 i = 0; i < MatCnt; ++i)
    {
        FStaticMeshMaterialSlot MaterialSlot = {};
        FbxSurfaceMaterial* Material = Node->GetMaterial(i);

        MaterialSlot.SlotName = GetFbxMaterialName(Material);
        MaterialSlot.Material = BuildMaterialInterface(Material);

        OutMeshData.MatreialSlots.push_back(MaterialSlot);
    }

}

UMaterialInterface* FFBXImporter::BuildMaterialInterface(FbxSurfaceMaterial* Material)
{
    const FString MaterialName = GetFbxMaterialName(Material);
    if (UMaterial* ExistingMaterial = FResourceManager::Get().GetMaterial(MaterialName))
    {
        return ExistingMaterial;
    }

    UMaterial* ParentMaterial = FResourceManager::Get().GetMaterial("DefaultWhite");
    if (ParentMaterial == nullptr)
    {
        return nullptr;
    }

    UMaterialInstance* Instance = UMaterialInstance::CreateTransient(ParentMaterial);
    Instance->Name = MaterialName;

    const FVector DiffuseColor = GetFbxVectorProperty(Material, FbxSurfaceMaterial::sDiffuse, FVector(0.8f, 0.8f, 0.8f));
    const FVector SpecularColor = GetFbxVectorProperty(Material, FbxSurfaceMaterial::sSpecular, FVector::ZeroVector);
    const FVector EmissiveColor = GetFbxVectorProperty(Material, FbxSurfaceMaterial::sEmissive, FVector::ZeroVector);
    const float Shininess = GetFbxFloatProperty(Material, FbxSurfaceMaterial::sShininess, 0.0f);
    const float Transparency = GetFbxFloatProperty(Material, FbxSurfaceMaterial::sTransparencyFactor, 0.0f);

    Instance->SetParam("BaseColor", FMaterialParamValue(DiffuseColor));
    Instance->SetParam("SpecularColor", FMaterialParamValue(SpecularColor));
    Instance->SetParam("EmissiveColor", FMaterialParamValue(EmissiveColor));
    Instance->SetParam("Shininess", FMaterialParamValue(Shininess));
    Instance->SetParam("Opacity", FMaterialParamValue(1.0f - Transparency));

    UTexture* DefaultWhite = FResourceManager::Get().GetTexture("DefaultWhite");
    UTexture* DefaultNormal = FResourceManager::Get().GetTexture("DefaultNormal");

    UTexture* DiffuseTexture = nullptr;
    if (FbxFileTexture* FbxDiffuseTexture = GetFirstFileTexture(Material, FbxSurfaceMaterial::sDiffuse))
    {
        FString TexturePath = FbxDiffuseTexture->GetFileName();
        if (TexturePath.empty())
        {
            TexturePath = FbxDiffuseTexture->GetRelativeFileName();
        }

        if (!TexturePath.empty())
        {
            std::filesystem::path FsTexturePath(FPaths::ToWide(TexturePath));
            if (!FsTexturePath.is_absolute() && !ImportDirectory.empty())
            {
                FsTexturePath = std::filesystem::path(FPaths::ToWide(ImportDirectory)) / FsTexturePath;
            }

            TexturePath = FPaths::ToUtf8(FsTexturePath.lexically_normal().generic_wstring());
            DiffuseTexture = FResourceManager::Get().LoadTexture(TexturePath);
        }
    }

    Instance->SetParam("DiffuseMap", FMaterialParamValue(DiffuseTexture ? DiffuseTexture : DefaultWhite));
    Instance->SetParam("SpecularMap", FMaterialParamValue(DefaultWhite));
    Instance->SetParam("NormalMap", FMaterialParamValue(DefaultNormal));
    Instance->SetParam("BumpMap", FMaterialParamValue(DefaultWhite));
    Instance->SetParam("bHasDiffuseMap", FMaterialParamValue(DiffuseTexture != nullptr));
    Instance->SetParam("bHasSpecularMap", FMaterialParamValue(false));
    Instance->SetParam("bHasNormalMap", FMaterialParamValue(false));
    Instance->SetParam("bHasBumpMap", FMaterialParamValue(false));
    Instance->SetParam("ScrollUV", FMaterialParamValue(FVector2(0.0f, 0.0f)));

    return Instance;
}

FString FFBXImporter::GetFbxMaterialName(FbxSurfaceMaterial* Material) const
{
    return Material ? FString(Material->GetName()) : FString("DefaultMaterial");
}

FVector FFBXImporter::GetFbxVectorProperty(FbxSurfaceMaterial* Material, const char* PropertyName, const FVector& DefaultValue) const
{
    if (Material == nullptr)
    {
        return DefaultValue;
    }

    FbxProperty Property = Material->FindProperty(PropertyName);
    if (!Property.IsValid())
    {
        return DefaultValue;
    }

    const FbxDouble3 Value = Property.Get<FbxDouble3>();
    return FVector(
        static_cast<float>(Value[0]),
        static_cast<float>(Value[1]),
        static_cast<float>(Value[2]));
}

float FFBXImporter::GetFbxFloatProperty(FbxSurfaceMaterial* Material, const char* PropertyName, float DefaultValue) const
{
    if (Material == nullptr)
    {
        return DefaultValue;
    }

    FbxProperty Property = Material->FindProperty(PropertyName);
    if (!Property.IsValid())
    {
        return DefaultValue;
    }

    return static_cast<float>(Property.Get<FbxDouble>());
}

FbxFileTexture* FFBXImporter::GetFirstFileTexture(FbxSurfaceMaterial* Material, const char* PropertyName) const
{
    if (Material == nullptr)
    {
        return nullptr;
    }

    FbxProperty Property = Material->FindProperty(PropertyName);
    if (!Property.IsValid() || Property.GetSrcObjectCount<FbxFileTexture>() <= 0)
    {
        return nullptr;
    }

    return Property.GetSrcObject<FbxFileTexture>(0);
}

void FFBXImporter::BuildSectionsFromSlotIndices(const TArray<TArray<uint32>>& SlotIndices, FFBXStaticMeshImportData& OutMesh)
{
    OutMesh.Indices.clear();
    OutMesh.Sections.clear();

    for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(SlotIndices.size()); ++SlotIndex)
    {
        const TArray<uint32>& IndicesPerSlot = SlotIndices[SlotIndex];
        if (IndicesPerSlot.empty())
        {
            continue;
        }

        FStaticMeshSection Section = {};
        Section.StartIndex = static_cast<uint32>(OutMesh.Indices.size());
        Section.IndexCount = static_cast<uint32>(IndicesPerSlot.size());
        Section.MaterialSlotIndex = SlotIndex;

        OutMesh.Indices.insert(
            OutMesh.Indices.end(),
            IndicesPerSlot.begin(),
            IndicesPerSlot.end());

        OutMesh.Sections.push_back(Section);
    }
}

void FFBXImporter::BuildTangentsAndBitangents(FFBXStaticMeshImportData& OutMesh)
{
    if (OutMesh.Vertices.empty() || OutMesh.Indices.empty())
    {
        return;
    }

    TArray<FVector> AccumulatedTangents(OutMesh.Vertices.size(), FVector::ZeroVector);
    TArray<FVector> AccumulatedBitangents(OutMesh.Vertices.size(), FVector::ZeroVector);

    for (size_t TriangleIndex = 0; TriangleIndex + 2 < OutMesh.Indices.size(); TriangleIndex += 3)
    {
        const uint32 Index0 = OutMesh.Indices[TriangleIndex + 0];
        const uint32 Index1 = OutMesh.Indices[TriangleIndex + 1];
        const uint32 Index2 = OutMesh.Indices[TriangleIndex + 2];

        if (Index0 >= OutMesh.Vertices.size() ||
            Index1 >= OutMesh.Vertices.size() ||
            Index2 >= OutMesh.Vertices.size())
        {
            continue;
        }

        const FNormalVertex& Vertex0 = OutMesh.Vertices[Index0];
        const FNormalVertex& Vertex1 = OutMesh.Vertices[Index1];
        const FNormalVertex& Vertex2 = OutMesh.Vertices[Index2];

        const FVector Edge1 = Vertex1.Position - Vertex0.Position;
        const FVector Edge2 = Vertex2.Position - Vertex0.Position;

        const FVector2 DeltaUV1 = Vertex1.UVs - Vertex0.UVs;
        const FVector2 DeltaUV2 = Vertex2.UVs - Vertex0.UVs;

        const float Denominator = DeltaUV1.X * DeltaUV2.Y - DeltaUV2.X * DeltaUV1.Y;
        if (std::fabs(Denominator) < 1e-8f)
        {
            continue;
        }

        const float InvDenominator = 1.0f / Denominator;

        FVector Tangent = (Edge1 * DeltaUV2.Y - Edge2 * DeltaUV1.Y) * InvDenominator;
        FVector Bitangent = (Edge2 * DeltaUV1.X - Edge1 * DeltaUV2.X) * InvDenominator;

        Tangent.Normalize();
        Bitangent.Normalize();

        AccumulatedTangents[Index0] += Tangent;
        AccumulatedTangents[Index1] += Tangent;
        AccumulatedTangents[Index2] += Tangent;

        AccumulatedBitangents[Index0] += Bitangent;
        AccumulatedBitangents[Index1] += Bitangent;
        AccumulatedBitangents[Index2] += Bitangent;
    }

    for (size_t VertexIndex = 0; VertexIndex < OutMesh.Vertices.size(); ++VertexIndex)
    {
        FNormalVertex& Vertex = OutMesh.Vertices[VertexIndex];

        FVector Tangent = AccumulatedTangents[VertexIndex];
        FVector Bitangent = AccumulatedBitangents[VertexIndex];

        if (Tangent.SizeSquared() > 1e-8f)
        {
            Tangent.Normalize();
        }
        else
        {
            Tangent = FVector(1.0f, 0.0f, 0.0f);
        }

        if (Bitangent.SizeSquared() > 1e-8f)
        {
            Bitangent.Normalize();
        }
        else
        {
            Bitangent = FVector::CrossProduct(Vertex.Normal, Tangent);
            if (Bitangent.SizeSquared() > 1e-8f)
            {
                Bitangent.Normalize();
            }
            else
            {
                Bitangent = FVector(0.0f, 1.0f, 0.0f);
            }
        }

        Vertex.Tangent = Tangent;
        Vertex.Bitangent = Bitangent;
    }
}

FString FFBXImporter::GetFirstUVName(FbxMesh* Mesh)
{
    FbxStringList UVNames;
    Mesh->GetUVSetNames(UVNames);
    if (UVNames.GetCount() <= 0)
    {
        return "";
    }
    // 첫번째 채널 UV 반환 주로 Diffuse Normal 처럼 일반적인 Map 전용
    // LightMap을 쓰는 경우 다른 채널 써야함
    return FString(UVNames[0]);
}

FAABB FFBXImporter::BuildLocalBounds(const FFBXStaticMeshImportData& ImportData) const
{
    FAABB Bounds;
    Bounds.Reset();

    for (const FNormalVertex& Vertex : ImportData.Vertices)
    {
        Bounds.Expand(Vertex.Position);
    }

    return Bounds;
}

int32 FFBXImporter::GetPolygonMaterialIndex(FbxMesh* Mesh, int32 PolygonIndex)
{
    FbxLayerElementMaterial* MaterialElement = Mesh->GetElementMaterial();
    if (!MaterialElement)
    {
        return 0;
    }

    if (MaterialElement->GetMappingMode() == FbxLayerElement::eByPolygon)
    {
        return MaterialElement->GetIndexArray().GetAt(PolygonIndex);
    }

    // eAllSame
    return 0;
}

FMatrix FFBXImporter::ConvertFbxMatrix(const FbxAMatrix& fbxMatrix)
{
    FMatrix Result;
    for (int32 Row = 0; Row < 4; ++Row)
    {
        for (int32 Col = 0; Col < 4; ++Col)
        {
            Result.M[Row][Col] = static_cast<float>(fbxMatrix.Get(Row, Col));
        }
    }
    return Result;
}
