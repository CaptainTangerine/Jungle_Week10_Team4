#pragma once

#include "Render/Scene/RenderBus.h"
#include <unordered_set>

class AActor;
class FLineBatcher;
class FMeshBufferManager;
class UGizmoComponent;
class UPrimitiveComponent;

class FOverlayRenderCollector
{
public:
    void Initialize(FMeshBufferManager* InMeshBufferManager) { MeshBufferManager = InMeshBufferManager; }
    void Release() { MeshBufferManager = nullptr; }

    void CollectSelection(const TArray<AActor*>& SelectedActors, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, FLineBatcher* LineBatcher);
    bool CollectPickedComponent(UPrimitiveComponent* Component, FRenderBus& RenderBus);
    void CollectGizmo(UGizmoComponent* Gizmo, const FShowFlags& ShowFlags, FRenderBus& RenderBus, bool bIsActiveOperation);
    void CollectGrid(float GridSpacing, int32 GridHalfLineCount, FRenderBus& RenderBus, bool bOrthographic = false, const FGridRenderSettings& GridRenderSettings = MakeDefaultGridRenderSettings());
    void CollectBVHInternalNodeAABBs(UPrimitiveComponent* PrimitiveComponent, const FShowFlags& ShowFlags, FRenderBus& RenderBus, FLineBatcher* LineBatcher, TSet<int32>& SeenNodeIndices);

private:
    bool CollectFromSelectedActor(AActor* Actor, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, FLineBatcher* LineBatcher);

    FMeshBufferManager* MeshBufferManager = nullptr;
};
