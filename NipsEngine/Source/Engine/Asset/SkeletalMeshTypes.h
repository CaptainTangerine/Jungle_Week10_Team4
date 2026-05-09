#pragma once

#include "Asset/StaticMeshTypes.h"
#include "Core/Containers/String.h"
#include "Math/Matrix.h"
#include "Render/Resource/VertexTypes.h"

using FBoneIndexType = uint32;

struct FBoneWeightPair
{
    FBoneIndexType BoneIndex = 0;
    float Weight = 0.0f;
};

struct FSkinWeightInfo
{
    static constexpr uint32 MaxBoneInfluences = FSkeletalVertex::MaxBoneInfluences;

    FBoneIndexType BoneIndices[MaxBoneInfluences] = { 0, 0, 0, 0 };
    float BoneWeights[MaxBoneInfluences] = { 0.0f, 0.0f, 0.0f, 0.0f };

    bool HasAnyWeight() const
    {
        for (uint32 InfluenceIndex = 0; InfluenceIndex < MaxBoneInfluences; ++InfluenceIndex)
        {
            if (BoneWeights[InfluenceIndex] > 0.0f)
            {
                return true;
            }
        }
        return false;
    }
};

struct FSkinWeightVertexBuffer
{
    TArray<FSkinWeightInfo> SkinWeights;

    void Reset()
    {
        SkinWeights.clear();
    }

    void Resize(size_t VertexCount)
    {
        SkinWeights.resize(VertexCount);
    }

    size_t Num() const
    {
        return SkinWeights.size();
    }

    bool IsValidIndex(size_t VertexIndex) const
    {
        return VertexIndex < SkinWeights.size();
    }

    FSkinWeightInfo* GetSkinWeightInfo(size_t VertexIndex)
    {
        return IsValidIndex(VertexIndex) ? &SkinWeights[VertexIndex] : nullptr;
    }

    const FSkinWeightInfo* GetSkinWeightInfo(size_t VertexIndex) const
    {
        return IsValidIndex(VertexIndex) ? &SkinWeights[VertexIndex] : nullptr;
    }

    void BuildFromVertices(const TArray<FSkeletalVertex>& Vertices)
    {
        Resize(Vertices.size());
        for (size_t VertexIndex = 0; VertexIndex < Vertices.size(); ++VertexIndex)
        {
            FSkinWeightInfo& SkinWeightInfo = SkinWeights[VertexIndex];
            const FSkeletalVertex& Vertex = Vertices[VertexIndex];
            for (uint32 InfluenceIndex = 0; InfluenceIndex < FSkinWeightInfo::MaxBoneInfluences; ++InfluenceIndex)
            {
                SkinWeightInfo.BoneIndices[InfluenceIndex] = Vertex.BoneIndices[InfluenceIndex];
                SkinWeightInfo.BoneWeights[InfluenceIndex] = Vertex.BoneWeights[InfluenceIndex];
            }
        }
    }
};

struct FMeshBoneInfo
{
    FString Name;
    int32 ParentIndex = -1;
};

struct FReferenceSkeleton
{
    TArray<FMeshBoneInfo> BoneInfo;
    TArray<FMatrix> RefBonePose;
    TArray<FMatrix> InverseBindPose;

    void Reset()
    {
        BoneInfo.clear();
        RefBonePose.clear();
        InverseBindPose.clear();
    }

    int32 GetRawBoneNum() const
    {
        return static_cast<int32>(BoneInfo.size());
    }

    bool IsValidIndex(int32 BoneIndex) const
    {
        return BoneIndex >= 0 && BoneIndex < GetRawBoneNum();
    }

    int32 FindBoneIndex(const FString& BoneName) const
    {
        for (int32 BoneIndex = 0; BoneIndex < GetRawBoneNum(); ++BoneIndex)
        {
            if (BoneInfo[BoneIndex].Name == BoneName)
            {
                return BoneIndex;
            }
        }
        return -1;
    }

    int32 AddBone(const FMeshBoneInfo& InBoneInfo, const FMatrix& InRefBonePose, const FMatrix& InInverseBindPose)
    {
        BoneInfo.push_back(InBoneInfo);
        RefBonePose.push_back(InRefBonePose);
        InverseBindPose.push_back(InInverseBindPose);
        return GetRawBoneNum() - 1;
    }

    void SetBone(int32 BoneIndex, const FMeshBoneInfo& InBoneInfo, const FMatrix& InRefBonePose, const FMatrix& InInverseBindPose)
    {
        if (!IsValidIndex(BoneIndex))
        {
            return;
        }

        BoneInfo[BoneIndex] = InBoneInfo;
        RefBonePose[BoneIndex] = InRefBonePose;
        InverseBindPose[BoneIndex] = InInverseBindPose;
    }

    const FMeshBoneInfo& GetBoneInfo(int32 BoneIndex) const
    {
        return BoneInfo[BoneIndex];
    }

    const FMatrix& GetRefBonePose(int32 BoneIndex) const
    {
        return RefBonePose[BoneIndex];
    }

    const FMatrix& GetInverseBindPose(int32 BoneIndex) const
    {
        return InverseBindPose[BoneIndex];
    }
};

struct FBoneReference
{
    static constexpr FBoneIndexType InvalidIndex = static_cast<FBoneIndexType>(~0u);

    FString BoneName;
    FBoneIndexType BoneIndex = InvalidIndex;

    bool Initialize(const FReferenceSkeleton& ReferenceSkeleton)
    {
        const int32 FoundIndex = ReferenceSkeleton.FindBoneIndex(BoneName);
        if (FoundIndex < 0)
        {
            BoneIndex = InvalidIndex;
            return false;
        }

        BoneIndex = static_cast<FBoneIndexType>(FoundIndex);
        return true;
    }

    bool IsValid() const
    {
        return BoneIndex != InvalidIndex;
    }
};

struct FSkeletonBone
{
    FString Name;
    int32 ParentIndex = -1;
    FMatrix BindPose = FMatrix::Identity;
    FMatrix InverseBindPose = FMatrix::Identity;
};

struct FSkeletalMeshSection : public FStaticMeshSection
{
};

struct FSkeletalMeshLODRenderData
{
    const TArray<FSkeletalVertex>* StaticVertices = nullptr;
    const TArray<uint32>* Indices = nullptr;
    const TArray<FStaticMeshSection>* Sections = nullptr;
    const FSkinWeightVertexBuffer* SkinWeightVertexBuffer = nullptr;
};

struct FSkeletalMeshRenderData
{
    TArray<FNormalVertex> DynamicVertices;

    void Reset()
    {
        DynamicVertices.clear();
    }

    const TArray<FNormalVertex>& GetDynamicVertices() const
    {
        return DynamicVertices;
    }
};

struct FSkeletalMesh
{
    FString PathFileName;
    TArray<FSkeletalVertex> Vertices;
    TArray<uint32> Indices;
    TArray<FStaticMeshSection> Sections;
    TArray<FStaticMeshMaterialSlot> Slots;
    TArray<FSkeletonBone> Bones;
    FReferenceSkeleton RefSkeleton;
    FSkinWeightVertexBuffer SkinWeightVertexBuffer;
    FAABB LocalBounds;

    FStaticMeshRenderData RenderData;

    void RebuildDerivedData()
    {
        RefSkeleton.Reset();
        for (const FSkeletonBone& Bone : Bones)
        {
            FMeshBoneInfo BoneInfo = {};
            BoneInfo.Name = Bone.Name;
            BoneInfo.ParentIndex = Bone.ParentIndex;
            RefSkeleton.AddBone(BoneInfo, Bone.BindPose, Bone.InverseBindPose);
        }

        SkinWeightVertexBuffer.BuildFromVertices(Vertices);
    }

    const TArray<FSkeletalVertex>& GetSourceVertices() const
    {
        return Vertices;
    }

    const TArray<uint32>& GetIndices() const
    {
        return Indices;
    }

    const TArray<FStaticMeshSection>& GetSections() const
    {
        return Sections;
    }

    const TArray<FStaticMeshMaterialSlot>& GetMaterialSlots() const
    {
        return Slots;
    }

    const FReferenceSkeleton& GetRefSkeleton() const
    {
        return RefSkeleton;
    }

    const FSkinWeightVertexBuffer& GetSkinWeightVertexBuffer() const
    {
        return SkinWeightVertexBuffer;
    }
};
