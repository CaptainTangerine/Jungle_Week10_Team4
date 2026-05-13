#pragma once
#include "Math/Matrix.h"
#include "Asset/StaticMeshTypes.h"
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

struct FSkeletalMeshSceneNode
{
    FString Name;
    int32 ParentIndex = -1;
    TArray<int32> Children;

    FMatrix LocalTransformMatrix = FMatrix::Identity;
    FMatrix GlobalTransformMatrix = FMatrix::Identity;

    int32 StaticMeshIndex = -1;
    int32 SkeletalMeshIndex = -1;
    int32 BoneIndex = -1;
};

struct FSkeletalMeshEmbeddedStaticMesh
{
    FString Name;
    int32 SourceNodeIndex = -1;

    TArray<FNormalVertex> Vertices;
    TArray<uint32> Indices;
    TArray<FStaticMeshSection> Sections;
    TArray<FStaticMeshMaterialSlot> MaterialSlots;

    FAABB LocalBounds;
};

struct FSkeletalMesh
{
    FString FilePathName;
    FString SkeletonAssetPath;
    int32 SourceNodeIndex = -1;
    FMatrix SourceNodeLocalTransform = FMatrix::Identity;
    FMatrix SourceNodeGlobalTransform = FMatrix::Identity;
    int32 SourceSceneSkeletalMeshIndex = 0;

    TArray<FSkeletalVertex> Vertices;
    TArray<uint32> Indices;

    TArray<FSkeletalMeshSection> Sections;
    TArray<FSkeletalMeshMaterialSlot> Slots;

    TArray<FBoneInfo> Bones;
    TArray<FSkeletalMeshSceneNode> SceneNodes;
    TArray<FSkeletalMeshEmbeddedStaticMesh> EmbeddedStaticMeshes;

    FAABB LocalBounds;
};
