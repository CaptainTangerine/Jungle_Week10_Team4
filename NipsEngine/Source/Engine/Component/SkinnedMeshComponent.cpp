#include "SkinnedMeshComponent.h"

#include "Asset/SkeletalMesh.h"

DEFINE_CLASS(USkinnedMeshComponent, UMeshComponent)

namespace
{
    FVector SkinPosition(const FbxVertex& Vertex, const TArray<FMatrix>& SkinMatrices)
    {
        FVector SkinnedPosition = FVector::ZeroVector;
        float TotalWeight = 0.0f;

        for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
        {
            const int32 BoneIndex = Vertex.BoneIndices[InfluenceIndex];
            const float Weight = Vertex.BoneWeights[InfluenceIndex];
            if (Weight <= 0.0f || BoneIndex < 0 || BoneIndex >= static_cast<int32>(SkinMatrices.size()))
            {
                continue;
            }

            SkinnedPosition += SkinMatrices[BoneIndex].TransformPosition(Vertex.Position) * Weight;
            TotalWeight += Weight;
        }

        if (TotalWeight <= 0.0f)
        {
            return Vertex.Position;
        }

        if (TotalWeight < 0.999f || TotalWeight > 1.001f)
        {
            SkinnedPosition /= TotalWeight;
        }

        return SkinnedPosition;
    }
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
    if (SkeletalMesh == InSkeletalMesh)
    {
        return;
    }

    SkeletalMesh = InSkeletalMesh;
    RebuildBindPoseSkinMatrices();
    MarkBoundsDirty();
    MarkRenderStateDirty();
}

USkeletalMesh* USkinnedMeshComponent::GetSkeletalMesh() const
{
    return SkeletalMesh;
}

bool USkinnedMeshComponent::HasValidMesh() const
{
    return SkeletalMesh != nullptr && SkeletalMesh->HasValidMeshData();
}

const TArray<FMatrix>& USkinnedMeshComponent::GetSkinMatrices() const
{
    return SkinMatrices;
}

void USkinnedMeshComponent::UpdateWorldAABB() const
{
    WorldAABB.Reset();

    if (!HasValidMesh())
    {
        bBoundsDirty = false;
        return;
    }

    const FMatrix& WorldMatrix = GetWorldMatrix();
    for (const FbxVertex& Vertex : SkeletalMesh->GetVertices())
    {
        WorldAABB.Expand(WorldMatrix.TransformPosition(SkinPosition(Vertex, SkinMatrices)));
    }

    bBoundsDirty = false;
}

const FAABB& USkinnedMeshComponent::GetWorldAABB() const
{
    EnsureBoundsUpdated();
    return WorldAABB;
}

bool USkinnedMeshComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
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

    const float HitDistance = BoxT < 0.0f ? 0.0f : BoxT;

    OutHitResult.bHit = true;
    OutHitResult.HitComponent = this;
    OutHitResult.Distance = HitDistance;
    OutHitResult.Location = Ray.Origin + Ray.Direction * HitDistance;
    OutHitResult.Normal = FVector::ZeroVector;
    OutHitResult.FaceIndex = -1;

    return true;
}

bool USkinnedMeshComponent::ConsumeRenderStateDirty()
{
    const bool bWasDirty = bRenderStateDirty;
    bRenderStateDirty = false;
    return bWasDirty;
}

void USkinnedMeshComponent::RebuildBindPoseSkinMatrices()
{
    SkinMatrices.clear();

    if (!HasValidMesh())
    {
        return;
    }

    const TArray<FBone>& Bones = SkeletalMesh->GetBones();
    SkinMatrices.reserve(Bones.size());

    for (const FBone& Bone : Bones)
    {
        SkinMatrices.push_back(Bone.BoneToGlobalBind * Bone.MeshToBoneBind);
    }
}

void USkinnedMeshComponent::MarkBoundsDirty()
{
    bBoundsDirty = true;
}

void USkinnedMeshComponent::MarkRenderStateDirty()
{
    bRenderStateDirty = true;
}

void USkinnedMeshComponent::EnsureBoundsUpdated() const
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

    const_cast<USkinnedMeshComponent*>(this)->UpdateWorldAABB();
}
