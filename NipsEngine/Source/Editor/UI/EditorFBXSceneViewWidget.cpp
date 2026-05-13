#include "Editor/UI/EditorFBXSceneViewWidget.h"

#include "Asset/BinarySerializer.h"
#include "Asset/SkeletalMesh.h"
#include "Component/SceneComponent.h"
#include "Component/GizmoComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Core/Paths.h"
#include "Core/Logging/Log.h"
#include "Core/ResourceManager.h"
#include "Editor/EditorEngine.h"
#include "Editor/EditorRenderPipeline.h"
#include "Editor/Viewport/FFBXPreviewViewportLayout.h"
#include "Editor/Viewport/FBXPreviewViewportClient.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Slate/SlateUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/World.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx11.h"
#include "Object/Object.h"
#include "Render/Renderer/Renderer.h"

#include <commdlg.h>
#include <algorithm>
#include <chrono>
#include <filesystem>

namespace
{
    void SetOpaqueBlendStateCallback(const ImDrawList*, const ImDrawCmd* Cmd)
    {
        ID3D11DeviceContext* DeviceContext = static_cast<ID3D11DeviceContext*>(Cmd->UserCallbackData);
        if (!DeviceContext)
        {
            return;
        }

        const float BlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
        DeviceContext->OMSetBlendState(nullptr, BlendFactor, 0xffffffff);
    }

    const char* GetImportNodeTypeName(const FFBXImportNode& Node)
    {
        if (Node.SkeletalMeshIndex >= 0 && Node.StaticMeshIndex >= 0)
        {
            return "SkeletalMesh+StaticMesh";
        }
        if (Node.SkeletalMeshIndex >= 0)
        {
            return "SkeletalMesh";
        }
        if (Node.StaticMeshIndex >= 0)
        {
            return "StaticMesh";
        }
        if (Node.BoneIndex >= 0)
        {
            return "Bone";
        }
        return "Node";
    }

    ImVec4 GetImportNodeTypeColor(const FFBXImportNode& Node)
    {
        if (Node.SkeletalMeshIndex >= 0)
        {
            return ImVec4(1.0f, 0.72f, 0.34f, 1.0f);
        }
        if (Node.StaticMeshIndex >= 0)
        {
            return ImVec4(0.35f, 0.75f, 1.0f, 1.0f);
        }
        if (Node.BoneIndex >= 0)
        {
            return ImVec4(0.62f, 0.90f, 0.52f, 1.0f);
        }
        return ImVec4(0.72f, 0.72f, 0.72f, 1.0f);
    }

    void ApplyImportNodeTransform(USceneComponent* Component, const FFBXImportNode& Node)
    {
        if (Component == nullptr)
        {
            return;
        }

        Component->SetRelativeMatrix(Node.GlobalTransformMatrix);
    }

    FString BuildImportedActorName(const FString& LoadedFilePath)
    {
        if (LoadedFilePath.empty())
        {
            return "FBX_ImportedScene";
        }

        const std::filesystem::path FilePath(FPaths::ToWide(LoadedFilePath));
        const FString Stem = FPaths::ToUtf8(FilePath.stem().wstring());
        return Stem.empty() ? "FBX_ImportedScene" : FString("FBX_") + Stem;
    }

    FString GetPathStemName(const FString& Path)
    {
        const std::filesystem::path FilePath(FPaths::ToWide(Path));
        const FString Stem = FPaths::ToUtf8(FilePath.stem().wstring());
        return Stem.empty() ? FString("SkeletalMesh") : Stem;
    }

    uint64 GetFileWriteTimeTicks(const FString& Path)
    {
        namespace fs = std::filesystem;

        fs::path FilePath(FPaths::ToAbsolute(FPaths::ToWide(Path)));
        if (!fs::exists(FilePath))
        {
            return 0;
        }

        auto WriteTime = fs::last_write_time(FilePath);
        auto Duration = WriteTime.time_since_epoch();
        return static_cast<uint64>(std::chrono::duration_cast<std::chrono::seconds>(Duration).count());
    }

    FString MakeSkeletalMeshBinaryPath(const FString& SourcePath, int32 MeshIndex)
    {
        namespace fs = std::filesystem;

        fs::path SourceFsPath(FPaths::ToWide(SourcePath));
        fs::path BinDir = fs::path(FPaths::RootDir()) / "Asset" / "Mesh" / "Bin";

        if (!fs::exists(BinDir))
        {
            fs::create_directories(BinDir);
        }

        fs::path BinaryFileName = SourceFsPath.stem();
        if (MeshIndex > 0)
        {
            BinaryFileName += "_skel";
            BinaryFileName += std::to_string(MeshIndex);
        }
        BinaryFileName += ".skmesh";

        fs::path BinaryPath = BinDir / BinaryFileName;
        return FPaths::ToString(BinaryPath.wstring());
    }

    bool IsSkeletalMeshBinaryValid(
        const FString& SourcePath,
        const FString& BinaryPath,
        const FBinarySerializer& BinarySerializer)
    {
        FSkeletalMeshBinaryHeader Header;
        if (!BinarySerializer.ReadSkeletalMeshHeader(BinaryPath, Header))
        {
            return false;
        }

        const uint64 SourceWriteTime = GetFileWriteTimeTicks(SourcePath);
        return SourceWriteTime != 0 && Header.SourceFileWriteTime == SourceWriteTime;
    }

    void RestoreSkeletalMeshSlotMaterials(FSkeletalMesh& Mesh)
    {
        FResourceManager::Get().RestoreSkeletalMeshSlotMaterials(Mesh);
    }

    int32 AddImportSceneNode(FFBXImportScene& OutScene, int32 ParentIndex, FFBXImportNode Node)
    {
        const int32 NodeIndex = static_cast<int32>(OutScene.Nodes.size());
        Node.ParentIndex = ParentIndex;

        if (ParentIndex >= 0 && ParentIndex < static_cast<int32>(OutScene.Nodes.size()))
        {
            OutScene.Nodes[ParentIndex].Children.push_back(NodeIndex);
        }

        OutScene.Nodes.push_back(Node);
        return NodeIndex;
    }

    FFBXSkeletalMeshImportData BuildSkeletalImportDataFromMesh(const FSkeletalMesh& Mesh, int32 SourceNodeIndex)
    {
        FFBXSkeletalMeshImportData ImportData = {};
        ImportData.Name = Mesh.FilePathName;
        ImportData.SourceNodeIndex = SourceNodeIndex;
        ImportData.SourceNodeLocalTransformMatrix = Mesh.SourceNodeLocalTransform;
        ImportData.SourceNodeGlobalTransformMatrix = Mesh.SourceNodeGlobalTransform;
        ImportData.Vertices = Mesh.Vertices;
        ImportData.Indices = Mesh.Indices;
        ImportData.Sections = Mesh.Sections;
        ImportData.MaterialSlots = Mesh.Slots;
        ImportData.Bones = Mesh.Bones;
        return ImportData;
    }

    FFBXStaticMeshImportData BuildStaticImportDataFromEmbeddedMesh(const FSkeletalMeshEmbeddedStaticMesh& Mesh)
    {
        FFBXStaticMeshImportData ImportData = {};
        ImportData.Name = Mesh.Name;
        ImportData.SourceNodeIndex = Mesh.SourceNodeIndex;
        ImportData.Vertices = Mesh.Vertices;
        ImportData.Indices = Mesh.Indices;
        ImportData.Sections = Mesh.Sections;
        ImportData.MaterialSlots = Mesh.MaterialSlots;

        for (FStaticMeshMaterialSlot& Slot : ImportData.MaterialSlots)
        {
            if (Slot.Material == nullptr)
            {
                Slot.Material = FResourceManager::Get().GetMaterialInterface(Slot.SlotName);
            }

            if (Slot.Material == nullptr)
            {
                Slot.Material = FResourceManager::Get().GetMaterial("DefaultWhite");
            }
        }

        return ImportData;
    }

    FFBXStaticMeshImportData BuildStaticImportDataFromSkeletalMesh(
        const FSkeletalMesh& Mesh,
        int32 SourceNodeIndex)
    {
        FFBXStaticMeshImportData ImportData = {};
        ImportData.Name = Mesh.FilePathName.empty() ? FString("SkeletalMesh_Static") : Mesh.FilePathName;
        ImportData.SourceNodeIndex = SourceNodeIndex;
        ImportData.Indices = Mesh.Indices;

        ImportData.Vertices.reserve(Mesh.Vertices.size());
        for (const FSkeletalVertex& SkeletalVertex : Mesh.Vertices)
        {
            FNormalVertex StaticVertex = {};
            StaticVertex.Position = SkeletalVertex.Position;
            StaticVertex.Color = SkeletalVertex.Color;
            StaticVertex.Normal = SkeletalVertex.Normal;
            StaticVertex.UVs = SkeletalVertex.UVs;
            StaticVertex.Tangent = SkeletalVertex.Tangent;
            StaticVertex.Bitangent = SkeletalVertex.Bitangent;
            ImportData.Vertices.push_back(StaticVertex);
        }

        ImportData.Sections.reserve(Mesh.Sections.size());
        for (const FSkeletalMeshSection& SkeletalSection : Mesh.Sections)
        {
            FStaticMeshSection StaticSection = {};
            StaticSection.StartIndex = SkeletalSection.StartIndex;
            StaticSection.IndexCount = SkeletalSection.IndexCount;
            StaticSection.MaterialSlotIndex = SkeletalSection.MaterialSlotIndex;
            ImportData.Sections.push_back(StaticSection);
        }

        ImportData.MaterialSlots.reserve(Mesh.Slots.size());
        for (const FSkeletalMeshMaterialSlot& SkeletalSlot : Mesh.Slots)
        {
            FStaticMeshMaterialSlot StaticSlot = {};
            StaticSlot.SlotName = SkeletalSlot.SlotName;
            StaticSlot.Material = SkeletalSlot.Material;
            if (StaticSlot.Material == nullptr)
            {
                StaticSlot.Material = FResourceManager::Get().GetMaterialInterface(StaticSlot.SlotName);
            }

            if (StaticSlot.Material == nullptr)
            {
                StaticSlot.Material = FResourceManager::Get().GetMaterial("DefaultWhite");
            }

            ImportData.MaterialSlots.push_back(StaticSlot);
        }

        return ImportData;
    }

    void AppendStaticImportDataToBakedMesh(
        const FFBXStaticMeshImportData& SourceMesh,
        const FMatrix& LocalToBakedRoot,
        FFBXStaticMeshImportData& OutMesh)
    {
        const uint32 VertexBase = static_cast<uint32>(OutMesh.Vertices.size());
        const uint32 IndexBase = static_cast<uint32>(OutMesh.Indices.size());
        const int32 MaterialBase = static_cast<int32>(OutMesh.MaterialSlots.size());
        const FMatrix NormalToBakedRoot = LocalToBakedRoot.GetInverse().GetTransposed();

        OutMesh.Vertices.reserve(OutMesh.Vertices.size() + SourceMesh.Vertices.size());
        for (const FNormalVertex& Src : SourceMesh.Vertices)
        {
            FNormalVertex Dst = Src;
            Dst.Position = LocalToBakedRoot.TransformPosition(Src.Position);
            Dst.Normal = NormalToBakedRoot.TransformVector(Src.Normal).GetSafeNormal();
            Dst.Tangent = LocalToBakedRoot.TransformVector(Src.Tangent).GetSafeNormal();
            Dst.Bitangent = LocalToBakedRoot.TransformVector(Src.Bitangent).GetSafeNormal();
            OutMesh.Vertices.push_back(Dst);
        }

        OutMesh.Indices.reserve(OutMesh.Indices.size() + SourceMesh.Indices.size());
        for (uint32 Index : SourceMesh.Indices)
        {
            OutMesh.Indices.push_back(VertexBase + Index);
        }

        OutMesh.MaterialSlots.insert(
            OutMesh.MaterialSlots.end(),
            SourceMesh.MaterialSlots.begin(),
            SourceMesh.MaterialSlots.end());

        OutMesh.Sections.reserve(OutMesh.Sections.size() + SourceMesh.Sections.size());
        for (const FStaticMeshSection& SourceSection : SourceMesh.Sections)
        {
            FStaticMeshSection Section = {};
            Section.StartIndex = IndexBase + SourceSection.StartIndex;
            Section.IndexCount = SourceSection.IndexCount;
            Section.MaterialSlotIndex = MaterialBase + SourceSection.MaterialSlotIndex;
            OutMesh.Sections.push_back(Section);
        }
    }

    void CollapseStaticMeshesToSingleComponent(FFBXImportScene& Scene)
    {
        if (Scene.Nodes.empty() || Scene.StaticMeshes.size() <= 1)
        {
            return;
        }

        const int32 BakedSourceNodeIndex = 0;
        const FMatrix BakedRootGlobalInverse = Scene.Nodes[BakedSourceNodeIndex].GlobalTransformMatrix.GetInverse();
        const TArray<FFBXStaticMeshImportData> SourceMeshes = Scene.StaticMeshes;

        FFBXStaticMeshImportData BakedMesh = {};
        BakedMesh.SourceNodeIndex = BakedSourceNodeIndex;
        BakedMesh.Name = Scene.Nodes[BakedSourceNodeIndex].Name.empty()
            ? FString("BakedSceneStaticMesh")
            : Scene.Nodes[BakedSourceNodeIndex].Name + FString("_Static");

        for (const FFBXStaticMeshImportData& SourceMesh : SourceMeshes)
        {
            if (SourceMesh.Vertices.empty() || SourceMesh.Indices.empty())
            {
                continue;
            }

            FMatrix LocalToBakedRoot = FMatrix::Identity;
            if (SourceMesh.SourceNodeIndex >= 0 &&
                SourceMesh.SourceNodeIndex < static_cast<int32>(Scene.Nodes.size()))
            {
                LocalToBakedRoot = Scene.Nodes[SourceMesh.SourceNodeIndex].GlobalTransformMatrix * BakedRootGlobalInverse;
            }

            AppendStaticImportDataToBakedMesh(SourceMesh, LocalToBakedRoot, BakedMesh);
        }

        for (FFBXImportNode& Node : Scene.Nodes)
        {
            Node.StaticMeshIndex = -1;
            Node.SkeletalMeshIndex = -1;
        }

        Scene.StaticMeshes.clear();
        Scene.SkeletalMeshes.clear();
        if (BakedMesh.Vertices.empty() || BakedMesh.Indices.empty())
        {
            return;
        }

        Scene.StaticMeshes.push_back(BakedMesh);
        Scene.Nodes[BakedSourceNodeIndex].StaticMeshIndex = 0;
    }

    int32 FindBoneIndexByName(const FSkeletalMesh& Mesh, const FString& BoneName)
    {
        for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Mesh.Bones.size()); ++BoneIndex)
        {
            if (Mesh.Bones[BoneIndex].Name == BoneName)
            {
                return BoneIndex;
            }
        }

        return -1;
    }

    bool AppendCachedSceneToImportScene(
        const FSkeletalMesh& Mesh,
        bool bImportSkinnedMeshesAsStatic,
        FFBXImportScene& OutScene)
    {
        if (Mesh.SceneNodes.empty())
        {
            return false;
        }

        OutScene.Nodes.clear();
        OutScene.StaticMeshes.clear();
        OutScene.SkeletalMeshes.clear();

        OutScene.StaticMeshes.reserve(Mesh.EmbeddedStaticMeshes.size());
        for (const FSkeletalMeshEmbeddedStaticMesh& EmbeddedStaticMesh : Mesh.EmbeddedStaticMeshes)
        {
            OutScene.StaticMeshes.push_back(BuildStaticImportDataFromEmbeddedMesh(EmbeddedStaticMesh));
        }

        int32 SourceNodeIndex = Mesh.SourceNodeIndex;
        if (SourceNodeIndex < 0 || SourceNodeIndex >= static_cast<int32>(Mesh.SceneNodes.size()))
        {
            SourceNodeIndex = -1;
            for (int32 NodeIndex = 0; NodeIndex < static_cast<int32>(Mesh.SceneNodes.size()); ++NodeIndex)
            {
                if (Mesh.SceneNodes[NodeIndex].SkeletalMeshIndex == Mesh.SourceSceneSkeletalMeshIndex)
                {
                    SourceNodeIndex = NodeIndex;
                    break;
                }
            }
        }

        int32 StaticSkinnedMeshIndex = -1;
        if (bImportSkinnedMeshesAsStatic)
        {
            StaticSkinnedMeshIndex = static_cast<int32>(OutScene.StaticMeshes.size());
            OutScene.StaticMeshes.push_back(BuildStaticImportDataFromSkeletalMesh(Mesh, SourceNodeIndex));
        }

        OutScene.Nodes.reserve(Mesh.SceneNodes.size());
        for (int32 NodeIndex = 0; NodeIndex < static_cast<int32>(Mesh.SceneNodes.size()); ++NodeIndex)
        {
            const FSkeletalMeshSceneNode& CachedNode = Mesh.SceneNodes[NodeIndex];
            FFBXImportNode Node = {};
            Node.Name = CachedNode.Name;
            Node.ParentIndex = CachedNode.ParentIndex;
            Node.Children = CachedNode.Children;
            Node.LocalTransformMatrix = CachedNode.LocalTransformMatrix;
            Node.GlobalTransformMatrix = CachedNode.GlobalTransformMatrix;
            Node.StaticMeshIndex = CachedNode.StaticMeshIndex >= 0 &&
                    CachedNode.StaticMeshIndex < static_cast<int32>(OutScene.StaticMeshes.size())
                ? CachedNode.StaticMeshIndex
                : -1;

            if (CachedNode.SkeletalMeshIndex == Mesh.SourceSceneSkeletalMeshIndex)
            {
                if (bImportSkinnedMeshesAsStatic)
                {
                    Node.StaticMeshIndex = StaticSkinnedMeshIndex;
                }
                else
                {
                    Node.SkeletalMeshIndex = 0;
                }
            }

            Node.BoneIndex = bImportSkinnedMeshesAsStatic
                ? -1
                : (CachedNode.BoneIndex >= 0 &&
                        CachedNode.BoneIndex < static_cast<int32>(Mesh.Bones.size())
                    ? CachedNode.BoneIndex
                    : FindBoneIndexByName(Mesh, CachedNode.Name));

            OutScene.Nodes.push_back(Node);
        }

        if (SourceNodeIndex < 0)
        {
            SourceNodeIndex = OutScene.Nodes.empty() ? -1 : 0;
        }

        if (SourceNodeIndex >= 0 && SourceNodeIndex < static_cast<int32>(OutScene.Nodes.size()))
        {
            if (bImportSkinnedMeshesAsStatic)
            {
                OutScene.Nodes[SourceNodeIndex].StaticMeshIndex = StaticSkinnedMeshIndex;
            }
            else
            {
                OutScene.Nodes[SourceNodeIndex].SkeletalMeshIndex = 0;
            }
        }

        if (!bImportSkinnedMeshesAsStatic)
        {
            OutScene.SkeletalMeshes.push_back(BuildSkeletalImportDataFromMesh(Mesh, SourceNodeIndex));
        }
        return true;
    }

    void AddBoneNodesRecursive(
        const FSkeletalMesh& Mesh,
        const TArray<TArray<int32>>& BoneChildren,
        int32 BoneIndex,
        int32 ParentNodeIndex,
        FFBXImportScene& OutScene)
    {
        if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Mesh.Bones.size()))
        {
            return;
        }

        const FBoneInfo& Bone = Mesh.Bones[BoneIndex];

        FFBXImportNode BoneNode = {};
        BoneNode.Name = Bone.Name;
        BoneNode.BoneIndex = BoneIndex;
        BoneNode.LocalTransformMatrix = Bone.LocalTransform;
        BoneNode.GlobalTransformMatrix = Bone.GlobalTransform;

        const int32 BoneNodeIndex = AddImportSceneNode(OutScene, ParentNodeIndex, BoneNode);
        for (int32 ChildBoneIndex : BoneChildren[BoneIndex])
        {
            AddBoneNodesRecursive(Mesh, BoneChildren, ChildBoneIndex, BoneNodeIndex, OutScene);
        }
    }

    void AppendSkeletalMeshToImportScene(
        const FSkeletalMesh& Mesh,
        const FString& FallbackName,
        FFBXImportScene& OutScene,
        int32 RootNodeIndex)
    {
        FFBXImportNode MeshNode = {};
        MeshNode.Name = Mesh.FilePathName.empty() ? FallbackName : Mesh.FilePathName;
        MeshNode.SkeletalMeshIndex = static_cast<int32>(OutScene.SkeletalMeshes.size());
        MeshNode.LocalTransformMatrix = Mesh.SourceNodeLocalTransform;
        MeshNode.GlobalTransformMatrix = Mesh.SourceNodeGlobalTransform;

        const int32 MeshNodeIndex = AddImportSceneNode(OutScene, RootNodeIndex, MeshNode);
        OutScene.SkeletalMeshes.push_back(BuildSkeletalImportDataFromMesh(Mesh, MeshNodeIndex));

        TArray<TArray<int32>> BoneChildren;
        BoneChildren.resize(Mesh.Bones.size());
        TArray<int32> RootBoneIndices;

        for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Mesh.Bones.size()); ++BoneIndex)
        {
            const int32 ParentBoneIndex = Mesh.Bones[BoneIndex].ParentIndex;
            if (ParentBoneIndex >= 0 && ParentBoneIndex < static_cast<int32>(Mesh.Bones.size()))
            {
                BoneChildren[ParentBoneIndex].push_back(BoneIndex);
            }
            else
            {
                RootBoneIndices.push_back(BoneIndex);
            }
        }

        for (int32 RootBoneIndex : RootBoneIndices)
        {
            AddBoneNodesRecursive(Mesh, BoneChildren, RootBoneIndex, MeshNodeIndex, OutScene);
        }
    }

    void AppendSkeletalMeshAsStaticToImportScene(
        const FSkeletalMesh& Mesh,
        const FString& FallbackName,
        FFBXImportScene& OutScene,
        int32 RootNodeIndex)
    {
        FFBXImportNode MeshNode = {};
        MeshNode.Name = Mesh.FilePathName.empty() ? FallbackName : Mesh.FilePathName;
        MeshNode.StaticMeshIndex = static_cast<int32>(OutScene.StaticMeshes.size());
        MeshNode.LocalTransformMatrix = Mesh.SourceNodeLocalTransform;
        MeshNode.GlobalTransformMatrix = Mesh.SourceNodeGlobalTransform;

        const int32 MeshNodeIndex = AddImportSceneNode(OutScene, RootNodeIndex, MeshNode);
        OutScene.StaticMeshes.push_back(BuildStaticImportDataFromSkeletalMesh(Mesh, MeshNodeIndex));
    }

    bool LoadSkeletalMeshBinaryIntoScene(
        const FString& BinaryPath,
        const FString& SceneSourcePath,
        FBinarySerializer& BinarySerializer,
        FFBXImportScene& OutScene,
        bool bImportSkinnedMeshesAsStatic,
        double& OutLoadSec)
    {
        const auto LoadStart = std::chrono::steady_clock::now();

        FSkeletalMesh Mesh;
        if (!BinarySerializer.LoadSkeletalMesh(BinaryPath, Mesh))
        {
            return false;
        }

        const auto LoadEnd = std::chrono::steady_clock::now();
        OutLoadSec += std::chrono::duration<double>(LoadEnd - LoadStart).count();

        RestoreSkeletalMeshSlotMaterials(Mesh);
        if (OutScene.SkeletalMeshes.empty() && OutScene.StaticMeshes.empty() &&
            AppendCachedSceneToImportScene(Mesh, bImportSkinnedMeshesAsStatic, OutScene))
        {
            OutScene.SourceFilePath = SceneSourcePath;
            return true;
        }

        if (bImportSkinnedMeshesAsStatic)
        {
            AppendSkeletalMeshAsStaticToImportScene(Mesh, GetPathStemName(BinaryPath), OutScene, 0);
        }
        else
        {
            AppendSkeletalMeshToImportScene(Mesh, GetPathStemName(BinaryPath), OutScene, 0);
        }
        OutScene.SourceFilePath = SceneSourcePath;
        return true;
    }

    bool TryLoadSkeletalMeshSceneFromBinary(
        const FString& InputPath,
        FBinarySerializer& BinarySerializer,
        FFBXImportScene& OutScene,
        bool bImportSkinnedMeshesAsStatic,
        double& OutLoadSec)
    {
        namespace fs = std::filesystem;

        OutScene = FFBXImportScene{};
        OutLoadSec = 0.0;

        const fs::path InputFsPath(FPaths::ToWide(InputPath));
        const bool bInputIsSkeletalBinary = InputFsPath.extension() == L".skmesh";

        FFBXImportNode RootNode = {};
        RootNode.Name = BuildImportedActorName(InputPath);
        RootNode.LocalTransformMatrix = FMatrix::Identity;
        RootNode.GlobalTransformMatrix = FMatrix::Identity;
        AddImportSceneNode(OutScene, -1, RootNode);

        if (bInputIsSkeletalBinary)
        {
            if (!LoadSkeletalMeshBinaryIntoScene(
                    InputPath,
                    InputPath,
                    BinarySerializer,
                    OutScene,
                    bImportSkinnedMeshesAsStatic,
                    OutLoadSec))
            {
                OutScene = FFBXImportScene{};
                return false;
            }
            return true;
        }

        bool bLoadedAnyMesh = false;
        for (int32 MeshIndex = 0; MeshIndex < 1024; ++MeshIndex)
        {
            const FString BinaryPath = MakeSkeletalMeshBinaryPath(InputPath, MeshIndex);
            if (!IsSkeletalMeshBinaryValid(InputPath, BinaryPath, BinarySerializer))
            {
                break;
            }

            if (!LoadSkeletalMeshBinaryIntoScene(
                    BinaryPath,
                    InputPath,
                    BinarySerializer,
                    OutScene,
                    bImportSkinnedMeshesAsStatic,
                    OutLoadSec))
            {
                break;
            }

            bLoadedAnyMesh = true;
        }

        if (!bLoadedAnyMesh)
        {
            OutScene = FFBXImportScene{};
            return false;
        }

        return true;
    }

    struct FSkeletalMeshBinaryCacheStats
    {
        int32 LoadedCount = 0;
        int32 SavedCount = 0;
        int32 FailedCount = 0;
    };

    FSkeletalMesh* LoadOrCreateCachedSkeletalMesh(
        FFBXImporter& Importer,
        const FFBXImportScene& ImportScene,
        const FFBXSkeletalMeshImportData& MeshData,
        const FString& SourcePath,
        int32 MeshIndex,
        FBinarySerializer& BinarySerializer,
        FSkeletalMeshBinaryCacheStats& CacheStats)
    {
        const std::filesystem::path SourceFsPath(FPaths::ToWide(SourcePath));
        const bool bSourceIsSkeletalBinary = SourceFsPath.extension() == L".skmesh";
        const FString BinaryPath = bSourceIsSkeletalBinary ? SourcePath : MakeSkeletalMeshBinaryPath(SourcePath, MeshIndex);

        if (bSourceIsSkeletalBinary || IsSkeletalMeshBinaryValid(SourcePath, BinaryPath, BinarySerializer))
        {
            const auto BinaryStart = std::chrono::steady_clock::now();
            FSkeletalMesh* LoadedMesh = new FSkeletalMesh();
            if (BinarySerializer.LoadSkeletalMesh(BinaryPath, *LoadedMesh))
            {
                const auto BinaryEnd = std::chrono::steady_clock::now();
                const double BinaryLoadSec = std::chrono::duration<double>(BinaryEnd - BinaryStart).count();
                RestoreSkeletalMeshSlotMaterials(*LoadedMesh);
                ++CacheStats.LoadedCount;
                UE_LOG(
                    "[SkeletalMeshLoad] Source=Binary | Path=%s | MeshIndex=%d | BinarySec=%.6f | BinaryPath=%s",
                    SourcePath.c_str(),
                    MeshIndex,
                    BinaryLoadSec,
                    BinaryPath.c_str());
                return LoadedMesh;
            }

            delete LoadedMesh;
            ++CacheStats.FailedCount;
            UE_LOG(
                "[SkeletalMeshLoad] Binary load failed | Path=%s | MeshIndex=%d | BinaryPath=%s",
                SourcePath.c_str(),
                MeshIndex,
                BinaryPath.c_str());
        }

        if (bSourceIsSkeletalBinary)
        {
            return nullptr;
        }

        const auto CookStart = std::chrono::steady_clock::now();
        FSkeletalMesh* RawMesh = Importer.CreateSkeletalMeshFromtImportData(MeshData);
        const auto CookEnd = std::chrono::steady_clock::now();
        const double CookSec = std::chrono::duration<double>(CookEnd - CookStart).count();
        if (RawMesh == nullptr)
        {
            ++CacheStats.FailedCount;
            UE_LOG(
                "[SkeletalMeshLoad] Failed | Path=%s | MeshIndex=%d | CookSec=%.6f | BinaryPath=%s",
                SourcePath.c_str(),
                MeshIndex,
                CookSec,
                BinaryPath.c_str());
            return nullptr;
        }

        RawMesh->SkeletonAssetPath = SourcePath;
        Importer.AttachSceneDataToSkeletalMesh(ImportScene, MeshIndex, *RawMesh);
        RestoreSkeletalMeshSlotMaterials(*RawMesh);

        if (BinarySerializer.SaveSkeletalMesh(BinaryPath, SourcePath, *RawMesh))
        {
            ++CacheStats.SavedCount;
            UE_LOG(
                "[SkeletalMeshLoad] Source=FBX | Path=%s | MeshIndex=%d | CookSec=%.6f | BinarySave=OK | BinaryPath=%s",
                SourcePath.c_str(),
                MeshIndex,
                CookSec,
                BinaryPath.c_str());
        }
        else
        {
            ++CacheStats.FailedCount;
            UE_LOG(
                "[SkeletalMeshLoad] Source=FBX | Path=%s | MeshIndex=%d | CookSec=%.6f | BinarySave=FAIL | BinaryPath=%s",
                SourcePath.c_str(),
                MeshIndex,
                CookSec,
                BinaryPath.c_str());
        }

        return RawMesh;
    }

    bool TryAttachStaticMeshToBone(
        UStaticMeshComponent* StaticMeshComponent,
        int32 StaticSourceNodeIndex,
        const FFBXImportScene& ImportScene,
        const TArray<USkinnedMeshComponent*>& SkinnedMeshComponents)
    {
        if (StaticMeshComponent == nullptr ||
            StaticSourceNodeIndex < 0 ||
            StaticSourceNodeIndex >= static_cast<int32>(ImportScene.Nodes.size()))
        {
            return false;
        }

        const FFBXImportNode& StaticNode = ImportScene.Nodes[StaticSourceNodeIndex];
        int32 AncestorNodeIndex = StaticNode.ParentIndex;

        while (AncestorNodeIndex >= 0 && AncestorNodeIndex < static_cast<int32>(ImportScene.Nodes.size()))
        {
            const FFBXImportNode& AncestorNode = ImportScene.Nodes[AncestorNodeIndex];
            for (USkinnedMeshComponent* SkinnedMeshComponent : SkinnedMeshComponents)
            {
                if (SkinnedMeshComponent == nullptr)
                {
                    continue;
                }

                const int32 BoneIndex = SkinnedMeshComponent->FindBoneIndexByName(AncestorNode.Name);
                if (BoneIndex < 0)
                {
                    continue;
                }

                FMatrix BoneBindGlobalMeshTransform = FMatrix::Identity;
                if (!SkinnedMeshComponent->GetBindBoneGlobalMeshTransform(BoneIndex, BoneBindGlobalMeshTransform))
                {
                    continue;
                }

                const FMatrix BoneBindActorTransform = BoneBindGlobalMeshTransform * SkinnedMeshComponent->GetRelativeMatrix();
                const FMatrix AttachLocalOffset = StaticNode.GlobalTransformMatrix * BoneBindActorTransform.GetInverse();

                StaticMeshComponent->AttachToBone(SkinnedMeshComponent, BoneIndex, AttachLocalOffset);
                return true;
            }

            AncestorNodeIndex = AncestorNode.ParentIndex;
        }

        return false;
    }

    FWString GetFBXDialogInitialDir()
    {
        std::filesystem::path MeshDir(FPaths::RootDir());
        MeshDir /= L"Asset";
        MeshDir /= L"Mesh";
        MeshDir = MeshDir.lexically_normal();
        MeshDir.make_preferred();

        std::error_code Ec;
        std::filesystem::create_directories(MeshDir, Ec);
        return MeshDir.wstring();
    }
}

void FEditorFBXSceneViewWidget::Initialize(UEditorEngine* InEditorEngine)
{
    FEditorWidget::Initialize(InEditorEngine);
    WindowTitle = "FBX Scene Viewer";
    bOpen = true;
    EnsurePreviewViewport();
}

void FEditorFBXSceneViewWidget::Initialize(
    UEditorEngine* InEditorEngine,
    int32 InPreviewViewportId,
    const FString& InWindowTitle)
{
    FEditorWidget::Initialize(InEditorEngine);
    PreviewViewportId = InPreviewViewportId;
    WindowTitle = InWindowTitle.empty() ? "FBX Scene Viewer" : InWindowTitle;
    bOpen = true;
    EnsurePreviewViewport();
}

bool FEditorFBXSceneViewWidget::EnsurePreviewViewport()
{
    if (!EditorEngine)
    {
        return false;
    }

    FFBXPreviewViewportLayout& Layout = EditorEngine->GetFBXPreviewViewportLayout();
    if (PreviewViewportId >= 0 && Layout.FindPreview(PreviewViewportId))
    {
        return true;
    }

    FFBXPreviewViewport* Preview = Layout.CreatePreview(WindowTitle);
    if (!Preview)
    {
        PreviewViewportId = -1;
        return false;
    }

    PreviewViewportId = Preview->Id;
    return true;
}

FFBXPreviewViewportClient* FEditorFBXSceneViewWidget::GetPreviewClient()
{
    if (!EnsurePreviewViewport())
    {
        return nullptr;
    }

    FFBXPreviewViewport* Preview = EditorEngine->GetFBXPreviewViewportLayout().FindPreview(PreviewViewportId);
    return Preview ? &Preview->Client : nullptr;
}

UWorld* FEditorFBXSceneViewWidget::ResetPreviewWorld()
{
    if (!EnsurePreviewViewport())
    {
        return nullptr;
    }

    return EditorEngine->GetFBXPreviewViewportLayout().ResetPreviewWorld(PreviewViewportId);
}

bool FEditorFBXSceneViewWidget::OpenFBXFileDialog(FString& OutFilePath) const
{
    OutFilePath.clear();

    WCHAR FileBuffer[MAX_PATH] = { 0 };
    const FWString InitialDir = GetFBXDialogInitialDir();
    const std::filesystem::path PrevCwd = std::filesystem::current_path();

    std::error_code ChdirEc;
    std::filesystem::current_path(std::filesystem::path(InitialDir), ChdirEc);

    OPENFILENAMEW DialogDesc = {};
    DialogDesc.lStructSize = sizeof(DialogDesc);
    DialogDesc.hwndOwner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
    DialogDesc.lpstrFilter = L"FBX/SKMesh Files (*.fbx;*.skmesh)\0*.fbx;*.skmesh\0FBX Files (*.fbx)\0*.fbx\0Skeletal Mesh Binary (*.skmesh)\0*.skmesh\0All Files (*.*)\0*.*\0";
    DialogDesc.lpstrFile = FileBuffer;
    DialogDesc.nMaxFile = MAX_PATH;
    DialogDesc.lpstrInitialDir = InitialDir.c_str();
    DialogDesc.lpstrDefExt = L"fbx";
    DialogDesc.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    const BOOL bPicked = GetOpenFileNameW(&DialogDesc);

    std::error_code RestoreEc;
    std::filesystem::current_path(PrevCwd, RestoreEc);

    if (!bPicked)
    {
        return false;
    }

    OutFilePath = FPaths::ToUtf8(FileBuffer);
    return true;
}

void FEditorFBXSceneViewWidget::LoadFBXScene(const FString& FilePath)
{
    ImportScene = FFBXImportScene{};
    SelectedNodeIndex = -1;
    SelectedSkeletalMeshIndex = -1;
    SelectedBoneIndex = -1;
    PreviewSkinnedMeshComponents.clear();
    if (FFBXPreviewViewportClient* FBXClient = GetPreviewClient())
    {
        FBXClient->ClearBoneSelection();
    }
    LoadedFilePath = FilePath;

    FBinarySerializer BinarySerializer;
    double BinarySceneLoadSec = 0.0;
    if (TryLoadSkeletalMeshSceneFromBinary(
            FilePath,
            BinarySerializer,
            ImportScene,
            bImportSkinnedMeshesAsStatic,
            BinarySceneLoadSec))
    {
        SelectedNodeIndex = 0;
        StatusMessage = FString("SKMesh scene loaded. BinarySec=") + std::to_string(BinarySceneLoadSec);
        UE_LOG(
            "[SkeletalMeshSceneLoad] Source=Binary | Path=%s | BinarySec=%.6f | Nodes=%d | SkeletalMesh=%d",
            FilePath.c_str(),
            BinarySceneLoadSec,
            static_cast<int32>(ImportScene.Nodes.size()),
            static_cast<int32>(ImportScene.SkeletalMeshes.size()));
        if (bImportSkinnedMeshesAsStatic)
        {
            CollapseStaticMeshesToSingleComponent(ImportScene);
        }
        SpawnImportedFBXMeshActors();
        return;
    }

    const std::filesystem::path InputFsPath(FPaths::ToWide(FilePath));
    if (InputFsPath.extension() == L".skmesh")
    {
        StatusMessage = "Failed to load SKMesh binary scene.";
        return;
    }

    FFBXImporter Importer;
    FFBXImportOptions Options = {};
    Options.bImportSkinnedMeshesAsStatic = bImportSkinnedMeshesAsStatic;
    ImportScene = Importer.Import(FilePath, Options);
    if (ImportScene.Nodes.empty())
    {
        StatusMessage = "Failed to import FBX scene.";
        return;
    }

    SelectedNodeIndex = 0;
    if (bImportSkinnedMeshesAsStatic)
    {
        CollapseStaticMeshesToSingleComponent(ImportScene);
    }
    StatusMessage = "FBX import scene loaded.";
    SpawnImportedFBXMeshActors();
}

void FEditorFBXSceneViewWidget::Render(float DeltaTime)
{
    (void)DeltaTime;
    if (!bOpen)
    {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(1100.0f, 650.0f), ImGuiCond_Once);
    if (!ImGui::Begin(WindowTitle.c_str(), &bOpen))
    {
        ImGui::End();
        return;
    }

    // 뷰포트 피킹 결과를 선택 노드에 반영
    if (FFBXPreviewViewportClient* FBXClient = GetPreviewClient())
    {
        const int32 Picked = FBXClient->ConsumePickedNodeIndex();
        if (Picked >= 0)
        {
            SelectedNodeIndex = Picked;
            SelectRootBoneForNode(Picked);
        }
    }

    RenderToolbar();
    ImGui::Separator();

    static float SidePanelWidth = 320.0f;
    constexpr float SplitterW   = 5.0f;
    const float AvailW = ImGui::GetContentRegionAvail().x;
    const float ViewportWidth = std::max(100.0f, AvailW - SidePanelWidth - SplitterW - ImGui::GetStyle().ItemSpacing.x * 2.0f);

    // 왼쪽: 3D 뷰포트
    ImGui::BeginChild("##FBXViewport", ImVec2(ViewportWidth, 0), false);
    RenderViewport();
    ImGui::EndChild();

    ImGui::SameLine();

    // 드래그 스플리터
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.5f, 0.5f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::Button("##VSplitter", ImVec2(SplitterW, -1.0f));
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (ImGui::IsItemActive())
    {
        SidePanelWidth -= ImGui::GetIO().MouseDelta.x;
        SidePanelWidth = std::max(180.0f, std::min(SidePanelWidth, AvailW - 200.0f));
    }

    ImGui::SameLine();

    // 오른쪽: Summary + Tree + Details
    ImGui::BeginChild("##FBXSidePanel", ImVec2(SidePanelWidth, 0), false);

    RenderSummary();
    ImGui::Separator();

    // Scene Node Graph — 축소된 고정 높이 (최대 160px)
    if (ImGui::CollapsingHeader("Scene Node Graph", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const float TreeHeight = std::min(ImGui::GetContentRegionAvail().y * 0.35f, 160.0f);
        ImGui::BeginChild("##FBXTreePanel", ImVec2(0, TreeHeight), true);
        RenderTree();
        ImGui::EndChild();
    }

    ImGui::BeginChild("##FBXDetailsPanel", ImVec2(0, 0), true);
    RenderDetails();
    ImGui::EndChild();

    ImGui::EndChild();

    ImGui::End();
}

void FEditorFBXSceneViewWidget::RenderViewport()
{
    if (!EditorEngine) { return; }
    FFBXPreviewViewportClient* FBXClient = GetPreviewClient();
    if (!FBXClient) { return; }

    const ImVec2 AvailSize = ImGui::GetContentRegionAvail();
    const int32 W = std::max(1, static_cast<int32>(AvailSize.x));
    const int32 H = std::max(1, static_cast<int32>(AvailSize.y));
    const ImVec2 ImageSize(static_cast<float>(W), static_cast<float>(H));

    const ImVec2 ImagePos = ImGui::GetCursorScreenPos();

    const ImVec2 ImageEnd(ImagePos.x + ImageSize.x, ImagePos.y + ImageSize.y);
    const bool bHovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImagePos, ImageEnd);
    FBXClient->SetViewportRect(
        static_cast<int32>(ImagePos.x),
        static_cast<int32>(ImagePos.y),
        W,
        H);
    FBXClient->SetHovered(bHovered);

    //땜빵코드 이후에 제거 필요 Week10
    if (bHovered || FBXClient->IsInputCaptured())
    {
        FGuiInputState& GuiState = InputSystem::Get().GetGuiInputState();
        GuiState.bViewportInputBlocked = true;
        GuiState.bViewportInputBlockCapturesMouse = FBXClient->IsInputCaptured();
        GuiState.ViewportInputBlockRect = FBXClient->GetViewportRect();
    }

    // 렌더 파이프라인에서 이번 프레임에 렌더된 SRV를 가져와 표시
    const FEditorRenderPipeline* Pipeline = EditorEngine->GetEditorRenderPipeline();
    ID3D11ShaderResourceView* SRV = Pipeline ? Pipeline->GetFBXPreviewSRV(PreviewViewportId) : nullptr;

    if (SRV)
    {
        ID3D11DeviceContext* DeviceContext = EditorEngine->GetRenderer().GetFD3DDevice().GetDeviceContext();
        ImDrawList* DrawList = ImGui::GetWindowDrawList();

        DrawList->AddCallback(SetOpaqueBlendStateCallback, DeviceContext);
        ImGui::Image(reinterpret_cast<ImTextureID>(SRV), ImageSize);
        DrawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

        if (!ImportScene.Nodes.empty())
        {
            int32 TotalVertices = 0;
            int32 TotalTriangles = 0;
            for (const FFBXStaticMeshImportData& Mesh : ImportScene.StaticMeshes)
            {
                TotalVertices  += static_cast<int32>(Mesh.Vertices.size());
                TotalTriangles += static_cast<int32>(Mesh.Indices.size()) / 3;
            }
            for (const FFBXSkeletalMeshImportData& Mesh : ImportScene.SkeletalMeshes)
            {
                TotalVertices  += static_cast<int32>(Mesh.Vertices.size());
                TotalTriangles += static_cast<int32>(Mesh.Indices.size()) / 3;
            }

            char VertText[64];
            char TriText[64];
            snprintf(VertText, sizeof(VertText), "Vertex : %d", TotalVertices);
            snprintf(TriText,  sizeof(TriText),  "Triangle  : %d", TotalTriangles);

            const float     Pad  = 8.0f;
            const float     LineH = ImGui::GetTextLineHeight();
            const ImVec2    Base(ImagePos.x + Pad, ImagePos.y + Pad);

            // 그림자
            DrawList->AddText(ImVec2(Base.x + 1, Base.y + 1),            IM_COL32(0, 0, 0, 200), VertText);
            DrawList->AddText(ImVec2(Base.x + 1, Base.y + LineH + 3 + 1), IM_COL32(0, 0, 0, 200), TriText);
            // 본문
            DrawList->AddText(Base,                                        IM_COL32(255, 255, 255, 255), VertText);
            DrawList->AddText(ImVec2(Base.x, Base.y + LineH + 3),         IM_COL32(255, 255, 255, 255), TriText);
        }
    }
    else
    {
        // SRV가 아직 없으면 안내 텍스트 표시
        const ImVec2 TextSize = ImGui::CalcTextSize("Open an FBX file to preview.");
        ImGui::SetCursorPos(ImVec2(
            (AvailSize.x - TextSize.x) * 0.5f,
            (AvailSize.y - TextSize.y) * 0.5f));
        ImGui::TextDisabled("Open an FBX file to preview.");
    }
}

void FEditorFBXSceneViewWidget::RenderToolbar()
{
    // --- 액션 버튼 ---
    if (ImGui::Button("Open FBX"))
    {
        FString PickedPath;
        if (OpenFBXFileDialog(PickedPath))
        {
            LoadFBXScene(PickedPath);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear"))
    {
        ImportScene = FFBXImportScene{};
        LoadedFilePath.clear();
        SelectedNodeIndex = -1;
        SelectedSkeletalMeshIndex = -1;
        SelectedBoneIndex = -1;
        PreviewSkinnedMeshComponents.clear();
        if (FFBXPreviewViewportClient* FBXClient = GetPreviewClient())
        {
            FBXClient->ClearBoneSelection();
            FBXClient->ClearPickableComponents();
            ResetPreviewWorld();
        }
        StatusMessage = "No FBX loaded.";
    }

    // --- Show Flags ---
    ImGui::Separator();

    if (FFBXPreviewViewportClient* FBXClient = GetPreviewClient())
    {
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::BeginCombo("##FBXShowFlags", "Show Flags"))
        {
            bool bGrid = FBXClient->GetShowGrid();
            bool bAxis = FBXClient->GetShowAxis();
            if (ImGui::Checkbox("Grid", &bGrid)) { FBXClient->SetShowGrid(bGrid); }
            if (ImGui::Checkbox("Axis", &bAxis)) { FBXClient->SetShowAxis(bAxis); }
            ImGui::EndCombo();
        }
        ImGui::SameLine();

        if (UGizmoComponent* PreviewGizmo = FBXClient->GetPreviewGizmo())
        {
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            int32 GizmoMode = static_cast<int32>(PreviewGizmo->GetGizmoMode());
            if (ImGui::RadioButton("Translate", &GizmoMode, static_cast<int32>(EGizmoMode::Translate)))
            {
                FBXClient->SetPreviewGizmoMode(EGizmoMode::Translate);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Rotate", &GizmoMode, static_cast<int32>(EGizmoMode::Rotate)))
            {
                FBXClient->SetPreviewGizmoMode(EGizmoMode::Rotate);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Scale", &GizmoMode, static_cast<int32>(EGizmoMode::Scale)))
            {
                FBXClient->SetPreviewGizmoMode(EGizmoMode::Scale);
            }
            ImGui::SameLine();
        }

        // --- View Mode ---
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::TextDisabled("View Mode");
        ImGui::SameLine();

        static constexpr EViewMode ViewModes[] = {
            EViewMode::Lit,
            EViewMode::Unlit,
            EViewMode::Wireframe,
            EViewMode::SceneDepth,
            EViewMode::WorldNormal,
        };
        static constexpr const char* ViewModeLabels[] = {
            "Lit",
            "Unlit",
            "Wireframe",
            "Scene Depth",
            "World Normal",
        };

        EViewMode CurrentMode = FBXClient->GetViewMode();
        const char* CurrentLabel = "Lit";
        for (int32 i = 0; i < static_cast<int32>(IM_ARRAYSIZE(ViewModes)); ++i)
        {
            if (ViewModes[i] == CurrentMode) { CurrentLabel = ViewModeLabels[i]; break; }
        }

        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::BeginCombo("##FBXViewMode", CurrentLabel))
        {
            for (int32 i = 0; i < static_cast<int32>(IM_ARRAYSIZE(ViewModes)); ++i)
            {
                const bool bSel = (ViewModes[i] == CurrentMode);
                if (ImGui::Selectable(ViewModeLabels[i], bSel))
                    FBXClient->SetViewMode(ViewModes[i]);
                if (bSel)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    }

    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextDisabled("Import");
    ImGui::SameLine();
    if (ImGui::Checkbox("Skinned -> Static", &bImportSkinnedMeshesAsStatic) && !LoadedFilePath.empty())
    {
        LoadFBXScene(LoadedFilePath);
    }
}

void FEditorFBXSceneViewWidget::RenderSummary() const
{
    ImGui::TextDisabled("%s", StatusMessage.c_str());

    if (!LoadedFilePath.empty())
    {
        ImGui::TextWrapped("File: %s", LoadedFilePath.c_str());
    }

    ImGui::Text("Nodes: %d  StaticMesh: %d  SkeletalMesh: %d",
                static_cast<int32>(ImportScene.Nodes.size()),
                static_cast<int32>(ImportScene.StaticMeshes.size()),
                static_cast<int32>(ImportScene.SkeletalMeshes.size()));
}

void FEditorFBXSceneViewWidget::RenderTree()
{
    if (ImportScene.Nodes.empty())
    {
        ImGui::TextDisabled("Open an FBX file to inspect imported meshes.");
        return;
    }

    RenderNodeRecursive(0);
}

void FEditorFBXSceneViewWidget::RenderNodeRecursive(int32 NodeIndex)
{
    if (NodeIndex < 0 || NodeIndex >= static_cast<int32>(ImportScene.Nodes.size()))
    {
        return;
    }

    const FFBXImportNode& Node = ImportScene.Nodes[NodeIndex];
    ImGui::PushID(NodeIndex);

    ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (Node.Children.empty())
    {
        Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (SelectedNodeIndex == NodeIndex)
    {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, GetImportNodeTypeColor(Node));
    const bool bOpen = ImGui::TreeNodeEx("##Node", Flags, "[%s] %s", GetImportNodeTypeName(Node), Node.Name.c_str());
    ImGui::PopStyleColor();

    if (ImGui::IsItemClicked())
    {
        SelectedNodeIndex = NodeIndex;
    }

    if (!Node.Children.empty() && bOpen)
    {
        for (int32 ChildIndex : Node.Children)
        {
            RenderNodeRecursive(ChildIndex);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void FEditorFBXSceneViewWidget::RenderBoneTreeRecursive(
    const FFBXSkeletalMeshImportData& Mesh,
    int32 BoneIndex,
    int32 SkeletalMeshIndex,
    USkinnedMeshComponent* SkinnedMeshComponent)
{
    if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Mesh.Bones.size()))
    {
        return;
    }

    const FBoneInfo& Bone = Mesh.Bones[BoneIndex];

    // 이 본의 자식이 있는지 확인
    bool bHasChildren = false;
    for (const FBoneInfo& OtherBone : Mesh.Bones)
    {
        if (OtherBone.ParentIndex == BoneIndex)
        {
            bHasChildren = true;
            break;
        }
    }

    ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!bHasChildren)
    {
        Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (SelectedSkeletalMeshIndex == SkeletalMeshIndex && SelectedBoneIndex == BoneIndex)
    {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }

    ImGui::PushID(BoneIndex);
    const bool bOpen = ImGui::TreeNodeEx("##Bone", Flags, "%s", Bone.Name.c_str());

    if (ImGui::IsItemClicked())
    {
        SelectedSkeletalMeshIndex = SkeletalMeshIndex;
        SelectedBoneIndex = BoneIndex;
        if (SkinnedMeshComponent)
        {
            if (FFBXPreviewViewportClient* FBXClient = GetPreviewClient())
            {
                FBXClient->SelectBone(SkinnedMeshComponent, BoneIndex);
            }
        }
    }

    if (bHasChildren && bOpen)
    {
        for (int32 i = 0; i < static_cast<int32>(Mesh.Bones.size()); ++i)
        {
            if (Mesh.Bones[i].ParentIndex == BoneIndex)
            {
                RenderBoneTreeRecursive(Mesh, i, SkeletalMeshIndex, SkinnedMeshComponent);
            }
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void FEditorFBXSceneViewWidget::RenderDetails()
{
    if (SelectedNodeIndex < 0 || SelectedNodeIndex >= static_cast<int32>(ImportScene.Nodes.size()))
    {
        ImGui::TextDisabled("Select a node.");
        return;
    }

    const FFBXImportNode& Node = ImportScene.Nodes[SelectedNodeIndex];

    if (Node.StaticMeshIndex >= 0 && Node.StaticMeshIndex < static_cast<int32>(ImportScene.StaticMeshes.size()))
    {
        const FFBXStaticMeshImportData& Mesh = ImportScene.StaticMeshes[Node.StaticMeshIndex];
        ImGui::Text("Static Mesh: %s", Mesh.Name.c_str());
        ImGui::Text("Vertices: %d  Indices: %d", static_cast<int32>(Mesh.Vertices.size()), static_cast<int32>(Mesh.Indices.size()));
        ImGui::Spacing();

        if (ImGui::TreeNode("Sections"))
        {
            for (int32 i = 0; i < static_cast<int32>(Mesh.Sections.size()); ++i)
            {
                const FStaticMeshSection& S = Mesh.Sections[i];
                ImGui::Text("#%d  Start=%u  Count=%u  Slot=%d", i, S.StartIndex, S.IndexCount, S.MaterialSlotIndex);
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Material Slots"))
        {
            for (int32 i = 0; i < static_cast<int32>(Mesh.MaterialSlots.size()); ++i)
            {
                ImGui::Text("#%d  %s", i, Mesh.MaterialSlots[i].SlotName.c_str());
            }
            ImGui::TreePop();
        }
    }

    if (Node.SkeletalMeshIndex >= 0 && Node.SkeletalMeshIndex < static_cast<int32>(ImportScene.SkeletalMeshes.size()))
    {
        const FFBXSkeletalMeshImportData& Mesh = ImportScene.SkeletalMeshes[Node.SkeletalMeshIndex];
        ImGui::Text("Skeletal Mesh: %s", Mesh.Name.c_str());
        ImGui::Text("Bones: %d", static_cast<int32>(Mesh.Bones.size()));
        ImGui::Spacing();

        USkinnedMeshComponent* SkinnedMeshComponent = nullptr;
        if (Node.SkeletalMeshIndex < static_cast<int32>(PreviewSkinnedMeshComponents.size()))
        {
            SkinnedMeshComponent = PreviewSkinnedMeshComponents[Node.SkeletalMeshIndex];
        }

        if (ImGui::TreeNode("Bones"))
        {
            for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Mesh.Bones.size()); ++BoneIndex)
            {
                if (Mesh.Bones[BoneIndex].ParentIndex == -1)
                {
                    RenderBoneTreeRecursive(Mesh, BoneIndex, Node.SkeletalMeshIndex, SkinnedMeshComponent);
                }
            }
            ImGui::TreePop();
        }
    }
}

void FEditorFBXSceneViewWidget::SelectRootBoneForNode(int32 NodeIndex)
{
    if (!EditorEngine || NodeIndex < 0 || NodeIndex >= static_cast<int32>(ImportScene.Nodes.size()))
    {
        return;
    }

    const FFBXImportNode& Node = ImportScene.Nodes[NodeIndex];
    if (Node.SkeletalMeshIndex < 0 ||
        Node.SkeletalMeshIndex >= static_cast<int32>(ImportScene.SkeletalMeshes.size()) ||
        Node.SkeletalMeshIndex >= static_cast<int32>(PreviewSkinnedMeshComponents.size()))
    {
        SelectedSkeletalMeshIndex = -1;
        SelectedBoneIndex = -1;

        // Static mesh 노드인 경우 기즈모를 해당 컴포넌트에 표시
        if (Node.StaticMeshIndex >= 0)
        {
            if (FFBXPreviewViewportClient* FBXClient = GetPreviewClient())
            {
                UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(FBXClient->GetSelectedComponent());
                if (StaticMeshComp)
                {
                    FBXClient->SelectStaticMesh(StaticMeshComp);
                    return;
                }
            }
        }

        if (FFBXPreviewViewportClient* FBXClient = GetPreviewClient())
        {
            FBXClient->ClearBoneSelection();
        }
        return;
    }

    const FFBXSkeletalMeshImportData& Mesh = ImportScene.SkeletalMeshes[Node.SkeletalMeshIndex];
    int32 RootBoneIndex = -1;
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Mesh.Bones.size()); ++BoneIndex)
    {
        if (Mesh.Bones[BoneIndex].ParentIndex == -1)
        {
            RootBoneIndex = BoneIndex;
            break;
        }
    }

    USkinnedMeshComponent* SkinnedMeshComponent = PreviewSkinnedMeshComponents[Node.SkeletalMeshIndex];
    if (RootBoneIndex < 0 || SkinnedMeshComponent == nullptr)
    {
        SelectedSkeletalMeshIndex = -1;
        SelectedBoneIndex = -1;
        if (FFBXPreviewViewportClient* FBXClient = GetPreviewClient())
        {
            FBXClient->ClearBoneSelection();
        }
        return;
    }

    SelectedSkeletalMeshIndex = Node.SkeletalMeshIndex;
    SelectedBoneIndex = RootBoneIndex;
    if (FFBXPreviewViewportClient* FBXClient = GetPreviewClient())
    {
        FBXClient->SelectBone(SkinnedMeshComponent, RootBoneIndex);
    }
}

void FEditorFBXSceneViewWidget::SpawnImportedFBXMeshActors()
{
    // StaticMesh와 SkeletalMesh 데이터가 모두 없으면 생성할 것이 없으므로 조기 반환
    if (ImportScene.StaticMeshes.empty() && ImportScene.SkeletalMeshes.empty())
    {
        StatusMessage = "스폰할 메시 데이터가 없습니다.";
        return;
    }

    UWorld* World = ResetPreviewWorld();
    if (World == nullptr)
    {
        StatusMessage = "FBX Preview World가 null입니다. FBX Actor 스폰 실패.";
        return;
    }
    FFBXPreviewViewportClient* FBXClient = GetPreviewClient();
    if (!FBXClient)
    {
        StatusMessage = "FBX Preview Viewport가 null입니다. FBX Actor 스폰 실패.";
        return;
    }
    FBXClient->ClearBoneSelection();
    FBXClient->ClearPickableComponents();
    SelectedNodeIndex = -1;
    SelectedSkeletalMeshIndex = -1;
    SelectedBoneIndex = -1;
    PreviewSkinnedMeshComponents.clear();
    PreviewSkinnedMeshComponents.resize(ImportScene.SkeletalMeshes.size(), nullptr);

    FFBXImporter Importer;
    FBinarySerializer BinarySerializer;
    FSkeletalMeshBinaryCacheStats SkeletalMeshCacheStats;
    int32 StaticSpawnedCount  = 0;
    int32 SkeletalSpawnedCount = 0;

    // FBX 씬 전체를 하나의 Actor로 묶는다.
    // 각 메시 컴포넌트는 이 Actor의 RootComponent에 붙는다.
    AActor* Actor = World->SpawnActor<AActor>();
    Actor->SetFName(FName(BuildImportedActorName(LoadedFilePath).c_str()));

    USceneComponent* RootComponent = Actor->AddComponent<USceneComponent>();
    Actor->SetRootComponent(RootComponent);

    TArray<USkinnedMeshComponent*> SpawnedSkinnedMeshComponents;

    // Skeletal Mesh 처리
    // Static mesh가 이후 bone attachment 대상을 찾을 수 있도록 skinned component를 먼저 생성한다.
    for (int32 SkeletalMeshIndex = 0; SkeletalMeshIndex < static_cast<int32>(ImportScene.SkeletalMeshes.size()); ++SkeletalMeshIndex)
    {
        const FFBXSkeletalMeshImportData& MeshData = ImportScene.SkeletalMeshes[SkeletalMeshIndex];
        // 버텍스나 인덱스 데이터가 없는 노드는 건너뜀
        if (MeshData.Vertices.empty() || MeshData.Indices.empty())
        {
            continue;
        }

        FSkeletalMesh* RawMesh = LoadOrCreateCachedSkeletalMesh(
            Importer,
            ImportScene,
            MeshData,
            LoadedFilePath,
            SkeletalMeshIndex,
            BinarySerializer,
            SkeletalMeshCacheStats);
        if (RawMesh == nullptr)
        {
            continue;
        }

        // USkeletalMesh 에셋 오브젝트 생성 후 가공된 메시 데이터를 등록
        USkeletalMesh* SkeletalMeshAsset = UObjectManager::Get().CreateObject<USkeletalMesh>();
        SkeletalMeshAsset->SetMeshData(RawMesh);

        USkinnedMeshComponent* MeshComponent = Actor->AddComponent<USkinnedMeshComponent>();
        MeshComponent->AttachToComponent(RootComponent);
        MeshComponent->SetSkeletalMesh(SkeletalMeshAsset);

        // BindPose 버텍스 데이터를 CPU 측에 준비한다.
        // GPU 버퍼(VB/IB)는 첫 드로우 시 OpaqueRenderPass에서 Lazy Init된다.
        MeshComponent->InitializeSkinnedVerticesFromBindPose();

        // FBX 노드의 GlobalTransform을 컴포넌트 월드 트랜스폼에 반영
        if (MeshData.SourceNodeIndex >= 0 && MeshData.SourceNodeIndex < static_cast<int32>(ImportScene.Nodes.size()))
        {
            ApplyImportNodeTransform(MeshComponent, ImportScene.Nodes[MeshData.SourceNodeIndex]);
        }

        if (MeshData.SourceNodeIndex >= 0)
        {
            FBXClient->RegisterPickableComponent(MeshComponent, MeshData.SourceNodeIndex);
        }

        ++SkeletalSpawnedCount;
        SpawnedSkinnedMeshComponents.push_back(MeshComponent);
        PreviewSkinnedMeshComponents[SkeletalMeshIndex] = MeshComponent;
    }

    // Static Mesh 처리
    for (const FFBXStaticMeshImportData& MeshData : ImportScene.StaticMeshes)
    {
        // 버텍스나 인덱스 데이터가 없는 노드는 건너뜀
        if (MeshData.Vertices.empty() || MeshData.Indices.empty())
        {
            continue;
        }

        FStaticMesh* RawMesh = Importer.CreateStaticMeshFromImportData(MeshData);
        if (RawMesh == nullptr)
        {
            continue;
        }

        // UStaticMesh 에셋 오브젝트 생성 후 가공된 메시 데이터를 등록
        UStaticMesh* StaticMeshAsset = UObjectManager::Get().CreateObject<UStaticMesh>();
        StaticMeshAsset->SetMeshData(RawMesh);

        UStaticMeshComponent* MeshComponent = Actor->AddComponent<UStaticMeshComponent>();
        MeshComponent->AttachToComponent(RootComponent);
        MeshComponent->SetStaticMesh(StaticMeshAsset);

        // 정점은 노드 로컬 공간(GeometricTransform만 적용)이므로
        // GlobalTransformMatrix를 컴포넌트 월드 트랜스폼에 반영해야 씬 원점 기준 올바른 위치에 배치된다.
        if (MeshData.SourceNodeIndex >= 0 && MeshData.SourceNodeIndex < static_cast<int32>(ImportScene.Nodes.size()))
        {
            ApplyImportNodeTransform(MeshComponent, ImportScene.Nodes[MeshData.SourceNodeIndex]);
            TryAttachStaticMeshToBone(
                MeshComponent,
                MeshData.SourceNodeIndex,
                ImportScene,
                SpawnedSkinnedMeshComponents);
            FBXClient->RegisterPickableComponent(MeshComponent, MeshData.SourceNodeIndex);
        }

        ++StaticSpawnedCount;
    }

    // 공간 쿼리 인덱스 갱신 (새 컴포넌트의 AABB가 반영되어야 컬링/쿼리가 정상 동작)
    World->RebuildSpatialIndex();
    if (!World->HasBegunPlay())
    {
        World->BeginPlay();
    }

    const int32 TotalSpawned = StaticSpawnedCount + SkeletalSpawnedCount;
    if (TotalSpawned > 0)
    {
        StatusMessage = FString("FBX Actor 스폰 완료 — Static: ") + std::to_string(StaticSpawnedCount)
            + ", Skeletal: " + std::to_string(SkeletalSpawnedCount)
            + ", SKMesh Load: " + std::to_string(SkeletalMeshCacheStats.LoadedCount)
            + ", Save: " + std::to_string(SkeletalMeshCacheStats.SavedCount)
            + ", Fail: " + std::to_string(SkeletalMeshCacheStats.FailedCount) + ".";
    }
    else
    {
        StatusMessage = "유효한 메시 데이터가 없어 스폰하지 않았습니다.";
    }
}
