#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Asset/FBX/FBXImporter.h"
#include "Core/Containers/String.h"

class USkinnedMeshComponent;

class FEditorFBXSceneViewWidget : public FEditorWidget
{
public:
    virtual void Render(float DeltaTime) override;

private:
    bool OpenFBXFileDialog(FString& OutFilePath) const;
    void LoadFBXScene(const FString& FilePath);
    void RenderToolbar();
    void RenderViewport();
    void RenderSummary() const;
    void RenderTree();
    void RenderNodeRecursive(int32 NodeIndex);
    void RenderBoneTreeRecursive(const FFBXSkeletalMeshImportData& Mesh, int32 BoneIndex, int32 SkeletalMeshIndex, USkinnedMeshComponent* SkinnedMeshComponent);
    void RenderDetails();
    void SpawnImportedFBXMeshActors();
    void SelectRootBoneForNode(int32 NodeIndex);

private:
    FFBXImportScene ImportScene;
    FString LoadedFilePath;
    FString StatusMessage = "No FBX loaded.";
    int32 SelectedNodeIndex = -1;
    int32 SelectedSkeletalMeshIndex = -1;
    int32 SelectedBoneIndex = -1;
    TArray<USkinnedMeshComponent*> PreviewSkinnedMeshComponents;
    bool bImportSkinnedMeshesAsStatic = false;
};
