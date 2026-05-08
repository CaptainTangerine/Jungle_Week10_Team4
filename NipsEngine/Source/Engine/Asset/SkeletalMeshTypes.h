#pragma once

#include "Asset/StaticMeshTypes.h"
#include "Core/Containers/String.h"
#include "Math/Matrix.h"
#include "Render/Resource/VertexTypes.h"

struct FBoneWeightPair
{
    uint32 BoneIndex = 0;
    float Weight = 0.0f;
};

struct FSkeletonBone
{
    FString Name;
    int32 ParentIndex = -1;
    FMatrix BindPose = FMatrix::Identity;
    FMatrix InverseBindPose = FMatrix::Identity;
};

struct FSkeletalMesh
{
    FString PathFileName;
    TArray<FSkeletalVertex> Vertices;
    TArray<uint32> Indices;
    TArray<FStaticMeshSection> Sections;
    TArray<FStaticMeshMaterialSlot> Slots;
    TArray<FSkeletonBone> Bones;
    FAABB LocalBounds;

    FStaticMeshRenderData RenderData;
};
