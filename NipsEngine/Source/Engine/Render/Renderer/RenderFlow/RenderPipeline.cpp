#include "RenderPipeline.h"
#include "RenderPassContext.h"
#include "Core/Paths.h"
#include "LightCullingPass.h"
#include "SkyRenderPass.h"
#include "OpaqueRenderPass.h"
#include "DecalRenderPass.h"
#include "BufferVisualizationRenderPass.h"
#include "FogRenderPass.h"
#include "FXAARenderPass.h"
#include "FontRenderPass.h"
#include "SubUVRenderPass.h"
#include "BillboardRenderPass.h"
#include "TranslucentRenderPass.h"
#include "SelectionMaskRenderPass.h"
#include "GridRenderPass.h"
#include "LineBatchRenderPass.h"
#include "DepthLessRenderPass.h"
#include "DepthPrepassRenderPass.h"
#include "PostProcessOutlineRenderPass.h"
#include "ScreenEffectsRenderPass.h"
#include "ShadowPass.h"
#include "BlurPass.h"
#include "HitMapRenderPass.h"
#include "ToonOutlineRenderPass.h"
#include "Core/Logging/Log.h"
#include "UIRenderPass.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>

namespace
{
    FWString NormalizeShaderHotReloadPath(const FWString& InPath)
    {
        FWString Result = std::filesystem::path(InPath).lexically_normal().generic_wstring();
        std::transform(Result.begin(), Result.end(), Result.begin(),
            [](wchar_t Character) { return static_cast<wchar_t>(towlower(Character)); });
        return Result;
    }

    FWString NormalizeShaderHotReloadPath(const FString& InPath)
    {
        return NormalizeShaderHotReloadPath(FPaths::ToAbsolute(FPaths::ToWide(InPath)));
    }

    bool IsShaderIncludeFile(const FWString& InPath)
    {
        const SIZE_T DotIndex = InPath.find_last_of(L'.');
        if (DotIndex == FWString::npos)
        {
            return false;
        }

        FWString Extension = InPath.substr(DotIndex);
        std::transform(Extension.begin(), Extension.end(), Extension.begin(),
            [](wchar_t Character) { return static_cast<wchar_t>(towlower(Character)); });
        return Extension == L".hlsli";
    }
}

bool FRenderPipeline::Initialize()
{
    LightCullingPass = std::make_shared<FLightCullingPass>();
    LightCullingPass->Initialize();

    HitMapRenderPass = std::make_shared<FHitMapRenderPass>();
    HitMapRenderPass->Initialize();

    SkyRenderPass = std::make_shared<FSkyRenderPass>();
    SkyRenderPass->Initialize();

    ShadowPass = std::make_shared<FShadowPass>();
    ShadowPass->Initialize();

    BlurPass = std::make_shared<FBlurPass>();
    BlurPass->Initialize();

    DepthPrepassRenderPass = std::make_shared<FDepthPrepassRenderPass>();
    DepthPrepassRenderPass->Initialize();

    OpaqueRenderPass = std::make_shared<FOpaqueRenderPass>();
    OpaqueRenderPass->Initialize();

    DecalRenderPass = std::make_shared<FDecalRenderPass>();
    DecalRenderPass->Initialize();

    // 버퍼 시각화 계열 view mode는 이 전용 pass를 통해 확장한다.
    BufferVisualizationRenderPass = std::make_shared<FBufferVisualizationRenderPass>();
    BufferVisualizationRenderPass->Initialize();

    FogRenderPass = std::make_shared<FFogRenderPass>();
    FogRenderPass->Initialize();

    FXAARenderPass = std::make_shared<FFXAARenderPass>();
    FXAARenderPass->Initialize();

    FontRenderPass = std::make_shared<FFontRenderPass>();
    FontRenderPass->Initialize();

    SubUVRenderPass = std::make_shared<FSubUVRenderPass>();
    SubUVRenderPass->Initialize();

    BillboardRenderPass = std::make_shared<FBillboardRenderPass>();
    BillboardRenderPass->Initialize();

    TranslucentRenderPass = std::make_shared<FTranslucentRenderPass>();
    TranslucentRenderPass->Initialize();

    SelectionMaskRenderPass = std::make_shared<FSelectionMaskRenderPass>();
    SelectionMaskRenderPass->Initialize();

    GridRenderPass = std::make_shared<FGridRenderPass>();
    GridRenderPass->Initialize();

    LineBatchRenderPass = std::make_shared<FLineBatchRenderPass>();
    LineBatchRenderPass->Initialize();

    DepthLessRenderPass = std::make_shared<FDepthLessRenderPass>();
    DepthLessRenderPass->Initialize();

    PostProcessOutlineRenderPass = std::make_shared<FPostProcessOutlineRenderPass>();
    PostProcessOutlineRenderPass->Initialize();

    ScreenEffectsRenderPass = std::make_shared<FScreenEffectsRenderPass>();
    ScreenEffectsRenderPass->Initialize();

    ToonOutlineRenderPass = std::make_shared<FToonOutlineRenderPass>();
    ToonOutlineRenderPass->Initialize();

    UIRenderPass = std::make_shared<FUIRenderPass>();
    UIRenderPass->Initialize();

    FogRenderPass->SetSkipWireframe(true);
    FXAARenderPass->SetSkipWireframe(true);

    /**
     * 각 Render Pass 는 자신의 출력 SRV/RTV 를 다음 패스로 넘긴다.
     * 마지막 패스가 남긴 OutSRV/OutRTV 가 RenderTargets.FinalSRV/FinalRTV 가 된다.
     */
    RenderPasses.push_back(ShadowPass);
    //RenderPasses.push_back(BlurPass);
    RenderPasses.push_back(DepthPrepassRenderPass);
    RenderPasses.push_back(LightCullingPass);
    RenderPasses.push_back(SkyRenderPass);
    RenderPasses.push_back(ToonOutlineRenderPass);
    RenderPasses.push_back(OpaqueRenderPass);

    RenderPasses.push_back(DecalRenderPass);
    // SceneColor를 만든 뒤 fog/fxaa 전에 덮어쓸 수 있는 view mode 확장 지점이다.
    RenderPasses.push_back(BufferVisualizationRenderPass);
    RenderPasses.push_back(HitMapRenderPass);

    RenderPasses.push_back(LineBatchRenderPass); // FIXME: GameJam을 위해 임시로 앞으로 당겼음
    RenderPasses.push_back(FogRenderPass);
    RenderPasses.push_back(FXAARenderPass);
    RenderPasses.push_back(FontRenderPass);
    RenderPasses.push_back(SubUVRenderPass);
    RenderPasses.push_back(BillboardRenderPass);
    RenderPasses.push_back(TranslucentRenderPass);
    RenderPasses.push_back(SelectionMaskRenderPass);
    RenderPasses.push_back(GridRenderPass);
    // RenderPasses.push_back(LineBatchRenderPass);
    RenderPasses.push_back(UIRenderPass);      // 게임 UI — 그리드 뒤, 에디터 앞

    RenderPasses.push_back(DepthLessRenderPass);
    RenderPasses.push_back(PostProcessOutlineRenderPass);
    RenderPasses.push_back(ScreenEffectsRenderPass);

    return true;
}

void FRenderPipeline::ProcessShaderHotReloads(const std::set<FWString>& DirtyFiles, ID3D11Device* Device)
{
    if (Device == nullptr || DirtyFiles.empty())
    {
        return;
    }

    const bool bAnyIncludeChanged = std::any_of(
        DirtyFiles.begin(),
        DirtyFiles.end(),
        [](const FWString& DirtyFile) { return IsShaderIncludeFile(DirtyFile); });

    const auto ShouldReload = [&](const FString& RelativePath)
    {
        return bAnyIncludeChanged || DirtyFiles.contains(NormalizeShaderHotReloadPath(RelativePath));
    };

    // These compute passes are not registered through FResourceManager's shader-variant system.
    // A changed .hlsli therefore conservatively reloads both passes instead of trying to build a
    // separate include graph here.
    if (BlurPass && ShouldReload(FBlurPass::ComputeShaderPath))
    {
        if (!BlurPass->ReloadComputeShader(Device))
        {
            UE_LOG("[ShaderHotReload] Failed to reload compute shader: %s", FBlurPass::ComputeShaderPath);
        }
    }

    if (LightCullingPass && ShouldReload(FLightCullingPass::ComputeShaderPath))
    {
        if (!LightCullingPass->ReloadComputeShader(Device))
        {
            UE_LOG("[ShaderHotReload] Failed to reload compute shader: %s", FLightCullingPass::ComputeShaderPath);
        }
    }
}

bool FRenderPipeline::Render(const FRenderPassContext* Context)
{
    OutSRV = nullptr;
    OutRTV = nullptr;

    for (std::shared_ptr<FBaseRenderPass> Pass : RenderPasses)
    {
        if (Context && Context->RenderBus && !Context->RenderBus->GetShowFlags().bShadow && (Pass == ShadowPass || Pass == BlurPass)) continue;

        Pass->SetPrevPassSRV(OutSRV);
        Pass->SetPrevPassRTV(OutRTV);
        Pass->Render(Context);

        OutSRV = Pass->GetOutSRV();
        OutRTV = Pass->GetOutRTV();
    }

    Context->RenderTargets->FinalSRV = OutSRV;
    Context->RenderTargets->FinalRTV = OutRTV;

    return true;
}

void FRenderPipeline::Release()
{
    if (LightCullingPass)
    {
        LightCullingPass->Release();
        LightCullingPass.reset();
    }

    if (HitMapRenderPass)
    {
        HitMapRenderPass->Release();
        HitMapRenderPass.reset();
    }

    if (SkyRenderPass)
    {
        SkyRenderPass->Release();
        SkyRenderPass.reset();
    }

    if (ShadowPass)
    {
        ShadowPass->Release();
        ShadowPass.reset();
    }

    if (BlurPass)
    {
        BlurPass->Release();
        BlurPass.reset();
    }

    if (DepthPrepassRenderPass)
    {
        DepthPrepassRenderPass->Release();
        DepthPrepassRenderPass.reset();
    }

    if (OpaqueRenderPass)
    {
        OpaqueRenderPass->Release();
        OpaqueRenderPass.reset();
    }

    if (DecalRenderPass)
    {
        DecalRenderPass->Release();
        DecalRenderPass.reset();
    }

    if (ToonOutlineRenderPass)
    {
        ToonOutlineRenderPass->Release();
        ToonOutlineRenderPass.reset();
    }

    if (BufferVisualizationRenderPass)
    {
        BufferVisualizationRenderPass->Release();
        BufferVisualizationRenderPass.reset();
    }

    if (FogRenderPass)
    {
        FogRenderPass->Release();
        FogRenderPass.reset();
    }

    if (FXAARenderPass)
    {
        FXAARenderPass->Release();
        FXAARenderPass.reset();
    }

    if (FontRenderPass)
    {
        FontRenderPass->Release();
        FontRenderPass.reset();
    }

    if (SubUVRenderPass)
    {
        SubUVRenderPass->Release();
        SubUVRenderPass.reset();
    }

    if (BillboardRenderPass)
    {
        BillboardRenderPass->Release();
        BillboardRenderPass.reset();
    }

    if (TranslucentRenderPass)
    {
        TranslucentRenderPass->Release();
        TranslucentRenderPass.reset();
    }

    if (SelectionMaskRenderPass)
    {
        SelectionMaskRenderPass->Release();
        SelectionMaskRenderPass.reset();
    }

    if (GridRenderPass)
    {
        GridRenderPass->Release();
        GridRenderPass.reset();
    }

    if (LineBatchRenderPass)
    {
        LineBatchRenderPass->Release();
        LineBatchRenderPass.reset();
    }

    if (DepthLessRenderPass)
    {
        DepthLessRenderPass->Release();
        DepthLessRenderPass.reset();
    }

    if (PostProcessOutlineRenderPass)
    {
        PostProcessOutlineRenderPass->Release();
        PostProcessOutlineRenderPass.reset();
    }

    if (UIRenderPass)
    {
        UIRenderPass->Release();
        UIRenderPass.reset();
    }

    if (ScreenEffectsRenderPass)
    {
        ScreenEffectsRenderPass->Release();
        ScreenEffectsRenderPass.reset();
    }
}

