#pragma once
#include "Component/MeshComponent.h"

class USkeletalMesh;

class USkinnedMeshComponent : public UMeshComponent
{
public:
    DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)
    
    USkinnedMeshComponent() = default;
    ~USkinnedMeshComponent() override = default;
    
    void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
    USkeletalMesh* GetSkeletalMesh() const;
    bool HasValidMesh() const;
    
    const TArray<FMatrix>& GetSkinMatrices() const;
    
    void UpdateWorldAABB() const override;
    const FAABB& GetWorldAABB() const override;
    bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
    
    bool ConsumeRenderStateDirty();
    
protected:
    void RebuildBindPoseSkinMatrices();
    void MarkBoundsDirty();
    void MarkRenderStateDirty();
    void EnsureBoundsUpdated() const;
    
protected:
    USkeletalMesh* SkeletalMesh = nullptr;
    TArray<FMatrix> SkinMatrices;
    
    mutable bool bBoundsDirty = true;
    bool bRenderStateDirty = true;
    
};
