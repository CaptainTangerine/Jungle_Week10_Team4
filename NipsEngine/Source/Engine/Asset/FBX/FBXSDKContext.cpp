#include "FBXSDKContext.h"
#include "Core/CoreTypes.h"

#if defined(_DEBUG)
#pragma comment(lib, "ThirdParty/FBX SDK/lib/x64/debug/libfbxsdk-md.lib")
#pragma comment(lib, "ThirdParty/FBX SDK/lib/x64/debug/libxml2-md.lib")
#pragma comment(lib, "ThirdParty/FBX SDK/lib/x64/debug/zlib-md.lib")
#else
#pragma comment(lib, "ThirdParty/FBX SDK/lib/x64/release/libfbxsdk-md.lib")
#pragma comment(lib, "ThirdParty/FBX SDK/lib/x64/release/libxml2-md.lib")
#pragma comment(lib, "ThirdParty/FBX SDK/lib/x64/release/zlib-md.lib")
#endif

FFBXSDKContext::~FFBXSDKContext()
{
    Shutdown();
}

bool FFBXSDKContext::Initialize()
{
    if (Manager != nullptr)
    {
        return true;
    }
    Manager = FbxManager::Create();
    if (Manager == nullptr)
    {
        UE_LOG("[FBX] Failed to create FbxManager");
        return false;
    }

    IOSettings = FbxIOSettings::Create(Manager, IOSROOT);
    if (IOSettings == nullptr)
    {
        UE_LOG("[FBX] Failed to create FbxIOSettings");
        Shutdown();
        return false;
    }

    Manager->SetIOSettings(IOSettings);

    UE_LOG("[FBX] FBX SDK initialized");
    return true;
}

FFBXSceneHandle FFBXSDKContext::LoadScene(const FString& FilePath)
{
    if (!Manager)
    {
        UE_LOG("[FBX] LoadScene failed. FFBXSDKContext is not initialized.");
        return FFBXSceneHandle{};
    }

    // FBXImporter : FBX 파일을 읽는 객체
    FbxImporter* Importer = FbxImporter::Create(Manager, "");

    if (!Importer->Initialize(FilePath.c_str(), -1, IOSettings))
    {
        UE_LOG("Call to FbxImporter::Initialize() failed. FilePath : %s", FilePath.c_str());
        Importer->Destroy();
        return FFBXSceneHandle{};
    }

    FbxScene* Scene = FbxScene::Create(Manager, "myScene");
    if (!Scene)
    {
        UE_LOG("[FBX] Failed to create FbxScene. FilePath : %s", FilePath.c_str());
        Importer->Destroy();
        return FFBXSceneHandle{};
    }

    if (!Importer->Import(Scene))
    {
        UE_LOG("[FBX] Failed to import scene. FilePath : %s", FilePath.c_str());
        Importer->Destroy();
        return FFBXSceneHandle{ Scene }; // 소멸자가 Scene->Destroy() 처리
    }

    Importer->Destroy();
    ConvertSceneToUnreal(Scene);

    return FFBXSceneHandle{ Scene };
}

void FFBXSDKContext::Shutdown()
{
    if(Manager)
    {
        Manager->Destroy();
        Manager = nullptr;
        IOSettings = nullptr;
    }
}

void FFBXSDKContext::ConvertSceneToUnreal(FbxScene* Scene)
{
    if (!Scene)
    {
        return;
    }

    // 외부에서 불러온 툴은 좌표계가 다 달라서 언리얼 기준으로 맞춰줌
    const FbxAxisSystem UnrealAxisSystem(
        FbxAxisSystem::eZAxis, // UP
        FbxAxisSystem::eParityOdd, // Z-Up 좌수계에서 Unreal식 front 축을 고르는 FBX SDK 옵션
        FbxAxisSystem::eLeftHanded
    );

    const FbxAxisSystem SourceAxisSystem = Scene->GetGlobalSettings().GetAxisSystem();
    if (SourceAxisSystem != UnrealAxisSystem)
    {
        UnrealAxisSystem.DeepConvertScene(Scene);
    }

    const FbxSystemUnit CentimeterUnit(100.0);
    const FbxSystemUnit SourceUnit = Scene->GetGlobalSettings().GetSystemUnit();
    if (SourceUnit != CentimeterUnit)
    {
        FbxSystemUnit::ConversionOptions ConversionOptions;
        ConversionOptions.mConvertRrsNodes = true;
        ConversionOptions.mConvertLimits = true;
        ConversionOptions.mConvertClusters = true;
        ConversionOptions.mConvertLightIntensity = true;
        ConversionOptions.mConvertPhotometricLProperties = true;
        ConversionOptions.mConvertCameraClipPlanes = true;

        CentimeterUnit.ConvertScene(Scene, ConversionOptions);
    }
}
