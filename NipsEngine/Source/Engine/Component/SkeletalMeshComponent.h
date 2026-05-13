#pragma once
#include "SkinnedMeshComponent.h"

class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
    DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

    void PostDuplicate(UObject* Original) override;
    void Serialize(FArchive& Ar) override;

    void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh) override;

    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;

private:
    void SerializeSkeletalMeshAsset(FArchive& Ar);
    void RestoreSavedOverrideMaterials(const TArray<UMaterialInterface*>& SavedMaterials);
    void ReloadSkeletalMeshFromAssetPath();
    void ApplyPropertyEdit(const char* PropertyName);

private:
    FString SkeletalMeshAssetPath;
    int32 SkeletalMeshIndex = 0;
};
