#pragma once
#include "MeshComponent.h"
#include "Asset/SkeletalMesh.h"
#include "Render/Resource/Buffer.h"

struct FSkinnedMeshRenderResource
{
    TArray<FNormalVertex> SkinnedVertices;
    TArray<uint32>        IndexData;
    FVertexBuffer DynamicSkinnedVertexBuffer;
    FIndexBuffer  StaticIB;
    const USkeletalMesh* SourceSkeletalMesh = nullptr;

    bool EnsureDynamicVertexBuffer(ID3D11Device* Device, uint32 VertexCount);
    bool EnsureStaticIndexBuffer(ID3D11Device* Device);
    bool InitializeFromBindPose(const USkeletalMesh* SkeletalMesh);
    bool Upload(ID3D11DeviceContext* Context);
    void Release();
    const USkeletalMesh* GetSourceSkeletalMesh() const { return SourceSkeletalMesh; }
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

    bool EnsureDynamicSkinnedVertexBuffer(ID3D11Device* Device);
    bool InitializeSkinnedVerticesFromBindPose();
    bool UploadSkinnedVertices(ID3D11DeviceContext* Context);
    void ReleaseDynamicSkinResources();

    FSkinnedMeshRenderResource& GetSkinnedMeshRenderResource() { return SkinnedRenderResource; }
    const FSkinnedMeshRenderResource& GetSkinnedMeshRenderResource() const { return SkinnedRenderResource; }
    FVertexBuffer& GetDynamicSkinnedVertexBuffer() { return SkinnedRenderResource.DynamicSkinnedVertexBuffer; }
    const FVertexBuffer& GetDynamicSkinnedVertexBuffer() const { return SkinnedRenderResource.DynamicSkinnedVertexBuffer; }
    TArray<FNormalVertex>& GetSkinnedVertices() { return SkinnedRenderResource.SkinnedVertices; }
    const TArray<FNormalVertex>& GetSkinnedVertices() const { return SkinnedRenderResource.SkinnedVertices; }

protected:
    void MarkBoundsDirty();
    void MarkRenderStateDirty();
    void EnsureBoundsUpdated() const;
    void RebuildMaterialsFromMesh();

protected:
    USkeletalMesh* SkeletalMeshAsset = nullptr;

    // SkeletalMeshAsset은 공유되는 자원이라 FSkinnedMeshRenderResource 내부에 SkinnedVertices 보관
    FSkinnedMeshRenderResource SkinnedRenderResource;

    FString SkeletalMeshAssetPath;

    mutable bool bBoundsDirty = true;
    bool bRenderStateDirty = true;
};
