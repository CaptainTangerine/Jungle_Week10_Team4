#pragma once

#include "Actor.h"
#include "Engine/Core/SoundManager.h"
#include "GameFramework/OceanSystem.h"

class UTextRenderComponent;
class UDecalComponent;
class USubUVComponent;
class UMaterialInterface;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UCameraComponent;

class ASceneActor : public AActor
{
public:
    DECLARE_CLASS(ASceneActor, AActor)
    ASceneActor() = default;

    void InitDefaultComponents();
};

class AStaticMeshActor : public AActor
{
public:
    DECLARE_CLASS(AStaticMeshActor, AActor)
    AStaticMeshActor() = default;

    void InitDefaultComponents();
public:
    virtual void BeginPlay() override;
};

class ASkeletalMeshActor : public AActor
{
public:
    DECLARE_CLASS(ASkeletalMeshActor, AActor)
    ASkeletalMeshActor() = default;

    void InitDefaultComponents();
};

class AWaterActor : public AActor
{
public:
    DECLARE_CLASS(AWaterActor, AActor)
    AWaterActor() = default;

    void InitDefaultComponents();
};

class ACameraActor : public AActor
{
public:
    DECLARE_CLASS(ACameraActor, AActor)
    ACameraActor() = default;

    void InitDefaultComponents() override;
    UCameraComponent* GetCameraComponent() const;
};

class ACineCameraActor : public ACameraActor
{
public:
    DECLARE_CLASS(ACineCameraActor, ACameraActor)
    ACineCameraActor() = default;

    void InitDefaultComponents() override;
};

class AGlobalOceanActor : public AActor
{
public:
    DECLARE_CLASS(AGlobalOceanActor, AActor)
    AGlobalOceanActor() = default;
    ~AGlobalOceanActor() override;

    void InitDefaultComponents();
    void BeginPlay() override;
    void Tick(float DeltaTime) override;
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;

private:
    void ClampSettings();
    void RebuildRings();
    UStaticMeshComponent* CreateOceanTile(
        UStaticMesh* TileMesh,
        UMaterialInterface* SharedWaterMaterial,
        const FVector& RelativeLocation,
        const FVector& RelativeScale);
    void ApplyGlobalProfileToWaterComponent();
    void UpdateOceanFollowTransform();
    void RegisterToOceanSystemIfNeeded();
    void UnregisterFromOceanSystemIfNeeded();

private:
    class USceneComponent* OceanRoot = nullptr;
    class UWaterComponent* WaterComponent = nullptr;
    TArray<class UStaticMeshComponent*> RingTiles;

    int32 RingCount = 4;
    float BaseTileSize = 128.0f;
    float RingScaleMultiplier = 2.0f;
    bool bFollowCamera = true;
    bool bSnapToGrid = false;
    float SnapGridSize = 128.0f;
    float OceanHeight = 0.0f;

    FOceanWaterProfile GlobalWaterProfile = {};
    uint64 LastAppliedProfileRevision = 0;
    bool bOceanSystemRegistered = false;
};

class ASubUVActor : public AActor
{
public:
    DECLARE_CLASS(ASubUVActor, AActor)
    ASubUVActor() = default;

    void InitDefaultComponents();
    USubUVComponent* GetSubUVComponent() const { return SubUVComponent; }

private:
    USubUVComponent* SubUVComponent = nullptr;
};

class ATextRenderActor : public AActor
{
public:
    DECLARE_CLASS(ATextRenderActor, AActor)
    ATextRenderActor() = default;

    void InitDefaultComponents();
};

class ABillboardActor : public AActor
{
public:
    DECLARE_CLASS(ABillboardActor, AActor)
    ABillboardActor() = default;

    void InitDefaultComponents();
};

class ADecalActor : public AActor
{
public:
    DECLARE_CLASS(ADecalActor, AActor)
    ADecalActor() = default;

    void InitDefaultComponents();
};

class ALightActor : public AActor {
public:
    DECLARE_CLASS(ALightActor, AActor)
    ALightActor() = default;

    void PostDuplicate(UObject* Original) override;

protected:
    void SetupBillboard(class USceneComponent* Root);
};

class ADirectionalLightActor : public ALightActor {
public:
    DECLARE_CLASS(ADirectionalLightActor, ALightActor)
    ADirectionalLightActor() = default;

    void InitDefaultComponents();
};

class AAmbientLightActor : public ALightActor {
public:
    DECLARE_CLASS(AAmbientLightActor, ALightActor)
    AAmbientLightActor() = default;

    void InitDefaultComponents();
};

class APointLightActor : public ALightActor {
public:
    DECLARE_CLASS(APointLightActor, ALightActor)
    APointLightActor() = default;

    void InitDefaultComponents();
};

class ASpotLightActor : public ALightActor {
public:
    DECLARE_CLASS(ASpotLightActor, ALightActor)
    ASpotLightActor() = default;

    void InitDefaultComponents();
};

class ASkyAtmosphereActor : public AActor {
public:
    DECLARE_CLASS(ASkyAtmosphereActor, AActor)
    ASkyAtmosphereActor() = default;

    void InitDefaultComponents();
};

class AHeightFogActor : public AActor {
public:
    DECLARE_CLASS(AHeightFogActor, AActor)
    AHeightFogActor() = default;

    void InitDefaultComponents();
};
