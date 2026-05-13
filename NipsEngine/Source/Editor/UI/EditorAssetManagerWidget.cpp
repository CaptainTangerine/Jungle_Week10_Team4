#include "Editor/UI/EditorAssetManagerWidget.h"

#include "Editor/EditorEngine.h"
#include "Editor/Viewport/FFBXPreviewViewportLayout.h"
#include "ImGui/imgui.h"
#include "Render/Renderer/Renderer.h"

#include <string>
#include <utility>

void FEditorAssetManagerWidget::Initialize(UEditorEngine* InEditorEngine)
{
    FEditorWidget::Initialize(InEditorEngine);
    CreateFBXSceneViewer();
}

void FEditorAssetManagerWidget::Render(float DeltaTime)
{
    RemoveClosedViewers();

    ImGui::SetNextWindowSize(ImVec2(360.0f, 260.0f), ImGuiCond_Once);
    if (ImGui::Begin("Asset Manager"))
    {
        const int32 MaxFBXSceneViewers =
            FRenderer::MaxViewportResourceCount - FRenderer::EditorViewportResourceCount;
        if (static_cast<int32>(FBXSceneViewers.size()) < MaxFBXSceneViewers &&
            ImGui::Button("New FBX Scene Viewer"))
        {
            CreateFBXSceneViewer();
        }

        ImGui::Separator();
        ImGui::Text(
            "FBX Scene Viewers: %d / %d",
            static_cast<int32>(FBXSceneViewers.size()),
            MaxFBXSceneViewers);

        if (ImGui::BeginChild("##AssetManagerViewerList", ImVec2(0.0f, 0.0f), true))
        {
            for (FAssetViewerEntry& Entry : FBXSceneViewers)
            {
                ImGui::PushID(Entry.PreviewViewportId);
                ImGui::Text("%s", Entry.DisplayName.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Close"))
                {
                    Entry.FBXSceneViewWidget.Close();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();

    for (FAssetViewerEntry& Entry : FBXSceneViewers)
    {
        Entry.FBXSceneViewWidget.Render(DeltaTime);
    }

    RemoveClosedViewers();
}

void FEditorAssetManagerWidget::CreateFBXSceneViewer()
{
    if (!EditorEngine)
    {
        return;
    }

    const int32 ViewerNumber = NextFBXSceneViewerNumber++;
    const FString DisplayName = FString("FBX Scene Viewer ") + std::to_string(ViewerNumber);

    FFBXPreviewViewport* Preview = EditorEngine->GetFBXPreviewViewportLayout().CreatePreview(DisplayName);
    if (!Preview)
    {
        return;
    }

    FAssetViewerEntry Entry;
    Entry.PreviewViewportId = Preview->Id;
    Entry.DisplayName = DisplayName;
    Entry.FBXSceneViewWidget.Initialize(
        EditorEngine,
        Preview->Id,
        DisplayName + "###FBXSceneViewer" + std::to_string(Preview->Id));

    FBXSceneViewers.push_back(std::move(Entry));
}

void FEditorAssetManagerWidget::RemoveClosedViewers()
{
    if (!EditorEngine)
    {
        return;
    }

    for (auto It = FBXSceneViewers.begin(); It != FBXSceneViewers.end();)
    {
        if (!It->FBXSceneViewWidget.IsOpen())
        {
            EditorEngine->GetFBXPreviewViewportLayout().DestroyPreview(It->PreviewViewportId);
            It = FBXSceneViewers.erase(It);
        }
        else
        {
            ++It;
        }
    }
}
