#pragma once

#include "PrimitiveComponent.h"
#include "Core/CoreTypes.h"
#include "Core/Containers/Array.h"

class AActor;
class UMaterialInterface;
struct FMeshData;

enum EGizmoMode
{
    Translate,
    Rotate,
    Scale,
    End
};

struct FGizmoDelta
{
    EGizmoMode Mode = EGizmoMode::End;

    int32 AxisIdx = -1;
    FVector Axis = FVector::ZeroVector;

    //Delta Direction
    FVector WorldDelta = FVector::ZeroVector;
    //Delta Amount
    float Amount = 0.f;
};

class UGizmoComponent : public UPrimitiveComponent
{
private:

    AActor* TargetActor = nullptr;
    const TArray<AActor*>* AllSelectedActors = nullptr;
    EGizmoMode CurMode = EGizmoMode::Translate;
    FVector LastIntersectionLocation;
    const float AxisLength = 1.0f;
    float Radius = 0.1f;
    const float ScaleSensitivity = 1.0f;
    int32 SelectedAxis = -1;
    bool bIsFirstFrameOfDrag = true;
    bool bIsHolding = false;
    bool bIsWorldSpace = true;
    bool bPressedOnHandle = false;

    FGizmoDelta PendingDelta;

    bool IntersectRayAxis(const FRay& Ray, FVector AxisEnd, float& OutRayT);
    const FMeshData* GetActiveMeshData() const;

    // Drag delta commit handlers.
    void CommitDelta(float DragAmount);
    void CommitTranslateDelta(float DragAmount);
    void CommitRotateDelta(float DragAmount);
    void CommitScaleDelta(float DragAmount);

    void UpdateLinearDrag(const FRay& Ray);
    void UpdateAngularDrag(const FRay& Ray);


public:
    DECLARE_CLASS(UGizmoComponent, UPrimitiveComponent)
    UGizmoComponent();

    // Gizmo components are editor-only and are not duplicated.
    virtual UGizmoComponent* Duplicate() override { return nullptr; }

    void UpdateWorldAABB() const override;
    bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;

    FVector GetVectorForAxis(int32 Axis);
    void RenderGizmo() {}
    void SetTarget(AActor* NewTarget);
    void SetSelectedActors(const TArray<AActor*>* InSelectedActors) { AllSelectedActors = InSelectedActors; }
    inline void SetHolding(bool bHold) { bIsHolding = bHold; }
    inline bool IsHolding() const { return bIsHolding; }
    inline bool IsHovered() const { return SelectedAxis != -1; }
    inline bool HasTarget() const { return TargetActor != nullptr; }
    inline AActor* GetTarget() const { return TargetActor; }
    inline int32 GetSelectedAxis() const { return SelectedAxis; }

    inline void SetPressedOnHandle(bool bPressed) { bPressedOnHandle = bPressed; }
    inline bool IsPressedOnHandle() const { return bPressedOnHandle; }

    void UpdateHoveredAxis(int Index);
    void UpdateDrag(const FRay& Ray);
    void DragEnd();

    void SetTargetLocation(FVector NewLocation);
    void SetTargetRotation(FVector NewRotation);
    void SetTargetScale(FVector NewScale);

    void SetNextMode();
    void UpdateGizmoMode(EGizmoMode NewMode);
    EGizmoMode GetGizmoMode() const { return CurMode; }
    inline void SetTranslateMode() { UpdateGizmoMode(EGizmoMode::Translate); }
    inline void SetRotateMode() { UpdateGizmoMode(EGizmoMode::Rotate); }
    inline void SetScaleMode() { UpdateGizmoMode(EGizmoMode::Scale); }
    void UpdateGizmoTransform();
    void ApplyScreenSpaceScaling(const FVector& CameraLocation);

    // Ortho-only scaling based on viewport OrthoHeight.
    void ApplyScreenSpaceScalingOrtho(float OrthoHeight);
    void SetWorldSpace(bool bWorldSpace);
    bool IsWorldSpace() const { return bIsWorldSpace; }
    void Deactivate() override;

    EPrimitiveType GetPrimitiveType() const override;

    UMaterialInterface* GetMaterial() { return Material; }

    //Skeletal Mesh Preview Related
    void ShowAtLocation(const FVector& Location);
    FGizmoDelta ConsumePendingDelta();

private:
    const FMeshData* GizmoMeshData = nullptr;
    UMaterialInterface* Material = nullptr;
    
    FVector LocalExtents = FVector(1.5f, 1.5f, 1.5f);
};
