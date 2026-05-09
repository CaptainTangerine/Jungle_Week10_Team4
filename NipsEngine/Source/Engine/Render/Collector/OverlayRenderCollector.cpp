#include "OverlayRenderCollector.h"

#include "Component/BillboardComponent.h"
#include "Component/GizmoComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/TextRenderComponent.h"
#include "Core/ResourceManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/World.h"
#include "Render/LineBatcher.h"
#include "Render/Resource/Material.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Spatial/WorldSpatialIndex.h"

namespace
{
    FColor MakeBVHInternalNodeColor(int32 PathIndexFromLeaf, int32 PathLength)
    {
        if (PathLength <= 1)
        {
            return FColor::Yellow();
        }

        const float T = static_cast<float>(PathIndexFromLeaf) / static_cast<float>(PathLength - 1);
        return FColor::Lerp(FColor::Cyan(), FColor::Yellow(), T);
    }

    FMatrix MakeViewSubUVSelectionMatrix(const USubUVComponent* SubUVComp, const FRenderBus& RenderBus)
    {
        const FVector WorldScale = SubUVComp->GetWorldScale();
        return USubUVComponent::MakeBillboardWorldMatrix(
            SubUVComp->GetWorldLocation(),
            FVector(
                WorldScale.X > 0.01f ? WorldScale.X : 0.01f,
                SubUVComp->GetWidth() * WorldScale.Y,
                SubUVComp->GetHeight() * WorldScale.Z),
            RenderBus.GetCameraForward(),
            RenderBus.GetCameraRight(),
            RenderBus.GetCameraUp());
    }

    FAABB BuildQuadAABB(const FMatrix& WorldMatrix)
    {
        static constexpr FVector LocalQuadCorners[4] =
        {
            FVector(0.0f, -0.5f,  0.5f),
            FVector(0.0f,  0.5f,  0.5f),
            FVector(0.0f,  0.5f, -0.5f),
            FVector(0.0f, -0.5f, -0.5f)
        };

        FAABB Box;
        Box.Reset();

        for (const FVector& Corner : LocalQuadCorners)
        {
            Box.Expand(WorldMatrix.TransformPosition(Corner));
        }

        return Box;
    }

    FAABB BuildRenderAABB(const UPrimitiveComponent* PrimitiveComponent, const FRenderBus& RenderBus)
    {
        switch (PrimitiveComponent->GetPrimitiveType())
        {
        case EPrimitiveType::EPT_Billboard:
        {
            const UBillboardComponent* BillboardComponent = static_cast<const UBillboardComponent*>(PrimitiveComponent);
            return BuildQuadAABB(UBillboardComponent::MakeBillboardWorldMatrix(
                BillboardComponent->GetWorldLocation(),
                FVector(0.00f, BillboardComponent->GetWidth(), BillboardComponent->GetHeight()),
                RenderBus.GetCameraForward(),
                RenderBus.GetCameraRight(),
                RenderBus.GetCameraUp()));
        }
        case EPrimitiveType::EPT_Text:
        {
            const UTextRenderComponent* TextComp = static_cast<const UTextRenderComponent*>(PrimitiveComponent);
            return BuildQuadAABB(TextComp->GetTextMatrix());
        }
        case EPrimitiveType::EPT_SubUV:
        {
            const USubUVComponent* SubUVComp = static_cast<const USubUVComponent*>(PrimitiveComponent);
            return BuildQuadAABB(MakeViewSubUVSelectionMatrix(SubUVComp, RenderBus));
        }
        default:
            return PrimitiveComponent->GetWorldAABB();
        }
    }
}

void FOverlayRenderCollector::CollectSelection(
    const TArray<AActor*>& SelectedActors,
    const FShowFlags& ShowFlags,
    EViewMode ViewMode,
    FRenderBus& RenderBus,
    FLineBatcher* LineBatcher)
{
    if (MeshBufferManager == nullptr)
    {
        return;
    }

    bool bHasSelectionMask = false;
    for (AActor* Actor : SelectedActors)
    {
        bHasSelectionMask |= CollectFromSelectedActor(Actor, ShowFlags, ViewMode, RenderBus, LineBatcher);
    }

    if (bHasSelectionMask)
    {
        FRenderCommand PostProcessCmd = {};
        PostProcessCmd.Type = ERenderCommandType::PostProcessOutline;
        PostProcessCmd.Material = FResourceManager::Get().GetMaterial("OutlineMaterial");

        UMaterial* Material = Cast<UMaterial>(PostProcessCmd.Material);
        Material->SetVector2("OutlineViewportSize", RenderBus.GetViewportSize());
        Material->SetVector2("OutlineViewportOrigin", RenderBus.GetViewportOrigin());
        Material->DepthStencilType = EDepthStencilType::Default;
        Material->RasterizerType = ERasterizerType::SolidBackCull;
        Material->BlendType = EBlendType::AlphaBlend;

        RenderBus.AddCommand(ERenderPass::PostProcessOutline, PostProcessCmd);
    }
}

void FOverlayRenderCollector::CollectGrid(
    float GridSpacing,
    int32 GridHalfLineCount,
    FRenderBus& RenderBus,
    bool bOrthographic,
    const FGridRenderSettings& GridRenderSettings)
{
    FRenderCommand Cmd = {};
    Cmd.Type = ERenderCommandType::Grid;
    Cmd.Constants.Grid.GridSpacing = GridSpacing;
    Cmd.Constants.Grid.GridHalfLineCount = GridHalfLineCount;
    Cmd.Constants.Grid.bOrthographic = bOrthographic;
    Cmd.Constants.Grid.RenderSettings = GridRenderSettings;
    RenderBus.AddCommand(ERenderPass::Grid, Cmd);
}

void FOverlayRenderCollector::CollectGizmo(
    UGizmoComponent* Gizmo,
    const FShowFlags& ShowFlags,
    FRenderBus& RenderBus,
    bool bIsActiveOperation)
{
    if (ShowFlags.bGizmo == false) return;
    if (!Gizmo || !Gizmo->IsVisible()) return;
    if (MeshBufferManager == nullptr) return;

    FMeshBuffer* GizmoMesh = &MeshBufferManager->GetMeshBuffer(Gizmo->GetPrimitiveType());
    FMatrix WorldMatrix = Gizmo->GetWorldMatrix();
    bool bHolding = Gizmo->IsHolding();
    int32 SelectedAxis = Gizmo->GetSelectedAxis();

    auto CreateGizmoCmd = [&](bool bInner) {
        FRenderCommand Cmd = {};
        Cmd.Type = ERenderCommandType::Gizmo;
        Cmd.MeshBuffer = GizmoMesh;

        Cmd.SectionIndexStart = 0;
        Cmd.SectionIndexCount = GizmoMesh->GetIndexBuffer().GetIndexCount();

        Cmd.PerObjectConstants = FPerObjectConstants{ WorldMatrix };

        UMaterial* Material = Cast<UMaterial>(Gizmo->GetMaterial());
        Cmd.Material = Material;

        if (bInner)
        {
            Material->DepthStencilType = EDepthStencilType::GizmoInside;
            Material->BlendType = EBlendType::AlphaBlend;
        }
        else
        {
            Material->DepthStencilType = EDepthStencilType::GizmoOutside;
            Material->BlendType = EBlendType::Opaque;
        }

        Material->SetVector4("GizmoColorTint", FVector4(1.0f, 1.0f, 1.0f, 1.0f));
        Material->SetBool("bIsInnerGizmo", bInner);
        Material->SetBool("bClicking", bHolding);
        Material->SetUInt("SelectedAxis", (SelectedAxis >= 0 && bIsActiveOperation) ? static_cast<uint32>(SelectedAxis) : 0xffffffffu);
        Material->SetFloat("HoveredAxisOpacity", 0.3f);

        return Cmd;
    };

    RenderBus.AddCommand(ERenderPass::DepthLess, CreateGizmoCmd(false));

    if (!bHolding)
    {
        RenderBus.AddCommand(ERenderPass::DepthLess, CreateGizmoCmd(true));
    }
}

bool FOverlayRenderCollector::CollectFromSelectedActor(
    AActor* Actor,
    const FShowFlags& ShowFlags,
    EViewMode ViewMode,
    FRenderBus& RenderBus,
    FLineBatcher* LineBatcher)
{
    if (!Actor->IsVisible()) return false;
    if (MeshBufferManager == nullptr) return false;

    bool bHasSelectionMask = false;
    TSet<int32> SeenBVHNodeIndices;

    for (UPrimitiveComponent* primitiveComponent : Actor->GetPrimitiveComponents())
    {
        if (!primitiveComponent->IsVisible()) continue;
        if (primitiveComponent->IsEditorOnly())
        {
            UWorld* World = Actor->GetFocusedWorld();
            if (World && World->GetWorldType() != EWorldType::Editor)
                continue;
        }

        FMeshBuffer* MeshBuffer = nullptr;
        if (primitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_StaticMesh)
        {
            auto* StaticMeshComp = static_cast<UStaticMeshComponent*>(primitiveComponent);
            MeshBuffer = MeshBufferManager->GetStaticMeshBuffer(StaticMeshComp->GetStaticMesh());
        }
        else
        {
            MeshBuffer = &MeshBufferManager->GetMeshBuffer(primitiveComponent->GetPrimitiveType());
        }

        if (!MeshBuffer)
        {
            continue;
        }

        FRenderCommand BaseCmd{};
        BaseCmd.MeshBuffer = MeshBuffer;
        BaseCmd.PerObjectConstants = FPerObjectConstants(primitiveComponent->GetWorldMatrix());
        BaseCmd.SectionIndexStart = 0;
        BaseCmd.SectionIndexCount = MeshBuffer->GetIndexBuffer().GetIndexCount();

        if (primitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_Text)
        {
            UTextRenderComponent* TextComp = static_cast<UTextRenderComponent*>(primitiveComponent);
            const FFontResource* Font = TextComp->GetFont();
            if (!Font || !Font->IsLoaded()) continue;
            const FString& Text = TextComp->GetText();
            if (Text.empty()) continue;

            FMatrix WorldMatrix = TextComp->GetTextMatrix();

            FRenderCommand TextCmd = BaseCmd;
            BaseCmd.PerObjectConstants = FPerObjectConstants(WorldMatrix);
            TextCmd.PerObjectConstants = FPerObjectConstants(TextComp->GetWorldMatrix(), TextComp->GetColor());
            TextCmd.Type = ERenderCommandType::Font;
            TextCmd.Constants.Font.Text = &Text;
            TextCmd.Constants.Font.Font = Font;
            TextCmd.Constants.Font.Scale = TextComp->GetFontSize();
            RenderBus.AddCommand(ERenderPass::Font, TextCmd);
        }
        else if (primitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_SubUV)
        {
            BaseCmd.PerObjectConstants.Model = MakeViewSubUVSelectionMatrix(
                static_cast<USubUVComponent*>(primitiveComponent),
                RenderBus);
        }
        else if(primitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_Billboard)
        {
            const UBillboardComponent* BComp = static_cast<const UBillboardComponent*>(primitiveComponent);
            BaseCmd.PerObjectConstants.Model = UBillboardComponent::MakeBillboardWorldMatrix(
                BComp->GetWorldLocation(),
                FVector(0.01f, BComp->GetWidth(), BComp->GetHeight()),
                RenderBus.GetCameraForward(),
                RenderBus.GetCameraRight(),
                RenderBus.GetCameraUp());
        }

        if (!primitiveComponent->SupportsOutline()) continue;

        FRenderCommand MaskCmd = BaseCmd;
        if (primitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_Billboard)
        {
            MaskCmd.Type = ERenderCommandType::BillboardSelectionMask;
            MaskCmd.Constants.Billboard.Texture = static_cast<UBillboardComponent*>(primitiveComponent)->GetTexture();
        }
        else
        {
            MaskCmd.Type = ERenderCommandType::SelectionMask;
        }
        RenderBus.AddCommand(ERenderPass::SelectionMask, MaskCmd);
        bHasSelectionMask = true;

        if (ShowFlags.bBoundingVolume && LineBatcher != nullptr)
        {
            LineBatcher->AddAABB(BuildRenderAABB(primitiveComponent, RenderBus), FColor::White());
        }

        CollectBVHInternalNodeAABBs(primitiveComponent, ShowFlags, RenderBus, LineBatcher, SeenBVHNodeIndices);
    }

    return bHasSelectionMask;
}

void FOverlayRenderCollector::CollectBVHInternalNodeAABBs(
    UPrimitiveComponent* PrimitiveComponent,
    const FShowFlags& ShowFlags,
    FRenderBus& RenderBus,
    FLineBatcher* LineBatcher,
    TSet<int32>& SeenNodeIndices)
{
    if (!ShowFlags.bBoundingVolume || !ShowFlags.bBVHBoundingVolume || PrimitiveComponent == nullptr || LineBatcher == nullptr)
    {
        return;
    }

    AActor* Owner = PrimitiveComponent->GetOwner();
    UWorld* World = Owner ? Owner->GetFocusedWorld() : nullptr;
    if (World == nullptr)
    {
        return;
    }

    const FWorldSpatialIndex& SpatialIndex = World->GetSpatialIndex();
    const int32 ObjectIndex = SpatialIndex.FindObjectIndex(PrimitiveComponent);
    if (ObjectIndex == FBVH::INDEX_NONE)
    {
        return;
    }

    const FBVH& BVH = SpatialIndex.GetBVH();
    const TArray<int32>& ObjectToLeafNode = BVH.GetObjectToLeafNode();
    if (ObjectIndex < 0 || ObjectIndex >= static_cast<int32>(ObjectToLeafNode.size()))
    {
        return;
    }

    const int32 LeafNodeIndex = ObjectToLeafNode[ObjectIndex];
    if (LeafNodeIndex == FBVH::INDEX_NONE)
    {
        return;
    }

    const TArray<FBVH::FNode>& Nodes = BVH.GetNodes();
    if (LeafNodeIndex < 0 || LeafNodeIndex >= static_cast<int32>(Nodes.size()))
    {
        return;
    }

    TArray<int32> PathToRoot;
    PathToRoot.reserve(16);

    int32 CurrentNodeIndex = Nodes[LeafNodeIndex].Parent;
    while (CurrentNodeIndex != FBVH::INDEX_NONE)
    {
        if (CurrentNodeIndex < 0 || CurrentNodeIndex >= static_cast<int32>(Nodes.size()))
        {
            break;
        }

        PathToRoot.push_back(CurrentNodeIndex);
        CurrentNodeIndex = Nodes[CurrentNodeIndex].Parent;
    }

    for (int32 PathIndex = 0; PathIndex < static_cast<int32>(PathToRoot.size()); ++PathIndex)
    {
        const int32 NodeIndex = PathToRoot[PathIndex];
        if (!SeenNodeIndices.insert(NodeIndex).second)
        {
            continue;
        }

        const FBVH::FNode& Node = Nodes[NodeIndex];
        if (Node.IsLeaf())
        {
            continue;
        }

        const FColor Color = MakeBVHInternalNodeColor(PathIndex, static_cast<int32>(PathToRoot.size()));
        LineBatcher->AddAABB(Node.Bounds, Color);
    }
}
