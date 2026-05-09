#pragma once
#include "Core/CoreTypes.h"
#include "Core/Containers/Array.h"

#include <memory>
#include <set>

struct FRenderPassContext;
struct ID3D11Device;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;

class FShadowPass;
class FBlurPass;
class FToonOutlineRenderPass;
class FFXAARenderPass;
class FFogRenderPass;
class FSkyRenderPass;
class FLightRenderPass;
class FDecalRenderPass;
class FBufferVisualizationRenderPass;
class FFontRenderPass;
class FSubUVRenderPass;
class FBillboardRenderPass;
class FTranslucentRenderPass;
class FSelectionMaskRenderPass;
class FGridRenderPass;
class FLineBatchRenderPass;
class FDepthLessRenderPass;
class FDepthPrepassRenderPass;
class FHitMapRenderPass;
class FPostProcessOutlineRenderPass;
class FScreenEffectsRenderPass;
class FOpaqueRenderPass;
class FLightCullingPass;
class FUIRenderPass;
class FBaseRenderPass;

class FRenderPipeline
{
public:
    bool Initialize();
    void ProcessShaderHotReloads(const std::set<FWString>& DirtyFiles, ID3D11Device* Device);
    bool Render(const FRenderPassContext* Context);
    void Release();

    ID3D11ShaderResourceView* GetOutSRV() const { return OutSRV; }

private:
    std::shared_ptr<FLightCullingPass> LightCullingPass;
    std::shared_ptr<FHitMapRenderPass> HitMapRenderPass;
    std::shared_ptr<FSkyRenderPass> SkyRenderPass;
    std::shared_ptr<FShadowPass> ShadowPass;
    std::shared_ptr<FBlurPass> BlurPass;
    std::shared_ptr<FDepthPrepassRenderPass> DepthPrepassRenderPass;
    std::shared_ptr<FOpaqueRenderPass> OpaqueRenderPass;
    std::shared_ptr<FDecalRenderPass> DecalRenderPass;
    std::shared_ptr<FBufferVisualizationRenderPass> BufferVisualizationRenderPass;
    std::shared_ptr<FFogRenderPass> FogRenderPass;
    std::shared_ptr<FFXAARenderPass> FXAARenderPass;
    std::shared_ptr<FFontRenderPass> FontRenderPass;
    std::shared_ptr<FSubUVRenderPass> SubUVRenderPass;
    std::shared_ptr<FBillboardRenderPass> BillboardRenderPass;
    std::shared_ptr<FTranslucentRenderPass> TranslucentRenderPass;
    std::shared_ptr<FSelectionMaskRenderPass> SelectionMaskRenderPass;
    std::shared_ptr<FGridRenderPass> GridRenderPass;
    std::shared_ptr<FLineBatchRenderPass> LineBatchRenderPass;
    std::shared_ptr<FDepthLessRenderPass> DepthLessRenderPass;
    std::shared_ptr<FPostProcessOutlineRenderPass> PostProcessOutlineRenderPass;
    std::shared_ptr<FScreenEffectsRenderPass> ScreenEffectsRenderPass;
    std::shared_ptr<FToonOutlineRenderPass> ToonOutlineRenderPass;
    std::shared_ptr<FUIRenderPass>          UIRenderPass;

    ID3D11ShaderResourceView* OutSRV = nullptr;
    ID3D11RenderTargetView* OutRTV = nullptr;

    TArray<std::shared_ptr<FBaseRenderPass>> RenderPasses;
};
