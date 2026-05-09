#pragma once

#include "Component/SkinnedMeshComponent.h"

class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
    DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

    USkeletalMeshComponent() = default;
    ~USkeletalMeshComponent() override = default;

    EPrimitiveType GetPrimitiveType() const override { return EPrimitiveType::EPT_SkeletalMesh; }
};
