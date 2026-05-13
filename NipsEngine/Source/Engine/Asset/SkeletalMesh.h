
#pragma once

#include "SkeletalMeshTypes.h"
#include "Object/Object.h"

class USkeletalMesh : public UObject
{
public:
    DECLARE_CLASS(USkeletalMesh, UObject)

public:
    USkeletalMesh() = default;
    ~USkeletalMesh() override;


    // Getter, Setter
public:
    void SetMeshData(FSkeletalMesh* InMeshData);
    FSkeletalMesh* GetMeshData();
    const FSkeletalMesh* GetMeshData() const;

    const FString& GetAssetPathFileName() const;
    int32 GetSourceSceneSkeletalMeshIndex() const;

    const TArray<FSkeletalVertex>& GetVertices() const;
    const TArray<uint32>& GetIndices() const;
    const TArray<FSkeletalMeshSection>& GetSections() const;
    const TArray<FSkeletalMeshMaterialSlot>& GetMaterialSlots() const;
    const TArray<FBoneInfo>& GetBones() const;
    const FAABB& GetLocalBounds() const;

    bool HasValidMeshData() const;

private:
    FSkeletalMesh* MeshData = nullptr;
    // TODO : LOD 
};
