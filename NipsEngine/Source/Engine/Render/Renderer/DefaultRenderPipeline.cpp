#include "DefaultRenderPipeline.h"

#include "Renderer.h"
#include "Engine/Runtime/Engine.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "GameFramework/World.h"

FDefaultRenderPipeline::FDefaultRenderPipeline(UEngine* InEngine, FRenderer& InRenderer)
    : Engine(InEngine)
{
    Collector.Initialize(InRenderer.GetFD3DDevice().GetDevice(), InRenderer.GetFD3DDevice().GetDeviceContext());
}

FDefaultRenderPipeline::~FDefaultRenderPipeline()
{
    Collector.Release();
}
