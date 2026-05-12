#include "Editor/Viewport/FBXPreviewViewportClient.h"

#include "Math/Rotator.h"
#include "Math/Quat.h"
#include "Runtime/SceneView.h"
#include "Engine/Slate/SlateUtils.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/Math/Utils.h"
#include "Render/LineBatcher.h"

#include <algorithm>
#include <cmath>

#include "Component/GizmoComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Object/Object.h"

namespace
{
    void AddBonePyramid(FLineBatcher& LineBatcher, const FVector& Start, const FVector& End, const FVector4& Color)
    {
        const FVector Segment = End - Start;
        const float SegmentLength = Segment.Size();
        if (SegmentLength <= 1.0e-4f)
        {
            return;
        }

        const FVector BoneDirection = Segment / SegmentLength;
        FVector BaseAxisA = FVector::ZeroVector;
        FVector BaseAxisB = FVector::ZeroVector;
        BoneDirection.FindBestAxisVectors(BaseAxisA, BaseAxisB);

        const float BaseRadius = std::max(SegmentLength * 0.08f, 0.01f);
        const float BaseOffset = std::min(SegmentLength * 0.18f, BaseRadius * 4.0f);
        const FVector BaseCenter = Start + BoneDirection * BaseOffset;

        constexpr float TriangleAngle0 = 0.0f;
        constexpr float TriangleAngle1 = MathUtil::TwoPi / 3.0f;
        constexpr float TriangleAngle2 = MathUtil::TwoPi * 2.0f / 3.0f;

        const auto MakeBasePoint = [&](float Angle)
        {
            return BaseCenter +
                (BaseAxisA * std::cos(Angle) + BaseAxisB * std::sin(Angle)) * BaseRadius;
        };

        const FVector Base0 = MakeBasePoint(TriangleAngle0);
        const FVector Base1 = MakeBasePoint(TriangleAngle1);
        const FVector Base2 = MakeBasePoint(TriangleAngle2);

        LineBatcher.AddLine(Start, Base0, Color);
        LineBatcher.AddLine(Start, Base1, Color);
        LineBatcher.AddLine(Start, Base2, Color);

        LineBatcher.AddLine(Base0, Base1, Color);
        LineBatcher.AddLine(Base1, Base2, Color);
        LineBatcher.AddLine(Base2, Base0, Color);

        LineBatcher.AddLine(Base0, End, Color);
        LineBatcher.AddLine(Base1, End, Color);
        LineBatcher.AddLine(Base2, End, Color);
    }
}

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

    if (bHovered && PreviewGizmo && Input.GetKeyDown(VK_SPACE))
    {
        PreviewGizmo->SetNextMode();
    }

    if (bInputCaptured && Input.GetKey(VK_RBUTTON))
    {
        MoveCamera(DeltaTime);
    }

    const float DeltaX = static_cast<float>(Input.MouseDeltaX());
    const float DeltaY = static_cast<float>(Input.MouseDeltaY());
    const bool bAltDown = Input.GetKey(VK_MENU);

    TickPreviewGizmoInteraction();
    if (PreviewGizmo)
    {
        const FGizmoDelta GizmoDelta = PreviewGizmo->ConsumePendingDelta();
        if (GizmoDelta.Mode != EGizmoMode::End)
        {
            ApplyPreviewGizmoDelta(GizmoDelta);
        }
    }

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

void FFBXPreviewViewportClient::CreatePreviewGizmo()
{
    if (PreviewGizmo) return;
    
    PreviewGizmo = UObjectManager::Get().CreateObject<UGizmoComponent>();
    PreviewGizmo->Deactivate();
}

void FFBXPreviewViewportClient::DestroyPreviewGizmo()
{
    if (!PreviewGizmo) return;
    
    UObjectManager::Get().DestroyObject(PreviewGizmo);
    PreviewGizmo = nullptr;
}

void FFBXPreviewViewportClient::ClearBoneSelection()
{
    SelectedBoneIndex = -1;
    SelectedSkinnedMeshComponent = nullptr;
    
    if (PreviewGizmo)
    {
        PreviewGizmo->Deactivate();
    }
}

void FFBXPreviewViewportClient::SelectBone(USkinnedMeshComponent* InSkinnedMesh, int32 InBoneIndex)
{
    SelectedSkinnedMeshComponent = InSkinnedMesh;
    SelectedBoneIndex = InBoneIndex;
    
    if (!PreviewGizmo) CreatePreviewGizmo();
    
    if (!SelectedSkinnedMeshComponent || SelectedBoneIndex<0)
    {
        ClearBoneSelection();
        return;
    }
    
    FMatrix BoneMeshTransform = FMatrix::Identity;
    if (!SelectedSkinnedMeshComponent->GetCurrentBoneGlobalMeshTransform(SelectedBoneIndex,BoneMeshTransform))
    {
        ClearBoneSelection();
        return;
    }
    
    const FMatrix BoneWorldTransform = BoneMeshTransform * SelectedSkinnedMeshComponent->GetWorldMatrix();
    
    const FVector BoneWorldLocation = BoneWorldTransform.TransformPosition(FVector::ZeroVector);
    
    if (PreviewGizmo)
    {
        PreviewGizmo->ShowAtLocation(BoneWorldLocation);
        PreviewGizmo->ApplyScreenSpaceScaling(Camera.GetLocation());
    }
}

void FFBXPreviewViewportClient::SetPreviewGizmoMode(EGizmoMode NewMode)
{
    if (!PreviewGizmo)
    {
        return;
    }

    PreviewGizmo->UpdateGizmoMode(NewMode);
    SyncPreviewGizmoToSelectedBone();
}

void FFBXPreviewViewportClient::AddSelectedBoneDebugLines(FLineBatcher& LineBatcher) const
{
    if (!SelectedSkinnedMeshComponent || SelectedBoneIndex < 0)
    {
        return;
    }

    USkeletalMesh* SkeletalMesh = SelectedSkinnedMeshComponent->GetSkeletalMesh();
    if (!SkeletalMesh)
    {
        return;
    }

    const TArray<FBoneInfo>& Bones = SkeletalMesh->GetBones();
    if (SelectedBoneIndex >= static_cast<int32>(Bones.size()))
    {
        return;
    }

    auto GetBoneWorldLocation = [this](int32 BoneIndex, FVector& OutWorldLocation) -> bool
    {
        FMatrix BoneLocalToMesh = FMatrix::Identity;
        if (!SelectedSkinnedMeshComponent->GetCurrentBoneGlobalMeshTransform(BoneIndex, BoneLocalToMesh))
        {
            return false;
        }

        const FMatrix BoneLocalToWorld = BoneLocalToMesh * SelectedSkinnedMeshComponent->GetWorldMatrix();
        OutWorldLocation = BoneLocalToWorld.TransformPosition(FVector::ZeroVector);
        return true;
    };

    FVector SelectedBoneWorldLocation = FVector::ZeroVector;
    if (!GetBoneWorldLocation(SelectedBoneIndex, SelectedBoneWorldLocation))
    {
        return;
    }

    const FVector4 ParentLineColor = FVector4(1.0f, 0.82f, 0.18f, 1.0f);
    const FVector4 ChildLineColor = FVector4(0.15f, 0.72f, 1.0f, 1.0f);

    const int32 ParentBoneIndex = Bones[SelectedBoneIndex].ParentIndex;
    if (ParentBoneIndex >= 0 && ParentBoneIndex < static_cast<int32>(Bones.size()))
    {
        FVector ParentBoneWorldLocation = FVector::ZeroVector;
        if (GetBoneWorldLocation(ParentBoneIndex, ParentBoneWorldLocation))
        {
            AddBonePyramid(LineBatcher, ParentBoneWorldLocation, SelectedBoneWorldLocation, ParentLineColor);
        }
    }

    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Bones.size()); ++BoneIndex)
    {
        if (Bones[BoneIndex].ParentIndex != SelectedBoneIndex)
        {
            continue;
        }

        FVector ChildBoneWorldLocation = FVector::ZeroVector;
        if (GetBoneWorldLocation(BoneIndex, ChildBoneWorldLocation))
        {
            AddBonePyramid(LineBatcher, SelectedBoneWorldLocation, ChildBoneWorldLocation, ChildLineColor);
        }
    }
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

void FFBXPreviewViewportClient::TickPreviewGizmoInteraction()
{
    if (!PreviewGizmo || !PreviewGizmo->IsVisible() || (!bHovered && !bInputCaptured))
    {
        return;
    }

    InputSystem& Input = InputSystem::Get();
    if (Input.GetKey(VK_MENU))
    {
        return;
    }

    POINT MousePoint = Input.GetMousePos();
    if (Window)
    {
        MousePoint = Window->ScreenToClientPoint(MousePoint);
    }

    const float LocalX = static_cast<float>(MousePoint.x - ViewRect.X);
    const float LocalY = static_cast<float>(MousePoint.y - ViewRect.Y);
    const FRay Ray = Camera.DeprojectScreenToWorld(LocalX, LocalY, WindowWidth, WindowHeight);

    if (bHovered && Input.MouseMoved())
    {
        FHitResult HoverHit{};
        PreviewGizmo->RaycastMesh(Ray, HoverHit);
    }

    if (bHovered && Input.GetKeyDown(VK_LBUTTON))
    {
        FHitResult HitResult{};
        PreviewGizmo->SetPressedOnHandle(PreviewGizmo->RaycastMesh(Ray, HitResult));
    }

    if (Input.GetLeftDragging())
    {
        if (PreviewGizmo->IsPressedOnHandle() && !PreviewGizmo->IsHolding())
        {
            PreviewGizmo->SetHolding(true);
        }

        if (PreviewGizmo->IsHolding())
        {
            PreviewGizmo->UpdateDrag(Ray);
        }
    }

    if (Input.GetLeftDragEnd() || Input.GetKeyUp(VK_LBUTTON))
    {
        PreviewGizmo->DragEnd();
    }
}

void FFBXPreviewViewportClient::ApplyPreviewGizmoDelta(const FGizmoDelta& Delta)
{
    switch (Delta.Mode)
    {
    case EGizmoMode::Translate:
        ApplySelectedBoneTranslation(Delta.WorldDelta);
        break;
    case EGizmoMode::Rotate:
        ApplySelectedBoneRotation(Delta.Axis, Delta.Amount);
        break;
    case EGizmoMode::Scale:
        ApplySelectedBoneScale(Delta.AxisIdx, Delta.Amount);
        break;
    default:
        break;
    }

    SyncPreviewGizmoToSelectedBone();
}

void FFBXPreviewViewportClient::ApplySelectedBoneTranslation(const FVector& WorldDelta)
{
    if (!SelectedSkinnedMeshComponent || SelectedBoneIndex < 0)
    {
        return;
    }

    FMatrix BoneLocal = FMatrix::Identity;
    if (!SelectedSkinnedMeshComponent->GetCurrentBoneLocalTransform(SelectedBoneIndex, BoneLocal))
    {
        return;
    }

    FVector MeshSpaceDelta =
        SelectedSkinnedMeshComponent->GetWorldMatrix().GetInverse().TransformVector(WorldDelta);
    FVector ParentBoneSpaceDelta = MeshSpaceDelta;

    if (USkeletalMesh* SkeletalMesh = SelectedSkinnedMeshComponent->GetSkeletalMesh())
    {
        const TArray<FBoneInfo>& Bones = SkeletalMesh->GetBones();
        if (SelectedBoneIndex >= 0 && SelectedBoneIndex < static_cast<int32>(Bones.size()))
        {
            const int32 ParentIndex = Bones[SelectedBoneIndex].ParentIndex;
            if (ParentIndex >= 0 && ParentIndex < static_cast<int32>(Bones.size()))
            {
                FMatrix ParentBoneLocalToMesh = FMatrix::Identity;
                if (SelectedSkinnedMeshComponent->GetCurrentBoneGlobalMeshTransform(ParentIndex, ParentBoneLocalToMesh))
                {
                    ParentBoneSpaceDelta = ParentBoneLocalToMesh.GetInverse().TransformVector(MeshSpaceDelta);
                }
            }
        }
    }

    BoneLocal.SetTranslation(BoneLocal.GetTranslation() + ParentBoneSpaceDelta);
    SelectedSkinnedMeshComponent->SetCurrentBoneLocalTransform(SelectedBoneIndex, BoneLocal);
}

void FFBXPreviewViewportClient::ApplySelectedBoneRotation(const FVector& WorldAxis, float Angle)
{
    if (!SelectedSkinnedMeshComponent || SelectedBoneIndex < 0)
    {
        return;
    }

    FMatrix BoneLocal = FMatrix::Identity;
    if (!SelectedSkinnedMeshComponent->GetCurrentBoneLocalTransform(SelectedBoneIndex, BoneLocal))
    {
        return;
    }

    FVector MeshSpaceAxis = SelectedSkinnedMeshComponent->GetWorldMatrix().GetInverse().TransformVector(WorldAxis);
    FVector ParentBoneSpaceAxis = MeshSpaceAxis;

    if (USkeletalMesh* SkeletalMesh = SelectedSkinnedMeshComponent->GetSkeletalMesh())
    {
        const TArray<FBoneInfo>& Bones = SkeletalMesh->GetBones();
        if (SelectedBoneIndex >= 0 && SelectedBoneIndex < static_cast<int32>(Bones.size()))
        {
            const int32 ParentIndex = Bones[SelectedBoneIndex].ParentIndex;
            if (ParentIndex >= 0 && ParentIndex < static_cast<int32>(Bones.size()))
            {
                FMatrix ParentBoneLocalToMesh = FMatrix::Identity;
                if (SelectedSkinnedMeshComponent->GetCurrentBoneGlobalMeshTransform(ParentIndex, ParentBoneLocalToMesh))
                {
                    ParentBoneSpaceAxis = ParentBoneLocalToMesh.GetInverse().TransformVector(MeshSpaceAxis);
                }
            }
        }
    }

    BoneLocal = BoneLocal * FMatrix::MakeRotationAxis(ParentBoneSpaceAxis, Angle);
    SelectedSkinnedMeshComponent->SetCurrentBoneLocalTransform(SelectedBoneIndex, BoneLocal);
}

void FFBXPreviewViewportClient::ApplySelectedBoneScale(int32 AxisIndex, float ScaleDelta)
{
    if (!SelectedSkinnedMeshComponent || SelectedBoneIndex < 0)
    {
        return;
    }

    FMatrix BoneLocal = FMatrix::Identity;
    if (!SelectedSkinnedMeshComponent->GetCurrentBoneLocalTransform(SelectedBoneIndex, BoneLocal))
    {
        return;
    }

    const float ScaleValue = std::max(1.0f + ScaleDelta, 0.001f);
    FVector Scale(1.0f, 1.0f, 1.0f);
    switch (AxisIndex)
    {
    case 0:
        Scale.X = ScaleValue;
        break;
    case 1:
        Scale.Y = ScaleValue;
        break;
    case 2:
        Scale.Z = ScaleValue;
        break;
    default:
        return;
    }

    BoneLocal = BoneLocal * FMatrix::MakeScale(Scale);
    SelectedSkinnedMeshComponent->SetCurrentBoneLocalTransform(SelectedBoneIndex, BoneLocal);
}

void FFBXPreviewViewportClient::SyncPreviewGizmoToSelectedBone()
{
    if (!PreviewGizmo || !SelectedSkinnedMeshComponent || SelectedBoneIndex < 0)
    {
        return;
    }

    FMatrix BoneMeshTransform = FMatrix::Identity;
    if (!SelectedSkinnedMeshComponent->GetCurrentBoneGlobalMeshTransform(SelectedBoneIndex, BoneMeshTransform))
    {
        return;
    }

    const FMatrix BoneWorldTransform = BoneMeshTransform * SelectedSkinnedMeshComponent->GetWorldMatrix();
    PreviewGizmo->SetWorldLocation(BoneWorldTransform.TransformPosition(FVector::ZeroVector));
    PreviewGizmo->SetVisibility(true);
    PreviewGizmo->ApplyScreenSpaceScaling(Camera.GetLocation());
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
