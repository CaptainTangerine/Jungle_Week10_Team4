#include "SkinnedMeshComponent.h"

#include "Core/PlatformTime.h"
#include "Core/ResourceManager.h"
#include "Render/Resource/Material.h"

#include <cfloat>
#include <cmath>

DEFINE_CLASS(USkinnedMeshComponent, UMeshComponent)
REGISTER_FACTORY(USkinnedMeshComponent)


enum class EBoneVisitState : uint8
{
    Unvisited,
    Visiting,
    Complete
};


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
        USkeletalMesh* LoadedMesh = SkeletalMeshAssetPath.empty()
            ? nullptr
            : FResourceManager::Get().LoadSkeletalMesh(SkeletalMeshAssetPath);
        SetSkeletalMesh(LoadedMesh);
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
        SetSkeletalMesh(SkeletalMeshAssetPath.empty()
            ? nullptr
            : FResourceManager::Get().LoadSkeletalMesh(SkeletalMeshAssetPath));
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

    FAABB LocalBounds;
    if (!SkinnedRenderResource.SkinnedVertices.empty())
    {
        for (const FNormalVertex& Vertex : SkinnedRenderResource.SkinnedVertices)
        {
            LocalBounds.Expand(Vertex.Position);
        }
    }
    else
    {
        LocalBounds = SkeletalMeshAsset->GetLocalBounds();
    }

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

void USkinnedMeshComponent::UpdateCPUSkinning()
{
    if (SkeletalMeshAsset == nullptr || !SkeletalMeshAsset->HasValidMeshData())
    {
        return;
    }

    const TArray<FBoneInfo>& Bones = SkeletalMeshAsset->GetBones();
    const TArray<FSkeletalVertex>& SourceVertices = SkeletalMeshAsset->GetVertices();

    if (Bones.empty() || SourceVertices.empty())
    {
        return;
    }

    if (SkinnedRenderResource.SkinnedVertices.size() != SourceVertices.size())
    {
        InitializeSkinnedVerticesFromBindPose();
    }

    if (CurrentBoneLocalTransforms.size() != Bones.size())
    {
        CurrentBoneLocalTransforms.resize(Bones.size());
        for (int32 BoneIdx = 0; BoneIdx < static_cast<int32>(Bones.size()); ++BoneIdx)
        {
            CurrentBoneLocalTransforms[BoneIdx] = Bones[BoneIdx].LocalTransform;
        }
    }

    // 테스트용 Head 흔들림은 현재 포즈를 덮지 않고 skinning 계산에만 임시 반영한다.
    int32 DebugHeadBoneIndex = -1;
    FMatrix DebugHeadOriginalLocal = FMatrix::Identity;
    const double TimeSeconds = FPlatformTime::Seconds();
    constexpr float DegToRad = MathUtil::PI / 180.0f;
    const float DebugYawRadians = static_cast<float>(std::sin(TimeSeconds * 5.f) * 0.2f) * DegToRad;

    for (int32 BoneIdx = 0; BoneIdx < static_cast<int32>(Bones.size()); ++BoneIdx)
    {
        const FString& BoneName = Bones[BoneIdx].Name;
        const bool bIsHeadBone =
            BoneName.find("Head") != FString::npos ||
            BoneName.find("head") != FString::npos ||
            BoneName.find("HEAD") != FString::npos;

        if (!bIsHeadBone)
        {
            continue;
        }

        DebugHeadBoneIndex = BoneIdx;
        DebugHeadOriginalLocal = CurrentBoneLocalTransforms[BoneIdx];
        CurrentBoneLocalTransforms[BoneIdx] =DebugHeadOriginalLocal * FMatrix::MakeRotationX(DebugYawRadians);
        break;
    }

    // 현재 Bone Local Transform으로 Global Transform을 계산한다.
    // 인덱스 순서가 부모부터 시작된다는 보장이 없다고 판단하여 DFS로 순회하면서 보장
    RebuildCurrentBoneGlobalTransforms(Bones);

    const int32 BoneCount = static_cast<int32>(Bones.size());
    TArray<FMatrix> SkinningMatrices(BoneCount);
    for (int32 BoneIdx = 0; BoneIdx < BoneCount; ++BoneIdx)
    {
        SkinningMatrices[BoneIdx] = Bones[BoneIdx].InverseBindTransform * CurrentBoneGlobalMeshTransforms[BoneIdx];
    }

    // 매 프레임 마다 Vertex 마다 수정을 해서 비효율
    // TODO : CPU Skinning으로 가중치를 셰이더에서 계산
    for (int32 VertexIdx = 0; VertexIdx < static_cast<int32>(SourceVertices.size()); ++VertexIdx)
    {
        const FSkeletalVertex& SrcVertex = SourceVertices[VertexIdx];
        FNormalVertex& DstVertex = SkinnedRenderResource.SkinnedVertices[VertexIdx];

        FVector SkinnedPosition = {};
        FVector SkinnedNormal = {};
        FVector SkinnedTangent = {};
        FVector SkinnedBiTangent = {};

        float TotalWeight = 0.f;

        for (int32 BlendIdx = 0; BlendIdx < 4; ++BlendIdx)
        {
            const int32 BoneIndex = SrcVertex.BoneIndices[BlendIdx];
            const float TargetBoneWeight = SrcVertex.BoneWeights[BlendIdx];

            if (TargetBoneWeight <= 0.f)
            {
                continue;
            }
            TotalWeight += TargetBoneWeight;

            const FMatrix& SkinnedMatrix = SkinningMatrices[BoneIndex];

            SkinnedPosition += SkinnedMatrix.TransformPosition(SrcVertex.Position) * TargetBoneWeight;
            SkinnedNormal += SkinnedMatrix.TransformVector(SrcVertex.Normal) * TargetBoneWeight;
            SkinnedTangent += SkinnedMatrix.TransformVector(SrcVertex.Tangent) * TargetBoneWeight;
            SkinnedBiTangent += SkinnedMatrix.TransformVector(SrcVertex.Bitangent) * TargetBoneWeight;
        }

        if (TotalWeight > 0.0f)
        {
            DstVertex.Position = SkinnedPosition;
            DstVertex.Normal = SkinnedNormal.GetSafeNormal();
            DstVertex.Tangent = SkinnedTangent.GetSafeNormal();
            DstVertex.Bitangent = SkinnedBiTangent.GetSafeNormal();
        }
        else
        {
            // Bone weight가 없는 정점은 bind pose 그대로 둠
            DstVertex.Position = SrcVertex.Position;
            DstVertex.Normal = SrcVertex.Normal;
            DstVertex.Tangent = SrcVertex.Tangent;
            DstVertex.Bitangent = SrcVertex.Bitangent;
        }

        DstVertex.Color = SrcVertex.Color;
        DstVertex.UVs = SrcVertex.UVs;
    }

    MarkBoundsDirty();
    MarkRenderStateDirty();
}

int32 USkinnedMeshComponent::FindBoneIndexByName(const FString& BoneName) const
{
    if (SkeletalMeshAsset == nullptr || BoneName.empty())
    {
        return -1;
    }

    const TArray<FBoneInfo>& Bones = SkeletalMeshAsset->GetBones();
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Bones.size()); ++BoneIndex)
    {
        if (Bones[BoneIndex].Name == BoneName)
        {
            return BoneIndex;
        }
    }

    return -1;
}

bool USkinnedMeshComponent::GetCurrentBoneLocalTransform(int32 BoneIndex, FMatrix& OutTransform) const
{
    if (SkeletalMeshAsset == nullptr)
    {
        return false;
    }

    const TArray<FBoneInfo>& Bones = SkeletalMeshAsset->GetBones();
    if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size()))
    {
        return false;
    }

    if (BoneIndex >= static_cast<int32>(CurrentBoneLocalTransforms.size()))
    {
        OutTransform = Bones[BoneIndex].LocalTransform;
        return true;
    }

    OutTransform = CurrentBoneLocalTransforms[BoneIndex];
    return true;
}

bool USkinnedMeshComponent::SetCurrentBoneLocalTransform(int32 BoneIndex, const FMatrix& InTransform)
{
    if (SkeletalMeshAsset == nullptr)
    {
        return false;
    }

    const TArray<FBoneInfo>& Bones = SkeletalMeshAsset->GetBones();
    if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size()))
    {
        return false;
    }

    if (CurrentBoneLocalTransforms.size() != Bones.size())
    {
        CurrentBoneLocalTransforms.resize(Bones.size());
        for (int32 CurrentBoneIndex = 0; CurrentBoneIndex < static_cast<int32>(Bones.size()); ++CurrentBoneIndex)
        {
            CurrentBoneLocalTransforms[CurrentBoneIndex] = Bones[CurrentBoneIndex].LocalTransform;
        }
    }

    CurrentBoneLocalTransforms[BoneIndex] = InTransform;
    RebuildCurrentBoneGlobalTransforms(Bones);
    UpdateCPUSkinning();
    return true;
}

bool USkinnedMeshComponent::GetCurrentBoneGlobalMeshTransform(int32 BoneIndex, FMatrix& OutTransform) const
{
    if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(CurrentBoneGlobalMeshTransforms.size()))
    {
        return GetBindBoneGlobalMeshTransform(BoneIndex, OutTransform);
    }

    OutTransform = CurrentBoneGlobalMeshTransforms[BoneIndex];
    return true;
}

bool USkinnedMeshComponent::GetBindBoneGlobalMeshTransform(int32 BoneIndex, FMatrix& OutTransform) const
{
    if (SkeletalMeshAsset == nullptr)
    {
        return false;
    }

    const TArray<FBoneInfo>& Bones = SkeletalMeshAsset->GetBones();
    if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size()))
    {
        return false;
    }

    OutTransform = Bones[BoneIndex].GlobalTransform;
    return true;
}

void USkinnedMeshComponent::RebuildCurrentBoneGlobalTransforms(const TArray<FBoneInfo>& Bones)
{
    const int32 BoneCount = static_cast<int32>(Bones.size());
    if (BoneCount <= 0)
    {
        CurrentBoneGlobalMeshTransforms.clear();
        return;
    }

    CurrentBoneGlobalMeshTransforms.resize(Bones.size());

    TArray<EBoneVisitState> VisitStates;
    VisitStates.resize(Bones.size(), EBoneVisitState::Unvisited);

    TArray<int32> Stack;
    Stack.reserve(BoneCount);

    for (int32 StartBoneIndex = 0; StartBoneIndex < BoneCount; ++StartBoneIndex)
    {
        if (VisitStates[StartBoneIndex] == EBoneVisitState::Complete)
        {
            continue;
        }

        Stack.clear();
        Stack.push_back(StartBoneIndex);

        while (!Stack.empty())
        {
            const int32 BoneIndex = Stack.back();

            if (BoneIndex < 0 || BoneIndex >= BoneCount)
            {
                Stack.pop_back();
                continue;
            }

            if (VisitStates[BoneIndex] == EBoneVisitState::Complete)
            {
                Stack.pop_back();
                continue;
            }

            const int32 ParentIndex = Bones[BoneIndex].ParentIndex;
            const bool bHasValidParent = ParentIndex >= 0 && ParentIndex < BoneCount;

            if (VisitStates[BoneIndex] == EBoneVisitState::Unvisited)
            {
                VisitStates[BoneIndex] = EBoneVisitState::Visiting;

                if (bHasValidParent)
                {
                    if (VisitStates[ParentIndex] == EBoneVisitState::Visiting)
                    {
                        CurrentBoneGlobalMeshTransforms[BoneIndex] = CurrentBoneLocalTransforms[BoneIndex];
                        VisitStates[BoneIndex] = EBoneVisitState::Complete;
                        Stack.pop_back();
                        continue;
                    }

                    if (VisitStates[ParentIndex] != EBoneVisitState::Complete)
                    {
                        Stack.push_back(ParentIndex);
                        continue;
                    }
                }
            }

            if (bHasValidParent && VisitStates[ParentIndex] == EBoneVisitState::Complete)
            {
                CurrentBoneGlobalMeshTransforms[BoneIndex] =
                    CurrentBoneLocalTransforms[BoneIndex] *
                    CurrentBoneGlobalMeshTransforms[ParentIndex];
            }
            else
            {
                CurrentBoneGlobalMeshTransforms[BoneIndex] = CurrentBoneLocalTransforms[BoneIndex];
            }

            VisitStates[BoneIndex] = EBoneVisitState::Complete;
            Stack.pop_back();
        }
    }
}


void USkinnedMeshComponent::ReleaseDynamicSkinResources()
{
    SkinnedRenderResource.Release();
    CurrentBoneLocalTransforms.clear();
    CurrentBoneGlobalMeshTransforms.clear();
}

void USkinnedMeshComponent::MarkBoundsDirty()
{
    bBoundsDirty = true;
    NotifySpatialIndexDirty();
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
