#include "SkeletalMeshComponent.h"

#include <algorithm>

#include "Core/ResourceManager.h"

DEFINE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
REGISTER_FACTORY(USkeletalMeshComponent)

void USkeletalMeshComponent::PostDuplicate(UObject* Original)
{
    USkinnedMeshComponent::PostDuplicate(Original);

    const USkeletalMeshComponent* Orig = Cast<USkeletalMeshComponent>(Original);
    SkeletalMeshAssetPath = Orig ? Orig->SkeletalMeshAssetPath : FString();
    SkeletalMeshIndex = Orig ? Orig->SkeletalMeshIndex : 0;
}

void USkeletalMeshComponent::Serialize(FArchive& Ar)
{
    USkinnedMeshComponent::Serialize(Ar);
    SerializeSkeletalMeshAsset(Ar);
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
    USkinnedMeshComponent::SetSkeletalMesh(InSkeletalMesh);

    if (InSkeletalMesh == nullptr)
    {
        SkeletalMeshAssetPath.clear();
        SkeletalMeshIndex = 0;
        return;
    }

    SkeletalMeshAssetPath = InSkeletalMesh->GetAssetPathFileName();
    SkeletalMeshIndex = InSkeletalMesh->GetSourceSceneSkeletalMeshIndex();
}

void USkeletalMeshComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    USkinnedMeshComponent::GetEditableProperties(OutProps);
    OutProps.push_back({ "SkeletalMesh", EPropertyType::String, &SkeletalMeshAssetPath });
    OutProps.push_back({ "Skeletal Mesh Index", EPropertyType::Int, &SkeletalMeshIndex });
}

void USkeletalMeshComponent::PostEditProperty(const char* PropertyName)
{
    USkinnedMeshComponent::PostEditProperty(PropertyName);
    ApplyPropertyEdit(PropertyName);
}

void USkeletalMeshComponent::SerializeSkeletalMeshAsset(FArchive& Ar)
{
    Ar << "SkeletalMesh" << SkeletalMeshAssetPath;
    Ar << "SkeletalMeshIndex" << SkeletalMeshIndex;

    if (!Ar.IsLoading())
    {
        return;
    }

    TArray<UMaterialInterface*> SavedMaterials = Materials;
    ReloadSkeletalMeshFromAssetPath();
    RestoreSavedOverrideMaterials(SavedMaterials);
}

void USkeletalMeshComponent::RestoreSavedOverrideMaterials(const TArray<UMaterialInterface*>& SavedMaterials)
{
    const int32 RestoreCount = static_cast<int32>(std::min(SavedMaterials.size(), Materials.size()));
    for (int32 i = 0; i < RestoreCount; ++i)
    {
        if (SavedMaterials[i] != nullptr)
        {
            SetMaterial(i, SavedMaterials[i]);
        }
    }
}

void USkeletalMeshComponent::ReloadSkeletalMeshFromAssetPath()
{
    if (SkeletalMeshAssetPath.empty())
    {
        SetSkeletalMesh(nullptr);
        return;
    }

    if (SkeletalMeshIndex < 0)
    {
        SkeletalMeshIndex = 0;
    }

    USkeletalMesh* Mesh = FResourceManager::Get().LoadSkeletalMesh(SkeletalMeshAssetPath, SkeletalMeshIndex);
    SetSkeletalMesh(Mesh);
}

void USkeletalMeshComponent::ApplyPropertyEdit(const char* PropertyName)
{
    switch (PropertyNameId(PropertyName))
    {
    case PropertyNameIdConstexpr("SkeletalMesh"):
    case PropertyNameIdConstexpr("Skeletal Mesh Index"):
        ReloadSkeletalMeshFromAssetPath();
        return;

    default:
        return;
    }
}
