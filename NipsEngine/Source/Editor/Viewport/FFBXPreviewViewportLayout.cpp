#include "Core/EnginePCH.h"
#include "FFBXPreviewViewportLayout.h"

#include "Editor/EditorEngine.h"
#include "Component/Light/AmbientLightComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "GameFramework/PrimitiveActors.h"
#include "GameFramework/World.h"
#include "Render/Renderer/Renderer.h"

namespace
{
    FName MakeFBXPreviewWorldHandle(int32 PreviewId)
    {
        return FName(FString("FBXPreviewWorld_") + std::to_string(PreviewId));
    }

    bool IsResourceIndexInUse(const TArray<FFBXPreviewViewport>& Viewports, int32 ResourceIndex)
    {
        for (const FFBXPreviewViewport& Viewport : Viewports)
        {
            if (Viewport.ResourceIndex == ResourceIndex)
            {
                return true;
            }
        }
        return false;
    }

    void AddPreviewDefaultLights(UWorld* World)
    {
        if (!World)
        {
            return;
        }

        AAmbientLightActor* AmbientActor = World->SpawnActor<AAmbientLightActor>();
        if (!AmbientActor)
        {
            return;
        }

        AmbientActor->SetTag("FBXPreviewAmbientLight");
        if (UAmbientLightComponent* AmbientLight = Cast<UAmbientLightComponent>(AmbientActor->GetRootComponent()))
        {
            AmbientLight->SetLightColor(FColor(1.0f, 1.0f, 1.0f, 1.0f));
            AmbientLight->SetIntensity(0.6f);
        }

        ADirectionalLightActor* DirectionalActor = World->SpawnActor<ADirectionalLightActor>();
        if (!DirectionalActor)
        {
            return;
        }

        DirectionalActor->SetTag("FBXPreviewDirectionalLight");
        if (UDirectionalLightComponent* DirectionalLight = Cast<UDirectionalLightComponent>(DirectionalActor->GetRootComponent()))
        {
            const FVector DirectionToLight = FVector(-0.45f, -0.35f, 0.82f).GetSafeNormal();
            DirectionalLight->SetRelativeRotationQuat(FQuat(FMatrix::MakeFromX(DirectionToLight * -1.0f)));
            DirectionalLight->SetLightColor(FColor(1.0f, 0.96f, 0.9f, 1.0f));
            DirectionalLight->SetIntensity(1.2f);
            DirectionalLight->SetCastShadows(false);
            DirectionalLight->SetDayNightAttenuationEnabled(false);
        }
    }
}

void FFBXPreviewViewportLayout::Init(FWindowsWindow* InWindow, UWorld* World, UEditorEngine* EditorEngine)
{
    Window = InWindow;
    Engine = EditorEngine;
}

void FFBXPreviewViewportLayout::Shutdown()
{
    if (Engine)
    {
        for (FFBXPreviewViewport& Viewport : Viewports)
        {
            Viewport.Client.SetWorld(nullptr);
            Viewport.World = nullptr;
            Viewport.SRV = nullptr;

            if (Viewport.WorldHandle != FName::None)
            {
                Engine->DestroyWorldContext(Viewport.WorldHandle);
            }
        }
    }

    Viewports.clear();
    NextPreviewId = 1;
    Window = nullptr;
    Engine = nullptr;
}

void FFBXPreviewViewportLayout::Tick(float DeltaTime)
{
    for (FFBXPreviewViewport& Viewport : Viewports)
    {
        Viewport.Client.Tick(DeltaTime);
    }
}

FFBXPreviewViewport* FFBXPreviewViewportLayout::CreatePreview(const FString& Name)
{
    if (!Engine)
    {
        return nullptr;
    }

    const int32 PreviewId = NextPreviewId++;
    int32 ResourceIndex = -1;
    for (int32 Index = FRenderer::EditorViewportResourceCount; Index < FRenderer::MaxViewportResourceCount; ++Index)
    {
        if (!IsResourceIndexInUse(Viewports, Index))
        {
            ResourceIndex = Index;
            break;
        }
    }

    if (ResourceIndex < 0)
    {
        return nullptr;
    }

    const FName WorldHandle = MakeFBXPreviewWorldHandle(PreviewId);
    const FString WorldName = Name.empty()
        ? FString("FBX Preview World ") + std::to_string(PreviewId)
        : Name;

    FWorldContext& Context = Engine->CreateWorldContext(EWorldType::ViewerPreview, WorldHandle, WorldName);
    if (Context.World)
    {
        Context.World->SetWorldType(EWorldType::ViewerPreview);
        Engine->ApplySpatialIndexMaintenanceSettings(Context.World);
        AddPreviewDefaultLights(Context.World);
    }

    FFBXPreviewViewport Viewport;
    Viewport.Id = PreviewId;
    Viewport.ResourceIndex = ResourceIndex;
    Viewport.WorldHandle = WorldHandle;
    Viewport.World = Context.World;
    Viewport.SRV = nullptr;
    Viewport.Client.Initialize(Window);
    Viewport.Client.SetWorld(Context.World);

    Viewports.push_back(Viewport);
    return &Viewports.back();
}

void FFBXPreviewViewportLayout::DestroyPreview(int32 PreviewId)
{
    for (auto It = Viewports.begin(); It != Viewports.end(); ++It)
    {
        if (It->Id != PreviewId)
        {
            continue;
        }

        It->Client.SetWorld(nullptr);
        It->World = nullptr;
        It->SRV = nullptr;

        if (Engine && It->WorldHandle != FName::None)
        {
            Engine->DestroyWorldContext(It->WorldHandle);
        }

        Viewports.erase(It);
        return;
    }
}

UWorld* FFBXPreviewViewportLayout::ResetPreviewWorld(int32 PreviewId)
{
    FFBXPreviewViewport* Preview = FindPreview(PreviewId);
    if (!Preview || !Engine)
    {
        return nullptr;
    }

    Preview->Client.SetWorld(nullptr);
    Preview->World = nullptr;
    Preview->SRV = nullptr;

    if (Preview->WorldHandle != FName::None)
    {
        Engine->DestroyWorldContext(Preview->WorldHandle);
    }

    FWorldContext& Context = Engine->CreateWorldContext(
        EWorldType::ViewerPreview,
        Preview->WorldHandle,
        FString("FBX Preview World ") + std::to_string(Preview->Id));
    if (Context.World)
    {
        Context.World->SetWorldType(EWorldType::ViewerPreview);
        Engine->ApplySpatialIndexMaintenanceSettings(Context.World);
        AddPreviewDefaultLights(Context.World);
    }

    Preview->World = Context.World;
    Preview->Client.SetWorld(Context.World);
    return Context.World;
}

FFBXPreviewViewport* FFBXPreviewViewportLayout::FindPreview(int32 PreviewId)
{
    for (FFBXPreviewViewport& Viewport : Viewports)
    {
        if (Viewport.Id == PreviewId)
        {
            return &Viewport;
        }
    }
    return nullptr;
}

const FFBXPreviewViewport* FFBXPreviewViewportLayout::FindPreview(int32 PreviewId) const
{
    for (const FFBXPreviewViewport& Viewport : Viewports)
    {
        if (Viewport.Id == PreviewId)
        {
            return &Viewport;
        }
    }
    return nullptr;
}

TArray<FFBXPreviewViewport>& FFBXPreviewViewportLayout::GetPreviews()
{
    return Viewports;
}

const TArray<FFBXPreviewViewport>& FFBXPreviewViewportLayout::GetPreviews() const
{
    return Viewports;
}
