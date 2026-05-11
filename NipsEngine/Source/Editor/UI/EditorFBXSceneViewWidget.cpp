#include "Editor/UI/EditorFBXSceneViewWidget.h"

#include "Asset/SkeletalMesh.h"
#include "Component/SceneComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Core/Paths.h"
#include "GameFramework/Actor.h"
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
    if (ImGui::Checkbox("Skinned -> Static", &bImportSkinnedMeshesAsStatic) && !LoadedFilePath.empty())
    {
        LoadFBXScene(LoadedFilePath);
    }

    ImGui::SameLine();

    const bool bHasMeshes = !ImportScene.StaticMeshes.empty() || !ImportScene.SkeletalMeshes.empty();
    if (!bHasMeshes)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Spawn Actors"))
    {
        SpawnImportedFBXMeshActors();
    }
    if (!bHasMeshes)
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

    UWorld* World = GEngine ? GEngine->GetWorld() : nullptr;
    if (World == nullptr)
    {
        StatusMessage = "World가 null입니다. FBX Actor 스폰 실패.";
        return;
    }

    FFBXImporter Importer;
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
    for (const FFBXSkeletalMeshImportData& MeshData : ImportScene.SkeletalMeshes)
    {
        // 버텍스나 인덱스 데이터가 없는 노드는 건너뜀
        if (MeshData.Vertices.empty() || MeshData.Indices.empty())
        {
            continue;
        }

        FSkeletalMesh* RawMesh = Importer.CreateSkeletalMeshFromtImportData(MeshData);
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

    const int32 TotalSpawned = StaticSpawnedCount + SkeletalSpawnedCount;
    if (TotalSpawned > 0)
    {
        StatusMessage = FString("FBX Actor 스폰 완료 — Static: ") + std::to_string(StaticSpawnedCount)
            + ", Skeletal: " + std::to_string(SkeletalSpawnedCount) + ".";
    }
    else
    {
        StatusMessage = "유효한 메시 데이터가 없어 스폰하지 않았습니다.";
    }
}
