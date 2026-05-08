#pragma once

#include "Asset/FBX/FBXSceneTypes.h"
#include <fbxsdk.h>

class FFBXSceneTreeBuilder
{
public:
    FFBXSceneTreeBuilder() = default;
    ~FFBXSceneTreeBuilder() = default;

public:
    static bool Build(FbxScene* Scene, OUT FFBXSceneSnapShot& OutSnapShot);

private:
    static void AddNodeRecursive(FbxNode* Node, int32 ParentIndex, OUT FFBXSceneSnapShot& OutSnapShot);
    static EFBXSceneNodeType ConvertNodeType(FbxNode* Node);

};
