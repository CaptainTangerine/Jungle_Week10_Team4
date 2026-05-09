#pragma once

#include "Asset/SkeletalMeshTypes.h"
#include "Object/Object.h"

class USkeleton;

class USkeletalMesh : public UObject
{
public:
    DECLARE_CLASS(USkeletalMesh, UObject)

    USkeletalMesh() = default;
    ~USkeletalMesh() override;

    void SetMeshData(FSkeletalMesh* InMeshData);
    FSkeletalMesh* GetMeshData(int32 LOD = 0);
    const FSkeletalMesh* GetMeshData(int32 LOD = 0) const;

    void SetSkeleton(USkeleton* InSkeleton) { Skeleton = InSkeleton; }
    USkeleton* GetSkeleton() const { return Skeleton; }

    const FString& GetAssetPathFileName() const;
    const TArray<FSkeletalVertex>& GetSourceVertices() const;
    const TArray<uint32>& GetIndices() const;
    const TArray<FStaticMeshSection>& GetSections() const;
    const TArray<FStaticMeshMaterialSlot>& GetMaterialSlots() const;
    const FReferenceSkeleton& GetRefSkeleton() const;
    const FSkinWeightVertexBuffer& GetSkinWeightVertexBuffer() const;
    const FAABB& GetLocalBounds() const;

    bool HasValidMeshData() const;
    int32 GetValidLODCount() const { return ValidLODCount; }

private:
    void RebuildLocalBoundsFromMeshData();

private:
    FSkeletalMesh* MeshData = nullptr;
    USkeleton* Skeleton = nullptr;
    int32 ValidLODCount = 1;
};
