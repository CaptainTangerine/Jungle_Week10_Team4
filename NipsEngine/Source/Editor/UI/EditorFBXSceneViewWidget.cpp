#include "Editor/UI/EditorFBXSceneViewWidget.h"

#include "Asset/FBX/FBXSDKContext.h"
#include "Asset/FBX/FBXSceneTreeBuilder.h"
#include "Core/Paths.h"
#include "ImGui/imgui.h"

#include <commdlg.h>
#include <filesystem>

namespace
{
    const char* GetNodeTypeName(EFBXSceneNodeType Type)
    {
        switch (Type)
        {
        case EFBXSceneNodeType::Root:
            return "Root";
        case EFBXSceneNodeType::Null:
            return "Null";
        case EFBXSceneNodeType::Mesh:
            return "Mesh";
        case EFBXSceneNodeType::Skeleton:
            return "Skeleton";
        case EFBXSceneNodeType::Camera:
            return "Camera";
        case EFBXSceneNodeType::Light:
            return "Light";
        default:
            return "Unknown";
        }
    }

    ImVec4 GetNodeTypeColor(EFBXSceneNodeType Type)
    {
        switch (Type)
        {
        case EFBXSceneNodeType::Root:
            return ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
        case EFBXSceneNodeType::Mesh:
            return ImVec4(0.35f, 0.75f, 1.0f, 1.0f);
        case EFBXSceneNodeType::Skeleton:
            return ImVec4(1.0f, 0.75f, 0.35f, 1.0f);
        case EFBXSceneNodeType::Camera:
            return ImVec4(0.55f, 0.95f, 0.55f, 1.0f);
        case EFBXSceneNodeType::Light:
            return ImVec4(1.0f, 0.95f, 0.4f, 1.0f);
        case EFBXSceneNodeType::Null:
            return ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
        default:
            return ImVec4(0.9f, 0.45f, 0.45f, 1.0f);
        }
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
    Snapshot.Clear();
    SelectedNodeIndex = -1;
    LoadedFilePath = FilePath;

    FFBXSDKContext Context;
    if (!Context.Initialize())
    {
        StatusMessage = "Failed to initialize FBX SDK.";
        return;
    }

    FbxScene* Scene = Context.LoadScene(FilePath);
    if (!Scene)
    {
        StatusMessage = "Failed to load FBX scene.";
        return;
    }

    const bool bBuilt = FFBXSceneTreeBuilder::Build(Scene, Snapshot);
    Scene->Destroy();

    if (!bBuilt)
    {
        StatusMessage = "Failed to build FBX scene tree.";
        return;
    }

    SelectedNodeIndex = Snapshot.RootIndex;
    StatusMessage = "FBX scene loaded.";
}

void FEditorFBXSceneViewWidget::Render(float DeltaTime)
{
    (void)DeltaTime;

    ImGui::SetNextWindowSize(ImVec2(760.0f, 520.0f), ImGuiCond_Once);
    if (!ImGui::Begin("FBX Scene Viewer"))
    {
        ImGui::End();
        return;
    }

    RenderToolbar();
    ImGui::Separator();
    RenderSummary();
    ImGui::Separator();

    const float DetailsWidth = 300.0f;
    const float TreeWidth = ImGui::GetContentRegionAvail().x - DetailsWidth - ImGui::GetStyle().ItemSpacing.x;
    ImGui::BeginChild("##FBXTreePanel", ImVec2(TreeWidth > 240.0f ? TreeWidth : 240.0f, 0), true);
    RenderTree();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##FBXDetailsPanel", ImVec2(0, 0), true);
    RenderDetails();
    ImGui::EndChild();

    ImGui::End();
}

void FEditorFBXSceneViewWidget::RenderToolbar()
{
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
        Snapshot.Clear();
        LoadedFilePath.clear();
        SelectedNodeIndex = -1;
        StatusMessage = "No FBX loaded.";
    }

    ImGui::SameLine();
    ImGui::Checkbox("Auto Expand", &bAutoExpand);
}

void FEditorFBXSceneViewWidget::RenderSummary() const
{
    ImGui::TextDisabled("%s", StatusMessage.c_str());

    if (!LoadedFilePath.empty())
    {
        ImGui::TextWrapped("File: %s", LoadedFilePath.c_str());
    }

    int32 MeshCount = 0;
    int32 SkeletonCount = 0;
    int32 CameraCount = 0;
    int32 LightCount = 0;
    int32 NullCount = 0;

    for (const FFBXSceneNode& Node : Snapshot.Nodes)
    {
        switch (Node.Type)
        {
        case EFBXSceneNodeType::Mesh:
            ++MeshCount;
            break;
        case EFBXSceneNodeType::Skeleton:
            ++SkeletonCount;
            break;
        case EFBXSceneNodeType::Camera:
            ++CameraCount;
            break;
        case EFBXSceneNodeType::Light:
            ++LightCount;
            break;
        case EFBXSceneNodeType::Null:
            ++NullCount;
            break;
        default:
            break;
        }
    }

    ImGui::Text("Nodes: %d  Mesh: %d  Skeleton: %d  Camera: %d  Light: %d  Null: %d",
                static_cast<int32>(Snapshot.Nodes.size()), MeshCount, SkeletonCount, CameraCount, LightCount, NullCount);
}

void FEditorFBXSceneViewWidget::RenderTree()
{
    if (!Snapshot.IsValid())
    {
        ImGui::TextDisabled("Open an FBX file to inspect its scene tree.");
        return;
    }

    RenderNodeRecursive(Snapshot.RootIndex);
}

void FEditorFBXSceneViewWidget::RenderNodeRecursive(int32 NodeIndex)
{
    if (NodeIndex < 0 || NodeIndex >= static_cast<int32>(Snapshot.Nodes.size()))
    {
        return;
    }

    const FFBXSceneNode& Node = Snapshot.Nodes[NodeIndex];
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
    if (bAutoExpand && NodeIndex == Snapshot.RootIndex)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, GetNodeTypeColor(Node.Type));
    const bool bOpen = ImGui::TreeNodeEx("##Node", Flags, "[%s] %s", GetNodeTypeName(Node.Type), Node.Name.c_str());
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

void FEditorFBXSceneViewWidget::RenderDetails() const
{
    ImGui::Text("Node Details");
    ImGui::Separator();

    if (SelectedNodeIndex < 0 || SelectedNodeIndex >= static_cast<int32>(Snapshot.Nodes.size()))
    {
        ImGui::TextDisabled("Select a node.");
        return;
    }

    const FFBXSceneNode& Node = Snapshot.Nodes[SelectedNodeIndex];
    const FVector Euler = Node.Rotation.Euler();

    ImGui::Text("Index: %d", SelectedNodeIndex);
    ImGui::Text("Name: %s", Node.Name.c_str());
    ImGui::TextColored(GetNodeTypeColor(Node.Type), "Type: %s", GetNodeTypeName(Node.Type));
    ImGui::Text("Attribute: %s", Node.AttributeName.empty() ? "<None>" : Node.AttributeName.c_str());
    ImGui::Text("Parent: %d", Node.ParentIndex);
    ImGui::Text("Children: %d", static_cast<int32>(Node.Children.size()));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Local Transform");
    ImGui::Text("T: %.3f, %.3f, %.3f", Node.Translation.X, Node.Translation.Y, Node.Translation.Z);
    ImGui::Text("R: %.3f, %.3f, %.3f", Euler.X, Euler.Y, Euler.Z);
    ImGui::Text("S: %.3f, %.3f, %.3f", Node.Scale.X, Node.Scale.Y, Node.Scale.Z);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Quaternion");
    ImGui::Text("X: %.4f", Node.Rotation.X);
    ImGui::Text("Y: %.4f", Node.Rotation.Y);
    ImGui::Text("Z: %.4f", Node.Rotation.Z);
    ImGui::Text("W: %.4f", Node.Rotation.W);
}
