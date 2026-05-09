#include "OpaqueRenderPass.h"
#include "Render/Device/D3DDevice.h"
#include "Render/Scene/RenderBus.h"
#include "Render/Resource/RenderResources.h"
#include "Render/Resource/Material.h"
#include "Render/Common/WaterRenderingCommon.h"
#include "Core/ResourceManager.h"
#include "Core/Logging/Log.h"
#include "SceneLightBinding.h"
#include <cstring>

namespace
{
    UShader* ResolveOpaqueShaderOverride(const FRenderPassContext* Context)
    {
        if (!Context || !Context->RenderBus)
        {
            return nullptr;
        }

        if (Context->RenderBus->GetViewMode() != EViewMode::Unlit)
        {
            return nullptr;
        }

        return FResourceManager::Get().GetShader("Shaders/UberUnlit.hlsl");
    }

    bool IsWaterDrawCommand(const FRenderCommand& Cmd)
    {
        return Cmd.Water.bValid || (Cmd.Material != nullptr && Cmd.Material->IsWaterMaterial());
    }

    bool HasDirectionalLight(const FRenderBus* RenderBus)
    {
        if (RenderBus == nullptr)
        {
            return false;
        }

        for (const FRenderLight& Light : RenderBus->GetLights())
        {
            if (Light.Type == static_cast<uint32>(ELightType::LightType_Directional))
            {
                return true;
            }
        }

        return false;
    }

    void LogWaterLightingFallbackWarnings(const FRenderPassContext* Context)
    {
        static bool bWaterGlobalLightBufferMissingLogged = false;
        static bool bWaterDirectionalMissingLogged = false;

        if (Context->SceneGlobalLightBufferSRV == nullptr && !bWaterGlobalLightBufferMissingLogged)
        {
            UE_LOG("[Water] Global light buffer is missing. Water falls back to Stage 1 animated color only.");
            bWaterGlobalLightBufferMissingLogged = true;
        }

        if (!HasDirectionalLight(Context->RenderBus) && !bWaterDirectionalMissingLogged)
        {
            UE_LOG("[Water] Directional light is missing. Water keeps Stage 1 animation and uses available local-light highlights.");
            bWaterDirectionalMissingLogged = true;
        }
    }

    UShader* ResolveWaterShader()
    {
        UShader* WaterShader = FResourceManager::Get().GetShader("Shaders/Water.hlsl");
        if (WaterShader != nullptr)
        {
            return WaterShader;
        }

        FResourceManager::Get().LoadShader("Shaders/Water.hlsl", "mainVS", "mainPS", static_cast<const D3D_SHADER_MACRO*>(nullptr));
        return FResourceManager::Get().GetShader("Shaders/Water.hlsl");
    }

    bool EnsureWaterUniformConstantBuffer(ID3D11Device* Device, TComPtr<ID3D11Buffer>& WaterUniformConstantBuffer)
    {
        if (WaterUniformConstantBuffer)
        {
            return true;
        }

        D3D11_BUFFER_DESC Desc = {};
        Desc.ByteWidth = sizeof(FWaterUniformData);
        Desc.Usage = D3D11_USAGE_DYNAMIC;
        Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return SUCCEEDED(Device->CreateBuffer(&Desc, nullptr, WaterUniformConstantBuffer.GetAddressOf()));
    }

    bool BindWaterDrawResources(const FRenderPassContext* Context, const FRenderCommand& Cmd, TComPtr<ID3D11Buffer>& WaterUniformConstantBuffer)
    {
        if (Context == nullptr || Context->Device == nullptr || Context->DeviceContext == nullptr)
        {
            return false;
        }

        if (!EnsureWaterUniformConstantBuffer(Context->Device, WaterUniformConstantBuffer))
        {
            return false;
        }

        D3D11_MAPPED_SUBRESOURCE Mapped = {};
        if (FAILED(Context->DeviceContext->Map(WaterUniformConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
        {
            return false;
        }

        std::memcpy(Mapped.pData, &Cmd.Water.UniformData, sizeof(FWaterUniformData));
        Context->DeviceContext->Unmap(WaterUniformConstantBuffer.Get(), 0);

        ID3D11Buffer* WaterCB = WaterUniformConstantBuffer.Get();
        Context->DeviceContext->VSSetConstantBuffers(WaterShaderBindings::MaterialConstantBuffer, 1, &WaterCB);
        Context->DeviceContext->PSSetConstantBuffers(WaterShaderBindings::MaterialConstantBuffer, 1, &WaterCB);

        ID3D11ShaderResourceView* WaterSurfaceSRVs[WaterShaderBindings::TextureCount] =
        {
            Cmd.Water.NormalA ? Cmd.Water.NormalA->GetSRV() : nullptr,
            Cmd.Water.NormalB ? Cmd.Water.NormalB->GetSRV() : nullptr,
            Cmd.Water.Diffuse ? Cmd.Water.Diffuse->GetSRV() : nullptr
        };

        // Water keeps the existing forward mesh pass and only overrides its
        // dedicated per-draw resources when the material/component says so.
        Context->DeviceContext->PSSetShaderResources(
            WaterShaderBindings::FirstTextureRegister,
            WaterShaderBindings::TextureCount,
            WaterSurfaceSRVs);
        return true;
    }
}

bool FOpaqueRenderPass::Initialize()
{
    return true;
}

bool FOpaqueRenderPass::Begin(const FRenderPassContext* Context)
{
    const FRenderTargetSet* RenderTargets = Context->RenderTargets;
    ID3D11RenderTargetView* RTVs[3] = {
        RenderTargets->SceneColorRTV,
        RenderTargets->SceneNormalRTV,
        RenderTargets->SceneWorldPosRTV
    };
    ID3D11DepthStencilView* DSV = RenderTargets->DepthStencilView;

    Context->DeviceContext->OMSetRenderTargets(ARRAYSIZE(RTVs), RTVs, DSV);
    ID3D11DepthStencilState* DepthStencilState =
        FResourceManager::Get().GetOrCreateDepthStencilState(EDepthStencilType::DepthReadOnly);
    Context->DeviceContext->OMSetDepthStencilState(DepthStencilState, 0);
    OutSRV = RenderTargets->SceneColorSRV;
    OutRTV = RenderTargets->SceneColorRTV;

    Context->DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    return true;
}

bool FOpaqueRenderPass::DrawCommand(const FRenderPassContext* Context)
{
    const FRenderBus* RenderBus = Context->RenderBus;

    const TArray<FRenderCommand>& Commands = RenderBus->GetCommands(ERenderPass::Opaque);

    if (Commands.empty())
        return true;

    UShader* ShaderOverride = ResolveOpaqueShaderOverride(Context);
    UShader* WaterShader = ResolveWaterShader();
    static bool bWaterShaderMissingLogged = false;
    static bool bWaterConstantBufferUpdateFailedLogged = false;

    const auto BindSharedLightResources = [this, Context]()
    {
        // Material::Bind can touch shared shader bindings, so re-apply the
        // forward light resources before each draw to keep water and non-water
        // meshes on the same lighting contract.
        SceneLightBinding::BindResources(Context,
            VisibleLightConstantBuffer,
            DirectionalShadowConstantBuffer,
            SpotShadowInfoConstantBuffer,
            SpotShadowConstantsBuffer,
            SpotShadowConstantsSRV,
            SpotShadowConstantsCapacity,
            PointShadowInfoConstantBuffer,
            PointShadowConstantsBuffer,
            PointShadowConstantsSRV,
            PointShadowConstantsCapacity);
    };

    BindSharedLightResources();

    for (const FRenderCommand& Cmd : Commands)
    {
        if (Cmd.Type == ERenderCommandType::PostProcessOutline)
        {
            continue;
        }

        if (Cmd.MeshBuffer == nullptr || !Cmd.MeshBuffer->IsValid())
        {
            return false;
        }

        uint32 offset = 0;
        ID3D11Buffer* vertexBuffer = Cmd.MeshBuffer->GetVertexBuffer().GetBuffer();
        if (vertexBuffer == nullptr)
        {
            return false;
        }

        uint32 vertexCount = Cmd.MeshBuffer->GetVertexBuffer().GetVertexCount();
        uint32 stride = Cmd.MeshBuffer->GetVertexBuffer().GetStride();
        if (vertexCount == 0 || stride == 0)
        {
            return false;
        }

        if (Cmd.Material)
        {
            UShader* PerCommandShaderOverride = ShaderOverride;
            const bool bUseWaterPath = IsWaterDrawCommand(Cmd);
            if (bUseWaterPath)
            {
                if (WaterShader)
                {
                    PerCommandShaderOverride = WaterShader;
                }
                else if (!bWaterShaderMissingLogged)
                {
                    UE_LOG("[Water] Dedicated water shader is not loaded (Shaders/Water.hlsl). Falling back to material shader.");
                    bWaterShaderMissingLogged = true;
                }

                LogWaterLightingFallbackWarnings(Context);
            }

            Cmd.Material->Bind(Context->DeviceContext, Context->RenderBus, &Cmd.PerObjectConstants, PerCommandShaderOverride, Context);
            ID3D11DepthStencilState* DepthStencilState =
                FResourceManager::Get().GetOrCreateDepthStencilState(EDepthStencilType::DepthReadOnly);
            Context->DeviceContext->OMSetDepthStencilState(DepthStencilState, 0);

            if (bUseWaterPath)
            {
                if (!Cmd.Water.bValid && !bWaterConstantBufferUpdateFailedLogged)
                {
                    UE_LOG("[Water] Missing water draw data. Falling back to material-authored water parameters only.");
                    bWaterConstantBufferUpdateFailedLogged = true;
                }

                if (Cmd.Water.bValid && !BindWaterDrawResources(Context, Cmd, WaterUniformConstantBuffer) &&
                    !bWaterConstantBufferUpdateFailedLogged)
                {
                    UE_LOG("[Water] Failed to update/bind water uniform constant buffer (b2).");
                    bWaterConstantBufferUpdateFailedLogged = true;
                }
            }
        }

        BindSharedLightResources();

        CheckOverrideViewMode(Context);

        Context->DeviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

        ID3D11Buffer* indexBuffer = Cmd.MeshBuffer->GetIndexBuffer().GetBuffer();
        if (indexBuffer != nullptr)
        {
            uint32 indexStart = Cmd.SectionIndexStart;
            uint32 indexCount = Cmd.SectionIndexCount;
            Context->DeviceContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
            Context->DeviceContext->DrawIndexed(indexCount, indexStart, 0);
        }
        else
        {
            Context->DeviceContext->Draw(vertexCount, 0);
        }
    }

    return true;
}

bool FOpaqueRenderPass::End(const FRenderPassContext* Context)
{
    if (Context && Context->DeviceContext)
    {
        ID3D11Buffer* NullCB = nullptr;
        Context->DeviceContext->VSSetConstantBuffers(WaterShaderBindings::MaterialConstantBuffer, 1, &NullCB);
        Context->DeviceContext->PSSetConstantBuffers(WaterShaderBindings::MaterialConstantBuffer, 1, &NullCB);

        ID3D11ShaderResourceView* NullWaterSRVs[WaterShaderBindings::TextureCount] = { nullptr, nullptr, nullptr };
        Context->DeviceContext->PSSetShaderResources(
            WaterShaderBindings::FirstTextureRegister,
            WaterShaderBindings::TextureCount,
            NullWaterSRVs);
    }

    SceneLightBinding::UnbindResources(Context ? Context->DeviceContext : nullptr);
    return true;
}

bool FOpaqueRenderPass::Release()
{
    WaterUniformConstantBuffer.Reset();
    VisibleLightConstantBuffer.Reset();
    DirectionalShadowConstantBuffer.Reset();
    SpotShadowInfoConstantBuffer.Reset();
    SpotShadowConstantsBuffer.Reset();
    SpotShadowConstantsSRV.Reset();
    SpotShadowConstantsCapacity = 0;
    PointShadowInfoConstantBuffer.Reset();
    PointShadowConstantsBuffer.Reset();
    PointShadowConstantsSRV.Reset();
    PointShadowConstantsCapacity = 0;
    return true;
}
