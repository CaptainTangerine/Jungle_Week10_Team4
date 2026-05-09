#pragma once

#include "Render/Resource/VertexTypes.h"

class UMaterialInterface;

struct FBone
{
    std::string Name;
    std::string PathName;

    int ParentIndex;

    // Bind pose 시점의 Bone Local -> Parent Bone Local 변환.
    FMatrix BoneToParentBind; //Bone행렬
    // Bind pose 시점의 Bone Local -> Global 변환.
    FMatrix BoneToGlobalBind;
    // Bind pose 시점의 Mesh Local -> Bone Local 변환.
    FMatrix MeshToBoneBind;
    bool bHasClusterBind = false;
};

struct FSKeletalMeshSection
{
    uint32 StartIndex = 0;
    uint32 IndexCount = 0;
    int32 MaterialSlotIndex = -1;
};

struct FSkeletalMeshMaterialSlot
{
    FString SlotName;
    UMaterialInterface* Material = nullptr;
};


struct FSkeletalMeshAsset
{
    TArray<FbxVertex> Vertices;
    TArray<uint32> Indices;
    
    TArray<FBone> Bones;
    TArray<FSKeletalMeshSection> Sections;
    TArray<FSkeletalMeshMaterialSlot> MaterialSlots;
};
