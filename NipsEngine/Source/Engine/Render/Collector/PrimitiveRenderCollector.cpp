#include "PrimitiveRenderCollector.h"

#include "Component/BillboardComponent.h"
#include "Component/DecalComponent.h"
#include "Component/HeightFogComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SkyAtmosphereComponent.h"
#include "Component/ShapeComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/TextRenderComponent.h"
#include "Component/WaterComponent.h"
#include "Component/Light/LightComponent.h"
#include "Core/ResourceManager.h"
#include "Engine/Asset/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "GameFramework/World.h"
#include "Geometry/OBB.h"
#include "Render/LineBatcher.h"
#include "Render/Common/WaterRenderingCommon.h"
#include "Render/Renderer/RenderFlow/LightCullingPass.h"
#include "Render/Resource/Material.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Runtime/Stats/ScopeCycleCounter.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <unordered_set>

namespace
{
    struct FResolvedWaterTextures
    {
        UTexture* Diffuse = nullptr;
        UTexture* NormalA = nullptr;
        UTexture* NormalB = nullptr;
        bool bHasDiffuseMap = false;
        bool bHasNormalA = false;
        bool bHasNormalB = false;
    };

    bool TryGetTextureParam(UMaterialInterface* Material, const FString& ParamName, UTexture*& OutTexture)
    {
        OutTexture = nullptr;
        if (Material == nullptr)
        {
            return false;
        }

        FMaterialParamValue ParamValue;
        if (!Material->GetParam(ParamName, ParamValue) ||
            ParamValue.Type != EMaterialParamType::Texture ||
            !std::holds_alternative<UTexture*>(ParamValue.Value))
        {
            return false;
        }

        OutTexture = std::get<UTexture*>(ParamValue.Value);
        return true;
    }

    template<typename T>
    T* FindComponentByType(const AActor* Actor)
    {
        if (Actor == nullptr)
        {
            return nullptr;
        }

        for (UActorComponent* Component : Actor->GetComponents())
        {
            if (T* TypedComponent = Cast<T>(Component))
            {
                return TypedComponent;
            }
        }

        return nullptr;
    }

    bool ShouldUseWaterPath(const UStaticMeshComponent* StaticMeshComp, UMaterialInterface* Material, const UWaterComponent*& OutWaterComponent)
    {
        OutWaterComponent = StaticMeshComp ? FindComponentByType<UWaterComponent>(StaticMeshComp->GetOwner()) : nullptr;
        // Actor-owned UWaterComponent can opt a mesh into the water path even if
        // the shared base material is otherwise generic.
        return OutWaterComponent != nullptr || (Material != nullptr && Material->IsWaterMaterial());
    }

    float GetWaterElapsedSeconds()
    {
        static const auto WaterTimeStart = std::chrono::steady_clock::now();
        const auto WaterTimeNow = std::chrono::steady_clock::now();
        return std::chrono::duration<float>(WaterTimeNow - WaterTimeStart).count();
    }

    uint32 GetClampedWaterLocalLightIterationCount()
    {
        static bool bWaterLocalLightClampWarned = false;

        const FLightCullingOutputs& LightOutputs = FLightCullingPass::GetOutputs();
        const uint32 RawPointLightCount = LightOutputs.PointLightCount;
        const uint32 RawSpotLightCount = LightOutputs.SpotLightCount;
        // Water uses one per-draw scalar to cap both point and spot loops in Water.hlsl.
        const uint32 RawMaxLocalLightCount = std::max(RawPointLightCount, RawSpotLightCount);
        const uint32 ClampedLocalLightCount = std::min(RawMaxLocalLightCount, WaterRenderingLimits::MaxLocalLights);
        if ((RawPointLightCount > WaterRenderingLimits::MaxLocalLights ||
                RawSpotLightCount > WaterRenderingLimits::MaxLocalLights) &&
            !bWaterLocalLightClampWarned)
        {
            UE_LOG("[Water] Local point/spot light count (%u/%u) exceeds water shader limit (%u). Clamping for stable Stage 2 specular.",
                RawPointLightCount,
                RawSpotLightCount,
                WaterRenderingLimits::MaxLocalLights);
            bWaterLocalLightClampWarned = true;
        }

        return ClampedLocalLightCount;
    }

    void BuildWaterUniformData(const UWaterComponent* WaterComponent, uint32 LocalLightCount, FWaterRenderData& OutWater)
    {
        OutWater.bValid = true;
        OutWater.UniformData = FWaterUniformData{};

        const float WaterElapsedSeconds = GetWaterElapsedSeconds();
        if (WaterComponent != nullptr)
        {
            WaterComponent->FillWaterUniformData(OutWater.UniformData, WaterElapsedSeconds, LocalLightCount);
            return;
        }

        OutWater.UniformData.Time = WaterElapsedSeconds;
        OutWater.UniformData.WaterLocalLightCount = LocalLightCount;
    }

    void ApplyWaterTextureFallbacks(const FString& MaterialName, FResolvedWaterTextures& InOutTextures)
    {
        static std::unordered_set<FString> MissingNormalWarnedMaterials;
        static std::unordered_set<FString> MissingNormalAWarnedMaterials;
        static std::unordered_set<FString> MissingNormalBWarnedMaterials;

        UTexture* DefaultNormal = FResourceManager::Get().GetTexture("DefaultNormal");
        UTexture* DefaultWhite = FResourceManager::Get().GetTexture("DefaultWhite");

        if (InOutTextures.NormalA == nullptr)
        {
            if (MissingNormalAWarnedMaterials.insert(MaterialName).second)
            {
                UE_LOG("[Water] Material '%s' is missing WaterNormalA/NormalMap. Using fallback flat normal.", MaterialName.c_str());
            }
            InOutTextures.NormalA = DefaultNormal;
        }

        if (InOutTextures.NormalB == nullptr)
        {
            if (MissingNormalBWarnedMaterials.insert(MaterialName).second)
            {
                UE_LOG("[Water] Material '%s' is missing WaterNormalB. Water animation will use single-normal mode.",
                    MaterialName.c_str());
            }
            InOutTextures.NormalB = DefaultNormal;
        }

        InOutTextures.bHasNormalA = InOutTextures.NormalA != nullptr && InOutTextures.NormalA != DefaultNormal;
        InOutTextures.bHasNormalB = InOutTextures.NormalB != nullptr && InOutTextures.NormalB != DefaultNormal;
        InOutTextures.bHasDiffuseMap = InOutTextures.Diffuse != nullptr && InOutTextures.Diffuse != DefaultWhite;

        if (!InOutTextures.bHasNormalA && !InOutTextures.bHasNormalB)
        {
            if (MissingNormalWarnedMaterials.insert(MaterialName).second)
            {
                UE_LOG("[Water] Material '%s' has no water normal textures. Falling back to BaseColor-only animation.",
                    MaterialName.c_str());
            }
        }

        if (InOutTextures.Diffuse == nullptr)
        {
            InOutTextures.Diffuse = DefaultWhite;
        }
    }

    FResolvedWaterTextures ResolveWaterTextures(UMaterialInterface* Material)
    {
        FResolvedWaterTextures ResolvedTextures;
        if (Material == nullptr)
        {
            return ResolvedTextures;
        }

        UTexture* LegacyNormalMap = nullptr;
        TryGetTextureParam(Material, WaterMaterialParameterNames::DiffuseMap, ResolvedTextures.Diffuse);
        TryGetTextureParam(Material, WaterMaterialParameterNames::NormalMap, LegacyNormalMap);
        TryGetTextureParam(Material, WaterMaterialParameterNames::WaterNormalA, ResolvedTextures.NormalA);
        TryGetTextureParam(Material, WaterMaterialParameterNames::WaterNormalB, ResolvedTextures.NormalB);

        // Priority contract:
        // - NormalA = WaterNormalA if authored, otherwise legacy NormalMap.
        // - NormalB = WaterNormalB only (optional). If absent, water runs single-normal mode.
        if (ResolvedTextures.NormalA == nullptr)
        {
            ResolvedTextures.NormalA = LegacyNormalMap;
        }

        ApplyWaterTextureFallbacks(Material->GetName(), ResolvedTextures);
        return ResolvedTextures;
    }

    void CopyWaterTextureBindings(const FResolvedWaterTextures& ResolvedTextures, FWaterRenderData& OutWater)
    {
        OutWater.UniformData.bHasNormalMapA = ResolvedTextures.bHasNormalA ? 1u : 0u;
        OutWater.UniformData.bHasNormalMapB = ResolvedTextures.bHasNormalB ? 1u : 0u;
        OutWater.UniformData.bHasDiffuseMap = ResolvedTextures.bHasDiffuseMap ? 1u : 0u;
        OutWater.Diffuse = ResolvedTextures.Diffuse;
        OutWater.NormalA = ResolvedTextures.NormalA;
        OutWater.NormalB = ResolvedTextures.NormalB;
    }

    bool IsWaterAssetPath(const FString& MeshAssetPath)
    {
        return MeshAssetPath.find("Asset/Mesh/Water/") != FString::npos ||
            MeshAssetPath.find("Asset\\Mesh\\Water\\") != FString::npos;
    }

    void ConfigureWaterRenderData(const UStaticMeshComponent* StaticMeshComp, UMaterialInterface* Material, FRenderCommand& Cmd)
    {
        if (StaticMeshComp == nullptr || Material == nullptr)
        {
            return;
        }

        const UWaterComponent* WaterComponent = nullptr;
        if (!ShouldUseWaterPath(StaticMeshComp, Material, WaterComponent))
        {
            return;
        }

        // Water resource contract:
        // - b2: FWaterUniformData
        // - t0: WaterNormalA, t1: WaterNormalB, t2: DiffuseMap
        BuildWaterUniformData(WaterComponent, GetClampedWaterLocalLightIterationCount(), Cmd.Water);
        CopyWaterTextureBindings(ResolveWaterTextures(Material), Cmd.Water);
    }

    FMatrix MakeViewBillboardMatrix(const UPrimitiveComponent* Primitive, const FRenderBus& RenderBus)
    {
        const FMatrix WorldMatrix = Primitive->GetWorldMatrix();
        return UBillboardComponent::MakeBillboardWorldMatrix(
            WorldMatrix.GetOrigin(),
            WorldMatrix.GetScaleVector(),
            RenderBus.GetCameraForward(),
            RenderBus.GetCameraRight(),
            RenderBus.GetCameraUp());
    }

    float DebugMaxAbs3(const FVector& V)
	{
		return std::max({ std::fabs(V.X), std::fabs(V.Y), std::fabs(V.Z) });
	}

	float DebugCapsuleRadiusScale(const FVector& Scale)
	{
		return std::max(std::fabs(Scale.X), std::fabs(Scale.Y));
	}

	float DebugCapsuleHeightScale(const FVector& Scale)
	{
		return std::fabs(Scale.Z);
	}

	void AddOrientedBox(
		FLineBatcher* LineBatcher,
		const FVector& Center,
		const FVector& AxisX,
		const FVector& AxisY,
		const FVector& AxisZ,
		const FVector& Extent,
		const FColor& Color)
	{
		if (!LineBatcher)
		{
			return;
		}

		const FVector X = AxisX * Extent.X;
		const FVector Y = AxisY * Extent.Y;
		const FVector Z = AxisZ * Extent.Z;
		const FVector4 LineColor = Color.ToVector4();

		const FVector V[8] =
		{
			Center - X - Y - Z,
			Center + X - Y - Z,
			Center - X + Y - Z,
			Center + X + Y - Z,
			Center - X - Y + Z,
			Center + X - Y + Z,
			Center - X + Y + Z,
			Center + X + Y + Z,
		};

		const int32 Edges[12][2] =
		{
			{0, 1}, {1, 3}, {3, 2}, {2, 0},
			{4, 5}, {5, 7}, {7, 6}, {6, 4},
			{0, 4}, {1, 5}, {2, 6}, {3, 7},
		};

		for (const auto& Edge : Edges)
		{
			LineBatcher->AddLine(V[Edge[0]], V[Edge[1]], LineColor);
		}
	}

	void AddCapsuleProfile(
		FLineBatcher* LineBatcher,
		const FVector& Center,
		const FVector& Up,
		const FVector& Radial,
		float Radius,
		float CylinderHalfHeight,
		const FVector4& Color)
	{
		if (!LineBatcher)
		{
			return;
		}

		constexpr int32 ArcSegments = 12;
		const FVector TopCenter = Center + Up * CylinderHalfHeight;
		const FVector BottomCenter = Center - Up * CylinderHalfHeight;

		LineBatcher->AddLine(BottomCenter + Radial * Radius, TopCenter + Radial * Radius, Color);
		LineBatcher->AddLine(BottomCenter - Radial * Radius, TopCenter - Radial * Radius, Color);

		FVector Prev = TopCenter + Radial * Radius;
		for (int32 i = 1; i <= ArcSegments; ++i)
		{
			const float T = static_cast<float>(i) / static_cast<float>(ArcSegments);
			const float Angle = T * 3.1415926535f;
			const FVector Current = TopCenter + Radial * std::cos(Angle) * Radius + Up * std::sin(Angle) * Radius;
			LineBatcher->AddLine(Prev, Current, Color);
			Prev = Current;
		}

		Prev = BottomCenter - Radial * Radius;
		for (int32 i = 1; i <= ArcSegments; ++i)
		{
			const float T = static_cast<float>(i) / static_cast<float>(ArcSegments);
			const float Angle = 3.1415926535f + T * 3.1415926535f;
			const FVector Current = BottomCenter + Radial * std::cos(Angle) * Radius + Up * std::sin(Angle) * Radius;
			LineBatcher->AddLine(Prev, Current, Color);
			Prev = Current;
		}
	}

	void DrawCollisionShapeDebug(UShapeComponent* Shape, FLineBatcher* LineBatcher)
	{
		if (!Shape || !LineBatcher)
		{
			return;
		}

		const FColor Color = Shape->GetRuntimeDebugVisible()
			? Shape->GetRuntimeDebugColor()
			: Shape->GetShapeColor();

		if (USphereComponent* Sphere = Cast<USphereComponent>(Shape))
		{
			const FVector Center = Sphere->GetWorldLocation();
			const float Radius = Sphere->GetSphereRadius() * DebugMaxAbs3(Sphere->GetWorldAxisScale());
			const FVector Right = Sphere->GetRightVector();
			const FVector Up = Sphere->GetUpVector();
			const FVector Forward = Sphere->GetForwardVector();

			LineBatcher->AddCircle(Center, Right, Up, Radius, Color.ToVector4());
			LineBatcher->AddCircle(Center, Right, Forward, Radius, Color.ToVector4());
			LineBatcher->AddCircle(Center, Forward, Up, Radius, Color.ToVector4());

			constexpr int32 ExtraCircleCount = 4;
			for (int32 i = 1; i < ExtraCircleCount; ++i)
			{
				const float T = static_cast<float>(i) / static_cast<float>(ExtraCircleCount);
				const float Angle = (-0.5f + T) * 3.1415926535f;
				const float SliceOffset = std::sin(Angle) * Radius;
				const float SliceRadius = std::cos(Angle) * Radius;

				LineBatcher->AddCircle(Center + Forward * SliceOffset, Right, Up, SliceRadius, Color.ToVector4());
				LineBatcher->AddCircle(Center + Right * SliceOffset, Forward, Up, SliceRadius, Color.ToVector4());
				LineBatcher->AddCircle(Center + Up * SliceOffset, Forward, Right, SliceRadius, Color.ToVector4());
			}
			return;
		}

		if (UBoxComponent* Box = Cast<UBoxComponent>(Shape))
		{
			const FVector LocalExtent = Box->GetBoxExtent();
			const FVector WorldScale = Box->GetWorldAxisScale();
			const FVector WorldExtent(
				std::fabs(LocalExtent.X * WorldScale.X),
				std::fabs(LocalExtent.Y * WorldScale.Y),
				std::fabs(LocalExtent.Z * WorldScale.Z));

			AddOrientedBox(
				LineBatcher,
				Box->GetWorldLocation(),
				Box->GetForwardVector(),
				Box->GetRightVector(),
				Box->GetUpVector(),
				WorldExtent,
				Color);
			return;
		}

		if (UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Shape))
		{
			const FVector Center = Capsule->GetWorldLocation();
			const FVector Up = Capsule->GetUpVector();
			const FVector Right = Capsule->GetRightVector();
			const FVector Forward = Capsule->GetForwardVector();
			const FVector WorldScale = Capsule->GetWorldAxisScale();
			const float Radius = Capsule->GetCapsuleRadius() * DebugCapsuleRadiusScale(WorldScale);
			const float HalfHeight = std::max(Capsule->GetCapsuleHalfHeight() * DebugCapsuleHeightScale(WorldScale), Radius);
			const float CylinderHalfHeight = std::max(0.0f, HalfHeight - Radius);
			const FVector TopCenter = Center + Up * CylinderHalfHeight;
			const FVector BottomCenter = Center - Up * CylinderHalfHeight;

			LineBatcher->AddCircle(TopCenter, Right, Forward, Radius, Color.ToVector4());
			LineBatcher->AddCircle(BottomCenter, Right, Forward, Radius, Color.ToVector4());

			constexpr int32 CylinderRingCount = 3;
			for (int32 i = 1; i <= CylinderRingCount; ++i)
			{
				const float T = static_cast<float>(i) / static_cast<float>(CylinderRingCount + 1);
				const float Offset = -CylinderHalfHeight + (CylinderHalfHeight * 2.0f * T);
				LineBatcher->AddCircle(Center + Up * Offset, Right, Forward, Radius, Color.ToVector4());
			}

			constexpr int32 MeridianCount = 8;
			constexpr float Pi = 3.1415926535f;
			for (int32 i = 0; i < MeridianCount; ++i)
			{
				const float Angle = (static_cast<float>(i) / static_cast<float>(MeridianCount)) * Pi;
				const FVector Radial = Right * std::cos(Angle) + Forward * std::sin(Angle);
				AddCapsuleProfile(LineBatcher, Center, Up, Radial, Radius, CylinderHalfHeight, Color.ToVector4());
			}

			return;
		}

		LineBatcher->AddAABB(Shape->GetWorldAABB(), Color);
	}

    int32 SelectLODLevel(const FVector& CameraPos, const FAABB& Bounds, const FMatrix& ProjMatrix, int32 ValidLODCount)
    {
        bool IsOrthoGraphic = (std::abs(ProjMatrix.M[3][3] - 1.0f) < 1e-4f);
        if (ValidLODCount <= 1 || IsOrthoGraphic) return 0;

        const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
        const FVector Extent = (Bounds.Max - Bounds.Min) * 0.5f;
        const float SphereRadius = std::sqrt(Extent.X * Extent.X + Extent.Y * Extent.Y + Extent.Z * Extent.Z);

        const FVector Diff = Center - CameraPos;
        const float Dist = std::sqrt(Diff.X * Diff.X + Diff.Y * Diff.Y + Diff.Z * Diff.Z);

        if (Dist <= 1e-4f) return 0;

        const float ProjectedRadius = (SphereRadius / Dist) * ProjMatrix.M[2][1];
        const float ScreenCoverage = ProjectedRadius;

        static constexpr float Thresholds[] = { 0.15f, 0.08f, 0.05f, 0.02f };
        static constexpr int32 ThresholdCount = static_cast<int32>(sizeof(Thresholds) / sizeof(Thresholds[0]));

        const int32 MaxLOD = ValidLODCount - 1;
        for (int32 LOD = 0; LOD < MaxLOD; ++LOD)
        {
            float Threshold = (LOD < ThresholdCount) ? Thresholds[LOD] : 0.0f;
            if (ScreenCoverage >= Threshold)
                return LOD;
        }

        return MaxLOD;
    }
}

void FPrimitiveRenderCollector::CollectFromActor(
    AActor* Actor,
    const FShowFlags& ShowFlags,
    EViewMode ViewMode,
    FRenderBus& RenderBus,
    EWorldType WorldType,
    FRenderCollectionStats& LastStats,
    FLineBatcher* LineBatcher)
{
    if (Actor == nullptr || !Actor->IsVisible()) return;

    for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
    {
        CollectFromComponent(Primitive, ShowFlags, ViewMode, RenderBus, WorldType, LastStats, LineBatcher);
    }
}

void FPrimitiveRenderCollector::CollectFromComponent(
    UPrimitiveComponent* Primitive,
    const FShowFlags& ShowFlags,
    EViewMode ViewMode,
    FRenderBus& RenderBus,
    EWorldType WorldType,
    FRenderCollectionStats& LastStats,
    FLineBatcher* LineBatcher)
{
    if (Primitive == nullptr || MeshBufferManager == nullptr) return;
    UShapeComponent* RuntimeDebugShape = Cast<UShapeComponent>(Primitive);
    if (!Primitive->IsVisible() && !(RuntimeDebugShape && RuntimeDebugShape->GetRuntimeDebugVisible())) return;
    if (Primitive->IsEditorOnly() && WorldType != EWorldType::Editor) return;

    EPrimitiveType PrimType = Primitive->GetPrimitiveType();

    if (PrimType == EPrimitiveType::EPT_CollisionShape)
    {
        UShapeComponent* Shape = RuntimeDebugShape;
        if (ShowFlags.bCollisionDebug || (Shape && Shape->GetRuntimeDebugVisible()))
        {
            DrawCollisionShapeDebug(Shape, LineBatcher);
        }
        return;
    }

    static const FMaterial EngineDefaultMaterial{};

    switch (PrimType)
    {
    case EPrimitiveType::EPT_StaticMesh:
    {
        if (!ShowFlags.bPrimitives) return;

        UStaticMeshComponent* StaticMeshComp = static_cast<UStaticMeshComponent*>(Primitive);
        const UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh();

        if (!StaticMesh || !StaticMesh->HasValidMeshData()) return;

        FVector CameraPos = RenderBus.GetCameraPosition();
        FMatrix ProjMatrix = RenderBus.GetProj();
        FAABB Bounds = StaticMeshComp->GetWorldAABB();
        const int32 ValidLODCount = StaticMesh->GetValidLODCount();

        int32 SelectedLOD = 0;
        if (ShowFlags.bEnableLOD)
        {
            SelectedLOD = SelectLODLevel(CameraPos, Bounds, ProjMatrix, ValidLODCount);
        }

        FMeshBuffer* MeshBuffer = MeshBufferManager->GetStaticMeshBuffer(StaticMesh, SelectedLOD);
        if (!MeshBuffer) return;

        const FStaticMesh* MeshData = StaticMesh->GetMeshData(SelectedLOD);
        const TArray<FStaticMeshSection>& Sections = MeshData->Sections;
        const FString& MeshAssetPath = StaticMesh->GetAssetPathFileName();
        static std::unordered_set<FString> MissingWaterMaterialWarnedMeshes;

        for (int32 SectionIdx = 0; SectionIdx < static_cast<int32>(Sections.size()); ++SectionIdx)
        {
            const FStaticMeshSection& Section = Sections[SectionIdx];
            UMaterialInterface* Material = Cast<UMaterialInterface>(StaticMeshComp->GetMaterial(SectionIdx));
            if (Material == nullptr)
            {
                if (IsWaterAssetPath(MeshAssetPath) && MissingWaterMaterialWarnedMeshes.insert(MeshAssetPath).second)
                {
                    UE_LOG("[Water] Mesh '%s' has no assigned material. Falling back to DefaultWhite.", MeshAssetPath.c_str());
                }

                Material = FResourceManager::Get().GetMaterial("DefaultWhite");
                if (Material == nullptr)
                {
                    continue;
                }
            }

            FRenderCommand Cmd = {};
            Cmd.PerObjectConstants = FPerObjectConstants{ Primitive->GetWorldMatrix(), FColor::White().ToVector4() };
            Cmd.Type = ERenderCommandType::StaticMesh;
            Cmd.MeshBuffer = MeshBuffer;

            Cmd.SectionIndexStart = Section.StartIndex;
            Cmd.SectionIndexCount = Section.IndexCount;
            Cmd.Material = Material;

            // Water runtime params are component-owned and copied to per-draw command data.
            // Shared material instances are intentionally not mutated here.
            ConfigureWaterRenderData(StaticMeshComp, Material, Cmd);

            RenderBus.AddCommand(ERenderPass::Opaque, Cmd);

            if (Material->GetEffectiveLightingModel() == ELightingModel::Toon)
            {
                FRenderCommand OutlineCmd = {};
                OutlineCmd.Type = ERenderCommandType::ToonOutline;
                OutlineCmd.MeshBuffer = MeshBuffer;
                OutlineCmd.PerObjectConstants = FPerObjectConstants{
                    Primitive->GetWorldMatrix()
                };
                OutlineCmd.SectionIndexStart = Section.StartIndex;
                OutlineCmd.SectionIndexCount = Section.IndexCount;
                OutlineCmd.Material = Material;

                RenderBus.AddCommand(ERenderPass::ToonOutline, OutlineCmd);
            }
        }

        break;
    }

    case EPrimitiveType::EPT_Text:
    {
        if (!ShowFlags.bBillboardText) return;

        UTextRenderComponent* TextComp = static_cast<UTextRenderComponent*>(Primitive);
        const FFontResource* Font = TextComp->GetFont();
        if (!Font || !Font->IsLoaded()) return;

        const FString& Text = TextComp->GetText();
        if (Text.empty()) return;

        FRenderCommand Cmd = {};
        Cmd.Type = ERenderCommandType::Font;
        Cmd.PerObjectConstants = FPerObjectConstants{ TextComp->GetWorldMatrix(), TextComp->GetColor() };
        Cmd.Constants.Font.Text = &Text;
        Cmd.Constants.Font.Font = Font;
        Cmd.Constants.Font.Scale = TextComp->GetFontSize();

        RenderBus.AddCommand(ERenderPass::Font, Cmd);
        break;
    }

    case EPrimitiveType::EPT_SubUV:
    {
        USubUVComponent* SubUVComp = static_cast<USubUVComponent*>(Primitive);
        const FParticleResource* Particle = SubUVComp->GetParticle();
        if (!Particle || !Particle->IsLoaded()) return;

        FRenderCommand Cmd = {};
        Cmd.PerObjectConstants = FPerObjectConstants{
            MakeViewBillboardMatrix(Primitive, RenderBus),
            FColor::White().ToVector4()
        };
        Cmd.Type = ERenderCommandType::SubUV;
        Cmd.Constants.SubUV.Particle = Particle;
        Cmd.Constants.SubUV.FrameIndex = SubUVComp->GetFrameIndex();
        Cmd.Constants.SubUV.Width = SubUVComp->GetWidth();
        Cmd.Constants.SubUV.Height = SubUVComp->GetHeight();

        RenderBus.AddCommand(ERenderPass::SubUV, Cmd);
        break;
    }

    case EPrimitiveType::EPT_Billboard:
    {
        UBillboardComponent* BillboardComp = static_cast<UBillboardComponent*>(Primitive);
        UTexture* Texture = BillboardComp->GetTexture();

        UMaterial* BillboardMat = FResourceManager::Get().GetMaterial("BillboardMat");
        BillboardMat->DepthStencilType = EDepthStencilType::Default;
        BillboardMat->BlendType = EBlendType::AlphaBlend;
        BillboardMat->RasterizerType = ERasterizerType::SolidNoCull;
        BillboardMat->SamplerType = ESamplerType::EST_Linear;

        const FMatrix BillboardMatrix = UBillboardComponent::MakeBillboardWorldMatrix(
            BillboardComp->GetWorldLocation(),
            FVector(0.01f, BillboardComp->GetWidth(), BillboardComp->GetHeight()),
            RenderBus.GetCameraForward(),
            RenderBus.GetCameraRight(),
            RenderBus.GetCameraUp());

        FVector4 LightColor = FColor::White().ToVector4();
        if (ULightComponent* LightComponent = Cast<ULightComponent>(BillboardComp->GetParent()))
        {
            LightColor = LightComponent->GetLightColor().ToVector4();
        }

        FRenderCommand Cmd = {};
        Cmd.Type = ERenderCommandType::Billboard;
        Cmd.MeshBuffer = &MeshBufferManager->GetMeshBuffer(EPrimitiveType::EPT_Billboard);
        Cmd.PerObjectConstants = FPerObjectConstants{ BillboardMatrix, LightColor };
        Cmd.Material = BillboardMat;
        Cmd.Constants.Billboard.Texture = Texture;

        RenderBus.AddCommand(ERenderPass::Billboard, Cmd);
        break;
    }

    case EPrimitiveType::EPT_Decal:
    {
        if (!ShowFlags.bDecals) return;

        FScopeCycleCounter RenderDecalScope({});

        UDecalComponent* DecalComp = static_cast<UDecalComponent*>(Primitive);
        UMaterialInterface* Material = Cast<UMaterialInterface>(DecalComp->GetMaterial());

        UWorld* World = DecalComp->GetOwner() ? DecalComp->GetOwner()->GetFocusedWorld() : nullptr;
        if (World == nullptr) return;

        FOBB DecalOBB = FOBB::FromAABB(DecalComp->GetWorldAABB(), DecalComp->GetWorldMatrix());

        TArray<UPrimitiveComponent*> VisiblePrimitiveScratch;
        World->GetSpatialIndex().OBBQueryPrimitives(DecalOBB, VisiblePrimitiveScratch, OBBQueryScratch);

        for (UPrimitiveComponent* Prim : VisiblePrimitiveScratch)
        {
            if (Prim->GetPrimitiveType() != EPrimitiveType::EPT_StaticMesh) continue;

            UStaticMeshComponent* StaticMeshComp = static_cast<UStaticMeshComponent*>(Prim);
            if (DecalComp->AffectsOnlyWaterReceivers() && FindComponentByType<UWaterComponent>(StaticMeshComp->GetOwner()) == nullptr)
            {
                continue;
            }

            const UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh();

            if (!StaticMesh || !StaticMesh->HasValidMeshData()) continue;

            FVector CameraPos = RenderBus.GetCameraPosition();
            FMatrix ProjMatrix = RenderBus.GetProj();
            FAABB Bounds = StaticMeshComp->GetWorldAABB();
            const int32 ValidLODCount = StaticMesh->GetValidLODCount();

            int32 SelectedLOD = 0;
            if (ShowFlags.bEnableLOD)
            {
                SelectedLOD = SelectLODLevel(CameraPos, Bounds, ProjMatrix, ValidLODCount);
            }

            FMeshBuffer* MeshBuffer = MeshBufferManager->GetStaticMeshBuffer(StaticMesh, SelectedLOD);
            if (!MeshBuffer) return;

            const FStaticMesh* MeshData = StaticMesh->GetMeshData(SelectedLOD);
            const TArray<FStaticMeshSection>& Sections = MeshData->Sections;

            for (int32 SectionIdx = 0; SectionIdx < static_cast<int32>(Sections.size()); ++SectionIdx)
            {
                const FStaticMeshSection& Section = Sections[SectionIdx];

                FRenderCommand Cmd = {};
                Cmd.Type = ERenderCommandType::Decal;
                Cmd.PerObjectConstants = FPerObjectConstants{ Prim->GetWorldMatrix(), DecalComp->GetDecalColor().ToVector4() };
                Cmd.MeshBuffer = MeshBuffer;

                Cmd.SectionIndexStart = Section.StartIndex;
                Cmd.SectionIndexCount = Section.IndexCount;

                Cmd.Material = Material;
                Cmd.Constants.Decal.InvDecalWorld = DecalComp->GetDecalMatrix().GetInverse();

                RenderBus.AddCommand(ERenderPass::Decal, Cmd);
            }
        }

        if (WorldType == EWorldType::Editor && LineBatcher != nullptr)
        {
            LineBatcher->AddOBB(DecalOBB, FColor::Green());
        }

        LastStats.Decal.TotalDecalCount += 1;
        LastStats.Decal.CollectTimeMS += static_cast<int32>(RenderDecalScope.Finish());
        break;
    }

    case EPrimitiveType::EPT_FOG:
    {
        if (!ShowFlags.bFog)
            return;
        UHeightFogComponent* HeightFogComp = static_cast<UHeightFogComponent*>(Primitive);

        FRenderCommand Cmd = {};
        Cmd.Type = ERenderCommandType::Primitive;
        Cmd.Constants.Fog.FogDensity = HeightFogComp->GetFogDensity();
        Cmd.Constants.Fog.FogColor = HeightFogComp->GetFogInscatteringColor();
        Cmd.Constants.Fog.HeightFalloff = HeightFogComp->GetHeightFalloff();
        Cmd.Constants.Fog.FogHeight = HeightFogComp->GetFogHeight();
        Cmd.Constants.Fog.FogStartDistance = HeightFogComp->GetFogStartDistance();
        Cmd.Constants.Fog.FogMaxOpacity = HeightFogComp->GetFogMaxOpacity();
        Cmd.Constants.Fog.FogCutoffDistance = HeightFogComp->GetFogCutoffDistance();

        RenderBus.AddCommand(ERenderPass::Fog, Cmd);
        break;
    }
    case EPrimitiveType::EPT_SKY:
    {
        if (!RenderBus.GetCommands(ERenderPass::Sky).empty())
        {
            return;
        }

        USkyAtmosphereComponent* SkyComponent = static_cast<USkyAtmosphereComponent*>(Primitive);
        SkyComponent->RefreshSkyStateFromWorld();

        FRenderCommand Cmd = {};
        Cmd.Type = ERenderCommandType::Sky;
        SkyComponent->FillSkyConstants(RenderBus, Cmd.Constants.Sky);
        RenderBus.AddCommand(ERenderPass::Sky, Cmd);
        break;
    }
    default:
        if (PrimType == EPrimitiveType::EPT_TransGizmo || PrimType == EPrimitiveType::EPT_RotGizmo || PrimType == EPrimitiveType::EPT_ScaleGizmo)
        {
            return;
        }
        return;
    }
}
