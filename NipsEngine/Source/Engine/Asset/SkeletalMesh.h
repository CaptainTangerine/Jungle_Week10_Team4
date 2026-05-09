#pragma once

#include "SkeletalMeshTypes.h"
#include "Object/Object.h"

class USkeletalMesh : public UObject
{
public:
    DECLARE_CLASS(USkeletalMesh, UObject)

    USkeletalMesh() = default;
    ~USkeletalMesh() override;

    void SetMeshData(FSkeletalMeshAsset* InMeshData);

    FSkeletalMeshAsset* GetMeshData();
    const FSkeletalMeshAsset* GetMeshData() const;

    const TArray<FbxVertex>& GetVertices() const;
    const TArray<uint32>& GetIndices() const;
    const TArray<FBone>& GetBones() const;
    const TArray<FSKeletalMeshSection>& GetSections() const;
    const TArray<FSkeletalMeshMaterialSlot>& GetMaterialSlots() const;

    bool HasValidMeshData() const;

private:
    FSkeletalMeshAsset* MeshData = nullptr;
};
