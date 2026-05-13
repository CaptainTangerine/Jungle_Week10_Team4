#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Editor/UI/EditorFBXSceneViewWidget.h"
#include "Core/Containers/Array.h"
#include "Core/Containers/String.h"

class FEditorAssetManagerWidget : public FEditorWidget
{
public:
    void Initialize(UEditorEngine* InEditorEngine) override;
    void Render(float DeltaTime) override;

private:
    struct FAssetViewerEntry
    {
        int32 PreviewViewportId = -1;
        FString DisplayName;
        FEditorFBXSceneViewWidget FBXSceneViewWidget;
    };

    void CreateFBXSceneViewer();
    void RemoveClosedViewers();

private:
    TArray<FAssetViewerEntry> FBXSceneViewers;
    int32 NextFBXSceneViewerNumber = 1;
};
