#pragma once

#include "Asset/SkeletalMeshTypes.h"
#include "Object/Object.h"

class USkeleton : public UObject
{
public:
    DECLARE_CLASS(USkeleton, UObject)

    USkeleton() = default;

    void SetReferenceSkeleton(const FReferenceSkeleton& InReferenceSkeleton);
    const FReferenceSkeleton& GetReferenceSkeleton() const { return ReferenceSkeleton; }
    FReferenceSkeleton& GetMutableReferenceSkeleton() { return ReferenceSkeleton; }

    int32 GetBoneCount() const { return ReferenceSkeleton.GetRawBoneNum(); }
    int32 FindBoneIndex(const FString& BoneName) const { return ReferenceSkeleton.FindBoneIndex(BoneName); }

private:
    FReferenceSkeleton ReferenceSkeleton;
};
