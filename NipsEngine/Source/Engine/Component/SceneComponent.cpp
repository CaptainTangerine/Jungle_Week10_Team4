#include "SceneComponent.h"

#include <algorithm>

#include "Object/ObjectFactory.h"

DEFINE_CLASS(USceneComponent, UActorComponent)
REGISTER_FACTORY(USceneComponent)

USceneComponent::USceneComponent()
{
    CachedWorldMatrix = FMatrix::Identity;
    CachedWorldTransform = FTransform::Identity;
    RelativeRotationQuat = FQuat::Identity;
    CustomRelativeMatrix = FMatrix::Identity;
    bUseCustomRelativeMatrix = false;
    bTransformDirty = true;
    UpdateWorldMatrix();
}

USceneComponent::~USceneComponent()
{
    DetachFromParent();
    DetachAllChildren();
}

void USceneComponent::PostDuplicate(UObject* Original)
{
    UActorComponent::PostDuplicate(Original);

    SetOwner(nullptr);
    ResetHierarchyForDuplicate();
}

void USceneComponent::Serialize(FArchive& Ar)
{
    UActorComponent::Serialize(Ar);
    SerializeParentReference(Ar);
    SerializeRelativeTransformState(Ar);
}

void USceneComponent::AttachToComponent(USceneComponent* InParent)
{
    if (InParent == nullptr || InParent == this)
    {
        return;
    }

    SetParent(InParent);
}

void USceneComponent::SetParent(USceneComponent* NewParent)
{
    if (NewParent == ParentComponent || NewParent == this)
    {
        return;
    }

    DetachFromParent();
    ParentComponent = NewParent;

    if (ParentComponent != nullptr && !ParentComponent->ContainsChild(this))
    {
        ParentComponent->ChildComponents.push_back(this);
    }

    MarkTransformDirty();
}

void USceneComponent::AddChild(USceneComponent* NewChild)
{
    if (NewChild == nullptr)
    {
        return;
    }

    NewChild->SetParent(this);
}

void USceneComponent::RemoveChild(USceneComponent* Child)
{
    if (Child == nullptr)
    {
        return;
    }

    auto Iter = std::find(ChildComponents.begin(), ChildComponents.end(), Child);
    if (Iter == ChildComponents.end())
    {
        return;
    }

    if ((*Iter)->ParentComponent == this)
    {
        (*Iter)->ParentComponent = nullptr;
        (*Iter)->MarkTransformDirty();
    }

    ChildComponents.erase(Iter);
}

bool USceneComponent::ContainsChild(const USceneComponent* Child) const
{
    if (Child == nullptr)
    {
        return false;
    }

    return std::find(ChildComponents.begin(), ChildComponents.end(), Child) != ChildComponents.end();
}

void USceneComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UActorComponent::GetEditableProperties(OutProps);
    OutProps.push_back({ "Location", EPropertyType::Vec3, &RelativeLocation, 0.0f, 0.0f, 0.1f });
    OutProps.push_back({ "Rotation", EPropertyType::Vec3, &RelativeRotation, 0.0f, 0.0f, 0.1f });
    OutProps.push_back({ "Scale", EPropertyType::Vec3, &RelativeScale3D, 0.0f, 0.0f, 0.1f });
}

void USceneComponent::PostEditProperty(const char* PropertyName)
{
    UActorComponent::PostEditProperty(PropertyName);
    ClearCustomRelativeMatrix();
    SyncQuatFromEulerCache();
    MarkTransformDirty();
}

FRotator USceneComponent::GetRelativeRotator() const
{
    FRotator Rot = RelativeRotationQuat.Rotator();
    Rot.Normalize();
    return Rot;
}

FQuat USceneComponent::GetRelativeQuat() const
{
    return RelativeRotationQuat;
}

void USceneComponent::SetRelativeRotationRotator(const FRotator& NewRotation)
{
    FRotator Normalized = NewRotation;
    Normalized.Normalize();

    if (MathUtil::Abs(Normalized.Roll) < 1e-6f)
    {
        Normalized.Roll = 0.0f;
    }

    ApplyRelativeQuat(FQuat(Normalized));
}

void USceneComponent::SetRelativeRotationQuat(const FQuat& NewRotationQuat)
{
    ApplyRelativeQuat(NewRotationQuat);
}

void USceneComponent::SetRelativeLocation(const FVector& NewLocation)
{
    ClearCustomRelativeMatrix();
    RelativeLocation = NewLocation;
    MarkTransformDirty();
}

void USceneComponent::SetRelativeRotation(const FVector& NewRotation)
{
    ClearCustomRelativeMatrix();
    RelativeRotation = NewRotation;
    SyncQuatFromEulerCache();
    SyncEulerCacheFromQuat();
    MarkTransformDirty();
}

void USceneComponent::SetRelativeScale(const FVector& NewScale)
{
    ClearCustomRelativeMatrix();
    RelativeScale3D = NewScale;
    MarkTransformDirty();
}

void USceneComponent::SetRelativeMatrix(const FMatrix& NewRelativeMatrix)
{
    CustomRelativeMatrix = NewRelativeMatrix;
    bUseCustomRelativeMatrix = true;

    FMatrix RotationMatrix;
    if (CustomRelativeMatrix.Decompose(RelativeLocation, RotationMatrix, RelativeScale3D))
    {
        RelativeRotationQuat = FQuat(RotationMatrix).GetNormalized();
        SyncEulerCacheFromQuat();
    }
    else
    {
        RelativeLocation = CustomRelativeMatrix.GetOrigin();
        RelativeRotation = FVector::ZeroVector;
        RelativeRotationQuat = FQuat::Identity;
        RelativeScale3D = FVector(1.0f, 1.0f, 1.0f);
    }

    MarkTransformDirty();
}

void USceneComponent::MarkTransformDirty()
{
    bTransformDirty = true;
    OnTransformDirty();

    for (auto* Child : ChildComponents)
    {
        if (Child != nullptr)
        {
            Child->MarkTransformDirty();
        }
    }
}

FTransform USceneComponent::GetRelativeTransform() const
{
    if (bUseCustomRelativeMatrix)
    {
        return FTransform(CustomRelativeMatrix);
    }

    return FTransform(RelativeRotationQuat, RelativeLocation, RelativeScale3D);
}

FMatrix USceneComponent::GetRelativeMatrix() const
{
    if (bUseCustomRelativeMatrix)
    {
        return CustomRelativeMatrix;
    }

    return GetRelativeTransform().ToMatrixWithScale();
}

void USceneComponent::UpdateWorldMatrix() const
{
    if (!bTransformDirty)
    {
        return;
    }

    const FMatrix RelativeMatrix = GetRelativeMatrix();

    if (ParentComponent != nullptr)
    {
        CachedWorldMatrix = RelativeMatrix * ParentComponent->GetWorldMatrix();
    }
    else
    {
        CachedWorldMatrix = RelativeMatrix;
    }

    CachedWorldTransform = FTransform(CachedWorldMatrix);
    bTransformDirty = false;
}

const FMatrix& USceneComponent::GetWorldMatrix() const
{
    if (IsTransformDirty())
    {
        UpdateWorldMatrix();
    }

    return CachedWorldMatrix;
}

FTransform USceneComponent::GetWorldTransform() const
{
    if (IsTransformDirty())
    {
        UpdateWorldMatrix();
    }

    return CachedWorldTransform;
}

void USceneComponent::SetWorldLocation(FVector NewWorldLocation)
{
    if (ParentComponent != nullptr)
    {
        const FMatrix ParentWorldInverse = ParentComponent->GetWorldMatrix().GetInverse();
        const FVector NewRelativeLocation = ParentWorldInverse.TransformPosition(NewWorldLocation);
        SetRelativeLocation(NewRelativeLocation);
        return;
    }

    SetRelativeLocation(NewWorldLocation);
}

FVector USceneComponent::GetWorldLocation() const
{
    return GetWorldMatrix().GetOrigin();
}

FVector USceneComponent::GetWorldScale() const
{
    return GetWorldAxisScale();
}

FVector USceneComponent::GetWorldAxisScale() const
{
    return GetWorldMatrix().GetScaleVector();
}

FVector USceneComponent::GetForwardVector() const
{
    return GetWorldMatrix().GetUnitAxis(EAxis::X);
}

FVector USceneComponent::GetRightVector() const
{
    return GetWorldMatrix().GetUnitAxis(EAxis::Y);
}

FVector USceneComponent::GetUpVector() const
{
    return GetWorldMatrix().GetUnitAxis(EAxis::Z);
}

void USceneComponent::Move(const FVector& Delta)
{
    SetRelativeLocation(RelativeLocation + Delta);
}

void USceneComponent::MoveLocal(const FVector& Delta)
{
    const FQuat LocalQuat = GetRelativeQuat();

    const FVector LocalOffset =
        LocalQuat.GetAxisX() * Delta.X +
        LocalQuat.GetAxisY() * Delta.Y +
        LocalQuat.GetAxisZ() * Delta.Z;

    SetRelativeLocation(RelativeLocation + LocalOffset);
}

void USceneComponent::AddWorldOffset(const FVector& WorldDelta)
{
    if (ParentComponent == nullptr)
    {
        SetRelativeLocation(RelativeLocation + WorldDelta);
        return;
    }

    const FMatrix ParentWorldInverse = ParentComponent->GetWorldMatrix().GetInverse();
    const FVector LocalDelta = ParentWorldInverse.TransformVector(WorldDelta);
    SetRelativeLocation(RelativeLocation + LocalDelta);
}

void USceneComponent::AddRelativeYaw(float DeltaYawDegrees)
{
    if (MathUtil::Abs(DeltaYawDegrees) < 1e-6f)
    {
        return;
    }

    const FVector ParentUpAxis(0.0f, 0.0f, 1.0f);

    const FQuat CurrentQuat = GetRelativeQuat();
    FQuat DeltaQuat(ParentUpAxis, MathUtil::DegreesToRadians(DeltaYawDegrees));

    FQuat ResultQuat = DeltaQuat * CurrentQuat;
    ResultQuat.Normalize();

    SetRelativeRotationQuat(ResultQuat);
}

void USceneComponent::AddRelativePitch(float DeltaPitchDegrees)
{
    if (MathUtil::Abs(DeltaPitchDegrees) < 1e-6f)
    {
        return;
    }

    const FQuat CurrentQuat = GetRelativeQuat();
    const FVector LocalRightAxis = CurrentQuat.GetAxisY();

    FQuat DeltaQuat(LocalRightAxis, MathUtil::DegreesToRadians(DeltaPitchDegrees));
    FQuat ResultQuat = DeltaQuat * CurrentQuat;
    ResultQuat.Normalize();

    FRotator ResultRot = ResultQuat.Rotator();
    ResultRot.Pitch = MathUtil::Clamp(ResultRot.Pitch, -89.9f, 89.9f);
    ResultRot.Roll = 0.0f;
    ResultRot.Normalize();

    SetRelativeRotationRotator(ResultRot);
}

void USceneComponent::Rotate(float DeltaYaw, float DeltaPitch)
{
    if (MathUtil::Abs(DeltaYaw) > 1e-6f)
    {
        AddRelativeYaw(DeltaYaw);
    }

    if (MathUtil::Abs(DeltaPitch) > 1e-6f)
    {
        AddRelativePitch(DeltaPitch);
    }
}

void USceneComponent::SerializeParentReference(FArchive& Ar)
{
    if (!Ar.IsSaving() || GetParent() == nullptr)
    {
        return;
    }

    uint32 ParentUUID = GetParent()->GetUUID();
    Ar << "ParentUUID" << ParentUUID;
}

void USceneComponent::SerializeRelativeTransformState(FArchive& Ar)
{
    Ar << "Location" << RelativeLocation;
    Ar << "Rotation" << RelativeRotation;
    Ar << "Scale" << RelativeScale3D;
    Ar << "UseCustomRelativeMatrix" << bUseCustomRelativeMatrix;
    if (bUseCustomRelativeMatrix)
    {
        Ar << "CustomRelativeMatrix" << CustomRelativeMatrix;
    }

    if (!Ar.IsLoading())
    {
        return;
    }

    SyncQuatFromEulerCache();
    if (bUseCustomRelativeMatrix)
    {
        SetRelativeMatrix(CustomRelativeMatrix);
        return;
    }

    MarkTransformDirty();
}

void USceneComponent::ResetHierarchyForDuplicate()
{
    bTransformDirty = true;
    ParentComponent = nullptr;
    ChildComponents.clear();
}

void USceneComponent::DetachFromParent()
{
    if (ParentComponent == nullptr)
    {
        return;
    }

    ParentComponent->RemoveChild(this);
    ParentComponent = nullptr;
}

void USceneComponent::DetachAllChildren()
{
    for (auto* Child : ChildComponents)
    {
        if (Child == nullptr)
        {
            continue;
        }

        Child->ParentComponent = nullptr;
        Child->MarkTransformDirty();
    }

    ChildComponents.clear();
}

void USceneComponent::SyncQuatFromEulerCache()
{
    RelativeRotationQuat = FQuat::MakeFromEuler(RelativeRotation);
    RelativeRotationQuat.Normalize();
}

void USceneComponent::SyncEulerCacheFromQuat()
{
    RelativeRotation = RelativeRotationQuat.Euler();
}

void USceneComponent::ApplyRelativeQuat(const FQuat& NewRotationQuat)
{
    ClearCustomRelativeMatrix();
    RelativeRotationQuat = NewRotationQuat.GetNormalized();
    SyncEulerCacheFromQuat();
    MarkTransformDirty();
}

void USceneComponent::ClearCustomRelativeMatrix()
{
    bUseCustomRelativeMatrix = false;
    CustomRelativeMatrix = FMatrix::Identity;
}
