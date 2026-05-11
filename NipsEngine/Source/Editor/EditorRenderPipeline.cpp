#include "EditorRenderPipeline.h"

#include "Editor/EditorEngine.h"
#include "Editor/Viewport/FBXPreviewViewportClient.h"
#include "Render/Renderer/Renderer.h"
#include "Core/Logging/Stats.h"
#include "Core/Logging/GPUProfiler.h"
#include "Runtime/SceneView.h"
#include "Engine/Component/GizmoComponent.h"

FEditorRenderPipeline::FEditorRenderPipeline(UEditorEngine* InEditor, FRenderer& InRenderer) : Editor(InEditor)
{
    Collector.Initialize(InRenderer.GetFD3DDevice().GetDevice(), InRenderer.GetFD3DDevice().GetDeviceContext());
    ViewportCullingStats.resize(FEditorViewportLayout::MaxViewports);
    ViewportDecalStats.resize(FEditorViewportLayout::MaxViewports);
    ViewportShadowStats.resize(FEditorViewportLayout::MaxViewports);
}

FEditorRenderPipeline::~FEditorRenderPipeline() { Collector.Release(); }

void FEditorRenderPipeline::Execute(float DeltaTime, FRenderer& Renderer)
{
#if STATS
    FStatManager::Get().TakeSnapshot();
    FGPUProfiler::Get().TakeSnapshot();
#endif

    for (FRenderCollector::FCullingStats& Stats : ViewportCullingStats) { Stats = {}; }
    for (FRenderCollector::FShadowStats& ShadowStats : ViewportShadowStats) { ShadowStats = {}; }
    if (!Editor->GetFocusedWorld()) return;

    // 1회: 전체 백버퍼 클리어 (색상 + 깊이/스텐실)
    Renderer.BeginFrame();

    // 4개 뷰포트를 순서대로 렌더링
    for (int32 i = 0; i < FEditorViewportLayout::MaxViewports; ++i)
    {
        RenderViewport(Renderer, i);
    }

    RenderFBXPreview(Renderer);

    Renderer.UseBackBufferRenderTargets();

    // ImGui UI 오버레이
    Editor->RenderUI(DeltaTime);

    Renderer.EndFrame();
}

void FEditorRenderPipeline::RenderViewport(FRenderer& Renderer, int32 ViewportIndex)
{
    FSceneView SceneView;
    FEditorViewportClient* VC = nullptr;
    if (!PrepareViewport(Renderer, ViewportIndex, SceneView, VC)) { return; }

    UWorld* World = VC->GetFocusedWorld();
    const FEditorSettings& Settings = Editor->GetSettings();
    const FShowFlags& ShowFlags = Settings.ShowFlags;
    const EViewMode ViewMode = SceneView.ViewMode;
    const FFrustum& ViewFrustum = SceneView.CameraFrustum;

    Renderer.GetDebugLineBatcher().Clear();
    Renderer.GetDebugRingBatcher().Clear();
    Collector.SetLineBatcher(&Renderer.GetDebugLineBatcher());
    Collector.SetRingBatcher(&Renderer.GetDebugRingBatcher());
    Collector.CollectWorld(World, ShowFlags, ViewMode, Bus, &ViewFrustum);
    ViewportCullingStats[ViewportIndex] = Collector.GetLastCullingStats();
    ViewportDecalStats[ViewportIndex] = Collector.GetLastDecalStats();
    ViewportShadowStats[ViewportIndex] = Collector.GetLastShadowStats();

    Collector.CollectGrid(
        Settings.GridSpacing,
        Settings.GridHalfLineCount,
        Bus,
        SceneView.bOrthographic,
        Settings.GridRenderSettings);

    // 뷰포트가 편집 모드일 때만 기즈모·선택 오버레이를 그립니다.
    if (VC->GetPlayState() == EViewportPlayState::Editing)
    {
        if (UGizmoComponent* Gizmo = Editor->GetGizmo())
        {
            if (SceneView.bOrthographic)
                Gizmo->ApplyScreenSpaceScalingOrtho(SceneView.CameraOrthoHeight);
            else
                Gizmo->ApplyScreenSpaceScaling(SceneView.CameraPosition);
        }

        Collector.CollectGizmo(Editor->GetGizmo(), ShowFlags, Bus, VC->GetViewportState()->bHovered);
        Collector.CollectSelection(Editor->GetSelectionManager().GetSelectedActors(), ShowFlags, ViewMode, Bus);
    }

    // CPU 배처 데이터 준비 → GPU 드로우 (SetSubViewport 영역에만 출력됨)
    Renderer.PrepareBatchers(Bus);
    Renderer.Render(Bus);
}

// 지정한 에디터 뷰포트의 렌더 타겟과 RenderBus 기본 상태를 준비합니다.
bool FEditorRenderPipeline::PrepareViewport(FRenderer& Renderer, int32 ViewportIndex, FSceneView& OutSceneView, FEditorViewportClient*& OutViewportClient)
{
    OutViewportClient = Editor->GetViewportLayout().GetViewportClient(ViewportIndex);
    if (OutViewportClient == nullptr)
    {
        return false;
    }

    OutViewportClient->BuildSceneView(OutSceneView);

    const FViewportRect& Rect = OutSceneView.ViewRect;
    if (Rect.Width <= 0 || Rect.Height <= 0)
    {
        return false;
    }

    FSceneViewport& SceneViewport = Editor->GetViewportLayout().GetSceneViewport(ViewportIndex);
    FViewportRenderResource& ViewportResource = Editor->GetRenderer().AcquireViewportResource(Rect.Width, Rect.Height, ViewportIndex);
    SceneViewport.SetRenderTargetSet(&ViewportResource.GetView());

    Renderer.BeginViewportFrame(SceneViewport.GetViewportRenderTargets());

    const FEditorSettings& Settings = Editor->GetSettings();
    Bus.Clear();
    Bus.SetViewProjection(OutSceneView.ViewMatrix, OutSceneView.ProjectionMatrix);
    Bus.SetCameraPlane(OutSceneView.NearPlane, OutSceneView.FarPlane);
    Bus.SetRenderSettings(OutSceneView.ViewMode, Settings.ShowFlags);
    Bus.SetViewportSize(FVector2(static_cast<float>(Rect.Width), static_cast<float>(Rect.Height)));
    Bus.SetViewportOrigin(FVector2(0.0f, 0.0f));
    Bus.SetFXAAEnabled(Settings.bEnableFXAA && !OutSceneView.bOrthographic);
    Bus.SetShadowFilterType(Settings.ShadowFilterType);

    FScreenEffectSettings ScreenEffects;
    ScreenEffects.bGammaCorrectionEnabled = Settings.ShowFlags.bGammaCorrection;
    ScreenEffects.Gamma = Settings.ShowFlags.GammaValue;
    Bus.SetScreenEffectSettings(ScreenEffects);

    return true;
}

const FRenderCollector::FCullingStats& FEditorRenderPipeline::GetViewportCullingStats(int32 ViewportIndex) const
{
    static const FRenderCollector::FCullingStats EmptyStats{};

    if (ViewportIndex < 0 || ViewportIndex >= static_cast<int32>(ViewportCullingStats.size()))
    {
        return EmptyStats;
    }

    return ViewportCullingStats[ViewportIndex];
}

const FRenderCollector::FDecalStats& FEditorRenderPipeline::GetViewportDecalStats(int32 ViewportIndex) const
{
    static const FRenderCollector::FDecalStats EmptyStats{};

    if (ViewportIndex < 0 || ViewportIndex >= static_cast<int32>(ViewportCullingStats.size()))
    {
        return EmptyStats;
    }

    return ViewportDecalStats[ViewportIndex];
}

const FRenderCollector::FShadowStats& FEditorRenderPipeline::GetViewportShadowStats(int32 ViewportIndex) const
{
    static const FRenderCollector::FShadowStats EmptyStats{};

    if (ViewportIndex < 0 || ViewportIndex >= static_cast<int32>(ViewportShadowStats.size()))
    {
        return EmptyStats;
    }

    return ViewportShadowStats[ViewportIndex];
}

void FEditorRenderPipeline::RenderFBXPreview(FRenderer& Renderer)
{
    FBXPreviewSRV = nullptr;

    FFBXPreviewViewportClient& FBXClient = Editor->GetFBXPreviewViewportClient();
    UWorld* FBXWorld = FBXClient.GetWorld();
    if (!FBXWorld) { return; }

    const FEditorSettings& Settings = Editor->GetSettings();
    FSceneView SceneView;
    FBXClient.BuildSceneView(SceneView);

    const FViewportRect& Rect = SceneView.ViewRect;
    if (Rect.Width <= 0 || Rect.Height <= 0) { return; }

    FViewportRenderResource& Resource = Renderer.AcquireViewportResource(
        static_cast<uint32>(Rect.Width),
        static_cast<uint32>(Rect.Height),
        FBXPreviewResourceIndex);

    FRenderTargetSet& RTS = Resource.GetView();
    Renderer.BeginViewportFrame(&RTS);

    FShowFlags FBXShowFlags;
    FBXShowFlags.bGrid  = FBXClient.GetShowGrid();
    FBXShowFlags.bAxis  = FBXClient.GetShowAxis();
    FBXShowFlags.bGizmo = false;

    Bus.Clear();
    Bus.SetViewProjection(SceneView.ViewMatrix, SceneView.ProjectionMatrix);
    Bus.SetCameraPlane(SceneView.NearPlane, SceneView.FarPlane);
    Bus.SetRenderSettings(SceneView.ViewMode, FBXShowFlags);
    Bus.SetViewportSize(FVector2(static_cast<float>(Rect.Width), static_cast<float>(Rect.Height)));
    Bus.SetViewportOrigin(FVector2(0.0f, 0.0f));
    Bus.SetFXAAEnabled(false);

    Renderer.GetDebugLineBatcher().Clear();
    Renderer.GetDebugRingBatcher().Clear();
    Collector.SetLineBatcher(&Renderer.GetDebugLineBatcher());
    Collector.SetRingBatcher(&Renderer.GetDebugRingBatcher());
    Collector.CollectWorld(FBXWorld, FBXShowFlags, SceneView.ViewMode, Bus, &SceneView.CameraFrustum);

    Collector.CollectGrid(
        Settings.GridSpacing,
        Settings.GridHalfLineCount,
        Bus,
        false,
        Settings.GridRenderSettings);

    Renderer.PrepareBatchers(Bus);
    Renderer.Render(Bus);

    // GridRenderPass 등 후속 패스가 합성된 최종 결과를 사용
    FBXPreviewSRV = RTS.FinalSRV;
}
