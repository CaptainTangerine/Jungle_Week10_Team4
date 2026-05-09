#include "Editor/UI/EditorControlWidget.h"

#include "Editor/EditorEngine.h"
#include "Core/Paths.h"
#include "Core/ResourceManager.h"
#include "Engine/Asset/FbxLoader.h"
#include "Engine/Asset/SkeletalMesh.h"
#include "Engine/Asset/StaticMesh.h"
#include "Engine/Viewport/ViewportCamera.h"

#include "ImGui/imgui.h"
#include "Component/GizmoComponent.h"
#include "Component/SkeletalMeshComponent.h"
#include "Component/StaticMeshComponent.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PrimitiveActors.h"

#include <commdlg.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <utility>

#define SEPARATOR()     \
    ;                   \
    ImGui::Spacing();   \
    ImGui::Spacing();   \
    ImGui::Separator(); \
    ImGui::Spacing();   \
    ImGui::Spacing();

namespace
{
    // 월드에 특정 타입의 액터를 생성하고 초기화하는 템플릿 함수입니다.
    template <typename T>
    void SpawnActor(UWorld* World, const FVector& Location)
    {
        T* Actor = World->SpawnActor<T>();
        Actor->SetActorLocation(Location);
    }

    struct FSpawnEntry
    {
        const char* Label;
        void (*Spawn)(UWorld*, const FVector&);
    };

    static const FSpawnEntry PrimitiveTypes[] = {
        { "Scene", SpawnActor<ASceneActor> },
        { "Pawn", SpawnActor<APawn> },
        { "Camera", SpawnActor<ACameraActor> },
        { "Cine Camera", SpawnActor<ACineCameraActor> },
        { "StaticMesh", SpawnActor<AStaticMeshActor> },
        { "SkeletalMesh", SpawnActor<ASkeletalMeshActor> },
        { "Water", SpawnActor<AWaterActor> },
        { "Global Ocean", SpawnActor<AGlobalOceanActor> },
        { "TextRender", SpawnActor<ATextRenderActor> },
        { "SubUV", SpawnActor<ASubUVActor> },
        { "Billboard", SpawnActor<ABillboardActor> },
        { "Decal", SpawnActor<ADecalActor> },
        { "Directional Light", SpawnActor<ADirectionalLightActor> },
        { "Ambient Light", SpawnActor<AAmbientLightActor> },
        { "Point Light", SpawnActor<APointLightActor> },
        { "Spot Light", SpawnActor<ASpotLightActor> },
        { "Sky Atmosphere", SpawnActor<ASkyAtmosphereActor> },
        { "Height Fog", SpawnActor<AHeightFogActor> },
    };

    FStaticMesh* BuildStaticPreviewMesh(const FSkeletalMeshAsset& SkeletalMeshAsset, const char* SourcePath)
    {
        if (SkeletalMeshAsset.Vertices.empty() || SkeletalMeshAsset.Indices.empty())
        {
            return nullptr;
        }

        FStaticMesh* PreviewMesh = new FStaticMesh();
        PreviewMesh->PathFileName = SourcePath ? SourcePath : "";
        PreviewMesh->Vertices.reserve(SkeletalMeshAsset.Vertices.size());
        PreviewMesh->Indices = SkeletalMeshAsset.Indices;

        for (const FbxVertex& SourceVertex : SkeletalMeshAsset.Vertices)
        {
            FNormalVertex PreviewVertex = {};
            PreviewVertex.Position = SourceVertex.Position;
            PreviewVertex.Normal = SourceVertex.Normal;
            PreviewVertex.UVs = SourceVertex.UV;
            PreviewVertex.Color = FColor{ 1.0f, 1.0f, 1.0f, 1.0f };
            PreviewMesh->Vertices.push_back(PreviewVertex);
        }

        if (SkeletalMeshAsset.MaterialSlots.empty())
        {
            FStaticMeshMaterialSlot Slot = {};
            Slot.SlotName = "FBX Preview";
            PreviewMesh->Slots.push_back(Slot);
        }
        else
        {
            PreviewMesh->Slots.reserve(SkeletalMeshAsset.MaterialSlots.size());
            for (const FSkeletalMeshMaterialSlot& SourceSlot : SkeletalMeshAsset.MaterialSlots)
            {
                FStaticMeshMaterialSlot Slot = {};
                Slot.SlotName = SourceSlot.SlotName;
                PreviewMesh->Slots.push_back(Slot);
            }
        }

        if (SkeletalMeshAsset.Sections.empty())
        {
            FStaticMeshSection Section = {};
            Section.StartIndex = 0;
            Section.IndexCount = static_cast<uint32>(PreviewMesh->Indices.size());
            Section.MaterialSlotIndex = 0;
            PreviewMesh->Sections.push_back(Section);
        }
        else
        {
            PreviewMesh->Sections.reserve(SkeletalMeshAsset.Sections.size());
            const int32 MaxMaterialSlotIndex = static_cast<int32>(PreviewMesh->Slots.size()) - 1;

            for (const FSKeletalMeshSection& SourceSection : SkeletalMeshAsset.Sections)
            {
                if (SourceSection.IndexCount == 0)
                {
                    continue;
                }

                FStaticMeshSection Section = {};
                Section.StartIndex = SourceSection.StartIndex;
                Section.IndexCount = SourceSection.IndexCount;
                Section.MaterialSlotIndex = std::clamp(SourceSection.MaterialSlotIndex, 0, MaxMaterialSlotIndex);
                PreviewMesh->Sections.push_back(Section);
            }
        }

        if (PreviewMesh->Sections.empty())
        {
            delete PreviewMesh;
            return nullptr;
        }

        return PreviewMesh;
    }

    UStaticMeshComponent* FindStaticMeshComponent(AActor* Actor)
    {
        if (Actor == nullptr)
        {
            return nullptr;
        }

        if (UStaticMeshComponent* RootStaticMesh = Cast<UStaticMeshComponent>(Actor->GetRootComponent()))
        {
            return RootStaticMesh;
        }

        for (UActorComponent* Component : Actor->GetComponents())
        {
            if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
            {
                return StaticMeshComponent;
            }
        }

        return nullptr;
    }

    USkeletalMeshComponent* FindSkeletalMeshComponent(AActor* Actor)
    {
        if (Actor == nullptr)
        {
            return nullptr;
        }

        if (USkeletalMeshComponent* RootSkeletalMesh = Cast<USkeletalMeshComponent>(Actor->GetRootComponent()))
        {
            return RootSkeletalMesh;
        }

        for (UActorComponent* Component : Actor->GetComponents())
        {
            if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(Component))
            {
                return SkeletalMeshComponent;
            }
        }

        return nullptr;
    }

    void RenderSkeletonBoneNode(
        const FSkeletalMeshAsset& SkeletalMeshAsset,
        const TArray<TArray<int32>>& ChildrenByBoneIndex,
        int32 BoneIndex)
    {
        if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(SkeletalMeshAsset.Bones.size()))
        {
            return;
        }

        const FBone& Bone = SkeletalMeshAsset.Bones[BoneIndex];
        const bool bHasChildren = !ChildrenByBoneIndex[BoneIndex].empty();

        char Label[512] = {};
        snprintf(
            Label,
            sizeof(Label),
            "%d: %s  parent=%d",
            BoneIndex,
            Bone.Name.c_str(),
            Bone.ParentIndex);

        ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (!bHasChildren)
        {
            Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        const bool bOpen = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(BoneIndex)), Flags, "%s", Label);
        if (ImGui::IsItemHovered() && !Bone.PathName.empty())
        {
            ImGui::SetTooltip("%s", Bone.PathName.c_str());
        }

        if (bHasChildren && bOpen)
        {
            for (int32 ChildBoneIndex : ChildrenByBoneIndex[BoneIndex])
            {
                RenderSkeletonBoneNode(SkeletalMeshAsset, ChildrenByBoneIndex, ChildBoneIndex);
            }
            ImGui::TreePop();
        }
    }

    void RenderSkeletonTree(const FSkeletalMeshAsset& SkeletalMeshAsset)
    {
        ImGui::Text("Skeleton: %zu bones", SkeletalMeshAsset.Bones.size());

        if (SkeletalMeshAsset.Bones.empty())
        {
            ImGui::TextUnformatted("No bones parsed.");
            return;
        }

        TArray<TArray<int32>> ChildrenByBoneIndex;
        ChildrenByBoneIndex.resize(SkeletalMeshAsset.Bones.size());

        TArray<int32> RootBoneIndices;
        for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(SkeletalMeshAsset.Bones.size()); ++BoneIndex)
        {
            const int32 ParentIndex = SkeletalMeshAsset.Bones[BoneIndex].ParentIndex;
            if (ParentIndex >= 0 && ParentIndex < static_cast<int32>(SkeletalMeshAsset.Bones.size()))
            {
                ChildrenByBoneIndex[ParentIndex].push_back(BoneIndex);
            }
            else
            {
                RootBoneIndices.push_back(BoneIndex);
            }
        }

        if (ImGui::BeginChild("FbxSkeletonTree", ImVec2(0.0f, 180.0f), true))
        {
            for (int32 RootBoneIndex : RootBoneIndices)
            {
                RenderSkeletonBoneNode(SkeletalMeshAsset, ChildrenByBoneIndex, RootBoneIndex);
            }
        }
        ImGui::EndChild();
    }
}

bool FEditorControlWidget::OpenFbxFileDialog(FString& OutFilePath) const
{
    OutFilePath.clear();

    WCHAR FileBuffer[MAX_PATH] = { 0 };
    OPENFILENAMEW DialogDesc = {};
    DialogDesc.lStructSize = sizeof(DialogDesc);
    DialogDesc.hwndOwner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
    DialogDesc.lpstrFilter = L"FBX Files (*.fbx)\0*.fbx\0All Files (*.*)\0*.*\0";
    DialogDesc.lpstrFile = FileBuffer;
    DialogDesc.nMaxFile = MAX_PATH;
    DialogDesc.lpstrDefExt = L"fbx";
    DialogDesc.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    const BOOL bPicked = GetOpenFileNameW(&DialogDesc);
    if (!bPicked)
    {
        return false;
    }

    OutFilePath = FPaths::ToUtf8(FileBuffer);
    return true;
}

// 에디터 컨트롤 위젯을 초기화하고 기본 상태를 설정합니다.
void FEditorControlWidget::Initialize(UEditorEngine* InEditorEngine)
{
    FEditorWidget::Initialize(InEditorEngine);
    SelectedPrimitiveType = 0;
    FbxImportPathBuffer[0] = '\0';
    LastFbxImportStatus.clear();
    LastImportedFbxAsset = {};
    bHasLastImportedFbxAsset = false;
}

// 컨트롤 패널 UI를 렌더링하고 액터 생성 및 카메라 제어 기능을 처리합니다.
void FEditorControlWidget::Render(float DeltaTime)
{
    (void)DeltaTime;
    if (!EditorEngine)
    {
        return;
    }

    ImGui::SetNextWindowCollapsed(false, ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(500.0f, 480.0f), ImGuiCond_Once);

    ImGui::Begin("Jungle Control Panel");

    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);

    if (ImGui::BeginCombo("Actor", PrimitiveTypes[SelectedPrimitiveType].Label))
    {
        for (int i = 0; i < IM_ARRAYSIZE(PrimitiveTypes); i++)
        {
            const bool bIsSelected = (SelectedPrimitiveType == i);
            if (ImGui::Selectable(PrimitiveTypes[i].Label, bIsSelected))
            {
                SelectedPrimitiveType = i;
            }

            if (bIsSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("Spawn"))
    {
        UWorld* World = EditorEngine->GetFocusedWorld();
        if (!World)
        {
            ImGui::End();
            return;
        }

        for (int32 i = 0; i < NumberOfSpawnedActors; i++)
        {
            PrimitiveTypes[SelectedPrimitiveType].Spawn(World, CurSpawnPoint);
        }
        NumberOfSpawnedActors = 1;
    }
    ImGui::InputInt("Number of Spawn", &NumberOfSpawnedActors, 1, 10);

    SEPARATOR();

    ImGui::TextUnformatted("FBX Import Test");
    ImGui::InputText("FBX Path", FbxImportPathBuffer, IM_ARRAYSIZE(FbxImportPathBuffer));
    ImGui::SameLine();
    if (ImGui::Button("Browse##FbxImport"))
    {
        FString PickedPath;
        if (OpenFbxFileDialog(PickedPath))
        {
            strncpy_s(FbxImportPathBuffer, sizeof(FbxImportPathBuffer), PickedPath.c_str(), _TRUNCATE);
            LastFbxImportStatus.clear();
        }
    }

    if (ImGui::Button("Import FBX"))
    {
        if (FbxImportPathBuffer[0] == '\0')
        {
            LastFbxImportStatus = "FBX path is empty.";
        }
        else
        {
            FSkeletalMeshAsset ImportedAsset;
            FFbxLoader Loader;
            const bool bImportSuccess = Loader.ImportFbxFile(FbxImportPathBuffer, ImportedAsset);
            if (bImportSuccess)
            {
                LastImportedFbxAsset = std::move(ImportedAsset);
                bHasLastImportedFbxAsset = true;
                LastFbxImportStatus = "FBX import succeeded. Check log for parsed counts.";
            }
            else
            {
                bHasLastImportedFbxAsset = false;
                LastFbxImportStatus = "FBX import failed. Check log for details.";
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Import FBX Preview"))
    {
        if (FbxImportPathBuffer[0] == '\0')
        {
            LastFbxImportStatus = "FBX path is empty.";
        }
        else
        {
            UWorld* World = EditorEngine->GetFocusedWorld();
            if (World == nullptr)
            {
                LastFbxImportStatus = "No focused world.";
            }
            else
            {
                FSkeletalMeshAsset ImportedAsset;
                FFbxLoader Loader;
                const bool bImportSuccess = Loader.ImportFbxFile(FbxImportPathBuffer, ImportedAsset);
                if (!bImportSuccess)
                {
                    bHasLastImportedFbxAsset = false;
                    LastFbxImportStatus = "FBX import failed. Check log for details.";
                }
                else
                {
                    LastImportedFbxAsset = ImportedAsset;
                    bHasLastImportedFbxAsset = true;

                    FSkeletalMeshAsset* PreviewMeshData = new FSkeletalMeshAsset(std::move(ImportedAsset));
                    USkeletalMesh* PreviewMesh = UObjectManager::Get().CreateObject<USkeletalMesh>();
                    PreviewMesh->SetMeshData(PreviewMeshData);

                    ASkeletalMeshActor* PreviewActor = World->SpawnActor<ASkeletalMeshActor>();
                    PreviewActor->SetActorLocation(CurSpawnPoint);

                    USkeletalMeshComponent* SkeletalMeshComponent = FindSkeletalMeshComponent(PreviewActor);
                    if (SkeletalMeshComponent == nullptr)
                    {
                        LastFbxImportStatus = "Preview actor has no skeletal mesh component.";
                    }
                    else
                    {
                        SkeletalMeshComponent->SetSkeletalMesh(PreviewMesh);

                        const int32 MaterialSlotCount = static_cast<int32>(PreviewMesh->GetMaterialSlots().size());
                        UMaterialInterface* DefaultMaterial = FResourceManager::Get().GetMaterial("DefaultWhite");
                        for (int32 SlotIndex = 0; SlotIndex < MaterialSlotCount; ++SlotIndex)
                        {
                            SkeletalMeshComponent->SetMaterial(SlotIndex, DefaultMaterial);
                        }

                        World->RebuildSpatialIndex();
                        LastFbxImportStatus = "FBX preview spawned as a skeletal mesh actor.";
                    }
                }
            }
        }
    }

    if (!LastFbxImportStatus.empty())
    {
        ImGui::TextWrapped("%s", LastFbxImportStatus.c_str());
    }

    if (bHasLastImportedFbxAsset)
    {
        RenderSkeletonTree(LastImportedFbxAsset);
    }

    SEPARATOR();

    // Camera
    FViewportCamera* Camera = EditorEngine->GetCamera();
    if (Camera == nullptr)
    {
        ImGui::End();
        return;
    }

    bool bIsOrtho = (Camera->GetProjectionType() == EViewportProjectionType::Orthographic);
    if (ImGui::Checkbox("Orthographic", &bIsOrtho))
    {
        Camera->SetProjectionType(bIsOrtho ? EViewportProjectionType::Orthographic : EViewportProjectionType::Perspective);
    }

    float CameraFOV_Deg = MathUtil::RadiansToDegrees(Camera->GetFOV());
    if (ImGui::DragFloat("Camera FOV", &CameraFOV_Deg, 0.5f, 1.0f, 90.0f))
    {
        Camera->SetFOV(MathUtil::DegreesToRadians(CameraFOV_Deg));
    }

    float OrthoHeight = Camera->GetOrthoHeight();
    if (ImGui::DragFloat("Ortho Height", &OrthoHeight, 0.1f, 0.1f, 1000.0f))
    {
        Camera->SetOrthoHeight(MathUtil::Clamp(OrthoHeight, 0.1f, 1000.0f));
    }

    FVector CamPos = Camera->GetLocation();
    float CameraLocation[3] = { CamPos.X, CamPos.Y, CamPos.Z };
    if (ImGui::DragFloat3("Camera Location", CameraLocation, 0.1f, 0.0f, 0.0f, "%.1f"))
    {
        Camera->SetLocation(FVector(CameraLocation[0], CameraLocation[1], CameraLocation[2]));
        EditorEngine->GetViewportLayout().GetViewportClient(0)->SyncCameraTarget();
    }

    FVector CamRot = Camera->GetRotation().Rotator().Euler();
    float CameraRotation[3] = { CamRot.X, CamRot.Y, CamRot.Z };
    if (ImGui::DragFloat3("Camera Rotation", CameraRotation, 0.1f, 0.0f, 0.0f, "%.1f"))
    {
        CameraRotation[1] = MathUtil::Clamp(CameraRotation[1], -89.9f, 89.9f);
        FRotator NewRotation = FRotator::MakeFromEuler(FVector(CameraRotation[0], CameraRotation[1], CameraRotation[2]));

        NewRotation.Normalize();
        Camera->SetRotation(NewRotation);
    }

    ImGui::PopItemWidth();

    SEPARATOR();

    // Gizmo Space / Mode
    int32 SelectedSpace = EditorEngine->GetGizmo()->IsWorldSpace() ? 0 : 1;
    if (ImGui::RadioButton("World", &SelectedSpace, 0))
    {
        EditorEngine->GetGizmo()->SetWorldSpace(true);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Local", &SelectedSpace, 1))
    {
        EditorEngine->GetGizmo()->SetWorldSpace(false);
    }

    SEPARATOR();

    if (ImGui::Button("Translate"))
        EditorEngine->GetGizmo()->SetTranslateMode();

    ImGui::SameLine();

    if (ImGui::Button("Rotate"))
        EditorEngine->GetGizmo()->SetRotateMode();

    ImGui::SameLine();

    if (ImGui::Button("Scale"))
        EditorEngine->GetGizmo()->SetScaleMode();

    ImGui::End();
}
