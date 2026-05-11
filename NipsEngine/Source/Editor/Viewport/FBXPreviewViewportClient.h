#pragma once

#include "Runtime/ViewportClient.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Runtime/ViewportRect.h"

class  UWorld;
struct FSceneView;
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

private:
    void SyncAnglesFromCamera();
    void UpdateCameraRotation();
    void OrbitCamera(float DeltaX, float DeltaY);
    void PanCamera(float DeltaX, float DeltaY);
    void ZoomCamera(float Notches);
    void MoveCamera(float DeltaTime);

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

};
