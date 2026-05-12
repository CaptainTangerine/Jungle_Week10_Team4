#pragma once

#include <functional>
#include "Core/CollisionTypes.h"
#include "Editor/Utility/EditorUIUtils.h"
#include "Engine/Geometry/Ray.h"
#include "Engine/Input/ViewportInputRouter.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Render/Common/RenderTypes.h"
#include "Runtime/ViewportClient.h"
#include "Spatial/WorldSpatialIndex.h"

enum EEditorViewportType
{
    EVT_Perspective = 0,
    EVT_OrthoXY = 1,
    EVT_OrthoXZ = 2,
    EVT_OrthoYZ = 3,
    EVT_OrthoNegativeXY = 4,
    EVT_OrthoNegativeXZ = 5,
    EVT_OrthoNegativeYZ = 6,

    EVT_OrthoTop = EVT_OrthoXY,
    EVT_OrthoLeft = EVT_OrthoXZ,
    EVT_OrthoFront = EVT_OrthoNegativeYZ,
    EVT_OrthoBack = EVT_OrthoYZ,
    EVT_OrthoBottom = EVT_OrthoNegativeXY,
    EVT_OrthoRight = EVT_OrthoNegativeXZ,
    LVT_MAX = 7,
};

class UEditorEngine;
class UWorld;
class UGizmoComponent;
class FEditorSettings;
class FSelectionManager;
class FSceneViewport;
struct FGizmoDelta;
struct FEditorViewportState;

class FEditorViewportClient : public FViewportClient
{
public:
    void Initialize(FWindowsWindow* InWindow, UEditorEngine* InEditor);
    UWorld* GetFocusedWorld() const { return World; }
    void SetWorld(UWorld* InWorld);
    void StartPIE(UWorld* InWorld);
    void EndPIE(UWorld* InWorld);

    EViewportPlayState GetPlayState() const { return PlayState; }
    void SetPlayState(EViewportPlayState InState) { PlayState = InState; }

    void SaveCameraSnapshot();
    void RestoreCameraSnapshot();

    void SetGizmo(UGizmoComponent* InGizmo);
    void SetSettings(const FEditorSettings* InSettings);
    void SetSelectionManager(FSelectionManager* InSelectionManager);

    UGizmoComponent* GetGizmo() { return Gizmo; }
    FViewportInputRouter& GetInputRouter() { return InputRouter; }
    const FViewportInputRouter& GetInputRouter() const { return InputRouter; }

    void SetViewportSize(float InWidth, float InHeight) override;

    float GetMoveSpeed() { return InputRouter.GetEditorController().GetMoveSpeed(); }
    void SetMoveSpeed(float InSpeed) { InputRouter.GetEditorController().SetMoveSpeed(InSpeed); }
    void FocusSelection() { FocusPrimarySelection(); }

    void CreateCamera();
    void DestroyCamera();
    void ResetCamera();
    FViewportCamera* GetCamera() { return bHasCamera ? &Camera : nullptr; }
    const FViewportCamera* GetCamera() const { return bHasCamera ? &Camera : nullptr; }
    void SyncCameraTarget() { InputRouter.GetEditorController().ResetTargetLocation(); }

    void Tick(float DeltaTime) override;
    void BuildSceneView(FSceneView& OutView) const override;

    EEditorViewportType GetViewportType() const { return ViewportType; }
    void SetViewportType(EEditorViewportType InType) { ViewportType = InType; }

    FSceneViewport* GetViewport() { return Viewport; }
    const FSceneViewport* GetViewport() const { return Viewport; }
    void SetViewport(FSceneViewport* InViewport) { Viewport = InViewport; }

    FEditorViewportState* GetViewportState() { return State; }
    const FEditorViewportState* GetViewportState() const { return State; }
    void SetState(FEditorViewportState* InState) { State = InState; }

    void ApplyCameraMode();

    bool IsActiveOperation() const;
    bool IsBoxSelecting() const { return bBoxSelecting; }
    POINT GetBoxSelectStart() const { return BoxSelectStart; }
    POINT GetBoxSelectEnd() const { return BoxSelectEnd; }
    bool HasPendingActorPlacement() const { return bPendingActorPlacement; }
    const FVector& GetPendingActorPlacementLocation() const { return PendingActorPlacementLocation; }
    POINT GetPendingActorPlacementPopupPos() const { return PendingActorPlacementPopupPos; }
    void ClearPendingActorPlacement() { bPendingActorPlacement = false; }

    void SetEndPIECallback(std::function<void()> Callback) { InputRouter.GetPIEController().SetEndPIECallback(std::move(Callback)); }
    void ClearEndPIECallback() { InputRouter.GetPIEController().ClearEndPIECallback(); }

    bool RequestActorPlacement(float X, float Y, float PopupX, float PopupY);
    void DeleteSelectedActors();
    void DuplicateSelectedActors();
    void SelectAllActors();

private:
    void TickInteraction(float DeltaTime);
    void ApplyGizmoDeltaToSelection(const FGizmoDelta& Delta);
    void HandleBoxSelection();
    bool TryProjectWorldToViewport(const FVector& WorldPos, float& OutViewportX, float& OutViewportY, float& OutDepth) const;
    void FocusPrimarySelection();

private:
    UEditorEngine* Editor = nullptr;
    FSceneViewport* Viewport = nullptr;

    EEditorViewportType ViewportType = EVT_Perspective;
    FEditorViewportState* State = nullptr;

    UWorld* World = nullptr;
    UGizmoComponent* Gizmo = nullptr;
    const FEditorSettings* Settings = nullptr;
    FSelectionManager* SelectionManager = nullptr;

    FViewportCamera Camera;
    FViewportInputRouter InputRouter;
    bool bHasCamera = false;

    EViewportPlayState PlayState = EViewportPlayState::Editing;
    FCameraSnapshot SavedCamera;
    bool bHasCameraSnapshot = false;

    bool bBoxSelecting = false;
    POINT BoxSelectStart = { 0, 0 };
    POINT BoxSelectEnd = { 0, 0 };

    bool bPendingActorPlacement = false;
    FVector PendingActorPlacementLocation = FVector::ZeroVector;
    POINT PendingActorPlacementPopupPos = { 0, 0 };

    FWorldSpatialIndex::FPrimitiveFrustumQueryScratch FrustumQueryScratch;
};
