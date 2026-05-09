#pragma once

#include "Engine/Geometry/Transform.h"
#include "Component/ActorComponent.h"
#include "Math/Utils.h"

class AActor;

class USceneComponent : public UActorComponent
{
public:
    DECLARE_CLASS(USceneComponent, UActorComponent)

    USceneComponent();
    ~USceneComponent() override;

    virtual void PostDuplicate(UObject* Original) override;

    virtual void Serialize(FArchive& Ar) override;

    // Parent/child hierarchy management.
    void AttachToComponent(USceneComponent* InParent);
    void SetParent(USceneComponent* NewParent);
    USceneComponent* GetParent() const { return ParentComponent; }
    void AddChild(USceneComponent* NewChild);
    void RemoveChild(USceneComponent* Child);
    bool ContainsChild(const USceneComponent* Child) const;
    const TArray<USceneComponent*>& GetChildren() const { return ChildComponents; }

    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;

    virtual void UpdateWorldMatrix() const;
    virtual void AddWorldOffset(const FVector& WorldDelta);

    virtual void SetRelativeLocation(const FVector& NewLocation);
    virtual void SetRelativeRotation(const FVector& NewRotation);
    virtual void SetRelativeScale(const FVector& NewScale);
    void SetRelativeMatrix(const FMatrix& NewRelativeMatrix);

    void MarkTransformDirty();

    FTransform GetRelativeTransform() const;
    FTransform GetWorldTransform() const;
    const FMatrix& GetWorldMatrix() const;

    void SetWorldLocation(FVector NewWorldLocation);
    FVector GetWorldLocation() const;
    FVector GetWorldScale() const;
    FVector GetWorldAxisScale() const;

    FVector GetRelativeLocation() const { return RelativeLocation; }
    FVector GetRelativeRotation() const { return RelativeRotation; }
    FVector GetRelativeScale() const { return RelativeScale3D; }
    bool IsUsingCustomRelativeMatrix() const { return bUseCustomRelativeMatrix; }

    FVector GetForwardVector() const;
    FVector GetUpVector() const;
    FVector GetRightVector() const;

    FMatrix GetRelativeMatrix() const;

    void Move(const FVector& Delta);
    void MoveLocal(const FVector& Delta);

    // Keep existing input convention.
    // DeltaYaw: yaw in degrees around world-up (Z).
    // DeltaPitch: pitch in degrees around local-right (Y).
    void Rotate(float DeltaYaw, float DeltaPitch);

    FQuat GetRelativeQuat() const;
    void SetRelativeRotationQuat(const FQuat& NewRotationQuat);

protected:
    /** Hook fired when this component becomes transform-dirty. */
    virtual void OnTransformDirty() {}

    FRotator GetRelativeRotator() const;
    void SetRelativeRotationRotator(const FRotator& NewRotation);
    bool IsTransformDirty() const { return bTransformDirty; }

    // Apply yaw around world-up and pitch around local-right.
    void AddRelativeYaw(float DeltaYawDegrees);
    void AddRelativePitch(float DeltaPitchDegrees);

private:
    void SerializeParentReference(FArchive& Ar);
    void SerializeRelativeTransformState(FArchive& Ar);

    void ResetHierarchyForDuplicate();
    void DetachFromParent();
    void DetachAllChildren();

    void SyncQuatFromEulerCache();
    void SyncEulerCacheFromQuat();
    void ApplyRelativeQuat(const FQuat& NewRotationQuat);
    void ClearCustomRelativeMatrix();

protected:
    USceneComponent* ParentComponent = nullptr;
    TArray<USceneComponent*> ChildComponents;

    mutable FMatrix CachedWorldMatrix{};
    mutable FTransform CachedWorldTransform{};
    mutable bool bTransformDirty = true;

    FVector RelativeLocation{};
    // Euler cache for editor display and serialization (Roll, Pitch, Yaw in degrees).
    // RelativeRotationQuat is the authoritative rotation state.
    FVector RelativeRotation{};

    // Authoritative rotation storage using quaternion to avoid gimbal lock.
    FQuat RelativeRotationQuat = FQuat::Identity;
    FVector RelativeScale3D{ 1.0f, 1.0f, 1.0f };

    bool bUseCustomRelativeMatrix = false;
    FMatrix CustomRelativeMatrix = FMatrix::Identity;
};
