#include "GameFramework/PrimitiveActors.h"

#include "Component/CameraComponent.h"
#include "Component/BillboardComponent.h"
#include "Component/DecalComponent.h"
#include "Component/HeightFogComponent.h"
#include "Component/Light/AmbientLightComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Component/Light/PointLightComponent.h"
#include "Component/Light/SpotLightComponent.h"
#include "Component/SkyAtmosphereComponent.h"
#include "Component/SkeletalMeshComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/TextRenderComponent.h"
#include "Component/WaterComponent.h"
#include "Core/ActorTags.h"
#include "Core/ResourceManager.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "GameFramework/OceanSystem.h"
#include "GameFramework/World.h"
#include "Render/Common/WaterRenderingCommon.h"

#include <cmath>

namespace
{
    UMaterialInterface* ResolveDefaultWaterMaterial()
    {
        UStaticMesh* DefaultWaterMesh = FResourceManager::Get().LoadStaticMesh(WaterDefaultAssets::MeshPath);
        if (DefaultWaterMesh == nullptr)
        {
            return nullptr;
        }

        const TArray<FStaticMeshMaterialSlot>& Slots = DefaultWaterMesh->GetMaterialSlots();
        return Slots.empty() ? nullptr : Slots[0].Material;
    }

    UStaticMesh* LoadOceanTileMesh()
    {
        UStaticMesh* TileMesh = FResourceManager::Get().LoadStaticMesh(WaterDefaultAssets::FlatTileMeshPath);
        if (TileMesh != nullptr)
        {
            return TileMesh;
        }

        static bool bFlatTileMissingLogged = false;
        if (!bFlatTileMissingLogged)
        {
            UE_LOG("[Ocean] Failed to load flat tile mesh (%s). Falling back to default wave mesh (%s).",
                WaterDefaultAssets::FlatTileMeshPath,
                WaterDefaultAssets::MeshPath);
            bFlatTileMissingLogged = true;
        }

        return FResourceManager::Get().LoadStaticMesh(WaterDefaultAssets::MeshPath);
    }

    float SnapToNearestGrid(float Value, float GridSize)
    {
        return std::round(Value / GridSize) * GridSize;
    }
}

DEFINE_CLASS(ASceneActor, AActor)
REGISTER_FACTORY(ASceneActor)

DEFINE_CLASS(AStaticMeshActor, AActor)
REGISTER_FACTORY(AStaticMeshActor)

DEFINE_CLASS(ASkeletalMeshActor, AActor)
REGISTER_FACTORY(ASkeletalMeshActor)

DEFINE_CLASS(AWaterActor, AActor)
REGISTER_FACTORY(AWaterActor)

DEFINE_CLASS(ACameraActor, AActor)
REGISTER_FACTORY(ACameraActor)

DEFINE_CLASS(ACineCameraActor, ACameraActor)
REGISTER_FACTORY(ACineCameraActor)

DEFINE_CLASS(AGlobalOceanActor, AActor)
REGISTER_FACTORY(AGlobalOceanActor)

DEFINE_CLASS(ASubUVActor, AActor)
REGISTER_FACTORY(ASubUVActor)

DEFINE_CLASS(ATextRenderActor, AActor)
REGISTER_FACTORY(ATextRenderActor)

DEFINE_CLASS(ABillboardActor, AActor)
REGISTER_FACTORY(ABillboardActor)

DEFINE_CLASS(ADecalActor, AActor)
REGISTER_FACTORY(ADecalActor)

DEFINE_CLASS(ALightActor, AActor)
// Base class, REGISTER_FACTORY 없음

DEFINE_CLASS(ADirectionalLightActor, ALightActor)
REGISTER_FACTORY(ADirectionalLightActor)

DEFINE_CLASS(AAmbientLightActor, ALightActor)
REGISTER_FACTORY(AAmbientLightActor)

DEFINE_CLASS(APointLightActor, ALightActor)
REGISTER_FACTORY(APointLightActor)

DEFINE_CLASS(ASpotLightActor, ALightActor)
REGISTER_FACTORY(ASpotLightActor)

DEFINE_CLASS(ASkyAtmosphereActor, AActor)
REGISTER_FACTORY(ASkyAtmosphereActor)

DEFINE_CLASS(AHeightFogActor, AActor)
REGISTER_FACTORY(AHeightFogActor)

void ASceneActor::InitDefaultComponents()
{
    auto SceneRoot = AddComponent<USceneComponent>();
    SetRootComponent(SceneRoot);

    UBillboardComponent* Billboard = AddComponent<UBillboardComponent>();
    Billboard->AttachToComponent(SceneRoot);
    Billboard->SetEditorOnly(true);
    Billboard->SetHiddenInEditor(true);
    Billboard->SetTexturePath("Asset/Texture/Icons/EmptyActor.PNG");
}

void AStaticMeshActor::InitDefaultComponents()
{
    auto* StaticMesh = AddComponent<UStaticMeshComponent>();
    SetRootComponent(StaticMesh);
}

void ASkeletalMeshActor::InitDefaultComponents()
{
    auto* SkeletalMesh = AddComponent<USkeletalMeshComponent>();
    SetRootComponent(SkeletalMesh);
}

USkeletalMeshComponent* ASkeletalMeshActor::GetSkeletalMeshComponent() const
{
    if (USkeletalMeshComponent* RootSkeletalMesh = Cast<USkeletalMeshComponent>(GetRootComponent()))
    {
        return RootSkeletalMesh;
    }

    for (UActorComponent* Component : GetComponents())
    {
        if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(Component))
        {
            return SkeletalMeshComponent;
        }
    }

    return nullptr;
}

void AWaterActor::InitDefaultComponents()
{
    auto* StaticMesh = AddComponent<UStaticMeshComponent>();
    SetRootComponent(StaticMesh);
    AddComponent<UWaterComponent>();

    UStaticMesh* WaterMesh = FResourceManager::Get().LoadStaticMesh(WaterDefaultAssets::MeshPath);
    if (WaterMesh == nullptr)
    {
        UE_LOG("[Water] Failed to load default water mesh: %s", WaterDefaultAssets::MeshPath);
        return;
    }

    StaticMesh->SetStaticMesh(WaterMesh);
    // Keep the mesh's own slot materials (OBJ/MTL or assigned material instance).
    // Runtime water behavior is driven per-object through UWaterComponent uniforms.
}

void ACameraActor::InitDefaultComponents()
{
    USceneComponent* SceneRoot = AddComponent<USceneComponent>();
    SetRootComponent(SceneRoot);

    UCameraComponent* CameraComponent = AddComponent<UCameraComponent>();
    CameraComponent->AttachToComponent(SceneRoot);

    UBillboardComponent* Billboard = AddComponent<UBillboardComponent>();
    Billboard->AttachToComponent(SceneRoot);
    Billboard->SetEditorOnly(true);
    Billboard->SetHiddenInEditor(true);
    Billboard->SetTexturePath("Asset/Texture/Pawn_64x.png");
}

UCameraComponent* ACameraActor::GetCameraComponent() const
{
    for (UActorComponent* Component : GetComponents())
    {
        if (UCameraComponent* CameraComponent = Cast<UCameraComponent>(Component))
        {
            return CameraComponent;
        }
    }

    return nullptr;
}

void ACineCameraActor::InitDefaultComponents()
{
    ACameraActor::InitDefaultComponents();
}

void AGlobalOceanActor::InitDefaultComponents()
{
    SetTickInEditor(true);

    OceanRoot = AddComponent<USceneComponent>();
    SetRootComponent(OceanRoot);

    WaterComponent = AddComponent<UWaterComponent>();

    GlobalWaterProfile = FOceanSystem::Get().GetWaterProfile();
    LastAppliedProfileRevision = FOceanSystem::Get().GetWaterProfileRevision();
    ApplyGlobalProfileToWaterComponent();

    RebuildRings();
    RegisterToOceanSystemIfNeeded();
    UpdateOceanFollowTransform();
}

AGlobalOceanActor::~AGlobalOceanActor()
{
    UnregisterFromOceanSystemIfNeeded();
}

void AGlobalOceanActor::BeginPlay()
{
    AActor::BeginPlay();
    RegisterToOceanSystemIfNeeded();
    UpdateOceanFollowTransform();
}

void AGlobalOceanActor::Tick(float DeltaTime)
{
    AActor::Tick(DeltaTime);
    (void)DeltaTime;

    const uint64 ProfileRevision = FOceanSystem::Get().GetWaterProfileRevision();
    if (ProfileRevision != LastAppliedProfileRevision)
    {
        GlobalWaterProfile = FOceanSystem::Get().GetWaterProfile();
        LastAppliedProfileRevision = ProfileRevision;
        ApplyGlobalProfileToWaterComponent();
    }

    UpdateOceanFollowTransform();
}

void AGlobalOceanActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnregisterFromOceanSystemIfNeeded();
    AActor::EndPlay(EndPlayReason);
}

void AGlobalOceanActor::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    AActor::GetEditableProperties(OutProps);

    OutProps.push_back({ "RingCount", EPropertyType::Int, &RingCount });
    OutProps.push_back({ "BaseTileSize", EPropertyType::Float, &BaseTileSize, 16.0f, 100000.0f, 1.0f });
    OutProps.push_back({ "RingScaleMultiplier", EPropertyType::Float, &RingScaleMultiplier, 1.0f, 8.0f, 0.01f });
    OutProps.push_back({ "FollowCamera", EPropertyType::Bool, &bFollowCamera });
    OutProps.push_back({ "SnapToGrid", EPropertyType::Bool, &bSnapToGrid });
    OutProps.push_back({ "SnapGridSize", EPropertyType::Float, &SnapGridSize, 1.0f, 100000.0f, 1.0f });
    OutProps.push_back({ "OceanHeight", EPropertyType::Float, &OceanHeight, -10000.0f, 10000.0f, 0.1f });

    OutProps.push_back({ "Ocean.NormalStrength", EPropertyType::Float, &GlobalWaterProfile.NormalStrength, 0.0f, 2.0f, 0.01f });
    OutProps.push_back({ "Ocean.Alpha", EPropertyType::Float, &GlobalWaterProfile.Alpha, 0.0f, 1.0f, 0.01f });
    OutProps.push_back({ "Ocean.ColorVariationStrength", EPropertyType::Float, &GlobalWaterProfile.ColorVariationStrength, 0.0f, 1.0f, 0.01f });
    OutProps.push_back({ "Ocean.NormalTilingAX", EPropertyType::Float, &GlobalWaterProfile.NormalTilingAX, 0.01f, 64.0f, 0.01f });
    OutProps.push_back({ "Ocean.NormalTilingAY", EPropertyType::Float, &GlobalWaterProfile.NormalTilingAY, 0.01f, 64.0f, 0.01f });
    OutProps.push_back({ "Ocean.NormalScrollSpeedAX", EPropertyType::Float, &GlobalWaterProfile.NormalScrollSpeedAX, -5.0f, 5.0f, 0.001f });
    OutProps.push_back({ "Ocean.NormalScrollSpeedAY", EPropertyType::Float, &GlobalWaterProfile.NormalScrollSpeedAY, -5.0f, 5.0f, 0.001f });
    OutProps.push_back({ "Ocean.NormalTilingBX", EPropertyType::Float, &GlobalWaterProfile.NormalTilingBX, 0.01f, 64.0f, 0.01f });
    OutProps.push_back({ "Ocean.NormalTilingBY", EPropertyType::Float, &GlobalWaterProfile.NormalTilingBY, 0.01f, 64.0f, 0.01f });
    OutProps.push_back({ "Ocean.NormalScrollSpeedBX", EPropertyType::Float, &GlobalWaterProfile.NormalScrollSpeedBX, -5.0f, 5.0f, 0.001f });
    OutProps.push_back({ "Ocean.NormalScrollSpeedBY", EPropertyType::Float, &GlobalWaterProfile.NormalScrollSpeedBY, -5.0f, 5.0f, 0.001f });
    OutProps.push_back({ "Ocean.WorldUVScaleX", EPropertyType::Float, &GlobalWaterProfile.WorldUVScaleX, 0.0001f, 1.0f, 0.0005f });
    OutProps.push_back({ "Ocean.WorldUVScaleY", EPropertyType::Float, &GlobalWaterProfile.WorldUVScaleY, 0.0001f, 1.0f, 0.0005f });
    OutProps.push_back({ "Ocean.WorldUVBlendFactor", EPropertyType::Float, &GlobalWaterProfile.WorldUVBlendFactor, 0.0f, 1.0f, 0.01f });
    OutProps.push_back({ "Ocean.BaseColor", EPropertyType::Color, &GlobalWaterProfile.BaseColor });
    OutProps.push_back({ "Ocean.WaterSpecularPower", EPropertyType::Float, &GlobalWaterProfile.WaterSpecularPower, 1.0f, 512.0f, 1.0f });
    OutProps.push_back({ "Ocean.WaterSpecularIntensity", EPropertyType::Float, &GlobalWaterProfile.WaterSpecularIntensity, 0.0f, 10.0f, 0.01f });
    OutProps.push_back({ "Ocean.WaterFresnelPower", EPropertyType::Float, &GlobalWaterProfile.WaterFresnelPower, 1.0f, 16.0f, 0.1f });
    OutProps.push_back({ "Ocean.WaterFresnelIntensity", EPropertyType::Float, &GlobalWaterProfile.WaterFresnelIntensity, 0.0f, 10.0f, 0.01f });
    OutProps.push_back({ "Ocean.WaterLightContributionScale", EPropertyType::Float, &GlobalWaterProfile.WaterLightContributionScale, 0.0f, 10.0f, 0.01f });
    OutProps.push_back({ "Ocean.HorizonFadeStart", EPropertyType::Float, &GlobalWaterProfile.HorizonFadeStart, -1.0f, 1.0f, 0.01f });
    OutProps.push_back({ "Ocean.HorizonFadeEnd", EPropertyType::Float, &GlobalWaterProfile.HorizonFadeEnd, -1.0f, 1.0f, 0.01f });
    OutProps.push_back({ "Ocean.NdotLFadeWidth", EPropertyType::Float, &GlobalWaterProfile.NdotLFadeWidth, 0.001f, 1.0f, 0.005f });
    OutProps.push_back({ "Ocean.EnableWaterSpecular", EPropertyType::Bool, &GlobalWaterProfile.bEnableWaterSpecular });
}

void AGlobalOceanActor::PostEditProperty(const char* PropertyName)
{
    AActor::PostEditProperty(PropertyName);
    (void)PropertyName;

    ClampSettings();
    FOceanSystem::Get().SetWaterProfile(GlobalWaterProfile);
    LastAppliedProfileRevision = FOceanSystem::Get().GetWaterProfileRevision();
    ApplyGlobalProfileToWaterComponent();
    RebuildRings();
    UpdateOceanFollowTransform();
}

void AGlobalOceanActor::ClampSettings()
{
    RingCount = MathUtil::Clamp(RingCount, 1, 16);
    BaseTileSize = MathUtil::Clamp(BaseTileSize, 16.0f, 100000.0f);
    RingScaleMultiplier = MathUtil::Clamp(RingScaleMultiplier, 1.0f, 8.0f);
    SnapGridSize = MathUtil::Clamp(SnapGridSize, 1.0f, 100000.0f);
}

void AGlobalOceanActor::RebuildRings()
{
    ClampSettings();

    for (UStaticMeshComponent* TileComponent : RingTiles)
    {
        if (TileComponent != nullptr)
        {
            RemoveComponent(TileComponent);
        }
    }
    RingTiles.clear();

    UStaticMesh* TileMesh = LoadOceanTileMesh();
    if (TileMesh == nullptr)
    {
        UE_LOG("[Ocean] Failed to build global ocean rings: no usable tile mesh.");
        return;
    }

    UMaterialInterface* SharedWaterMaterial = ResolveDefaultWaterMaterial();
    if (SharedWaterMaterial == nullptr)
    {
        static bool bMissingDefaultMaterialLogged = false;
        if (!bMissingDefaultMaterialLogged)
        {
            UE_LOG("[Ocean] Failed to find default water material from %s. Ocean tiles keep mesh-authored materials.",
                WaterDefaultAssets::MeshPath);
            bMissingDefaultMaterialLogged = true;
        }
    }

    float PreviousRingOuterHalfExtent = 0.0f;
    for (int32 RingIndex = 0; RingIndex < RingCount; ++RingIndex)
    {
        const float RingScale = BaseTileSize * std::pow(RingScaleMultiplier, static_cast<float>(RingIndex));

        auto AddRingTile = [&](float OffsetX, float OffsetY, float ScaleX, float ScaleY)
        {
            CreateOceanTile(
                TileMesh,
                SharedWaterMaterial,
                FVector(OffsetX, OffsetY, 0.0f),
                FVector(ScaleX, ScaleY, 1.0f));
        };

        if (RingIndex == 0)
        {
            AddRingTile(0.0f, 0.0f, RingScale, RingScale);
            PreviousRingOuterHalfExtent = RingScale * 0.5f;
            continue;
        }

        const float RingHalfExtent = RingScale * 0.5f;
        const float RingCenterOffset = PreviousRingOuterHalfExtent + RingHalfExtent;
        const float InnerSpan = PreviousRingOuterHalfExtent * 2.0f;

        // Keep the requested 8-piece ring layout, but use edge strips plus
        // corner quads so larger LOD rings still tile contiguously.
        AddRingTile(0.0f, +RingCenterOffset, InnerSpan, RingScale);
        AddRingTile(0.0f, -RingCenterOffset, InnerSpan, RingScale);
        AddRingTile(+RingCenterOffset, 0.0f, RingScale, InnerSpan);
        AddRingTile(-RingCenterOffset, 0.0f, RingScale, InnerSpan);

        AddRingTile(+RingCenterOffset, +RingCenterOffset, RingScale, RingScale);
        AddRingTile(-RingCenterOffset, +RingCenterOffset, RingScale, RingScale);
        AddRingTile(+RingCenterOffset, -RingCenterOffset, RingScale, RingScale);
        AddRingTile(-RingCenterOffset, -RingCenterOffset, RingScale, RingScale);

        PreviousRingOuterHalfExtent += RingScale;
    }
}

UStaticMeshComponent* AGlobalOceanActor::CreateOceanTile(
    UStaticMesh* TileMesh,
    UMaterialInterface* SharedWaterMaterial,
    const FVector& RelativeLocation,
    const FVector& RelativeScale)
{
    UStaticMeshComponent* Tile = AddComponent<UStaticMeshComponent>();
    Tile->AttachToComponent(OceanRoot ? OceanRoot : GetRootComponent());
    Tile->SetRelativeLocation(RelativeLocation);
    Tile->SetRelativeScale(RelativeScale);
    Tile->SetStaticMesh(TileMesh);
    if (SharedWaterMaterial != nullptr)
    {
        Tile->SetMaterial(0, SharedWaterMaterial);
    }

    RingTiles.push_back(Tile);
    return Tile;
}

void AGlobalOceanActor::ApplyGlobalProfileToWaterComponent()
{
    if (WaterComponent == nullptr)
    {
        return;
    }

    WaterComponent->ApplyWaterSurfaceProfile(GlobalWaterProfile);
}

void AGlobalOceanActor::UpdateOceanFollowTransform()
{
    if (!bFollowCamera)
    {
        return;
    }

    UWorld* World = GetFocusedWorld();
    if (World == nullptr || World->GetActiveCamera() == nullptr)
    {
        return;
    }

    FVector CameraLocation = World->GetActiveCamera()->GetLocation();
    float TargetX = CameraLocation.X;
    float TargetY = CameraLocation.Y;
    // Follow only camera XY; ocean height stays author-controlled for stable water level.
    if (bSnapToGrid)
    {
        TargetX = SnapToNearestGrid(TargetX, SnapGridSize);
        TargetY = SnapToNearestGrid(TargetY, SnapGridSize);
    }

    SetActorLocation(FVector(TargetX, TargetY, OceanHeight));
}

void AGlobalOceanActor::RegisterToOceanSystemIfNeeded()
{
    if (bOceanSystemRegistered)
    {
        return;
    }

    FOceanSystem::Get().RegisterGlobalOceanActor(this);
    bOceanSystemRegistered = true;
}

void AGlobalOceanActor::UnregisterFromOceanSystemIfNeeded()
{
    if (!bOceanSystemRegistered)
    {
        return;
    }

    FOceanSystem::Get().UnregisterGlobalOceanActor(this);
    bOceanSystemRegistered = false;
}

void AStaticMeshActor::BeginPlay()
{
    AActor::BeginPlay();

    if (CompareTag(ActorTags::Hazard))
    {
        for (UActorComponent* Comp : GetComponents())
        {
            USubUVComponent* SubUVComp = Cast<USubUVComponent>(Comp);
            if (!SubUVComp)
            {
                continue;
            }

            SubUVComp->SetLoop(false);
            SubUVComp->SetFrameIndex(0);
            SubUVComp->SetVisibility(false);
        }
    }

    for (UActorComponent* Comp : GetComponents())
    {
        UShapeComponent* ShapeComp = Cast<UShapeComponent>(Comp);
        if (!ShapeComp) continue;

        ShapeComp->SetGenerateOverlapEvents(true);

    }
}

void ASubUVActor::InitDefaultComponents()
{
    SetTickInEditor(true); // Editor Tick을 받도록 변경

    SubUVComponent = AddComponent<USubUVComponent>();
    SetRootComponent(SubUVComponent);
    SubUVComponent->SetParticle(FName("Explosion"));
    SubUVComponent->SetSpriteSize(2.0f, 2.0f);
    SubUVComponent->SetFrameRate(30.f);
}

void ATextRenderActor::InitDefaultComponents()
{
    UTextRenderComponent* Text = AddComponent<UTextRenderComponent>();
    SetRootComponent(Text);
    Text->SetFont(FName("Default"));
    Text->SetText("TextRender");
}

void ABillboardActor::InitDefaultComponents()
{
    UBillboardComponent* Billboard = AddComponent<UBillboardComponent>();
    SetRootComponent(Billboard);
    Billboard->SetTexturePath(("Asset/Texture/Pawn_64x.png"));
    Billboard->SetEditorOnly(true);
}

void ADecalActor::InitDefaultComponents()
{
    UDecalComponent* Decal = AddComponent<UDecalComponent>();
    SetRootComponent(Decal);

    UBillboardComponent* Billboard = AddComponent<UBillboardComponent>();
    Billboard->AttachToComponent(Decal);
    Billboard->SetEditorOnly(true);
    Billboard->SetTexturePath("Asset/Texture/Icons/S_DecalActorIcon.PNG");
}

void ADirectionalLightActor::InitDefaultComponents()
{
    UDirectionalLightComponent* DirLight = AddComponent<UDirectionalLightComponent>();
    SetRootComponent(DirLight);
    SetupBillboard(DirLight);
}

void AAmbientLightActor::InitDefaultComponents()
{
    UAmbientLightComponent* AmbientLight = AddComponent<UAmbientLightComponent>();
    SetRootComponent(AmbientLight);
    SetupBillboard(AmbientLight);
}

void APointLightActor::InitDefaultComponents()
{
    UPointLightComponent* PointLight = AddComponent<UPointLightComponent>();
    SetRootComponent(PointLight);
    SetupBillboard(PointLight);
}

void ASpotLightActor::InitDefaultComponents()
{
    USpotLightComponent* SpotLight = AddComponent<USpotLightComponent>();
    SetRootComponent(SpotLight);
    SetupBillboard(SpotLight);
}


void ASkyAtmosphereActor::InitDefaultComponents()
{
    USkyAtmosphereComponent* SkyAtmosphere = AddComponent<USkyAtmosphereComponent>();
    SetRootComponent(SkyAtmosphere);
    
    UBillboardComponent* Billboard = AddComponent<UBillboardComponent>();
    Billboard->AttachToComponent(SkyAtmosphere);
    Billboard->SetEditorOnly(true);
    Billboard->SetHiddenInEditor(true);
    Billboard->SetTexturePath("Asset/Texture/Icons/SkyLight.PNG");
}

void AHeightFogActor::InitDefaultComponents()
{
    UHeightFogComponent* HeightFog = AddComponent<UHeightFogComponent>();
    SetRootComponent(HeightFog);
    
    UBillboardComponent* Billboard = AddComponent<UBillboardComponent>();
    Billboard->AttachToComponent(HeightFog);
    Billboard->SetEditorOnly(true);
    Billboard->SetHiddenInEditor(true);
    Billboard->SetTexturePath("Asset/Texture/Icons/S_ExpoHeightFog.PNG");
}

void ALightActor::PostDuplicate(UObject* Original)
{
    AActor::PostDuplicate(Original);

    ULightComponentBase* LightComp = Cast<ULightComponentBase>(GetRootComponent());
    if (!LightComp)
    {
        for (UActorComponent* Comp : GetComponents())
        {
            if (LightComp = Cast<ULightComponentBase>(Comp))
                break;
        }
    }

    if (LightComp)
    {
        SetupBillboard(LightComp);
    }
}

void ALightActor::SetupBillboard(USceneComponent* Root)
{
    ULightComponentBase* LightComp = Cast<ULightComponentBase>(Root);
    if (LightComp && LightComp->GetBillboardTexturePath())
    {
        UBillboardComponent* Billboard = AddComponent<UBillboardComponent>();
        Billboard->AttachToComponent(LightComp);
        Billboard->SetEditorOnly(true);
        Billboard->SetHiddenInEditor(true);
        Billboard->SetTexturePath(LightComp->GetBillboardTexturePath());
    }
}
