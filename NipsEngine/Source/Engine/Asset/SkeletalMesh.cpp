#include "SkeletalMesh.h"

DEFINE_CLASS(USkeletalMesh, UObject)

USkeletalMesh::~USkeletalMesh()
{
    delete MeshData;
    MeshData = nullptr;
}

void USkeletalMesh::SetMeshData(FSkeletalMeshAsset* InMeshData)
{
    if (MeshData == InMeshData)
    {
        return;
    }

    delete MeshData;
    MeshData = InMeshData;
}

FSkeletalMeshAsset* USkeletalMesh::GetMeshData()
{
    return MeshData;
}

const FSkeletalMeshAsset* USkeletalMesh::GetMeshData() const
{
    return MeshData;
}

const TArray<FbxVertex>& USkeletalMesh::GetVertices() const
{
    static const TArray<FbxVertex> Empty = {};
    return MeshData ? MeshData->Vertices : Empty;
}

const TArray<uint32>& USkeletalMesh::GetIndices() const
{
    static const TArray<uint32> Empty = {};
    return MeshData ? MeshData->Indices : Empty;
}

const TArray<FBone>& USkeletalMesh::GetBones() const
{
    static const TArray<FBone> Empty = {};
    return MeshData ? MeshData->Bones : Empty;
}

const TArray<FSKeletalMeshSection>& USkeletalMesh::GetSections() const
{
    static const TArray<FSKeletalMeshSection> Empty = {};
    return MeshData ? MeshData->Sections : Empty;
}

const TArray<FSkeletalMeshMaterialSlot>& USkeletalMesh::GetMaterialSlots() const
{
    static const TArray<FSkeletalMeshMaterialSlot> Empty = {};
    return MeshData ? MeshData->MaterialSlots : Empty;
}

bool USkeletalMesh::HasValidMeshData() const
{
    return MeshData != nullptr
        && !MeshData->Vertices.empty()
        && !MeshData->Indices.empty()
        && !MeshData->Bones.empty();
}
