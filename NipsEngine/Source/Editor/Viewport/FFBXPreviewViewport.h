#pragma once
#include "FBXPreviewViewportClient.h"
#include "Object/FName.h"

class UWorld;

struct FFBXPreviewViewport
{
public:
    int32 Id = -1;
    int32 ResourceIndex = -1;
    FName WorldHandle;
    UWorld* World = nullptr;
    FFBXPreviewViewportClient Client;
    ID3D11ShaderResourceView* SRV = nullptr;
};
