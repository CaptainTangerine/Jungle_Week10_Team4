#pragma once
#include "MeshComponent.h"
#include "Asset/StaticMesh.h"
#include "Render/Resource/Material.h"

class USkinnedMeshComponent;

class UStaticMeshComponent : public UMeshComponent
{
public:
    DECLARE_CLASS(UStaticMeshComponent, UMeshComponent)
    UStaticMeshComponent();
    
    virtual void PostDuplicate(UObject* Original) override;

    virtual void Serialize(FArchive& Ar) override;

    void SetStaticMesh(UStaticMesh* InStaticMesh);
    UStaticMesh* GetStaticMesh() const;
    bool HasValidMesh() const;
    bool ImportStaticMeshFromFBX(const FString& FilePath);

    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char * PropertyName) override;

    void UpdateWorldAABB() const override;
    bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
    EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_StaticMesh; }

    const FAABB& GetWorldAABB() const override;

    bool ConsumeRenderStateDirty();
    void AttachToBone(USkinnedMeshComponent* InSkinnedMeshComponent, int32 InBoneIndex, const FMatrix& InAttachLocalOffset);
    void ClearBoneAttachment();
    void UpdateBoneAttachment();
    bool HasBoneAttachment() const { return AttachedSkinnedMeshComponent != nullptr && AttachedBoneIndex >= 0; }

private:
    void SerializeStaticMeshAsset(FArchive& Ar);
    void RestoreSavedOverrideMaterials(const TArray<UMaterialInterface*>& SavedMaterials);
    void ReloadStaticMeshFromAssetPath();
    void ApplyPropertyEdit(const char* PropertyName);

    void MarkBoundsDirty();
    void MarkRenderStateDirty();
    void EnsureBoundsUpdated() const;

private:
    UStaticMesh* StaticMeshAsset = nullptr;
    FString StaticMeshAssetPath;
    bool bNormalizeOnImport = false;

    USkinnedMeshComponent* AttachedSkinnedMeshComponent = nullptr;
    int32 AttachedBoneIndex = -1;
    FMatrix AttachLocalOffset = FMatrix::Identity;

    mutable bool bBoundsDirty = true;
    bool bRenderStateDirty = true;
};
