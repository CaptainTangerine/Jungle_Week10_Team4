#include "SkeletalMesh.h"

DEFINE_CLASS(USkeletalMesh, UObject)

void USkeletalMesh::SetMeshData(FSkeletalMesh* InMeshData)
{
    MeshData = InMeshData;
}

FSkeletalMesh* USkeletalMesh::GetMeshData()
{
    return MeshData;
}

const FSkeletalMesh* USkeletalMesh::GetMeshData() const
{
    return MeshData;
}

const TArray<FSkeletalVertex>& USkeletalMesh::GetVertices() const
{
    return MeshData->Vertices;
}

const TArray<uint32>& USkeletalMesh::GetIndices() const
{
    return MeshData->Indices;
}

const TArray<FSkeletalMeshSection>& USkeletalMesh::GetSections() const
{
    return MeshData->Sections;
}

const TArray<FSkeletalMeshMaterialSlot>& USkeletalMesh::GetMaterialSlots() const
{
    return MeshData->Slots;
}

const TArray<FBoneInfo>& USkeletalMesh::GetBones() const
{
    return MeshData->Bones;
}

const FAABB& USkeletalMesh::GetLocalBounds() const
{
    return MeshData->LocalBounds;
}

bool USkeletalMesh::HasValidMeshData() const
{
    return MeshData != nullptr
        && !MeshData->Vertices.empty()
        && !MeshData->Indices.empty();
}
