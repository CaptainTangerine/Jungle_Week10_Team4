#include "GameRenderPipeline.h"

#include "Game/GameEngine.h"
#include "Game/GameViewportClient.h"
#include "Engine/GameFramework/World.h"
#include "Engine/Runtime/SceneView.h"
#include "Engine/Render/Renderer/Renderer.h"

FGameRenderPipeline::FGameRenderPipeline(UGameEngine* InGameEngine, FRenderer& InRenderer)
    : GameEngine(InGameEngine)
{
    Collector.Initialize(InRenderer.GetFD3DDevice().GetDevice(), InRenderer.GetFD3DDevice().GetDeviceContext());
}

FGameRenderPipeline::~FGameRenderPipeline()
{
    Collector.Release();
}

void FGameRenderPipeline::Execute(float DeltaTime, FRenderer& Renderer)
{
    if (!GameEngine || !GameEngine->GetWorld())
    {
        return;
    }

    Renderer.BeginFrame();
    RenderViewport(Renderer);
    Renderer.EndFrame();
}

void FGameRenderPipeline::RenderViewport(FRenderer& Renderer)
{
	FSceneView SceneView;
    FGameViewportClient* ViewportClient = nullptr;
    if (!PrepareViewport(Renderer, SceneView, ViewportClient))
    {
        return;
    }

    UWorld* World = ViewportClient->GetWorld();
    const FShowFlags ShowFlags = Bus.GetShowFlags();
    const EViewMode ViewMode = SceneView.ViewMode;
    const FFrustum& ViewFrustum = SceneView.CameraFrustum;

    Renderer.GetDebugLineBatcher().Clear();
    Renderer.GetDebugRingBatcher().Clear();
    Collector.SetLineBatcher(&Renderer.GetDebugLineBatcher());
    Collector.SetRingBatcher(&Renderer.GetDebugRingBatcher());
    Collector.CollectWorld(World, ShowFlags, ViewMode, Bus, &ViewFrustum);

    Renderer.PrepareBatchers(Bus);
    Renderer.Render(Bus);
    Renderer.PresentToBackBuffer(Renderer.GetCurrentSceneSRV(), Bus.GetScreenEffectSettings());
}

bool FGameRenderPipeline::PrepareViewport(FRenderer& Renderer, FSceneView& OutSceneView, FGameViewportClient*& OutViewportClient)
{
    OutViewportClient = &GameEngine->GetViewportClient();
    if (OutViewportClient == nullptr || OutViewportClient->GetWorld() == nullptr)
    {
        return false;
    }

    OutViewportClient->BuildSceneView(OutSceneView);

    const FViewportRect& Rect = OutSceneView.ViewRect;
    if (Rect.Width <= 0 || Rect.Height <= 0)
    {
        return false;
    }

    FViewportRenderResource& ViewportResource = Renderer.AcquireViewportResource(static_cast<uint32>(Rect.Width), static_cast<uint32>(Rect.Height), 0);
    FRenderTargetSet& RenderTargets = ViewportResource.GetView();
    Renderer.BeginViewportFrame(&RenderTargets);

    FShowFlags ShowFlags;
    ShowFlags.bBVHBoundingVolume = false;
    ShowFlags.bBoundingVolume = false;
    ShowFlags.bEnableLOD = false;
    ShowFlags.bBillboardText = true;

    Bus.Clear();
    Bus.SetViewProjection(OutSceneView.ViewMatrix, OutSceneView.ProjectionMatrix);
    Bus.SetCameraPlane(OutSceneView.NearPlane, OutSceneView.FarPlane);
    Bus.SetRenderSettings(OutSceneView.ViewMode, ShowFlags);
    Bus.SetViewportSize(FVector2(static_cast<float>(Rect.Width), static_cast<float>(Rect.Height)));
    Bus.SetViewportOrigin(FVector2(static_cast<float>(Rect.X), static_cast<float>(Rect.Y)));
    Bus.SetFXAAEnabled(!OutSceneView.bOrthographic);
    Bus.SetShadowFilterType(EShadowFilterType::PCF);
    Bus.SetScreenEffectSettings(OutViewportClient->GetWorld()->GetPlayerCameraManager().GetScreenEffectSettings());

    return true;
}
