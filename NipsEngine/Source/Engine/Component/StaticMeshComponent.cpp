#include "StaticMeshComponent.h"

#include <algorithm>
#include <cfloat>

#include "Component/SkinnedMeshComponent.h"
#include "Core/ResourceManager.h"

DEFINE_CLASS(UStaticMeshComponent, UMeshComponent)
REGISTER_FACTORY(UStaticMeshComponent)

UStaticMeshComponent::UStaticMeshComponent()
{
    //	기본 도형은 Cube로 설정
    SetStaticMesh(FResourceManager::Get().LoadStaticMesh("Asset/Mesh/cube.obj"));
}

void UStaticMeshComponent::PostDuplicate(UObject* Original)
{
    UMeshComponent::PostDuplicate(Original);

    const UStaticMeshComponent* Orig = Cast<UStaticMeshComponent>(Original);
    StaticMeshAsset = Orig->StaticMeshAsset;
    StaticMeshAssetPath = Orig->StaticMeshAssetPath;
    bNormalizeOnImport = Orig->bNormalizeOnImport;
    ClearBoneAttachment();
    bBoundsDirty = true;
    bRenderStateDirty = true;

    Materials = TArray<UMaterialInterface*>(Orig->Materials.size());
    for (int32 i = 0; i < static_cast<int32>(Orig->Materials.size()); ++i)
    {
        if (UMaterialInstance* OrigMatInst = Cast<UMaterialInstance>(Orig->Materials[i]))
        {
            UMaterialInstance* MatInst = UMaterialInstance::CreateTransient(OrigMatInst->Parent);
            MatInst->OverridedParams = OrigMatInst->OverridedParams;
            if (OrigMatInst->HasLightingModelOverride())
            {
                MatInst->SetLightingModelOverride(OrigMatInst->GetLightingModelOverride());
            }
            else
            {
                MatInst->ClearLightingModelOverride();
            }
            Materials[i] = MatInst;
        }
        else
        {
            Materials[i] = Orig->Materials[i];
        }
    }
}

void UStaticMeshComponent::Serialize(FArchive& Ar)
{
    UMeshComponent::Serialize(Ar);
    SerializeStaticMeshAsset(Ar);

    if (Ar.IsLoading())
    {
        ClearBoneAttachment();
    }
}

void UStaticMeshComponent::SetStaticMesh(UStaticMesh* InStaticMesh)
{
    if (StaticMeshAsset == InStaticMesh)
    {
        return;
    }

    StaticMeshAsset = InStaticMesh;
    ReleaseOwnedMaterialInstances();
    Materials.clear();

    if (StaticMeshAsset != nullptr)
    {
        StaticMeshAssetPath = StaticMeshAsset->GetAssetPathFileName();

        const auto& Slots = StaticMeshAsset->GetMaterialSlots();
        const auto& Sections = StaticMeshAsset->GetSections();
        Materials.reserve(Sections.size());

        for (int32 i = 0; i < static_cast<int32>(Sections.size()); ++i)
        {
            Materials.push_back(Slots[Sections[i].MaterialSlotIndex].Material);
        }
    }
    else
    {
        StaticMeshAssetPath.clear();
    }

    MarkBoundsDirty();
    MarkRenderStateDirty();
}

UStaticMesh* UStaticMeshComponent::GetStaticMesh() const
{
    return StaticMeshAsset;
}

bool UStaticMeshComponent::HasValidMesh() const
{
    return StaticMeshAsset != nullptr && StaticMeshAsset->HasValidMeshData();
}

bool UStaticMeshComponent::ImportStaticMeshFromFBX(const FString& FilePath)
{
    if (FilePath.empty())
    {
        return false;
    }

    const FString PreviousPath = StaticMeshAssetPath;
    UStaticMesh* Mesh = FResourceManager::Get().LoadStaticMesh(FilePath, bNormalizeOnImport);
    if (Mesh == nullptr)
    {
        StaticMeshAssetPath = PreviousPath;
        return false;
    }

    SetStaticMesh(Mesh);
    StaticMeshAssetPath = FilePath;
    return true;
}

void UStaticMeshComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UMeshComponent::GetEditableProperties(OutProps);
    OutProps.push_back({ "StaticMesh", EPropertyType::String, &StaticMeshAssetPath });
    OutProps.push_back({ "Normalize On Import", EPropertyType::Bool, &bNormalizeOnImport });
}

void UStaticMeshComponent::PostEditProperty(const char* PropertyName)
{
    UMeshComponent::PostEditProperty(PropertyName);
    ApplyPropertyEdit(PropertyName);
}

void UStaticMeshComponent::UpdateWorldAABB() const
{
    WorldAABB.Reset();

    if (!HasValidMesh())
    {
        bBoundsDirty = false;
        return;
    }

    const FAABB& LocalBounds = StaticMeshAsset->GetLocalBounds();
    if (!LocalBounds.IsValid())
    {
        bBoundsDirty = false;
        return;
    }

    const FVector LocalCorners[8] = {
        FVector(LocalBounds.Min.X, LocalBounds.Min.Y, LocalBounds.Min.Z),
        FVector(LocalBounds.Max.X, LocalBounds.Min.Y, LocalBounds.Min.Z),
        FVector(LocalBounds.Min.X, LocalBounds.Max.Y, LocalBounds.Min.Z),
        FVector(LocalBounds.Max.X, LocalBounds.Max.Y, LocalBounds.Min.Z),
        FVector(LocalBounds.Min.X, LocalBounds.Min.Y, LocalBounds.Max.Z),
        FVector(LocalBounds.Max.X, LocalBounds.Min.Y, LocalBounds.Max.Z),
        FVector(LocalBounds.Min.X, LocalBounds.Max.Y, LocalBounds.Max.Z),
        FVector(LocalBounds.Max.X, LocalBounds.Max.Y, LocalBounds.Max.Z)
    };

    const FMatrix& WorldMatrix = GetWorldMatrix();

    for (const FVector& Corner : LocalCorners)
    {
        const FVector WorldPos = WorldMatrix.TransformPosition(Corner);
        WorldAABB.Expand(WorldPos);
    }

    bBoundsDirty = false;
}

bool UStaticMeshComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
    if (!HasValidMesh())
    {
        return false;
    }

    EnsureBoundsUpdated();

    float BoxT = 0.0f;
    if (!WorldAABB.IntersectRay(Ray, BoxT))
    {
        return false;
    }

    const TArray<FNormalVertex>& Vertices = StaticMeshAsset->GetVertices();
    const TArray<uint32>& Indices = StaticMeshAsset->GetIndices();

    if (Vertices.empty() || Indices.empty())
    {
        return false;
    }

    const FMatrix InvWorld = GetWorldMatrix().GetInverse();

    FRay LocalRay = Ray;
    LocalRay.Origin = InvWorld.TransformPosition(LocalRay.Origin);
    LocalRay.Direction = InvWorld.TransformVector(LocalRay.Direction);
    LocalRay.Direction.NormalizeSafe();

    bool bHit = false;
    float ClosestT = FLT_MAX;
    int32 BestFaceIndex = -1;
    FVector BestLocalNormal = FVector::ZeroVector;

    for (uint32 i = 0; i + 2 < static_cast<uint32>(Indices.size()); i += 3)
    {
        const uint32 I0 = Indices[i];
        const uint32 I1 = Indices[i + 1];
        const uint32 I2 = Indices[i + 2];

        if (I0 >= Vertices.size() || I1 >= Vertices.size() || I2 >= Vertices.size())
        {
            continue;
        }

        const FVector& V0 = Vertices[I0].Position;
        const FVector& V1 = Vertices[I1].Position;
        const FVector& V2 = Vertices[I2].Position;

        float HitT = 0.0f;
        if (IntersectTriangle(LocalRay.Origin, LocalRay.Direction, V0, V1, V2, HitT))
        {
            if (HitT < ClosestT)
            {
                ClosestT = HitT;
                bHit = true;
                BestFaceIndex = static_cast<int32>(i / 3);

                const FVector Edge1 = V1 - V0;
                const FVector Edge2 = V2 - V0;
                BestLocalNormal = FVector::CrossProduct(Edge1, Edge2).GetSafeNormal();
            }
        }
    }

    if (!bHit)
    {
        return false;
    }

    const FVector LocalHitLocation = LocalRay.Origin + LocalRay.Direction * ClosestT;
    const FVector WorldHitLocation = GetWorldMatrix().TransformPosition(LocalHitLocation);
    FVector WorldNormal = GetWorldMatrix().TransformVector(BestLocalNormal);
    WorldNormal.NormalizeSafe();

    OutHitResult.bHit = true;
    OutHitResult.HitComponent = this;
    OutHitResult.Distance = (WorldHitLocation - Ray.Origin).Size();
    OutHitResult.Location = WorldHitLocation;
    OutHitResult.Normal = WorldNormal;
    OutHitResult.FaceIndex = BestFaceIndex;

    return true;
}

const FAABB& UStaticMeshComponent::GetWorldAABB() const
{
    EnsureBoundsUpdated();
    return WorldAABB;
}

bool UStaticMeshComponent::ConsumeRenderStateDirty()
{
    const bool bWasDirty = bRenderStateDirty;
    bRenderStateDirty = false;
    return bWasDirty;
}

void UStaticMeshComponent::AttachToBone(
    USkinnedMeshComponent* InSkinnedMeshComponent,
    int32 InBoneIndex,
    const FMatrix& InAttachLocalOffset)
{
    AttachedSkinnedMeshComponent = InSkinnedMeshComponent;
    AttachedBoneIndex = InBoneIndex;
    AttachLocalOffset = InAttachLocalOffset;
    UpdateBoneAttachment();
}

void UStaticMeshComponent::ClearBoneAttachment()
{
    AttachedSkinnedMeshComponent = nullptr;
    AttachedBoneIndex = -1;
    AttachLocalOffset = FMatrix::Identity;
}

void UStaticMeshComponent::UpdateBoneAttachment()
{
    if (AttachedSkinnedMeshComponent == nullptr || AttachedBoneIndex < 0)
    {
        return;
    }

    FMatrix BoneGlobalMeshTransform = FMatrix::Identity;
    if (!AttachedSkinnedMeshComponent->GetCurrentBoneGlobalMeshTransform(AttachedBoneIndex, BoneGlobalMeshTransform))
    {
        return;
    }

    SetRelativeMatrix(
        AttachLocalOffset *
        BoneGlobalMeshTransform *
        AttachedSkinnedMeshComponent->GetRelativeMatrix());
    MarkBoundsDirty();
    MarkRenderStateDirty();
}

void UStaticMeshComponent::SerializeStaticMeshAsset(FArchive& Ar)
{
    Ar << "ObjStaticMeshAsset" << StaticMeshAssetPath;
    Ar << "NormalizeOnImport" << bNormalizeOnImport;

    if (!Ar.IsLoading())
    {
        return;
    }

    TArray<UMaterialInterface*> SavedMaterials = Materials;
    ReloadStaticMeshFromAssetPath();
    RestoreSavedOverrideMaterials(SavedMaterials);
}

void UStaticMeshComponent::RestoreSavedOverrideMaterials(const TArray<UMaterialInterface*>& SavedMaterials)
{
    const int32 RestoreCount = static_cast<int32>(std::min(SavedMaterials.size(), Materials.size()));
    for (int32 i = 0; i < RestoreCount; ++i)
    {
        if (SavedMaterials[i] != nullptr)
        {
            SetMaterial(i, SavedMaterials[i]);
        }
    }
}

void UStaticMeshComponent::ReloadStaticMeshFromAssetPath()
{
    if (StaticMeshAssetPath.empty())
    {
        SetStaticMesh(nullptr);
        return;
    }

    UStaticMesh* Mesh = FResourceManager::Get().LoadStaticMesh(StaticMeshAssetPath, bNormalizeOnImport);
    SetStaticMesh(Mesh);
}

void UStaticMeshComponent::ApplyPropertyEdit(const char* PropertyName)
{
    switch (PropertyNameId(PropertyName))
    {
    case PropertyNameIdConstexpr("StaticMesh"):
        ReloadStaticMeshFromAssetPath();
        return;

    case PropertyNameIdConstexpr("Normalize On Import"):
        if (!StaticMeshAssetPath.empty())
        {
            ReloadStaticMeshFromAssetPath();
        }
        return;

    case PropertyNameIdConstexpr("Materials"):
        for (int32 i = 0; i < static_cast<int32>(Materials.size()); ++i)
        {
            SetMaterial(i, Materials[i]);
        }
        return;

    default:
        return;
    }
}

void UStaticMeshComponent::MarkBoundsDirty()
{
    bBoundsDirty = true;
}

void UStaticMeshComponent::MarkRenderStateDirty()
{
    bRenderStateDirty = true;
}

void UStaticMeshComponent::EnsureBoundsUpdated() const
{
    if (!bBoundsDirty && !IsTransformDirty())
    {
        return;
    }

    if (IsTransformDirty())
    {
        (void)GetWorldMatrix();
        return;
    }

    const_cast<UStaticMeshComponent*>(this)->UpdateWorldAABB();
}
