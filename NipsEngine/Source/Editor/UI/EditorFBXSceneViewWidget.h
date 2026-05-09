#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Asset/FBX/FBXImporter.h"
#include "Core/Containers/String.h"

class FEditorFBXSceneViewWidget : public FEditorWidget
{
public:
    virtual void Render(float DeltaTime) override;

private:
    bool OpenFBXFileDialog(FString& OutFilePath) const;
    void LoadFBXScene(const FString& FilePath);
    void RenderToolbar();
    void RenderSummary() const;
    void RenderTree();
    void RenderNodeRecursive(int32 NodeIndex);
    void RenderDetails() const;
    void SpawnImportedStaticMeshActors();

private:
    FFBXImportScene ImportScene;
    FString LoadedFilePath;
    FString StatusMessage = "No FBX loaded.";
    int32 SelectedNodeIndex = -1;
    bool bImportSkinnedMeshesAsStatic = false;
};
