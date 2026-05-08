#pragma once


/** \enum EType Node attribute types.
    * - \e eUnknown
    * - \e eNull
    * - \e eMarker
    * - \e eSkeleton
    * - \e eMesh
    * - \e eNurbs
    * - \e ePatch
    * - \e eCamera
    * - \e eCameraStereo,
    * - \e eCameraSwitcher
    * - \e eLight
    * - \e eOpticalReference
    * - \e eOpticalMarker
    * - \e eNurbsCurve
    * - \e eTrimNurbsSurface
    * - \e eBoundary
    * - \e eNurbsSurface
    * - \e eShape
    * - \e eLODGroup
    * - \e eSubDiv
    * - \e eCachedEffect
    * - \e eLine
    */
enum EFBXSceneNodeType
{
    Root,
    Null,
    Mesh,
    Skeleton,
    Camera,
    Light,
    Unknown,
};

struct FFBXSceneNode
{
    FString Name;
    FString AttributeName;
    EFBXSceneNodeType Type = EFBXSceneNodeType::Unknown;

    FVector Translation;
    FQuat   Rotation; // 추후 애니메이션 보간을 위해 Quat저장
    FVector Scale;

    int32 ParentIndex = -1;
    TArray<int32> Children;
};


// Imgui로 FBX 시각화 전용
struct FFBXSceneSnapShot
{
    TArray<FFBXSceneNode> Nodes;
    int32 RootIndex = -1;

    bool IsValid() const
    {
        return (RootIndex >= 0 && RootIndex < static_cast<int32>(Nodes.size()));
    }

    void Clear()
    {
        Nodes.clear();
        RootIndex = -1;
    }
};
