#pragma once

#include "Runtime/ViewportClient.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Runtime/ViewportRect.h"
#include "Engine/Component/GizmoComponent.h"

class USkinnedMeshComponent;
class  UWorld;
class FLineBatcher;
struct FSceneView;
struct FGizmoDelta;
struct FViewportMouseEvent;

class FFBXPreviewViewportClient : public FViewportClient
{
public:
    void Initialize(FWindowsWindow* InWindow) override;

    void SetWorld(UWorld* InWorld) { PreviewWorld = InWorld; }
    UWorld* GetWorld() const { return PreviewWorld; }

    void SetViewportRect(int32 InX, int32 InY, int32 InWidth, int32 InHeight);
    void SetHovered(bool bInHovered) { bHovered = bInHovered; }
    bool IsHovered() const { return bHovered; }
    const FViewportRect& GetViewportRect() const { return ViewRect; }

    FViewportCamera& GetCamera() { return Camera; }
    const FViewportCamera& GetCamera() const { return Camera; }

    bool GetShowGrid() const { return bShowGrid; }
    bool GetShowAxis() const { return bShowAxis; }
    void SetShowGrid(bool bIn) { bShowGrid = bIn; }
    void SetShowAxis(bool bIn) { bShowAxis = bIn; }

    void Tick(float DeltaTime) override;
    void BuildSceneView(FSceneView& OutView) const override;

    //For Preview Gizmo
    void CreatePreviewGizmo();
    void DestroyPreviewGizmo();

    void ClearBoneSelection();
    void SelectBone(USkinnedMeshComponent* InSkinnedMesh, int32 InBoneIndex);
    UGizmoComponent* GetPreviewGizmo() const { return PreviewGizmo; }
    void SetPreviewGizmoMode(EGizmoMode NewMode);
    void AddSelectedBoneDebugLines(FLineBatcher& LineBatcher) const;

private:
    void SyncAnglesFromCamera();
    void UpdateCameraRotation();
    void OrbitCamera(float DeltaX, float DeltaY);
    void PanCamera(float DeltaX, float DeltaY);
    void ZoomCamera(float Notches);
    void MoveCamera(float DeltaTime);
    void TickPreviewGizmoInteraction();
    void ApplyPreviewGizmoDelta(const FGizmoDelta& Delta);
    void ApplySelectedBoneTranslation(const FVector& WorldDelta);
    void ApplySelectedBoneRotation(const FVector& WorldAxis, float Angle);
    void ApplySelectedBoneScale(int32 AxisIndex, float ScaleDelta);
    void SyncPreviewGizmoToSelectedBone();

private:
    UWorld* PreviewWorld = nullptr;
    bool bHovered = false;
    bool bInputCaptured = false;
    float Yaw = 0.0f;
    float Pitch = 0.0f;
    FVector OrbitTarget = FVector(0.0f, 0.0f, 1.0f);
    FViewportCamera Camera;
    FViewportRect ViewRect;

    bool bShowGrid = true;
    bool bShowAxis = false;

    //For Gizmo
    UGizmoComponent* PreviewGizmo = nullptr;
    USkinnedMeshComponent* SelectedSkinnedMeshComponent = nullptr;
    int32 SelectedBoneIndex = -1;

};
