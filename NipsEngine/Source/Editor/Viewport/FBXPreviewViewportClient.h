#pragma once

#include "Runtime/ViewportClient.h"
#include "Engine/Viewport/ViewportCamera.h"
#include "Runtime/ViewportRect.h"

class  UWorld;
struct FSceneView;
struct FViewportMouseEvent;

class FFBXPreviewViewportClient : public FViewportClient
{
public:
    void Initialize(FWindowsWindow* InWindow) override;

    void SetWorld(UWorld* InWorld) { PreviewWorld = InWorld; }
    UWorld* GetWorld() const { return PreviewWorld; }

    void SetViewportRect(int32 InWidth, int32 InHeight);
    const FViewportRect& GetViewportRect() const { return ViewRect; }

    FViewportCamera& GetCamera() { return Camera; }
    const FViewportCamera& GetCamera() const { return Camera; }

    void Tick(float DeltaTime) override;
    void BuildSceneView(FSceneView& OutView) const override;


private:
    UWorld* PreviewWorld = nullptr;
    FViewportCamera Camera;
    FViewportRect ViewRect;

};
