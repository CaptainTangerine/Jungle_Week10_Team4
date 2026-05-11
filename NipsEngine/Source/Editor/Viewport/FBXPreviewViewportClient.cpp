#include "Editor/Viewport/FBXPreviewViewportClient.h"

#include "Math/Rotator.h"
#include "Math/Quat.h"
#include "Runtime/SceneView.h"
#include "Engine/Slate/SlateUtils.h"

#include <algorithm>
#include <cmath>


void FFBXPreviewViewportClient::Initialize(FWindowsWindow* InWindow)
{
    FViewportClient::Initialize(InWindow);
    
    Camera.SetProjectionType(EViewportProjectionType::Perspective);
    Camera.SetNearPlane(0.1f);
    Camera.SetFarPlane(1000.0f);
    Camera.SetFOV(MathUtil::PI * 60.0f / 180.0f);

    // 정면(+X → -X 방향)에서 원점을 바라보는 위치, 약간 위에서 내려다봄
    Camera.SetLocation(FVector(0.f, 2.5f, 2.f));
    Camera.SetLookAt(FVector(0.f, 0.f, 1.f));
}

void FFBXPreviewViewportClient::SetViewportRect(int32 InWidth, int32 InHeight)
{
    ViewRect.X = 0;
    ViewRect.Y = 0;
    ViewRect.Width  = InWidth;
    ViewRect.Height = InHeight;
    Camera.OnResize(static_cast<uint32>(InWidth), static_cast<uint32>(InHeight));
}

void FFBXPreviewViewportClient::Tick(float DeltaTime)
{
    (void)DeltaTime;
}

void FFBXPreviewViewportClient::BuildSceneView(FSceneView& OutView) const
{
    OutView.ViewMatrix           = Camera.GetViewMatrix();
    OutView.ProjectionMatrix     = Camera.GetProjectionMatrix();
    OutView.ViewProjectionMatrix = OutView.ViewMatrix * OutView.ProjectionMatrix;

    OutView.CameraPosition = Camera.GetLocation();
    OutView.CameraForward  = Camera.GetForwardVector();
    OutView.CameraRight    = Camera.GetRightVector();
    OutView.CameraUp       = Camera.GetUpVector();

    OutView.NearPlane        = Camera.GetNearPlane();
    OutView.FarPlane         = Camera.GetFarPlane();
    OutView.bOrthographic    = false;
    OutView.CameraOrthoHeight = Camera.GetOrthoHeight();
    OutView.CameraFrustum    = Camera.GetFrustum();
    OutView.ViewRect         = ViewRect;
    OutView.ViewMode         = EViewMode::Unlit;
}
