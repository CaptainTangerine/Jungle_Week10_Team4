#include "SkinnedMeshComponent.h"

#include "Render/Resource/Material.h"

#include <cfloat>

DEFINE_CLASS(USkinnedMeshComponent, UMeshComponent)

bool FSkinnedMeshRenderResource::InitializeFromBindPose(const USkeletalMesh* SkeletalMesh)
{
    if (SourceSkeletalMesh != SkeletalMesh)
    {
        MeshBuffer.Release();
        bBindPoseDataDirty = true;
    }

    SourceSkeletalMesh = SkeletalMesh;

    if (SkeletalMesh == nullptr || !SkeletalMesh->HasValidMeshData())
    {
        SkinnedVertices.clear();
        IndexData.clear();
        MeshBuffer.Release();
        bBindPoseDataDirty = true;
        return false;
    }

    const TArray<FSkeletalVertex>& SourceVertices = SkeletalMesh->GetVertices();
    const TArray<uint32>& SourceIndices = SkeletalMesh->GetIndices();
    if (SourceVertices.empty() || SourceIndices.empty())
    {
        SkinnedVertices.clear();
        IndexData.clear();
        MeshBuffer.Release();
        bBindPoseDataDirty = true;
        return false;
    }

    if (!bBindPoseDataDirty &&
        SkinnedVertices.size() == SourceVertices.size() &&
        IndexData.size() == SourceIndices.size())
    {
        return true;
    }

    SkinnedVertices.resize(SourceVertices.size());
    IndexData = SourceIndices;

    for (int32 i = 0; i < static_cast<int32>(SourceVertices.size()); ++i)
    {
        const FSkeletalVertex& Source = SourceVertices[i];
        FNormalVertex& Target = SkinnedVertices[i];

        Target.Position = Source.Position;
        Target.Color = Source.Color;
        Target.Normal = Source.Normal;
        Target.UVs = Source.UVs;
        Target.Tangent = Source.Tangent;
        Target.Bitangent = Source.Bitangent;
    }

    bBindPoseDataDirty = false;
    return true;
}

bool FSkinnedMeshRenderResource::EnsureMeshBuffer(ID3D11Device* Device)
{
    if (Device == nullptr)
    {
        return false;
    }

    if (SkinnedVertices.empty() || IndexData.empty())
    {
        if (!InitializeFromBindPose(SourceSkeletalMesh))
        {
            return false;
        }
    }

    const FVertexBuffer& VertexBuffer = MeshBuffer.GetVertexBuffer();
    const FIndexBuffer& IndexBuffer = MeshBuffer.GetIndexBuffer();
    const bool bNeedsCreate =
        !MeshBuffer.IsValid() ||
        VertexBuffer.GetVertexCount() != static_cast<uint32>(SkinnedVertices.size()) ||
        VertexBuffer.GetStride() != sizeof(FNormalVertex) ||
        IndexBuffer.GetIndexCount() != static_cast<uint32>(IndexData.size());

    if (bNeedsCreate)
    {
        MeshBuffer.CreateDynamic(Device, SkinnedVertices, IndexData);
    }

    return MeshBuffer.IsValid();
}

bool FSkinnedMeshRenderResource::Upload(ID3D11DeviceContext* Context)
{
    if (Context == nullptr ||
        MeshBuffer.GetVertexBuffer().GetBuffer() == nullptr ||
        SkinnedVertices.empty())
    {
        return false;
    }

    const uint32 UploadVertexCount = static_cast<uint32>(SkinnedVertices.size());
    if (UploadVertexCount > MeshBuffer.GetVertexBuffer().GetVertexCount() ||
        MeshBuffer.GetVertexBuffer().GetStride() != sizeof(FNormalVertex))
    {
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE MappedResource = {};
    HRESULT hr = Context->Map(
        MeshBuffer.GetVertexBuffer().GetBuffer(),
        0,
        D3D11_MAP_WRITE_DISCARD,
        0,
        &MappedResource);
    if (FAILED(hr))
    {
        return false;
    }

    std::memcpy(
        MappedResource.pData,
        SkinnedVertices.data(),
        sizeof(FNormalVertex) * SkinnedVertices.size());

    Context->Unmap(MeshBuffer.GetVertexBuffer().GetBuffer(), 0);
    return true;
}

void FSkinnedMeshRenderResource::Release()
{
    SourceSkeletalMesh = nullptr;
    SkinnedVertices.clear();
    IndexData.clear();
    bBindPoseDataDirty = true;
    MeshBuffer.Release();
}

USkinnedMeshComponent::~USkinnedMeshComponent()
{
    ReleaseDynamicSkinResources();
}

void USkinnedMeshComponent::PostDuplicate(UObject* Original)
{
    UMeshComponent::PostDuplicate(Original);

    const USkinnedMeshComponent* Orig = Cast<USkinnedMeshComponent>(Original);
    SkeletalMeshAsset = Orig ? Orig->SkeletalMeshAsset : nullptr;
    SkeletalMeshAssetPath = Orig ? Orig->SkeletalMeshAssetPath : FString();

    ReleaseDynamicSkinResources();
    SkinnedRenderResource.SourceSkeletalMesh = SkeletalMeshAsset;

    bBoundsDirty = true;
    bRenderStateDirty = true;
}

void USkinnedMeshComponent::Serialize(FArchive& Ar)
{
    UMeshComponent::Serialize(Ar);
    Ar << "SkeletalMesh" << SkeletalMeshAssetPath;

    if (Ar.IsLoading())
    {
        ReleaseDynamicSkinResources();
        SkinnedRenderResource.SourceSkeletalMesh = SkeletalMeshAsset;
        MarkBoundsDirty();
        MarkRenderStateDirty();
    }
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
    if (SkeletalMeshAsset == InSkeletalMesh)
    {
        return;
    }

    SkeletalMeshAsset = InSkeletalMesh;
    ReleaseDynamicSkinResources();
    SkinnedRenderResource.SourceSkeletalMesh = SkeletalMeshAsset;

    // GPU 버퍼 생성 전에 CPU 측 데이터를 미리 채워둔다.
    // 이후 렌더 파이프라인은 SkeletalMesh 포인터 없이 Resource 내부 데이터만 참조한다.
    InitializeSkinnedVerticesFromBindPose();

    RebuildMaterialsFromMesh();

    MarkBoundsDirty();
    MarkRenderStateDirty();
}

bool USkinnedMeshComponent::HasValidMesh() const
{
    return SkeletalMeshAsset != nullptr && SkeletalMeshAsset->HasValidMeshData();
}

void USkinnedMeshComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UMeshComponent::GetEditableProperties(OutProps);
    OutProps.push_back({ "SkeletalMesh", EPropertyType::String, &SkeletalMeshAssetPath });
}

void USkinnedMeshComponent::PostEditProperty(const char* PropertyName)
{
    UMeshComponent::PostEditProperty(PropertyName);

    switch (PropertyNameId(PropertyName))
    {
    case PropertyNameIdConstexpr("SkeletalMesh"):
        MarkRenderStateDirty();
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

void USkinnedMeshComponent::UpdateWorldAABB() const
{
    WorldAABB.Reset();

    if (!HasValidMesh())
    {
        bBoundsDirty = false;
        return;
    }

    const FAABB& LocalBounds = SkeletalMeshAsset->GetLocalBounds();
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
        WorldAABB.Expand(WorldMatrix.TransformPosition(Corner));
    }

    bBoundsDirty = false;
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

    if (SkinnedRenderResource.SkinnedVertices.empty() || SkinnedRenderResource.IndexData.empty())
    {
        InitializeSkinnedVerticesFromBindPose();
    }

    const TArray<FNormalVertex>& Vertices = SkinnedRenderResource.SkinnedVertices;
    const TArray<uint32>& Indices = SkinnedRenderResource.IndexData;
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
        if (IntersectTriangle(LocalRay.Origin, LocalRay.Direction, V0, V1, V2, HitT) && HitT < ClosestT)
        {
            ClosestT = HitT;
            bHit = true;
            BestFaceIndex = static_cast<int32>(i / 3);

            const FVector Edge1 = V1 - V0;
            const FVector Edge2 = V2 - V0;
            BestLocalNormal = FVector::CrossProduct(Edge1, Edge2).GetSafeNormal();
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

const FAABB& USkinnedMeshComponent::GetWorldAABB() const
{
    EnsureBoundsUpdated();
    return WorldAABB;
}

bool USkinnedMeshComponent::ConsumeRenderStateDirty()
{
    const bool bWasDirty = bRenderStateDirty;
    bRenderStateDirty = false;
    return bWasDirty;
}

bool USkinnedMeshComponent::EnsureSkinnedMeshBuffer(ID3D11Device* Device)
{
    if (!HasValidMesh())
    {
        SkinnedRenderResource.MeshBuffer.Release();
        return false;
    }

    return SkinnedRenderResource.EnsureMeshBuffer(Device);
}

bool USkinnedMeshComponent::InitializeSkinnedVerticesFromBindPose()
{
    return SkinnedRenderResource.InitializeFromBindPose(SkeletalMeshAsset);
}

bool USkinnedMeshComponent::UploadSkinnedVertices(ID3D11DeviceContext* Context)
{
    return SkinnedRenderResource.Upload(Context);
}

void USkinnedMeshComponent::ReleaseDynamicSkinResources()
{
    SkinnedRenderResource.Release();
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

void USkinnedMeshComponent::RebuildMaterialsFromMesh()
{
    ReleaseOwnedMaterialInstances();
    Materials.clear();

    if (SkeletalMeshAsset == nullptr)
    {
        return;
    }

    const TArray<FSkeletalMeshSection>& Sections = SkeletalMeshAsset->GetSections();
    const TArray<FSkeletalMeshMaterialSlot>& Slots = SkeletalMeshAsset->GetMaterialSlots();
    Materials.reserve(Sections.size());

    for (const FSkeletalMeshSection& Section : Sections)
    {
        if (Section.MaterialSlotIndex >= 0 && Section.MaterialSlotIndex < static_cast<int32>(Slots.size()))
        {
            Materials.push_back(Slots[Section.MaterialSlotIndex].Material);
        }
        else
        {
            Materials.push_back(nullptr);
        }
    }
}
