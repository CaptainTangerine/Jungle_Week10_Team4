#include "Editor/UI/EditorFBXSceneViewWidget.h"

#include "Component/StaticMeshComponent.h"
#include "Core/Paths.h"
#include "GameFramework/PrimitiveActors.h"
#include "GameFramework/World.h"
#include "ImGui/imgui.h"
#include "Object/Object.h"
#include "Runtime/Engine.h"

#include <commdlg.h>
#include <filesystem>

namespace
{
    const char* GetImportNodeTypeName(const FFBXImportNode& Node)
    {
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

    void ApplyImportNodeTransform(AActor* Actor, const FFBXImportNode& Node)
    {
        if (Actor == nullptr)
        {
            return;
        }

        FVector Translation;
        FVector Scale;
        FMatrix RotationMatrix;
        if (!Node.GlobalTransformMatrix.Decompose(Translation, RotationMatrix, Scale))
        {
            Translation = Node.GlobalTransformMatrix.GetOrigin();
            Scale = FVector(1.0f, 1.0f, 1.0f);
            RotationMatrix = FMatrix::Identity;
        }

        Actor->SetActorLocation(Translation);
        Actor->SetActorRotation(RotationMatrix.GetEuler());
        Actor->SetActorScale(Scale);
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
    LoadedFilePath = FilePath;

    FFBXImporter Importer;
    ImportScene = Importer.Import(FilePath);
    if (ImportScene.Nodes.empty())
    {
        StatusMessage = "Failed to import FBX scene.";
        return;
    }

    SelectedNodeIndex = 0;
    StatusMessage = "FBX import scene loaded.";
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
        ImportScene = FFBXImportScene{};
        LoadedFilePath.clear();
        SelectedNodeIndex = -1;
        StatusMessage = "No FBX loaded.";
    }

    ImGui::SameLine();
    ImGui::Checkbox("Auto Expand", &bAutoExpand);

    ImGui::SameLine();
    const bool bHasStaticMeshes = !ImportScene.StaticMeshes.empty();
    if (!bHasStaticMeshes)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Spawn Static Meshes"))
    {
        SpawnImportedStaticMeshes();
    }
    if (!bHasStaticMeshes)
    {
        ImGui::EndDisabled();
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
    if (bAutoExpand && NodeIndex == 0)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
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

void FEditorFBXSceneViewWidget::RenderDetails() const
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
        ImGui::Text("Material Slots: %d", static_cast<int32>(Mesh.MatreialSlots.size()));

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
            for (int32 SlotIndex = 0; SlotIndex < static_cast<int32>(Mesh.MatreialSlots.size()); ++SlotIndex)
            {
                const FStaticMeshMaterialSlot& Slot = Mesh.MatreialSlots[SlotIndex];
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
        ImGui::Text("Skin Deformers: %d", Mesh.SkinDeformerCount);
        ImGui::Text("Control Points: %d", Mesh.ControlPointCount);
        ImGui::Text("Polygons: %d", Mesh.PolygonCount);
    }
}

void FEditorFBXSceneViewWidget::SpawnImportedStaticMeshes()
{
    if (ImportScene.StaticMeshes.empty())
    {
        StatusMessage = "No static mesh import data to spawn.";
        return;
    }

    UWorld* World = GEngine ? GEngine->GetWorld() : nullptr;
    if (World == nullptr)
    {
        StatusMessage = "Failed to spawn FBX static meshes. World is null.";
        return;
    }

    FFBXImporter Importer;
    int32 SpawnedCount = 0;

    for (const FFBXStaticMeshImportData& MeshData : ImportScene.StaticMeshes)
    {
        if (MeshData.Vertices.empty() || MeshData.Indices.empty())
        {
            continue;
        }

        FStaticMesh* RawMesh = Importer.CreateStaticMeshFromImportData(MeshData);
        if (RawMesh == nullptr)
        {
            continue;
        }

        UStaticMesh* StaticMeshAsset = UObjectManager::Get().CreateObject<UStaticMesh>();
        StaticMeshAsset->SetMeshData(RawMesh);

        AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
        Actor->SetFName(FName((FString("FBX_") + MeshData.Name).c_str()));

        UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Actor->GetRootComponent());
        if (MeshComponent == nullptr)
        {
            MeshComponent = Actor->AddComponent<UStaticMeshComponent>();
            Actor->SetRootComponent(MeshComponent);
        }

        MeshComponent->SetStaticMesh(StaticMeshAsset);

        if (MeshData.SourceNodeIndex >= 0 && MeshData.SourceNodeIndex < static_cast<int32>(ImportScene.Nodes.size()))
        {
            ApplyImportNodeTransform(Actor, ImportScene.Nodes[MeshData.SourceNodeIndex]);
        }

        ++SpawnedCount;
    }

    World->RebuildSpatialIndex();
    StatusMessage = SpawnedCount > 0
        ? FString("Spawned ") + std::to_string(SpawnedCount) + " FBX static mesh actor(s)."
        : FString("No valid static mesh data was spawned.");
}
