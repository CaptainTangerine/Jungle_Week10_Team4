#include "SkeletalMesh.h"

#include "Asset/Skeleton.h"

DEFINE_CLASS(USkeletalMesh, UObject)

USkeletalMesh::~USkeletalMesh()
{
    if (MeshData != nullptr)
    {
        delete MeshData;
        MeshData = nullptr;
    }
}

void USkeletalMesh::SetMeshData(FSkeletalMesh* InMeshData)
{
    if (MeshData == InMeshData)
    {
        return;
    }

    delete MeshData;
    MeshData = InMeshData;
    if (MeshData != nullptr)
    {
        MeshData->RebuildDerivedData();
    }
    RebuildLocalBoundsFromMeshData();
}

FSkeletalMesh* USkeletalMesh::GetMeshData(int32 LOD)
{
    (void)LOD;
    return MeshData;
}

const FSkeletalMesh* USkeletalMesh::GetMeshData(int32 LOD) const
{
    (void)LOD;
    return MeshData;
}

const FString& USkeletalMesh::GetAssetPathFileName() const
{
    static FString Empty = {};
    return MeshData ? MeshData->PathFileName : Empty;
}

const TArray<FSkeletalVertex>& USkeletalMesh::GetSourceVertices() const
{
    static const TArray<FSkeletalVertex> Empty = {};
    return MeshData ? MeshData->GetSourceVertices() : Empty;
}

const TArray<uint32>& USkeletalMesh::GetIndices() const
{
    static const TArray<uint32> Empty = {};
    return MeshData ? MeshData->GetIndices() : Empty;
}

const TArray<FStaticMeshSection>& USkeletalMesh::GetSections() const
{
    static const TArray<FStaticMeshSection> Empty = {};
    return MeshData ? MeshData->GetSections() : Empty;
}

const TArray<FStaticMeshMaterialSlot>& USkeletalMesh::GetMaterialSlots() const
{
    static const TArray<FStaticMeshMaterialSlot> Empty = {};
    return MeshData ? MeshData->GetMaterialSlots() : Empty;
}

const FReferenceSkeleton& USkeletalMesh::GetRefSkeleton() const
{
    static const FReferenceSkeleton Empty = {};
    if (Skeleton != nullptr)
    {
        return Skeleton->GetReferenceSkeleton();
    }

    return MeshData ? MeshData->GetRefSkeleton() : Empty;
}

const FSkinWeightVertexBuffer& USkeletalMesh::GetSkinWeightVertexBuffer() const
{
    static const FSkinWeightVertexBuffer Empty = {};
    return MeshData ? MeshData->GetSkinWeightVertexBuffer() : Empty;
}

const FAABB& USkeletalMesh::GetLocalBounds() const
{
    static const FAABB Empty = {};
    return MeshData ? MeshData->LocalBounds : Empty;
}

bool USkeletalMesh::HasValidMeshData() const
{
    return MeshData != nullptr
        && !MeshData->Vertices.empty()
        && !MeshData->Indices.empty();
}

void USkeletalMesh::RebuildLocalBoundsFromMeshData()
{
    if (MeshData == nullptr)
    {
        return;
    }

    MeshData->LocalBounds.Reset();
    for (const FSkeletalVertex& Vertex : MeshData->Vertices)
    {
        MeshData->LocalBounds.Expand(Vertex.Position);
    }
}
