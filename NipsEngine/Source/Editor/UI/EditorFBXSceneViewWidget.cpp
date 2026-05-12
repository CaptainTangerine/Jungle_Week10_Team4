#include "Editor/UI/EditorFBXSceneViewWidget.h"

#include "Asset/BinarySerializer.h"
#include "Asset/SkeletalMesh.h"
#include "Component/SceneComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Core/Paths.h"
#include "Core/Logging/Log.h"
#include "Core/ResourceManager.h"
#include "Editor/EditorEngine.h"
#include "Editor/EditorRenderPipeline.h"
#include "Editor/Viewport/FBXPreviewViewportClient.h"
#include "Engine/Slate/SlateUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/World.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_impl_dx11.h"
#include "Object/Object.h"
#include "Render/Renderer/Renderer.h"

#include <commdlg.h>
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

    struct FSkeletalMeshBinaryCacheStats
    {
        int32 LoadedCount = 0;
        int32 SavedCount = 0;
        int32 FailedCount = 0;
    };

    FSkeletalMesh* LoadOrCreateCachedSkeletalMesh(
        FFBXImporter& Importer,
        const FFBXSkeletalMeshImportData& MeshData,
        const FString& SourcePath,
        int32 MeshIndex,
        FBinarySerializer& BinarySerializer,
        FSkeletalMeshBinaryCacheStats& CacheStats)
    {
        const FString BinaryPath = MakeSkeletalMeshBinaryPath(SourcePath, MeshIndex);

        if (IsSkeletalMeshBinaryValid(SourcePath, BinaryPath, BinarySerializer))
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
    DialogDesc.lpstrFilter = L"FBX Files (*.fbx)\0*.fbx\0All Files (*.*)\0*.*\0";
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
    if (EditorEngine)
    {
        EditorEngine->GetFBXPreviewViewportClient().ClearBoneSelection();
    }
    LoadedFilePath = FilePath;

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
    StatusMessage = "FBX import scene loaded.";
    SpawnImportedFBXMeshActors();
}

void FEditorFBXSceneViewWidget::Render(float DeltaTime)
{
    (void)DeltaTime;

    ImGui::SetNextWindowSize(ImVec2(1100.0f, 650.0f), ImGuiCond_Once);
    if (!ImGui::Begin("FBX Scene Viewer"))
    {
        ImGui::End();
        return;
    }

    RenderToolbar();
    ImGui::Separator();

    const float SidePanelWidth = 320.0f;
    const float ViewportWidth  = ImGui::GetContentRegionAvail().x - SidePanelWidth - ImGui::GetStyle().ItemSpacing.x;

    // 왼쪽: 3D 뷰포트
    ImGui::BeginChild("##FBXViewport", ImVec2(ViewportWidth > 100.0f ? ViewportWidth : 100.0f, 0), false);
    RenderViewport();
    ImGui::EndChild();

    ImGui::SameLine();

    // 오른쪽: Summary + Tree + Details
    ImGui::BeginChild("##FBXSidePanel", ImVec2(0, 0), false);

    RenderSummary();
    ImGui::Separator();

    const float HalfHeight = ImGui::GetContentRegionAvail().y * 0.5f;
    ImGui::BeginChild("##FBXTreePanel", ImVec2(0, HalfHeight), true);
    RenderTree();
    ImGui::EndChild();

    ImGui::Separator();

    ImGui::BeginChild("##FBXDetailsPanel", ImVec2(0, 0), true);
    RenderDetails();
    ImGui::EndChild();

    ImGui::EndChild();

    ImGui::End();
}

void FEditorFBXSceneViewWidget::RenderViewport()
{
    if (!EditorEngine) { return; }

    // 이 Child 창의 실제 픽셀 크기를 다음 프레임 렌더 해상도로 예약
    const ImVec2 AvailSize = ImGui::GetContentRegionAvail();
    const int32 W = static_cast<int32>(AvailSize.x);
    const int32 H = static_cast<int32>(AvailSize.y);

    FFBXPreviewViewportClient& FBXClient = EditorEngine->GetFBXPreviewViewportClient();
    const ImVec2 ImagePos = ImGui::GetCursorScreenPos();
    POINT ViewportPos =
    {
        static_cast<LONG>(ImagePos.x),
        static_cast<LONG>(ImagePos.y)
    };
    if (FBXClient.GetWindow())
    {
        ViewportPos = FBXClient.GetWindow()->ScreenToClientPoint(ViewportPos);
    }

    const ImVec2 ImageEnd(ImagePos.x + AvailSize.x, ImagePos.y + AvailSize.y);
    const bool bHovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImagePos, ImageEnd);
    FBXClient.SetViewportRect(ViewportPos.x, ViewportPos.y, W, H);
    FBXClient.SetHovered(bHovered);

    // 렌더 파이프라인에서 이번 프레임에 렌더된 SRV를 가져와 표시
    const FEditorRenderPipeline* Pipeline = EditorEngine->GetEditorRenderPipeline();
    ID3D11ShaderResourceView* SRV = Pipeline ? Pipeline->GetFBXPreviewSRV() : nullptr;

    if (SRV)
    {
        ID3D11DeviceContext* DeviceContext = EditorEngine->GetRenderer().GetFD3DDevice().GetDeviceContext();
        ImDrawList* DrawList = ImGui::GetWindowDrawList();

        DrawList->AddCallback(SetOpaqueBlendStateCallback, DeviceContext);
        ImGui::Image(reinterpret_cast<ImTextureID>(SRV), AvailSize);
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
            snprintf(VertText, sizeof(VertText), "버텍스 : %d", TotalVertices);
            snprintf(TriText,  sizeof(TriText),  "트라이앵글  : %d", TotalTriangles);

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
        if (EditorEngine)
        {
            EditorEngine->GetFBXPreviewViewportClient().ClearBoneSelection();
            EditorEngine->ResetFBXPreviewWorld();
        }
        StatusMessage = "No FBX loaded.";
    }

    // --- Show Flags ---
    ImGui::Separator();
    ImGui::TextDisabled("Show Flags");
    ImGui::SameLine();

    if (EditorEngine)
    {
        FFBXPreviewViewportClient& FBXClient = EditorEngine->GetFBXPreviewViewportClient();

        bool bGrid = FBXClient.GetShowGrid();
        bool bAxis = FBXClient.GetShowAxis();
        if (ImGui::Checkbox("Grid", &bGrid))   { FBXClient.SetShowGrid(bGrid); }
        ImGui::SameLine();
        if (ImGui::Checkbox("Axis", &bAxis))   { FBXClient.SetShowAxis(bAxis); }
        ImGui::SameLine();
    }

    ImGui::SameLine();
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

void FEditorFBXSceneViewWidget::RenderDetails()
{
    ImGui::Text("Node Details");
    ImGui::Separator();

    if (SelectedNodeIndex < 0 || SelectedNodeIndex >= static_cast<int32>(ImportScene.Nodes.size()))
    {
        ImGui::TextDisabled("Select a node.");
        return;
    }

    const FFBXImportNode& Node = ImportScene.Nodes[SelectedNodeIndex];

    ImGui::Text("Index: %d", SelectedNodeIndex);
    ImGui::Text("Name: %s", Node.Name.c_str());
    ImGui::TextColored(GetImportNodeTypeColor(Node), "Type: %s", GetImportNodeTypeName(Node));
    ImGui::Text("Parent: %d", Node.ParentIndex);
    ImGui::Text("Children: %d", static_cast<int32>(Node.Children.size()));
    ImGui::Text("StaticMeshIndex: %d", Node.StaticMeshIndex);
    ImGui::Text("SkeletalMeshIndex: %d", Node.SkeletalMeshIndex);

    if (Node.StaticMeshIndex >= 0 && Node.StaticMeshIndex < static_cast<int32>(ImportScene.StaticMeshes.size()))
    {
        const FFBXStaticMeshImportData& Mesh = ImportScene.StaticMeshes[Node.StaticMeshIndex];
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Static Mesh");
        ImGui::Text("Name: %s", Mesh.Name.c_str());
        ImGui::Text("Vertices: %d", static_cast<int32>(Mesh.Vertices.size()));
        ImGui::Text("Indices: %d", static_cast<int32>(Mesh.Indices.size()));
        ImGui::Text("Sections: %d", static_cast<int32>(Mesh.Sections.size()));
        ImGui::Text("Material Slots: %d", static_cast<int32>(Mesh.MaterialSlots.size()));

        if (ImGui::TreeNode("Sections"))
        {
            for (int32 SectionIndex = 0; SectionIndex < static_cast<int32>(Mesh.Sections.size()); ++SectionIndex)
            {
                const FStaticMeshSection& Section = Mesh.Sections[SectionIndex];
                ImGui::Text("#%d Start=%u Count=%u Slot=%d",
                            SectionIndex, Section.StartIndex, Section.IndexCount, Section.MaterialSlotIndex);
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Material Slots"))
        {
            for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(Mesh.MaterialSlots.size()); ++SlotIndex)
            {
                const FStaticMeshMaterialSlot& Slot = Mesh.MaterialSlots[SlotIndex];
                ImGui::Text("#%d %s", SlotIndex, Slot.SlotName.c_str());
            }
            ImGui::TreePop();
        }
    }

    if (Node.SkeletalMeshIndex >= 0 && Node.SkeletalMeshIndex < static_cast<int32>(ImportScene.SkeletalMeshes.size()))
    {
        const FFBXSkeletalMeshImportData& Mesh = ImportScene.SkeletalMeshes[Node.SkeletalMeshIndex];
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Skeletal Mesh");
        ImGui::Text("Name: %s", Mesh.Name.c_str());

        if (ImGui::TreeNode("Bones"))
        {
            USkinnedMeshComponent* SkinnedMeshComponent = nullptr;
            if (Node.SkeletalMeshIndex < static_cast<int32>(PreviewSkinnedMeshComponents.size()))
            {
                SkinnedMeshComponent = PreviewSkinnedMeshComponents[Node.SkeletalMeshIndex];
            }

            for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Mesh.Bones.size()); ++BoneIndex)
            {
                const FBoneInfo& Bone = Mesh.Bones[BoneIndex];
                ImGui::PushID(BoneIndex);

                const bool bSelected =
                    SelectedSkeletalMeshIndex == Node.SkeletalMeshIndex &&
                    SelectedBoneIndex == BoneIndex;

                if (ImGui::Selectable(Bone.Name.c_str(), bSelected))
                {
                    SelectedSkeletalMeshIndex = Node.SkeletalMeshIndex;
                    SelectedBoneIndex = BoneIndex;

                    if (EditorEngine && SkinnedMeshComponent)
                    {
                        EditorEngine->GetFBXPreviewViewportClient().SelectBone(SkinnedMeshComponent, BoneIndex);
                    }
                }

                ImGui::PopID();
            }

            ImGui::TreePop();
        }
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

    UWorld* World = EditorEngine ? EditorEngine->ResetFBXPreviewWorld() : nullptr;
    if (World == nullptr)
    {
        StatusMessage = "FBX Preview World가 null입니다. FBX Actor 스폰 실패.";
        return;
    }
    EditorEngine->GetFBXPreviewViewportClient().ClearBoneSelection();
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
