#include "EditorComponentFactory.h"

#include "Component/BillboardComponent.h"
#include "Component/BoatWakeComponent.h"
#include "Component/HeightFogComponent.h"
#include "Component/Light/AmbientLightComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Component/Light/PointLightComponent.h"
#include "Component/Light/SpotLightComponent.h"
#include "Component/Movement/InterpToMovementComponent.h"
#include "Component/Movement/ProjectileMovementComponent.h"
#include "Component/Movement/PursuitMovementComponent.h"
#include "Component/Movement/RotatingMovementComponent.h"
#include "Component/SceneComponent.h"
#include "Component/Script/ScriptComponent.h"
#include "Component/ShapeComponent.h"
#include "Component/SkyAtmosphereComponent.h"
#include "Component/SpringArmComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SkeletalMeshComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/TextRenderComponent.h"
#include "Component/WaterComponent.h"
#include "Component/Movement/FloatingMovementComponent.h"

#include "Engine/GameFramework/Actor.h"

namespace
{
    constexpr const char* EmptyActorIconPath = "Asset/Texture/Icons/EmptyActor.PNG";
    constexpr const char* DefaultBillboardTexturePath = "Asset/Texture/Pawn_64x.png";
    constexpr const char* HeightFogIconPath = "Asset/Texture/Icons/S_ExpoHeightFog.PNG";

    template<typename TComponent>
    TComponent* AddComponent(AActor* Actor)
    {
        if (Actor == nullptr)
        {
            return nullptr;
        }

        return Actor->AddComponent<TComponent>();
    }

    UBillboardComponent* AttachEditorBillboard(
        AActor* Actor,
        USceneComponent* Parent,
        const char* TexturePath,
        bool bHiddenInEditor)
    {
        if (Actor == nullptr || Parent == nullptr || TexturePath == nullptr)
        {
            return nullptr;
        }

        UBillboardComponent* Billboard = AddComponent<UBillboardComponent>(Actor);
        if (Billboard == nullptr)
        {
            return nullptr;
        }

        Billboard->AttachToComponent(Parent);
        Billboard->SetEditorOnly(true);
        Billboard->SetHiddenInEditor(bHiddenInEditor);
        Billboard->SetTexturePath(TexturePath);
        return Billboard;
    }

    template<typename TComponent>
    UActorComponent* RegisterSimple(AActor* Actor)
    {
        return AddComponent<TComponent>(Actor);
    }

    template<typename TLightComponent>
    UActorComponent* RegisterLightWithBillboard(AActor* Actor)
    {
        TLightComponent* Component = AddComponent<TLightComponent>(Actor);
        if (Component == nullptr)
        {
            return nullptr;
        }

        AttachEditorBillboard(Actor, Component, TLightComponent::BillboardTexturePath, true);
        return Component;
    }

    UActorComponent* RegisterSceneComponent(AActor* Actor)
    {
        USceneComponent* Component = AddComponent<USceneComponent>(Actor);
        if (Component == nullptr)
        {
            return nullptr;
        }

        AttachEditorBillboard(Actor, Component, EmptyActorIconPath, true);
        return Component;
    }

    UActorComponent* RegisterSubUVComponent(AActor* Actor)
    {
        USubUVComponent* Component = AddComponent<USubUVComponent>(Actor);
        if (Component == nullptr)
        {
            return nullptr;
        }

        Component->SetParticle(FName("Explosion"));
        Component->SetSpriteSize(2.0f, 2.0f);
        Component->SetFrameRate(30.0f);
        return Component;
    }

    UActorComponent* RegisterTextRenderComponent(AActor* Actor)
    {
        UTextRenderComponent* Component = AddComponent<UTextRenderComponent>(Actor);
        if (Component == nullptr)
        {
            return nullptr;
        }

        Component->SetFont(FName("Default"));
        Component->SetText("TextRender");
        return Component;
    }

    UActorComponent* RegisterBillboardComponent(AActor* Actor)
    {
        UBillboardComponent* Component = AddComponent<UBillboardComponent>(Actor);
        if (Component == nullptr)
        {
            return nullptr;
        }

        Component->SetTexturePath(DefaultBillboardTexturePath);
        return Component;
    }

    UActorComponent* RegisterHeightFogComponent(AActor* Actor)
    {
        UHeightFogComponent* Component = AddComponent<UHeightFogComponent>(Actor);
        if (Component == nullptr)
        {
            return nullptr;
        }

        Component->SetFogDensity(0.0f);
        Component->SetFogInscatteringColor(FVector4(0.72f, 0.8f, 0.9f, 1.0f));
        AttachEditorBillboard(Actor, Component, HeightFogIconPath, false);
        return Component;
    }

    struct FComponentRegistrationSpec
    {
        const char* DisplayName;
        const char* Category;
        UActorComponent* (*Register)(AActor* Actor);
    };

    constexpr FComponentRegistrationSpec ComponentSpecs[] = {
        { "Scene Component", "Common", &RegisterSceneComponent },
        { "StaticMesh Component", "Common", &RegisterSimple<UStaticMeshComponent> },
        { "SkeletalMesh Component", "Common", &RegisterSimple<USkeletalMeshComponent> },
        { "Script Component", "Common", &RegisterSimple<UScriptComponent> },
        { "SubUV Component", "Common", &RegisterSubUVComponent },
        { "TextRender Component", "Common", &RegisterTextRenderComponent },
        { "Billboard Component", "Common", &RegisterBillboardComponent },
        { "BoatWake Component", "Common", &RegisterSimple<UBoatWakeComponent> },
        { "Water Component", "Common", &RegisterSimple<UWaterComponent> },
        { "HeightFog Component", "Common", &RegisterHeightFogComponent },
        { "SkyAtmosphere Component", "Common", &RegisterSimple<USkyAtmosphereComponent> },
        { "SpringArm Component", "Common", &RegisterSimple<USpringArmComponent> },

        { "RotatingMovement Component", "Movement", &RegisterSimple<URotatingMovementComponent> },
        { "InterpToMovement Component", "Movement", &RegisterSimple<UInterpToMovementComponent> },
        { "PursuitMovement Component", "Movement", &RegisterSimple<UPursuitMovementComponent> },
        { "ProjectileMovement Component", "Movement", &RegisterSimple<UProjectileMovementComponent> },
        { "FloatingMovement Component", "Movement", &RegisterSimple<UFloatingMovementComponent> },

        { "AmbientLight Component", "Light", &RegisterLightWithBillboard<UAmbientLightComponent> },
        { "DirectionalLight Component", "Light", &RegisterLightWithBillboard<UDirectionalLightComponent> },
        { "PointLight Component", "Light", &RegisterLightWithBillboard<UPointLightComponent> },
        { "SpotLight Component", "Light", &RegisterLightWithBillboard<USpotLightComponent> },

        { "Box Component", "Collision", &RegisterSimple<UBoxComponent> },
        { "Sphere Component", "Collision", &RegisterSimple<USphereComponent> },
        { "Capsule Component", "Collision", &RegisterSimple<UCapsuleComponent> },
    };
}

const TArray<FComponentMenuEntry>& FEditorComponentFactory::GetMenuRegistry()
{
    static const TArray<FComponentMenuEntry> Registry = []
    {
        TArray<FComponentMenuEntry> Entries;
        Entries.reserve(sizeof(ComponentSpecs) / sizeof(ComponentSpecs[0]));

        for (const FComponentRegistrationSpec& Spec : ComponentSpecs)
        {
            Entries.push_back({ Spec.DisplayName, Spec.Category, Spec.Register });
        }

        return Entries;
    }();

    return Registry;
}
