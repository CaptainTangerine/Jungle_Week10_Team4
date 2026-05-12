#include "RenderCollector.h"

#include "Render/LineBatcher.h"
#include "Render/RingBatcher.h"
#include "Collision/CollisionSystem.h"
#include "Render/Renderer/RenderFlow/ShadowAtlasManager.h"
#include "DriftSalvage/CollectionSystem.h"
#include "DriftSalvage/ExplosionSystem.h"
#include "GameFramework/World.h"
#include "GameFramework/Actor.h"
#include "Object/ActorIterator.h"
#include "Component/BillboardComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/ShapeComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/TextRenderComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Component/Light/LightComponent.h"
#include "Component/Light/PointLightComponent.h"
#include "Component/Light/SpotLightComponent.h"
#include "Engine/Geometry/Frustum.h"
#include <unordered_set>

namespace
{
    void AddDebugLightShape(const ULightComponent* LightComponent, FLineBatcher* LineBatcher)
    {
        if (LightComponent == nullptr || LineBatcher == nullptr || !LightComponent->IsDebugDrawEnabled() || !LightComponent->IsVisible())
        {
            return;
        }

        switch (LightComponent->GetLightType())
        {
        case ELightType::LightType_Directional:
        {
            const UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(LightComponent);
            if (Light != nullptr)
            {
                LineBatcher->AddDirectionalLight(
                    Light->GetWorldLocation(),
                    Light->GetForwardVector(),
                    Light->GetRightVector(),
                    Light->GetLightColor().ToVector4());
            }
            break;
        }
        case ELightType::LightType_Point:
        {
            const UPointLightComponent* Light = Cast<UPointLightComponent>(LightComponent);
            if (Light != nullptr)
            {
                LineBatcher->AddPointLight(
                    Light->GetWorldLocation(),
                    Light->GetAttenuationRadius(),
                    Light->GetRightVector(),
                    Light->GetUpVector());
            }
            break;
        }
        case ELightType::LightType_Spot:
        {
            const USpotLightComponent* Light = Cast<USpotLightComponent>(LightComponent);
            if (Light != nullptr)
            {
                LineBatcher->AddSpotLight(
                    Light->GetWorldLocation(),
                    Light->GetUpVector() * -1.0f,
                    Light->GetRightVector() * -1.0f,
                    Light->GetAttenuationRadius(),
                    Light->GetInnerConeAngle(),
                    Light->GetOuterConeAngle());
            }
            break;
        }
        default:
            break;
        }
    }

    // ─────────────────── Billboard, SubUV ───────────────────
    FMatrix MakeViewSubUVSelectionMatrix(const USubUVComponent* SubUVComp, const FRenderBus& RenderBus);

    // ─────────────────── AABB, BVH ───────────────────
    bool UsesCameraDependentRenderBounds(const UPrimitiveComponent* PrimitiveComponent);
    FAABB BuildQuadAABB(const FMatrix& WorldMatrix);
    FAABB BuildRenderAABB(const UPrimitiveComponent* PrimitiveComponent, const FRenderBus& RenderBus);
	void DrawDebugOverlap(FLineBatcher* LineBatcher, const FCollisionDebugContact& Contact);
	void DrawDebugContactPoint(FLineBatcher* LineBatcher, const FVector& Location, float Size);
    void DrawCollectionRing(UWorld* World, FRingBatcher* RingBatcher);
    void DrawExplosionDebugRings(UWorld* World, FLineBatcher* LineBatcher);

    bool IsRuntimeDebugShapeVisible(const UPrimitiveComponent* Primitive)
    {
        const UShapeComponent* Shape = Cast<UShapeComponent>(Primitive);
        return Shape && Shape->GetRuntimeDebugVisible();
    }
} // namespace

void FRenderCollector::ResetCullingStats()
{
    LastStats.Culling = {};
}

void FRenderCollector::ResetDecalStats()
{
    LastStats.Decal = {};
}

void FRenderCollector::ResetShadowStats()
{
    LastStats.Shadow = {};
}

void FRenderCollector::Initialize(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext)
{
    DeviceContext = InDeviceContext;
    MeshBufferManager.Create(InDevice);
    LightRenderCollector.Initialize(&MeshBufferManager);
    OverlayRenderCollector.Initialize(&MeshBufferManager);
    PrimitiveRenderCollector.Initialize(&MeshBufferManager);
}

void FRenderCollector::Release()
{
    LineBatcher = nullptr;
    RingBatcher = nullptr;
    DeviceContext = nullptr;
    LightRenderCollector.Release();
    OverlayRenderCollector.Release();
    PrimitiveRenderCollector.Release();
    MeshBufferManager.Release();
}

void FRenderCollector::CollectWorld(UWorld* World, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus,
                                    const FFrustum* ViewFrustum)
{
    ResetCullingStats();
    ResetDecalStats();
    ResetShadowStats();

    if (!World)
        return;

    PrepareSkinnedMeshResources(World);

    CollectLight(World, ShowFlags, RenderBus, ViewFrustum);
    if (ShowFlags.bShadow)
    {
        CollectShadowCasters(World, RenderBus);
    }

    if (ViewFrustum)
    {
        VisiblePrimitiveScratch.clear();
        World->GetSpatialIndex().FrustumQueryPrimitives(*ViewFrustum, VisiblePrimitiveScratch, FrustumQueryScratch);

        for (UPrimitiveComponent* Primitive : VisiblePrimitiveScratch)
        {
            if (Primitive == nullptr || UsesCameraDependentRenderBounds(Primitive) || !Primitive->IsEnableCull())
                continue;
            ++LastStats.Culling.BVHPassedPrimitiveCount;
            CollectFromComponent(Primitive, ShowFlags, ViewMode, RenderBus, World->GetWorldType());
        }
    }

    TSet<UPrimitiveComponent*> CollectCameraDependentPrimitives;
    if (ViewFrustum)
    {
        CollectCameraDependentPrimitives.reserve(32);
    }

    // Frustum이 없다면 액터 단위로 통째로 수집하고, 그렇지 않다면 BVH에서 누락된 컴포넌트들을 개별 수집
    for (TActorIterator<AActor> Iter(World); Iter; ++Iter)
    {
        AActor* Actor = *Iter;
        if (!Actor || !Actor->IsVisible())
            continue;

        if (!ViewFrustum)
        {
            for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
            {
                if (Primitive != nullptr && !Primitive->IsVisible())
                {
                    ++LastStats.Culling.TotalVisiblePrimitiveCount;
                }
            }
            CollectFromActor(Actor, ShowFlags, ViewMode, RenderBus, World->GetWorldType());
            continue; // early-continue
        }

        // 이미 처리된 컴포넌트, 중복된 컴포넌트는 제외하고 Frustum Culling 수행
        for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
        {
            if (!Primitive)
            {
                continue;
            }

            const bool bRuntimeDebugShapeVisible = IsRuntimeDebugShapeVisible(Primitive);
            if (!Primitive->IsVisible() && !bRuntimeDebugShapeVisible)
                continue;

            ++LastStats.Culling.TotalVisiblePrimitiveCount;

            const bool bIsCameraDependent = UsesCameraDependentRenderBounds(Primitive);
            if (!CollectCameraDependentPrimitives.insert(Primitive).second)
            {
                continue;
            }

            if (!bRuntimeDebugShapeVisible && !bIsCameraDependent && Primitive->IsEnableCull())
            {
                continue;
            }

            if (!bRuntimeDebugShapeVisible && bIsCameraDependent && Primitive->IsEnableCull())
            {
                if (ViewFrustum->Intersects(BuildRenderAABB(Primitive, RenderBus)) == FFrustum::EFrustumIntersectResult::Outside)
                    continue;
            }

            ++LastStats.Culling.FallbackPassedPrimitiveCount;
            CollectFromComponent(Primitive, ShowFlags, ViewMode, RenderBus, World->GetWorldType());
        }
    }
    if (ShowFlags.bCollisionDebug && LineBatcher != nullptr)
	{
		for (const FCollisionDebugContact& Contact : World->GetCollisionSystem().GetDebugContacts())
		{
			DrawDebugOverlap(LineBatcher, Contact);
		}
	}

    DrawCollectionRing(World, RingBatcher);
    DrawExplosionDebugRings(World, LineBatcher);
}

// ─────────────────── Sub Collects ────────────────────────────────────────────────────────────

// Frustum Culling을 통해 Light Collect와 Shadow Collect를 동시에 수행해줍니다.
void FRenderCollector::CollectLight(UWorld* World, const FShowFlags& ShowFlags, FRenderBus& RenderBus, const FFrustum* ViewFrustum)
{
    (void)ShowFlags;
    LightRenderCollector.CollectLight(World, RenderBus, LastStats, ViewFrustum);

    if (World == nullptr || LineBatcher == nullptr)
    {
        return;
    }

    for (const FLightSlot& Slot : World->GetWorldLightSlots())
    {
        const ULightComponent* LightComponent = Cast<ULightComponent>(Slot.LightData);
        if (!Slot.bAlive || LightComponent == nullptr)
        {
            continue;
        }

        AddDebugLightShape(LightComponent, LineBatcher);
    }
}

void FRenderCollector::CollectShadowCasters(UWorld* World, FRenderBus& RenderBus)
{
    LightRenderCollector.CollectShadowCasters(World, RenderBus);
}

void FRenderCollector::PrepareSkinnedMeshResources(UWorld* World)
{
    if (World == nullptr)
    {
        return;
    }

    ID3D11Device* Device = MeshBufferManager.GetDevice();
    ID3D11DeviceContext* Context = DeviceContext;
    if (Device == nullptr || Context == nullptr)
    {
        return;
    }

    for (TActorIterator<AActor> Iter(World); Iter; ++Iter)
    {
        AActor* Actor = *Iter;
        if (Actor == nullptr || !Actor->IsVisible())
        {
            continue;
        }

        for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
        {
            USkinnedMeshComponent* SkinnedMeshComp = Cast<USkinnedMeshComponent>(Primitive);
            if (SkinnedMeshComp == nullptr || !SkinnedMeshComp->IsVisible() || !SkinnedMeshComp->HasValidMesh())
            {
                continue;
            }

            if (!SkinnedMeshComp->InitializeSkinnedVerticesFromBindPose())
            {
                continue;
            }

            SkinnedMeshComp->UpdateCPUSkinning();
            SkinnedMeshComp->EnsureSkinnedMeshBuffer(Device);
            SkinnedMeshComp->UploadSkinnedVertices(Context);
        }
    }

    for (TActorIterator<AActor> Iter(World); Iter; ++Iter)
    {
        AActor* Actor = *Iter;
        if (Actor == nullptr || !Actor->IsVisible())
        {
            continue;
        }

        for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
        {
            UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Primitive);
            if (StaticMeshComp == nullptr || !StaticMeshComp->HasBoneAttachment())
            {
                continue;
            }

            StaticMeshComp->UpdateBoneAttachment();
        }
    }
}

void FRenderCollector::CollectSelection(const TArray<AActor*>& SelectedActors, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus)
{
    OverlayRenderCollector.CollectSelection(SelectedActors, ShowFlags, ViewMode, RenderBus, LineBatcher);
}

bool FRenderCollector::CollectPickedComponent(UPrimitiveComponent* Component, FRenderBus& RenderBus)
{
    return OverlayRenderCollector.CollectPickedComponent(Component, RenderBus);
}

void FRenderCollector::CollectGrid(
    float GridSpacing,
    int32 GridHalfLineCount,
    FRenderBus& RenderBus,
    bool bOrthographic,
    const FGridRenderSettings& GridRenderSettings)
{
    OverlayRenderCollector.CollectGrid(GridSpacing, GridHalfLineCount, RenderBus, bOrthographic, GridRenderSettings);
}

void FRenderCollector::CollectGizmo(UGizmoComponent* Gizmo, const FShowFlags& ShowFlags, FRenderBus& RenderBus, bool bIsActiveOperation)
{
    OverlayRenderCollector.CollectGizmo(Gizmo, ShowFlags, RenderBus, bIsActiveOperation);
}

void FRenderCollector::CollectFromActor(AActor* Actor, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, EWorldType WorldType)
{
    PrimitiveRenderCollector.CollectFromActor(Actor, ShowFlags, ViewMode, RenderBus, WorldType, LastStats, LineBatcher);
}

void FRenderCollector::CollectFromComponent(UPrimitiveComponent* Primitive, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, EWorldType WorldType)
{
    PrimitiveRenderCollector.CollectFromComponent(Primitive, ShowFlags, ViewMode, RenderBus, WorldType, LastStats, LineBatcher);
}

void FRenderCollector::CollectBVHInternalNodeAABBs(UPrimitiveComponent* PrimitiveComponent, const FShowFlags& ShowFlags, FRenderBus& RenderBus, TSet<int32>& SeenNodeIndices)
{
    OverlayRenderCollector.CollectBVHInternalNodeAABBs(PrimitiveComponent, ShowFlags, RenderBus, LineBatcher, SeenNodeIndices);
}

// ─────────────────── namespace ────────────────────────────────────────────────────────────

namespace
    {
    bool UsesCameraDependentRenderBounds(const UPrimitiveComponent* PrimitiveComponent)
    {
        if (PrimitiveComponent == nullptr)
        {
            return false;
        }

        switch (PrimitiveComponent->GetPrimitiveType())
        {
        case EPrimitiveType::EPT_Billboard:
        case EPrimitiveType::EPT_Text:
        case EPrimitiveType::EPT_SubUV:
            return true;
        default:
            return false;
        }
    }

    void DrawDebugOverlap(FLineBatcher* LineBatcher, const FCollisionDebugContact& Contact)
	{
		if (LineBatcher == nullptr)
		{
			return;
		}

		if (Contact.bHasOverlapBounds)
		{
			LineBatcher->AddAABB(Contact.OverlapBounds, FColor::Yellow());
		}

		DrawDebugContactPoint(LineBatcher, Contact.Location, 4.0f);
	}

	void DrawDebugContactPoint(FLineBatcher* LineBatcher, const FVector& Location, float Size)
	{
		if (LineBatcher == nullptr)
		{
			return;
		}

		const FVector4 Color = FColor::Yellow().ToVector4();

		LineBatcher->AddLine(
			Location - FVector(Size, 0.0f, 0.0f),
			Location + FVector(Size, 0.0f, 0.0f),
			Color);

		LineBatcher->AddLine(
			Location - FVector(0.0f, Size, 0.0f),
			Location + FVector(0.0f, Size, 0.0f),
			Color);

		LineBatcher->AddLine(
			Location - FVector(0.0f, 0.0f, Size),
			Location + FVector(0.0f, 0.0f, Size),
			Color);
	}

    void DrawCollectionRing(UWorld* World, FRingBatcher* RingBatcher)
    {
        if (!World || !RingBatcher)
        {
            return;
        }

        const FCollectionSystem& Collection = World->GetCollectionSystem();
        if (!Collection.IsRingVisible())
        {
            return;
        }

        const float OuterRadius = Collection.GetRingRadius();
        if (OuterRadius <= 0.0f)
        {
            return;
        }

        const float Thickness = std::max(0.15f, OuterRadius * 0.06f);
        const float InnerRadius = std::max(0.0f, OuterRadius - Thickness);

        RingBatcher->AddRing(
            Collection.GetRingCenter(),
            FVector::ForwardVector,
            FVector::RightVector,
            InnerRadius,
            OuterRadius,
            Collection.GetRingColor().ToVector4());
    }

void DrawExplosionDebugRings(UWorld* World, FLineBatcher* LineBatcher)
    {
        return;
        if (!World || !LineBatcher)
        {
            return;
        }

        const FExplosionSystem& Explosion = World->GetExplosionSystem();
        for (const FExplosionSystem::FDebugRing& Ring : Explosion.GetDebugRings())
        {
            if (Ring.Radius <= 0.0f || Ring.Lifetime <= 0.0f)
            {
                continue;
            }

            // 시간이 지날수록 ring이 커지면서 fade out — 충격파 느낌.
            const float t = std::min(1.0f, Ring.Age / Ring.Lifetime);
            const float DrawRadius = Ring.Radius * (0.6f + 0.4f * t);
            const float Alpha = std::max(0.05f, 1.0f - t);

            const FVector4 Color(1.0f, 0.45f, 0.1f, Alpha); // 주황색

            // 3축 평면 wireframe circle — 폭발 구체 형태로 시각화.
            LineBatcher->AddCircle(Ring.Center, FVector::ForwardVector, FVector::RightVector, DrawRadius, Color);
            LineBatcher->AddCircle(Ring.Center, FVector::ForwardVector, FVector::UpVector,    DrawRadius, Color);
            LineBatcher->AddCircle(Ring.Center, FVector::RightVector,   FVector::UpVector,    DrawRadius, Color);
        }
    }

    FMatrix MakeViewSubUVSelectionMatrix(const USubUVComponent* SubUVComp, const FRenderBus& RenderBus)
    {
        const FVector WorldScale = SubUVComp->GetWorldScale();
        return USubUVComponent::MakeBillboardWorldMatrix(
            SubUVComp->GetWorldLocation(),
            FVector(
                WorldScale.X > 0.01f ? WorldScale.X : 0.01f,
                SubUVComp->GetWidth() * WorldScale.Y,
                SubUVComp->GetHeight() * WorldScale.Z),
            RenderBus.GetCameraForward(),
            RenderBus.GetCameraRight(),
            RenderBus.GetCameraUp());
    }

    // BillBoardComponent를 상속받은 text, SubUV가 사용하는 AABB 계산함수(의존성 분리)
    FAABB BuildQuadAABB(const FMatrix& WorldMatrix)
    {
        static constexpr FVector LocalQuadCorners[4] = {
            FVector(0.0f, -0.5f, 0.5f),
            FVector(0.0f, 0.5f, 0.5f),
            FVector(0.0f, 0.5f, -0.5f),
            FVector(0.0f, -0.5f, -0.5f)
        };

        FAABB Box;
        Box.Reset();

        for (const FVector& Corner : LocalQuadCorners)
        {
            Box.Expand(WorldMatrix.TransformPosition(Corner));
        }

        return Box;
    }

    FAABB BuildRenderAABB(const UPrimitiveComponent* PrimitiveComponent, const FRenderBus& RenderBus)
    {
        switch (PrimitiveComponent->GetPrimitiveType())
        {
        case EPrimitiveType::EPT_Billboard:
        {
            const UBillboardComponent* BillboardComponent = static_cast<const UBillboardComponent*>(PrimitiveComponent);
            return BuildQuadAABB(UBillboardComponent::MakeBillboardWorldMatrix(
                BillboardComponent->GetWorldLocation(),
                FVector(0.00f, BillboardComponent->GetWidth(), BillboardComponent->GetHeight()),
                RenderBus.GetCameraForward(),
                RenderBus.GetCameraRight(),
                RenderBus.GetCameraUp()));
        }
        case EPrimitiveType::EPT_Text:
        {
            const UTextRenderComponent* TextComp = static_cast<const UTextRenderComponent*>(PrimitiveComponent);
            return BuildQuadAABB(TextComp->GetTextMatrix());
        }
        case EPrimitiveType::EPT_SubUV:
        {
            const USubUVComponent* SubUVComp = static_cast<const USubUVComponent*>(PrimitiveComponent);
            return BuildQuadAABB(MakeViewSubUVSelectionMatrix(SubUVComp, RenderBus));
        }

        default:
            return PrimitiveComponent->GetWorldAABB();
        }
    }
} // namespace
