#pragma once
#include "Math/Matrix.h"
#include "Render/Resource/VertexTypes.h"
#include "Render/Resource/Material.h"

struct FBoneInfo
{
    FString Name;
    int32   ParentIndex = -1;

    FMatrix LocalTransform;
    FMatrix GlobalTransform;
    FMatrix InverseBindTransform;
};

/* 중복이긴 하지만 명시적으로 다시 선언*/
struct FSkeletalMeshSection
{
    uint32 StartIndex = 0;
    uint32 IndexCount = 0;
    int32 MaterialSlotIndex = -1;
};

struct FSkeletalMeshMaterialSlot
{
    FString SlotName;
    UMaterialInterface* Material = nullptr;
    TMap<FString, FMaterialParamValue> SerializedMaterialParams;
    TMap<FString, FString> SerializedTextureParamPaths;
};

struct FSkeletalMesh
{
    FString FilePathName;
    FString SkeletonAssetPath;

    TArray<FSkeletalVertex> Vertices;
    TArray<uint32> Indices;

    TArray<FSkeletalMeshSection> Sections;
    TArray<FSkeletalMeshMaterialSlot> Slots;

    TArray<FBoneInfo> Bones;

    FAABB LocalBounds;
};
