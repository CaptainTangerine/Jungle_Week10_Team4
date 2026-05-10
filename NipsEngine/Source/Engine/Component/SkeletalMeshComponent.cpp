#include "SkeletalMeshComponent.h"

#include "Core/CollisionTypes.h"
#include "Core/ResourceManager.h"

#include <algorithm>
#include <cfloat>

DEFINE_CLASS(USkeletalMeshComponent, UMeshComponent)
REGISTER_FACTORY(USkeletalMeshComponent)

namespace
{
    FNormalVertex MakeBaseSkinnedVertex(const FSkeletalVertex& Source)
    {
        FNormalVertex Vertex = {};
        Vertex.Position = Source.Position;
        Vertex.Color = Source.Color;
        Vertex.Normal = Source.Normal;
        Vertex.UVs = Source.UVs;
        Vertex.Tangent = Source.Tangent;
        Vertex.Bitangent = Source.Bitangent;
        return Vertex;
    }
}



void USkeletalMeshComponent::PostDuplicate(UObject* Original)
{
    UMeshComponent::PostDuplicate(Original);

    const USkeletalMeshComponent* Orig = Cast<USkeletalMeshComponent>(Original);
    SkeletalMeshAsset = Orig->SkeletalMeshAsset;
    RawSkeletalMeshData = Orig->RawSkeletalMeshData;
    SkeletalMeshAssetPath = Orig->SkeletalMeshAssetPath;
    LocalTransforms = Orig->LocalTransforms;
    GlobalTransforms = Orig->GlobalTransforms;
    FinalSkinningTransforms = Orig->FinalSkinningTransforms;
    SkeletalMeshRenderData = Orig->SkeletalMeshRenderData;
    bBoundsDirty = true;
    bBoneTransformsDirty = true;
    bSkinnedVerticesDirty = true;
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

void USkeletalMeshComponent::Serialize(FArchive& Ar)
{
    UMeshComponent::Serialize(Ar);
    SerializeSkeletalMeshAsset(Ar);
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
    UMeshComponent::TickComponent(DeltaTime);
    EnsureSkinnedVerticesUpdated();
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
    if (SkeletalMeshAsset == InSkeletalMesh && RawSkeletalMeshData == nullptr)
    {
        return;
    }

    SkeletalMeshAsset = InSkeletalMesh;
    RawSkeletalMeshData = nullptr;
    SkeletalMeshAssetPath = SkeletalMeshAsset != nullptr
        ? SkeletalMeshAsset->GetAssetPathFileName()
        : FString();
    RefreshSkeletalMeshState();
}

void USkeletalMeshComponent::SetSkeletalMesh(std::nullptr_t)
{
    SetSkeletalMesh(static_cast<USkeletalMesh*>(nullptr));
}

void USkeletalMeshComponent::SetSkeletalMeshData(const FSkeletalMesh* InSkeletalMeshData)
{
    if (SkeletalMeshAsset == nullptr && RawSkeletalMeshData == InSkeletalMeshData)
    {
        return;
    }

    SkeletalMeshAsset = nullptr;
    RawSkeletalMeshData = InSkeletalMeshData;
    SkeletalMeshAssetPath = RawSkeletalMeshData != nullptr
        ? RawSkeletalMeshData->PathFileName
        : FString();
    RefreshSkeletalMeshState();
}

const FSkeletalMesh* USkeletalMeshComponent::GetSkeletalMeshData() const
{
    return SkeletalMeshAsset != nullptr ? SkeletalMeshAsset->GetMeshData() : RawSkeletalMeshData;
}

void USkeletalMeshComponent::RefreshSkeletalMeshState()
{
    ReleaseOwnedMaterialInstances();
    Materials.clear();

    const FSkeletalMesh* MeshData = GetSkeletalMeshData();
    if (MeshData != nullptr)
    {
        const TArray<FStaticMeshMaterialSlot>& Slots = MeshData->GetMaterialSlots();
        const TArray<FStaticMeshSection>& Sections = MeshData->GetSections();
        Materials.reserve(Sections.size());

        for (const FStaticMeshSection& Section : Sections)
        {
            if (Section.MaterialSlotIndex >= 0 &&
                Section.MaterialSlotIndex < static_cast<int32>(Slots.size()))
            {
                Materials.push_back(Slots[Section.MaterialSlotIndex].Material);
            }
            else
            {
                Materials.push_back(nullptr);
            }
        }
    }

    ResizePoseArrays();
    ResetPoseToBindPose();
    MarkSkinnedVerticesDirty();
    MarkBoundsDirty();
    MarkRenderStateDirty();
}

bool USkeletalMeshComponent::HasValidMesh() const
{
    const FSkeletalMesh* MeshData = GetSkeletalMeshData();
    return MeshData != nullptr &&
        !MeshData->Vertices.empty() &&
        !MeshData->Indices.empty();
}

int32 USkeletalMeshComponent::GetBoneCount() const
{
    const FReferenceSkeleton* ReferenceSkeleton = GetReferenceSkeleton();
    return ReferenceSkeleton != nullptr ? ReferenceSkeleton->GetRawBoneNum() : 0;
}

int32 USkeletalMeshComponent::FindBoneIndex(const FString& BoneName) const
{
    const FReferenceSkeleton* ReferenceSkeleton = GetReferenceSkeleton();
    if (ReferenceSkeleton == nullptr)
    {
        return -1;
    }

    return ReferenceSkeleton->FindBoneIndex(BoneName);
}

void USkeletalMeshComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UMeshComponent::GetEditableProperties(OutProps);
    OutProps.push_back({ "SkeletalMesh", EPropertyType::String, &SkeletalMeshAssetPath });
}

void USkeletalMeshComponent::PostEditProperty(const char* PropertyName)
{
    UMeshComponent::PostEditProperty(PropertyName);
    ApplyPropertyEdit(PropertyName);
}

void USkeletalMeshComponent::ResetPoseToBindPose()
{
    const FReferenceSkeleton* ReferenceSkeleton = GetReferenceSkeleton();
    if (ReferenceSkeleton == nullptr)
    {
        LocalTransforms.clear();
        GlobalTransforms.clear();
        FinalSkinningTransforms.clear();
        SkeletalMeshRenderData.Reset();
        MarkBoneTransformsDirty();
        return;
    }

    ResizePoseArrays();
    for (int32 BoneIndex = 0; BoneIndex < ReferenceSkeleton->GetRawBoneNum(); ++BoneIndex)
    {
        LocalTransforms[BoneIndex] = ReferenceSkeleton->GetRefBonePose(BoneIndex);
    }

    MarkBoneTransformsDirty();
    UpdateBoneTransforms();
}

bool USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FMatrix& LocalTransform)
{
    const FReferenceSkeleton* ReferenceSkeleton = GetReferenceSkeleton();
    if (ReferenceSkeleton == nullptr || !ReferenceSkeleton->IsValidIndex(BoneIndex))
    {
        return false;
    }

    ResizePoseArrays();
    LocalTransforms[BoneIndex] = LocalTransform;
    MarkBoneTransformsDirty();
    return true;
}

const TArray<FMatrix>& USkeletalMeshComponent::GetGlobalTransforms() const
{
    EnsureBoneTransformsUpdated();
    return GlobalTransforms;
}

const TArray<FMatrix>& USkeletalMeshComponent::GetFinalSkinningTransforms() const
{
    EnsureBoneTransformsUpdated();
    return FinalSkinningTransforms;
}

const TArray<FNormalVertex>& USkeletalMeshComponent::GetSkinnedVertices() const
{
    EnsureSkinnedVerticesUpdated();
    return SkeletalMeshRenderData.DynamicVertices;
}

const FSkeletalMeshRenderData& USkeletalMeshComponent::GetSkeletalMeshRenderData() const
{
    EnsureSkinnedVerticesUpdated();
    return SkeletalMeshRenderData;
}

void USkeletalMeshComponent::UpdateBoneTransforms()
{
    const FReferenceSkeleton* ReferenceSkeleton = GetReferenceSkeleton();
    if (ReferenceSkeleton == nullptr)
    {
        GlobalTransforms.clear();
        FinalSkinningTransforms.clear();
        bBoneTransformsDirty = false;
        return;
    }

    ResizePoseArrays();

    const int32 BoneCount = ReferenceSkeleton->GetRawBoneNum();
    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        const FMeshBoneInfo& BoneInfo = ReferenceSkeleton->GetBoneInfo(BoneIndex);
        if (BoneInfo.ParentIndex >= 0 && BoneInfo.ParentIndex < BoneIndex)
        {
            GlobalTransforms[BoneIndex] = LocalTransforms[BoneIndex] * GlobalTransforms[BoneInfo.ParentIndex];
        }
        else
        {
            GlobalTransforms[BoneIndex] = LocalTransforms[BoneIndex];
        }
    }

    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        FinalSkinningTransforms[BoneIndex] =
            ReferenceSkeleton->GetInverseBindPose(BoneIndex) * GlobalTransforms[BoneIndex];
    }

    bBoneTransformsDirty = false;
}

void USkeletalMeshComponent::UpdateSkinnedVertices()
{
    const FSkeletalMesh* MeshData = GetSkeletalMeshData();
    if (MeshData == nullptr)
    {
        SkeletalMeshRenderData.Reset();
        bSkinnedVerticesDirty = false;
        return;
    }

    EnsureBoneTransformsUpdated();

    const TArray<FSkeletalVertex>& SourceVertices = MeshData->GetSourceVertices();
    const FSkinWeightVertexBuffer* SkinWeightVertexBuffer = GetSkinWeightVertexBuffer();
    SkeletalMeshRenderData.DynamicVertices.resize(SourceVertices.size());

    for (size_t VertexIndex = 0; VertexIndex < SourceVertices.size(); ++VertexIndex)
    {
        const FSkeletalVertex& Source = SourceVertices[VertexIndex];
        FNormalVertex& Target = SkeletalMeshRenderData.DynamicVertices[VertexIndex];
        Target = MakeBaseSkinnedVertex(Source);

        FVector SkinnedPosition = FVector::ZeroVector;
        FVector SkinnedNormal = FVector::ZeroVector;
        FVector SkinnedTangent = FVector::ZeroVector;
        FVector SkinnedBitangent = FVector::ZeroVector;
        float TotalWeight = 0.0f;

        const FSkinWeightInfo* SkinWeightInfo = SkinWeightVertexBuffer != nullptr
            ? SkinWeightVertexBuffer->GetSkinWeightInfo(VertexIndex)
            : nullptr;

        for (uint32 InfluenceIndex = 0; InfluenceIndex < FSkinWeightInfo::MaxBoneInfluences; ++InfluenceIndex)
        {
            const float Weight = SkinWeightInfo != nullptr
                ? SkinWeightInfo->BoneWeights[InfluenceIndex]
                : Source.BoneWeights[InfluenceIndex];
            const FBoneIndexType BoneIndex = SkinWeightInfo != nullptr
                ? SkinWeightInfo->BoneIndices[InfluenceIndex]
                : Source.BoneIndices[InfluenceIndex];
            if (Weight <= 0.0f || BoneIndex >= FinalSkinningTransforms.size())
            {
                continue;
            }

            const FMatrix& SkinMatrix = FinalSkinningTransforms[BoneIndex];
            SkinnedPosition += SkinMatrix.TransformPosition(Source.Position) * Weight;
            SkinnedNormal += SkinMatrix.TransformVector(Source.Normal) * Weight;
            SkinnedTangent += SkinMatrix.TransformVector(Source.Tangent) * Weight;
            SkinnedBitangent += SkinMatrix.TransformVector(Source.Bitangent) * Weight;
            TotalWeight += Weight;
        }

        if (TotalWeight > 0.0f)
        {
            const float InvTotalWeight = 1.0f / TotalWeight;
            Target.Position = SkinnedPosition * InvTotalWeight;

            Target.Normal = SkinnedNormal * InvTotalWeight;
            if (!Target.Normal.Normalize())
            {
                Target.Normal = Source.Normal.GetSafeNormal();
            }

            Target.Tangent = SkinnedTangent * InvTotalWeight;
            if (!Target.Tangent.Normalize())
            {
                Target.Tangent = Source.Tangent.GetSafeNormal();
            }

            Target.Bitangent = SkinnedBitangent * InvTotalWeight;
            if (!Target.Bitangent.Normalize())
            {
                Target.Bitangent = Source.Bitangent.GetSafeNormal();
            }
        }
    }

    bSkinnedVerticesDirty = false;
    MarkBoundsDirty();
}

void USkeletalMeshComponent::UpdateWorldAABB() const
{
    WorldAABB.Reset();

    if (!HasValidMesh())
    {
        bBoundsDirty = false;
        return;
    }

    EnsureSkinnedVerticesUpdated();
    if (SkeletalMeshRenderData.DynamicVertices.empty())
    {
        bBoundsDirty = false;
        return;
    }

    const FMatrix& WorldMatrix = GetWorldMatrix();
    for (const FNormalVertex& Vertex : SkeletalMeshRenderData.DynamicVertices)
    {
        WorldAABB.Expand(WorldMatrix.TransformPosition(Vertex.Position));
    }

    bBoundsDirty = false;
}

bool USkeletalMeshComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
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

    const FSkeletalMesh* MeshData = GetSkeletalMeshData();
    const TArray<FNormalVertex>& Vertices = GetSkinnedVertices();
    const TArray<uint32>& Indices = MeshData->GetIndices();

    if (Vertices.empty() || Indices.empty())
    {
        return false;
    }

    const FMatrix WorldMatrix = GetWorldMatrix();
    const FMatrix InvWorld = WorldMatrix.GetInverse();

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
    const FVector WorldHitLocation = WorldMatrix.TransformPosition(LocalHitLocation);
    FVector WorldNormal = WorldMatrix.TransformVector(BestLocalNormal);
    WorldNormal.NormalizeSafe();

    OutHitResult.bHit = true;
    OutHitResult.HitComponent = this;
    OutHitResult.Distance = (WorldHitLocation - Ray.Origin).Size();
    OutHitResult.Location = WorldHitLocation;
    OutHitResult.Normal = WorldNormal;
    OutHitResult.FaceIndex = BestFaceIndex;

    return true;
}

const FAABB& USkeletalMeshComponent::GetWorldAABB() const
{
    EnsureBoundsUpdated();
    return WorldAABB;
}

bool USkeletalMeshComponent::ConsumeRenderStateDirty()
{
    const bool bWasDirty = bRenderStateDirty;
    bRenderStateDirty = false;
    return bWasDirty;
}

const FReferenceSkeleton* USkeletalMeshComponent::GetReferenceSkeleton() const
{
    if (SkeletalMeshAsset != nullptr)
    {
        return &SkeletalMeshAsset->GetRefSkeleton();
    }

    const FSkeletalMesh* MeshData = GetSkeletalMeshData();
    return MeshData != nullptr ? &MeshData->GetRefSkeleton() : nullptr;
}

const FSkinWeightVertexBuffer* USkeletalMeshComponent::GetSkinWeightVertexBuffer() const
{
    if (SkeletalMeshAsset != nullptr)
    {
        return &SkeletalMeshAsset->GetSkinWeightVertexBuffer();
    }

    const FSkeletalMesh* MeshData = GetSkeletalMeshData();
    return MeshData != nullptr ? &MeshData->GetSkinWeightVertexBuffer() : nullptr;
}

void USkeletalMeshComponent::SerializeSkeletalMeshAsset(FArchive& Ar)
{
    Ar << "SkeletalMeshAsset" << SkeletalMeshAssetPath;

    if (!Ar.IsLoading())
    {
        return;
    }

    TArray<UMaterialInterface*> SavedMaterials = Materials;
    ReloadSkeletalMeshFromAssetPath();
    RestoreSavedOverrideMaterials(SavedMaterials);
}

void USkeletalMeshComponent::RestoreSavedOverrideMaterials(const TArray<UMaterialInterface*>& SavedMaterials)
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

void USkeletalMeshComponent::ReloadSkeletalMeshFromAssetPath()
{
    if (SkeletalMeshAssetPath.empty())
    {
        SetSkeletalMesh(nullptr);
        return;
    }

    USkeletalMesh* Mesh = FResourceManager::Get().LoadSkeletalMesh(SkeletalMeshAssetPath);
    SetSkeletalMesh(Mesh);
}

void USkeletalMeshComponent::ApplyPropertyEdit(const char* PropertyName)
{
    switch (PropertyNameId(PropertyName))
    {
    case PropertyNameIdConstexpr("SkeletalMesh"):
        ReloadSkeletalMeshFromAssetPath();
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

void USkeletalMeshComponent::ResizePoseArrays()
{
    const FReferenceSkeleton* ReferenceSkeleton = GetReferenceSkeleton();
    const size_t BoneCount = ReferenceSkeleton != nullptr ? static_cast<size_t>(ReferenceSkeleton->GetRawBoneNum()) : 0;
    LocalTransforms.resize(BoneCount, FMatrix::Identity);
    GlobalTransforms.resize(BoneCount, FMatrix::Identity);
    FinalSkinningTransforms.resize(BoneCount, FMatrix::Identity);
}

void USkeletalMeshComponent::MarkBoundsDirty()
{
    bBoundsDirty = true;
}

void USkeletalMeshComponent::MarkRenderStateDirty()
{
    bRenderStateDirty = true;
}

void USkeletalMeshComponent::MarkBoneTransformsDirty()
{
    bBoneTransformsDirty = true;
    MarkSkinnedVerticesDirty();
}

void USkeletalMeshComponent::MarkSkinnedVerticesDirty()
{
    bSkinnedVerticesDirty = true;
    MarkBoundsDirty();
    MarkRenderStateDirty();
}

void USkeletalMeshComponent::EnsureBoundsUpdated() const
{
    const bool bWasTransformDirty = IsTransformDirty();
    if (!bBoundsDirty && !bWasTransformDirty)
    {
        return;
    }

    if (bWasTransformDirty)
    {
        (void)GetWorldMatrix();
    }

    const_cast<USkeletalMeshComponent*>(this)->UpdateWorldAABB();
}

void USkeletalMeshComponent::EnsureBoneTransformsUpdated() const
{
    if (!bBoneTransformsDirty)
    {
        return;
    }

    const_cast<USkeletalMeshComponent*>(this)->UpdateBoneTransforms();
}

void USkeletalMeshComponent::EnsureSkinnedVerticesUpdated() const
{
    if (!bSkinnedVerticesDirty)
    {
        return;
    }

    const_cast<USkeletalMeshComponent*>(this)->UpdateSkinnedVertices();
}
