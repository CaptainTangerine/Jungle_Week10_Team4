#pragma once

#include "Asset/SkeletalMesh.h"
#include "Asset/SkeletalMeshTypes.h"
#include "MeshComponent.h"
#include "Render/Resource/Material.h"

class USkeletalMeshComponent : public UMeshComponent
{
public:
    DECLARE_CLASS(USkeletalMeshComponent, UMeshComponent)
    USkeletalMeshComponent() = default;

    void PostDuplicate(UObject* Original) override;
    void Serialize(FArchive& Ar) override;
    void TickComponent(float DeltaTime) override;

    void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
    void SetSkeletalMesh(std::nullptr_t);
    void SetSkeletalMeshData(const FSkeletalMesh* InSkeletalMeshData);
    const USkeletalMesh* GetSkeletalMesh() const { return SkeletalMeshAsset; }
    USkeletalMesh* GetSkeletalMesh() { return SkeletalMeshAsset; }
    const FSkeletalMesh* GetSkeletalMeshData() const;
    bool HasValidMesh() const;

    int32 GetBoneCount() const;
    int32 FindBoneIndex(const FString& BoneName) const;

    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;

    void ResetPoseToBindPose();
    bool SetBoneLocalTransform(int32 BoneIndex, const FMatrix& LocalTransform);
    const TArray<FMatrix>& GetLocalTransforms() const { return LocalTransforms; }
    const TArray<FMatrix>& GetGlobalTransforms() const;
    const TArray<FMatrix>& GetFinalSkinningTransforms() const;
    const TArray<FNormalVertex>& GetSkinnedVertices() const;
    const FSkeletalMeshRenderData& GetSkeletalMeshRenderData() const;
    void UpdateBoneTransforms();
    void UpdateSkinnedVertices();

    void UpdateWorldAABB() const override;
    bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
    EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_SkeletalMesh; }
    const FAABB& GetWorldAABB() const override;

    bool ConsumeRenderStateDirty();

private:
    const FReferenceSkeleton* GetReferenceSkeleton() const;
    const FSkinWeightVertexBuffer* GetSkinWeightVertexBuffer() const;
    void SerializeSkeletalMeshAsset(FArchive& Ar);
    void RefreshSkeletalMeshState();
    void RestoreSavedOverrideMaterials(const TArray<UMaterialInterface*>& SavedMaterials);
    void ReloadSkeletalMeshFromAssetPath();
    void ApplyPropertyEdit(const char* PropertyName);
    void ResizePoseArrays();
    void MarkBoundsDirty();
    void MarkRenderStateDirty();
    void MarkBoneTransformsDirty();
    void MarkSkinnedVerticesDirty();
    void EnsureBoundsUpdated() const;
    void EnsureBoneTransformsUpdated() const;
    void EnsureSkinnedVerticesUpdated() const;

private:
    USkeletalMesh* SkeletalMeshAsset = nullptr;
    const FSkeletalMesh* RawSkeletalMeshData = nullptr;
    FString SkeletalMeshAssetPath;

    TArray<FMatrix> LocalTransforms;
    mutable TArray<FMatrix> GlobalTransforms;
    mutable TArray<FMatrix> FinalSkinningTransforms;
    mutable FSkeletalMeshRenderData SkeletalMeshRenderData;

    mutable bool bBoundsDirty = true;
    mutable bool bBoneTransformsDirty = true;
    mutable bool bSkinnedVerticesDirty = true;
    bool bRenderStateDirty = true;
};
