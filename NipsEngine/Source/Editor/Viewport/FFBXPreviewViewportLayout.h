#pragma once
#include "FFBXPreviewViewport.h"

class FSelectionManager;
class UWorld;
class UEditorEngine;
class FWindowsWindow;

class FFBXPreviewViewportLayout
{
public:
    void Init(FWindowsWindow* InWindow, UWorld* World, UEditorEngine* EditorEngine);
    void Shutdown();
    void Tick(float DeltaTime);
    
    FFBXPreviewViewport* CreatePreview(const FString& Name);
    void DestroyPreview(int32 PreviewId);
    UWorld* ResetPreviewWorld(int32 PreviewId);
    
    FFBXPreviewViewport* FindPreview(int32 PreviewId);
    const FFBXPreviewViewport* FindPreview(int32 PreviewId) const;
    TArray<FFBXPreviewViewport>& GetPreviews();
    const TArray<FFBXPreviewViewport>& GetPreviews() const;
    
private:
    int32 NextPreviewId = 1;
    TArray<FFBXPreviewViewport> Viewports;
    FWindowsWindow* Window = nullptr;
    UEditorEngine* Engine = nullptr;
};
