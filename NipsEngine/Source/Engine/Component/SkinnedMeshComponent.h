#pragma once
#include "MeshComponent.h"
#include "Asset/SkeletalMesh.h"
#include "Render/Resource/Buffer.h"

struct FSkinnedMeshRenderResource
{
    TArray<FNormalVertex> SkinnedVertices;
    TArray<uint32>        IndexData;
    FMeshBuffer           MeshBuffer;
    const USkeletalMesh* SourceSkeletalMesh = nullptr;
    bool bBindPoseDataDirty = true;

    bool InitializeFromBindPose(const USkeletalMesh* SkeletalMesh);
    bool EnsureMeshBuffer(ID3D11Device* Device);
    bool Upload(ID3D11DeviceContext* Context);
    void Release();
    const USkeletalMesh* GetSourceSkeletalMesh() const { return SourceSkeletalMesh; }
    FMeshBuffer& GetMeshBuffer() { return MeshBuffer; }
    const FMeshBuffer& GetMeshBuffer() const { return MeshBuffer; }
};

class USkinnedMeshComponent : public UMeshComponent
{
public:
    DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)
    ~USkinnedMeshComponent() override;

    void PostDuplicate(UObject* Original) override;
    void Serialize(FArchive& Ar) override;

    void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
    USkeletalMesh* GetSkeletalMesh() const { return SkeletalMeshAsset; }
    bool HasValidMesh() const;

    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;

    void UpdateWorldAABB() const override;
    bool RaycastMesh(const FRay& Ray, FHitResult& OutHitResult) override;
    EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_SkinnedMesh; }

    const FAABB& GetWorldAABB() const override;
    bool ConsumeRenderStateDirty();

    bool EnsureSkinnedMeshBuffer(ID3D11Device* Device);
    bool InitializeSkinnedVerticesFromBindPose();
    bool UploadSkinnedVertices(ID3D11DeviceContext* Context);
    void UpdateCPUSkinning();
    int32 FindBoneIndexByName(const FString& BoneName) const;
    bool GetCurrentBoneGlobalMeshTransform(int32 BoneIndex, FMatrix& OutTransform) const;
    bool GetBindBoneGlobalMeshTransform(int32 BoneIndex, FMatrix& OutTransform) const;

    void ReleaseDynamicSkinResources();

    FSkinnedMeshRenderResource& GetSkinnedMeshRenderResource() { return SkinnedRenderResource; }
    const FSkinnedMeshRenderResource& GetSkinnedMeshRenderResource() const { return SkinnedRenderResource; }
    TArray<FNormalVertex>& GetSkinnedVertices() { return SkinnedRenderResource.SkinnedVertices; }
    const TArray<FNormalVertex>& GetSkinnedVertices() const { return SkinnedRenderResource.SkinnedVertices; }

protected:
    void MarkBoundsDirty();
    void MarkRenderStateDirty();
    void EnsureBoundsUpdated() const;
    void RebuildMaterialsFromMesh();

    // DFS를 통해서 Bone GlobalTrnasform 재계산
    void RebuildCurrentBoneGlobalTransforms(const TArray<FBoneInfo>& Bones);

protected:
    USkeletalMesh* SkeletalMeshAsset = nullptr;
    FString SkeletalMeshAssetPath;
    // SkeletalMeshAsset은 공유되는 자원이라 FSkinnedMeshRenderResource 내부에 SkinnedVertices 보관
    FSkinnedMeshRenderResource SkinnedRenderResource;

    TArray<FMatrix> CurrentBoneLocalTransforms;
    TArray<FMatrix> CurrentBoneGlobalMeshTransforms;

    mutable bool bBoundsDirty = true;
    bool bRenderStateDirty = true;
};
