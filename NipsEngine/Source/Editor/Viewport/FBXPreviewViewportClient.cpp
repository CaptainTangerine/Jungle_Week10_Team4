#include "Editor/Viewport/FBXPreviewViewportClient.h"

#include "Math/Rotator.h"
#include "Math/Quat.h"
#include "Runtime/SceneView.h"
#include "Engine/Slate/SlateUtils.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Math/Utils.h"

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
    OrbitTarget = FVector(0.f, 0.f, 1.f);
    Camera.SetLookAt(OrbitTarget);
    SyncAnglesFromCamera();
}

void FFBXPreviewViewportClient::SetViewportRect(int32 InX, int32 InY, int32 InWidth, int32 InHeight)
{
    ViewRect.X = InX;
    ViewRect.Y = InY;
    ViewRect.Width  = InWidth;
    ViewRect.Height = InHeight;

    if (InWidth > 0 && InHeight > 0)
    {
        WindowWidth = static_cast<float>(InWidth);
        WindowHeight = static_cast<float>(InHeight);
        Camera.OnResize(static_cast<uint32>(InWidth), static_cast<uint32>(InHeight));
    }
}

void FFBXPreviewViewportClient::Tick(float DeltaTime)
{
    InputSystem& Input = InputSystem::Get();
    const bool bAnyMouseButtonDown =
        Input.GetKey(VK_LBUTTON) || Input.GetKey(VK_RBUTTON) || Input.GetKey(VK_MBUTTON);

    if (bHovered &&
        (Input.GetKeyDown(VK_LBUTTON) || Input.GetKeyDown(VK_RBUTTON) || Input.GetKeyDown(VK_MBUTTON)))
    {
        bInputCaptured = true;
    }

    if (!bAnyMouseButtonDown)
    {
        bInputCaptured = false;
    }

    if (!bHovered && !bInputCaptured)
    {
        return;
    }

    if (bInputCaptured && Input.GetKey(VK_RBUTTON))
    {
        MoveCamera(DeltaTime);
    }

    const float DeltaX = static_cast<float>(Input.MouseDeltaX());
    const float DeltaY = static_cast<float>(Input.MouseDeltaY());
    const bool bAltDown = Input.GetKey(VK_MENU);

    if (bInputCaptured && Input.GetKeyDown(VK_RBUTTON))
    {
        SyncAnglesFromCamera();
    }

    if (bInputCaptured && Input.GetRightDragging() && !bAltDown)
    {
        constexpr float RotateSpeed = 0.15f;
        Yaw += DeltaX * RotateSpeed;
        Pitch -= DeltaY * RotateSpeed;
        Pitch = MathUtil::Clamp(Pitch, -89.0f, 89.0f);
        UpdateCameraRotation();
    }

    if (bInputCaptured && Input.GetLeftDragging() && bAltDown)
    {
        OrbitCamera(DeltaX, DeltaY);
    }

    if (bInputCaptured && Input.GetMiddleDragging() && !bAltDown)
    {
        PanCamera(DeltaX, DeltaY);
    }

    if (bHovered && !MathUtil::IsNearlyZero(Input.GetScrollNotches()))
    {
        ZoomCamera(Input.GetScrollNotches());
    }
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

void FFBXPreviewViewportClient::SyncAnglesFromCamera()
{
    const FVector Forward = Camera.GetForwardVector().GetSafeNormal();
    if (Forward.IsNearlyZero())
    {
        return;
    }

    Pitch = MathUtil::RadiansToDegrees(std::asin(MathUtil::Clamp(Forward.Z, -1.0f, 1.0f)));
    Yaw = MathUtil::RadiansToDegrees(std::atan2(Forward.Y, Forward.X));
}

void FFBXPreviewViewportClient::UpdateCameraRotation()
{
    const float PitchRadians = MathUtil::DegreesToRadians(Pitch);
    const float YawRadians = MathUtil::DegreesToRadians(Yaw);

    FVector Forward(
        std::cos(PitchRadians) * std::cos(YawRadians),
        std::cos(PitchRadians) * std::sin(YawRadians),
        std::sin(PitchRadians));
    Forward = Forward.GetSafeNormal();

    const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
    if (Right.IsNearlyZero())
    {
        return;
    }

    const FVector Up = FVector::CrossProduct(Forward, Right).GetSafeNormal();
    FMatrix RotationMatrix = FMatrix::Identity;
    RotationMatrix.SetAxes(Forward, Right, Up);

    FQuat Rotation(RotationMatrix);
    Rotation.Normalize();
    Camera.SetRotation(Rotation);
}

void FFBXPreviewViewportClient::OrbitCamera(float DeltaX, float DeltaY)
{
    constexpr float OrbitSpeed = 0.15f;
    Yaw += DeltaX * OrbitSpeed;
    Pitch -= DeltaY * OrbitSpeed;
    Pitch = MathUtil::Clamp(Pitch, -89.0f, 89.0f);

    const float Distance = std::max(FVector::Dist(Camera.GetLocation(), OrbitTarget), 0.1f);
    UpdateCameraRotation();
    Camera.SetLocation(OrbitTarget - Camera.GetForwardVector() * Distance);
    Camera.SetLookAt(OrbitTarget);
    SyncAnglesFromCamera();
}

void FFBXPreviewViewportClient::PanCamera(float DeltaX, float DeltaY)
{
    constexpr float PanSpeed = 0.01f;
    const float DistanceScale = std::max(FVector::Dist(Camera.GetLocation(), OrbitTarget), 1.0f);
    const float PanScale = PanSpeed * DistanceScale;

    const FVector Delta =
        Camera.GetRightVector() * (-DeltaX * PanScale) +
        Camera.GetUpVector() * (DeltaY * PanScale);

    Camera.SetLocation(Camera.GetLocation() + Delta);
    OrbitTarget += Delta;
}

void FFBXPreviewViewportClient::ZoomCamera(float Notches)
{
    constexpr float ZoomSpeed = 0.35f;
    const float DistanceScale = std::max(FVector::Dist(Camera.GetLocation(), OrbitTarget), 1.0f);
    const FVector Delta = Camera.GetForwardVector() * (Notches * ZoomSpeed * DistanceScale);

    Camera.SetLocation(Camera.GetLocation() + Delta);
}

void FFBXPreviewViewportClient::MoveCamera(float DeltaTime)
{
    const InputSystem& Input = InputSystem::Get();
    if (Input.GetKey(VK_CONTROL) || Input.GetKey(VK_MENU) || Input.GetKey(VK_SHIFT))
    {
        return;
    }

    FVector Move = FVector::ZeroVector;
    const float MoveSpeed = std::max(FVector::Dist(Camera.GetLocation(), OrbitTarget), 1.0f) * 2.5f;

    if (Input.GetKey('W'))
    {
        Move += Camera.GetForwardVector();
    }
    if (Input.GetKey('S'))
    {
        Move -= Camera.GetForwardVector();
    }
    if (Input.GetKey('D'))
    {
        Move += Camera.GetRightVector();
    }
    if (Input.GetKey('A'))
    {
        Move -= Camera.GetRightVector();
    }
    if (Input.GetKey('E'))
    {
        Move += FVector::UpVector;
    }
    if (Input.GetKey('Q'))
    {
        Move -= FVector::UpVector;
    }

    if (Move.IsNearlyZero())
    {
        return;
    }

    const FVector Delta = Move.GetSafeNormal() * MoveSpeed * DeltaTime;
    Camera.SetLocation(Camera.GetLocation() + Delta);
    OrbitTarget += Delta;
}
