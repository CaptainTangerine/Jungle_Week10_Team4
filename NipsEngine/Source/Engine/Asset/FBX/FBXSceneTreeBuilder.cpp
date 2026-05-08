#include "FBXSceneTreeBuilder.h"

bool FFBXSceneTreeBuilder::Build(FbxScene* Scene, OUT FFBXSceneSnapShot& OutSnapShot)
{
    OutSnapShot.Clear();
    if (!Scene)
    {
        return false;
    }

    FbxNode* RootNode = Scene->GetRootNode();
    if (!RootNode)
    {
        return false;
    }

    AddNodeRecursive(RootNode, -1, OutSnapShot);
    OutSnapShot.RootIndex = 0;

    return OutSnapShot.IsValid();
}

void FFBXSceneTreeBuilder::AddNodeRecursive(FbxNode* Node, int32 ParentIndex, OUT FFBXSceneSnapShot& OutSnapShot)
{
    if (!Node)
    {
        return;
    }

    int32 CurrentIndex = static_cast<int32>(OutSnapShot.Nodes.size());

    FFBXSceneNode CurNode = {};
    CurNode.ParentIndex = ParentIndex;
    CurNode.Name = Node->GetName();
    CurNode.Type = (ParentIndex < 0) ? EFBXSceneNodeType::Root : ConvertNodeType(Node);

    if (FbxNodeAttribute* Attribute = Node->GetNodeAttribute())
    {
        CurNode.AttributeName = Attribute->GetName();
    }

    const FbxDouble3 Translation = Node->LclTranslation.Get();
    const FbxDouble3 Rotation = Node->LclRotation.Get();
    const FbxDouble3 Scaling = Node->LclScaling.Get();

    CurNode.Translation = FVector((float)Translation[0], (float)Translation[1], (float)Translation[2]);
    CurNode.Rotation = FQuat::MakeFromEuler(FVector((float)Rotation[0], (float)Rotation[1], (float)Rotation[2]));
    CurNode.Scale = FVector((float)Scaling[0], (float)Scaling[1], (float)Scaling[2]);

    OutSnapShot.Nodes.push_back(CurNode);

    if (ParentIndex >= 0)
    {
        OutSnapShot.Nodes[ParentIndex].Children.push_back(CurrentIndex);
    }

    for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
    {
        AddNodeRecursive(Node->GetChild(ChildIndex), CurrentIndex, OutSnapShot);
    }
}

EFBXSceneNodeType FFBXSceneTreeBuilder::ConvertNodeType(FbxNode* Node)
{
    if (!Node)
    {
        return EFBXSceneNodeType::Unknown;
    }

    FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
    if (!Attribute)
    {
        return EFBXSceneNodeType::Null;
    }

    switch (Attribute->GetAttributeType())
    {
    case FbxNodeAttribute::eNull:
        return EFBXSceneNodeType::Null;
    case FbxNodeAttribute::eMesh:
        return EFBXSceneNodeType::Mesh;
    case FbxNodeAttribute::eSkeleton:
        return EFBXSceneNodeType::Skeleton;
    case FbxNodeAttribute::eCamera:
        return EFBXSceneNodeType::Camera;
    case FbxNodeAttribute::eLight:
        return EFBXSceneNodeType::Light;
    default:
        return EFBXSceneNodeType::Unknown;
    }
}
