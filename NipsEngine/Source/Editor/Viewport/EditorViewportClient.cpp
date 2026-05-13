#include "Editor/Viewport/EditorViewportClient.h"

#include <algorithm>
#include <unordered_set>
#include "Component/GizmoComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Editor/Selection/SelectionManager.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/EditorEngine.h"
#include "Engine/Input/InputSystem.h"
#include "GameFramework/PrimitiveActors.h"
#include "GameFramework/World.h"
#include "Math/Vector4.h"
#include "Object/ActorIterator.h"
#include "Runtime/SceneView.h"
#include "Editor/Utility/EditorUIUtils.h"

void FEditorViewportClient::Initialize(FWindowsWindow* InWindow, UEditorEngine* InEditor)
{
    FViewportClient::Initialize(InWindow);
    Editor = InEditor;

    InputRouter.SetMode(EViewportInputMode::Editor);
    InputRouter.GetEditorController().SetViewportClient(this);
    InputRouter.GetGameInputController().SetWindow(InWindow);
}

void FEditorViewportClient::SetWorld(UWorld* InWorld)
{
    World = InWorld;
    InputRouter.GetEditorController().SetWorld(InWorld);
    InputRouter.GetGameInputController().SetWorld(InWorld);
}

void FEditorViewportClient::StartPIE(UWorld* InWorld)
{
    World = InWorld;
    InputRouter.GetGameInputController().SetWorld(InWorld);
    InputRouter.GetGameInputController().SetCamera(&Camera);
    InputRouter.GetPIEController().Reset();
    InputRouter.GetGameInputController().SetUIMode(false);
    InputRouter.SetMode(EViewportInputMode::PIE);
}

void FEditorViewportClient::EndPIE(UWorld* InWorld)
{
    World = InWorld;
    InputRouter.GetGameInputController().SetWorld(nullptr);
    InputRouter.GetGameInputController().Reset();
    InputRouter.GetEditorController().SetWorld(InWorld);
    InputRouter.GetEditorController().ResetTargetLocation();
    InputRouter.SetMode(EViewportInputMode::Editor);
    ClearEndPIECallback();
}

void FEditorViewportClient::SaveCameraSnapshot()
{
    if (!bHasCamera)
    {
        return;
    }

    SavedCamera.Location = Camera.GetLocation();
    SavedCamera.Rotation = Camera.GetRotation();
    SavedCamera.FOV = Camera.GetFOV();
    SavedCamera.NearPlane = Camera.GetNearPlane();
    SavedCamera.FarPlane = Camera.GetFarPlane();
    bHasCameraSnapshot = true;
}

void FEditorViewportClient::RestoreCameraSnapshot()
{
    if (!bHasCamera || !bHasCameraSnapshot)
    {
        return;
    }

    Camera.SetLocation(SavedCamera.Location);
    Camera.SetRotation(SavedCamera.Rotation);
    Camera.SetFOV(SavedCamera.FOV);
    Camera.SetNearPlane(SavedCamera.NearPlane);
    Camera.SetFarPlane(SavedCamera.FarPlane);
    InputRouter.GetEditorController().ResetTargetLocation();
}

void FEditorViewportClient::SetGizmo(UGizmoComponent* InGizmo)
{
    Gizmo = InGizmo;
    InputRouter.GetEditorController().SetGizmo(InGizmo);
}

void FEditorViewportClient::SetSettings(const FEditorSettings* InSettings)
{
    Settings = InSettings;
    InputRouter.GetEditorController().SetSettings(InSettings);
}

void FEditorViewportClient::SetSelectionManager(FSelectionManager* InSelectionManager)
{
    SelectionManager = InSelectionManager;
    InputRouter.GetEditorController().SetSelectionManager(InSelectionManager);
}

void FEditorViewportClient::CreateCamera()
{
    bHasCamera = true;
    Camera = FViewportCamera();
    Camera.OnResize(static_cast<uint32>(WindowWidth), static_cast<uint32>(WindowHeight));
    InputRouter.GetEditorController().SetCamera(&Camera);
    InputRouter.GetGameInputController().SetCamera(&Camera);
    InputRouter.GetEditorController().ResetTargetLocation();
}

void FEditorViewportClient::DestroyCamera()
{
    bHasCamera = false;
    InputRouter.GetEditorController().SetCamera(nullptr);
    InputRouter.GetGameInputController().SetCamera(nullptr);
}

void FEditorViewportClient::ResetCamera()
{
    if (!bHasCamera || !Settings)
    {
        return;
    }

    Camera.SetLocation(Settings->InitViewPos);

    const FVector Forward = (Settings->InitLookAt - Settings->InitViewPos).GetSafeNormal();
    if (!Forward.IsNearlyZero())
    {
        const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
        if (!Right.IsNearlyZero())
        {
            const FVector Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();
            FMatrix RotationMatrix = FMatrix::Identity;
            RotationMatrix.SetAxes(Forward, Right, Up);

            FQuat Rotation(RotationMatrix);
            Rotation.Normalize();
            Camera.SetRotation(Rotation);
        }
    }

    InputRouter.GetEditorController().ResetTargetLocation();
}

void FEditorViewportClient::SetViewportSize(float InWidth, float InHeight)
{
    FViewportClient::SetViewportSize(InWidth, InHeight);

    if (bHasCamera)
    {
        Camera.OnResize(static_cast<uint32>(WindowWidth), static_cast<uint32>(WindowHeight));
    }
}

void FEditorViewportClient::Tick(float DeltaTime)
{
    if (State && !State->bHovered)
    {
        return;
    }

    const float VX = State ? static_cast<float>(Viewport->GetRect().X) : 0.0f;
    const float VY = State ? static_cast<float>(Viewport->GetRect().Y) : 0.0f;
    InputRouter.GetEditorController().SetViewportRect(VX, VY, WindowWidth, WindowHeight);
    InputRouter.GetGameInputController().SetViewportRect(VX, VY, WindowWidth, WindowHeight);
    InputRouter.Tick(DeltaTime);

    if (World && World->GetWorldType() == EWorldType::PIE && bHasCamera)
    {
        FPlayerCameraManager& PlayerCameraManager = World->GetPlayerCameraManager();
        PlayerCameraManager.Update(World->GetUnscaledDeltaTime());
        PlayerCameraManager.ApplyToViewportCamera(Camera);
        World->SetActiveCamera(&Camera);
    }

    TickInteraction(DeltaTime);
}

void FEditorViewportClient::BuildSceneView(FSceneView& OutView) const
{
    if (!bHasCamera)
    {
        return;
    }

    OutView.ViewMatrix = Camera.GetViewMatrix();
    OutView.ProjectionMatrix = Camera.GetProjectionMatrix();
    OutView.ViewProjectionMatrix = OutView.ViewMatrix * OutView.ProjectionMatrix;

    OutView.CameraPosition = Camera.GetLocation();
    OutView.CameraForward = Camera.GetForwardVector();
    OutView.CameraRight = Camera.GetRightVector();
    OutView.CameraUp = Camera.GetUpVector();

    OutView.NearPlane = Camera.GetNearPlane();
    OutView.FarPlane = Camera.GetFarPlane();
    OutView.bOrthographic = Camera.IsOrthographic();
    OutView.CameraOrthoHeight = Camera.GetOrthoHeight();
    OutView.CameraFrustum = Camera.GetFrustum();

    if (State)
    {
        OutView.ViewRect = Viewport->GetRect();
        OutView.ViewMode = State->ViewMode;
    }
}

void FEditorViewportClient::ApplyCameraMode()
{
    Camera.SetRotation(FRotator(0.0f, 0.0f, 0.0f));

    switch (ViewportType)
    {
    case EVT_Perspective:
        Camera.SetProjectionType(EViewportProjectionType::Perspective);
        Camera.ClearCustomLookDir();
        Camera.SetLocation(FVector(5.0f, 3.0f, 5.0f));
        Camera.SetLookAt(FVector(0.0f, 0.0f, 0.0f));
        break;

    case EVT_OrthoTop:
        Camera.SetProjectionType(EViewportProjectionType::Orthographic);
        Camera.SetLocation(FVector(0.0f, 0.0f, 1000.0f));
        Camera.SetCustomLookDir(FVector(0.0f, 0.0f, -1.0f), FVector(1.0f, 0.0f, 0.0f));
        break;

    case EVT_OrthoBottom:
        Camera.SetProjectionType(EViewportProjectionType::Orthographic);
        Camera.SetLocation(FVector(0.0f, 0.0f, -1000.0f));
        Camera.SetCustomLookDir(FVector(0.0f, 0.0f, 1.0f), FVector(1.0f, 0.0f, 0.0f));
        break;

    case EVT_OrthoFront:
        Camera.SetProjectionType(EViewportProjectionType::Orthographic);
        Camera.SetLocation(FVector(1000.0f, 0.0f, 0.0f));
        Camera.SetCustomLookDir(FVector(-1.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f));
        break;

    case EVT_OrthoBack:
        Camera.SetProjectionType(EViewportProjectionType::Orthographic);
        Camera.SetLocation(FVector(-1000.0f, 0.0f, 0.0f));
        Camera.SetCustomLookDir(FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f));
        break;

    case EVT_OrthoLeft:
        Camera.SetProjectionType(EViewportProjectionType::Orthographic);
        Camera.SetLocation(FVector(0.0f, -1000.0f, 0.0f));
        Camera.SetCustomLookDir(FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f));
        break;

    case EVT_OrthoRight:
        Camera.SetProjectionType(EViewportProjectionType::Orthographic);
        Camera.SetLocation(FVector(0.0f, 1000.0f, 0.0f));
        Camera.SetCustomLookDir(FVector(0.0f, -1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f));
        break;

    default:
        break;
    }

    InputRouter.GetEditorController().ResetTargetLocation();
}

bool FEditorViewportClient::IsActiveOperation() const
{
    const InputSystem& Input = InputSystem::Get();
    return Input.GetRightDragging() || Input.GetMiddleDragging();
}
void FEditorViewportClient::TickInteraction(float DeltaTime)
{
    (void)DeltaTime;

    if (!bHasCamera || !Gizmo)
    {
        return;
    }

    if (World && World->GetWorldType() == EWorldType::PIE)
    {
        return;
    }

    if (Camera.IsOrthographic())
    {
        Gizmo->ApplyScreenSpaceScalingOrtho(Camera.GetOrthoHeight());
    }
    else
    {
        Gizmo->ApplyScreenSpaceScaling(Camera.GetLocation());
    }

    if (!World || !SelectionManager)
    {
        return;
    }

    FGizmoDelta GizmoDelta = Gizmo->ConsumePendingDelta();
    if (GizmoDelta.Mode != EGizmoMode::End)
    {
        ApplyGizmoDeltaToSelection(GizmoDelta);
        Gizmo->UpdateGizmoTransform();
    }

    POINT MousePoint = InputSystem::Get().GetMousePos();
    if (bBoxSelecting)
    {
        const FGuiInputState& GuiState = InputSystem::Get().GetGuiInputState();
        if (!GuiState.IsInViewportHost(MousePoint.x, MousePoint.y))
        {
            bBoxSelecting = false;
            return;
        }
    }

    if (Window)
    {
        MousePoint = Window->ScreenToClientPoint(MousePoint);
    }

    const float VX = State ? static_cast<float>(Viewport->GetRect().X) : 0.0f;
    const float VY = State ? static_cast<float>(Viewport->GetRect().Y) : 0.0f;
    const float LocalX = static_cast<float>(MousePoint.x) - VX;
    const float LocalY = static_cast<float>(MousePoint.y) - VY;

    if (bBoxSelecting && (LocalX < 0.0f || LocalY < 0.0f || LocalX > WindowWidth || LocalY > WindowHeight))
    {
        bBoxSelecting = false;
        return;
    }

    const InputSystem& Input = InputSystem::Get();
    const bool bCtrlDown = Input.GetKey(VK_CONTROL);
    const bool bAltDown = Input.GetKey(VK_MENU);

    if (Input.GetKeyDown(VK_LBUTTON) && bCtrlDown && bAltDown)
    {
        bBoxSelecting = true;
        BoxSelectStart = POINT{ static_cast<LONG>(LocalX), static_cast<LONG>(LocalY) };
        BoxSelectEnd = BoxSelectStart;
        return;
    }

    if (bBoxSelecting)
    {
        if (Input.GetLeftDragging())
        {
            BoxSelectEnd = POINT{ static_cast<LONG>(LocalX), static_cast<LONG>(LocalY) };
        }
        else if (Input.GetLeftDragEnd())
        {
            HandleBoxSelection();
            bBoxSelecting = false;
        }
        else if (Input.GetKeyUp(VK_LBUTTON))
        {
            bBoxSelecting = false;
        }
    }
}

void FEditorViewportClient::ApplyGizmoDeltaToSelection(const FGizmoDelta& Delta)
{
    if (!SelectionManager)
    {
        return;
    }

    const TArray<AActor*>& SelectedActors = SelectionManager->GetSelectedActors();
    if (SelectedActors.empty())
    {
        return;
    }

    switch (Delta.Mode)
    {
    case EGizmoMode::Translate:
        for (AActor* Actor : SelectedActors)
        {
            if (Actor)
            {
                Actor->AddActorWorldOffset(Delta.WorldDelta);
            }
        }
        break;

    case EGizmoMode::Rotate:
    {
        const FQuat DeltaQuat(Delta.Axis, Delta.Amount);
        for (AActor* Actor : SelectedActors)
        {
            if (!Actor || !Actor->GetRootComponent())
            {
                continue;
            }

            const FQuat CurrentQuat = FQuat::MakeFromEuler(Actor->GetActorRotation());
            Actor->SetActorRotation((CurrentQuat * DeltaQuat).Euler());
        }
        break;
    }

    case EGizmoMode::Scale:
        for (AActor* Actor : SelectedActors)
        {
            if (!Actor)
            {
                continue;
            }

            FVector NewScale = Actor->GetActorScale();
            switch (Delta.AxisIdx)
            {
            case 0:
                NewScale.X += Delta.Amount;
                break;
            case 1:
                NewScale.Y += Delta.Amount;
                break;
            case 2:
                NewScale.Z += Delta.Amount;
                break;
            default:
                break;
            }

            NewScale.X = std::max(NewScale.X, 0.001f);
            NewScale.Y = std::max(NewScale.Y, 0.001f);
            NewScale.Z = std::max(NewScale.Z, 0.001f);
            Actor->SetActorScale(NewScale);
        }
        break;

    default:
        break;
    }
}

bool FEditorViewportClient::TryProjectWorldToViewport(const FVector& WorldPos, float& OutViewportX, float& OutViewportY, float& OutDepth) const
{
    const FVector4 Clip = FMatrix::Identity.TransformVector4(FVector4(WorldPos, 1.0f), Camera.GetViewProjectionMatrix());
    if (MathUtil::IsNearlyZero(Clip.W))
    {
        return false;
    }

    const float InvW = 1.0f / Clip.W;
    const float NdcX = Clip.X * InvW;
    const float NdcY = Clip.Y * InvW;
    const float NdcZ = Clip.Z * InvW;
    if (NdcX < -1.0f || NdcX > 1.0f || NdcY < -1.0f || NdcY > 1.0f)
    {
        return false;
    }

    OutViewportX = (NdcX * 0.5f + 0.5f) * WindowWidth;
    OutViewportY = (1.0f - (NdcY * 0.5f + 0.5f)) * WindowHeight;
    OutDepth = NdcZ;
    return true;
}

void FEditorViewportClient::HandleBoxSelection()
{
    if (!SelectionManager || !World)
    {
        return;
    }

    const int32 MinX = std::min(BoxSelectStart.x, BoxSelectEnd.x);
    const int32 MinY = std::min(BoxSelectStart.y, BoxSelectEnd.y);
    const int32 MaxX = std::max(BoxSelectStart.x, BoxSelectEnd.x);
    const int32 MaxY = std::max(BoxSelectStart.y, BoxSelectEnd.y);
    const int32 Width = MaxX - MinX;
    const int32 Height = MaxY - MinY;

    if (Width < 2 || Height < 2)
    {
        return;
    }

    if (!InputSystem::Get().GetKey(VK_SHIFT))
    {
        SelectionManager->ClearSelection();
    }

    TArray<UPrimitiveComponent*> CandidatePrimitives;
    World->GetSpatialIndex().FrustumQueryPrimitives(Camera.GetFrustum(), CandidatePrimitives, FrustumQueryScratch);

    TSet<AActor*> SeenActors;
    SeenActors.reserve(CandidatePrimitives.size());

    for (UPrimitiveComponent* Primitive : CandidatePrimitives)
    {
        AActor* Actor = Primitive ? Primitive->GetOwner() : nullptr;
        if (!Actor || !Actor->GetRootComponent())
        {
            continue;
        }

        if (!SeenActors.insert(Actor).second)
        {
            continue;
        }

        float ViewportXValue = 0.0f;
        float ViewportYValue = 0.0f;
        float Depth = 0.0f;
        if (!TryProjectWorldToViewport(Actor->GetActorLocation(), ViewportXValue, ViewportYValue, Depth))
        {
            continue;
        }

        if (Depth < 0.0f || Depth > 1.0f)
        {
            continue;
        }

        const int32 Px = static_cast<int32>(ViewportXValue);
        const int32 Py = static_cast<int32>(ViewportYValue);
        if (Px >= MinX && Px <= MaxX && Py >= MinY && Py <= MaxY)
        {
            SelectionManager->AddSelect(Actor);
        }
    }
}

bool FEditorViewportClient::RequestActorPlacement(float X, float Y, float PopupX, float PopupY)
{
    if (!World || !bHasCamera)
    {
        return false;
    }

    FRay Ray = Camera.DeprojectScreenToWorld(X, Y, WindowWidth, WindowHeight);

    FHitResult BestHit{};
    bool bHasHit = false;
    float ClosestDistance = FLT_MAX;

    FWorldSpatialIndex::FPrimitiveRayQueryScratch QueryScratch;
    TArray<UPrimitiveComponent*> CandidatePrimitives;
    TArray<float> CandidateTs;
    World->GetSpatialIndex().RayQueryPrimitives(Ray, CandidatePrimitives, CandidateTs, QueryScratch);

    for (int32 CandidateIndex = 0; CandidateIndex < static_cast<int32>(CandidatePrimitives.size()); ++CandidateIndex)
    {
        if (CandidateTs[CandidateIndex] > ClosestDistance)
        {
            break;
        }

        UPrimitiveComponent* Primitive = CandidatePrimitives[CandidateIndex];
        if (!Primitive)
        {
            continue;
        }

        FHitResult HitResult{};
        if (Primitive->Raycast(Ray, HitResult) && HitResult.Distance < ClosestDistance)
        {
            ClosestDistance = HitResult.Distance;
            BestHit = HitResult;
            bHasHit = true;
        }
    }

    if (bHasHit)
    {
        PendingActorPlacementLocation = BestHit.Location;
    }
    else
    {
        constexpr float FallbackPlacementDistance = 10.0f;
        FVector FallbackDirection = Ray.Direction.GetSafeNormal();
        if (FallbackDirection.IsNearlyZero())
        {
            FallbackDirection = Camera.GetForwardVector().GetSafeNormal();
        }

        PendingActorPlacementLocation = Ray.Origin + FallbackDirection * FallbackPlacementDistance;
    }

    PendingActorPlacementPopupPos = { static_cast<LONG>(PopupX), static_cast<LONG>(PopupY) };
    bPendingActorPlacement = true;
    return true;
}

void FEditorViewportClient::FocusPrimarySelection()
{
    if (!SelectionManager || !bHasCamera)
    {
        return;
    }

    AActor* Primary = SelectionManager->GetPrimarySelection();
    if (!Primary)
    {
        return;
    }

    const FVector Target = Primary->GetActorLocation();
    if (Camera.IsOrthographic())
    {
        const FVector Forward = Camera.GetEffectiveForward().GetSafeNormal();
        float Distance = FVector::DotProduct(Camera.GetLocation() - Target, Forward);
        if (MathUtil::IsNearlyZero(Distance))
        {
            Distance = 1000.0f;
        }

        Camera.SetLocation(Target + Forward * Distance);
    }
    else
    {
        const FVector Forward = Camera.GetForwardVector().GetSafeNormal();
        Camera.SetLocation(Target - Forward * 5.0f);
        Camera.SetLookAt(Target);
    }

    InputRouter.GetEditorController().ResetTargetLocation();
}

void FEditorViewportClient::DeleteSelectedActors()
{
    if (!SelectionManager)
    {
        return;
    }

    const TArray<AActor*> SelectedActors = SelectionManager->GetSelectedActors();
    for (AActor* Actor : SelectedActors)
    {
        if (!Actor)
        {
            continue;
        }

        if (UWorld* ActorWorld = Actor->GetFocusedWorld())
        {
            ActorWorld->DestroyActor(Actor);
        }
    }

    SelectionManager->ClearSelection();
    Editor->GetMainPanel().GetPropertyWidget().ResetSelection();
}

void FEditorViewportClient::DuplicateSelectedActors()
{
    if (!SelectionManager || !World)
    {
        return;
    }

    const TArray<AActor*> SelectedActors = SelectionManager->GetSelectedActors();
    if (SelectedActors.empty())
    {
        return;
    }

    SelectionManager->ClearSelection();

    for (AActor* Actor : SelectedActors)
    {
        if (!Actor)
        {
            continue;
        }

        AActor* DuplicatedActor = Cast<AActor>(Actor->Duplicate());
        if (!DuplicatedActor)
        {
            continue;
        }

        DuplicatedActor->SetWorld(World);
        if (World->HasBegunPlay())
        {
            DuplicatedActor->BeginPlay();
        }
        World->GetPersistentLevel()->AddActor(DuplicatedActor);
        DuplicatedActor->SetActorLocation(Actor->GetActorLocation());
        SelectionManager->AddSelect(DuplicatedActor);
    }

    Editor->GetMainPanel().GetPropertyWidget().ResetSelection();
}

void FEditorViewportClient::SelectAllActors()
{
    if (!SelectionManager || !World)
    {
        return;
    }

    SelectionManager->ClearSelection();
    for (TActorIterator<AActor> Iter(World); Iter; ++Iter)
    {
        if (AActor* Actor = *Iter)
        {
            SelectionManager->AddSelect(Actor);
        }
    }
}
