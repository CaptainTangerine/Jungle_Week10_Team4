#pragma once
// FBX SDK의 생명주기를 관리
// 스마트 포인터를 쓸 시 Custom Deleter를 만들기 귀찮아 RawPointer로 관리

#include <fbxsdk.h>

class FFBXSDKContext
{
public:
    FFBXSDKContext() = default;
    ~FFBXSDKContext();

    FFBXSDKContext(const FFBXSDKContext&) = delete;
    FFBXSDKContext& operator=(const FFBXSDKContext&) = delete;

public:
    bool Initialize();
    FbxScene* LoadScene(const FString& FilePath);
    void Shutdown();

public:
    FbxManager* GetManager() { return Manager; };
    FbxIOSettings* GetIOSettings() { return IOSettings; };

private:
    // FbxGlobalSetting 세팅을 현재 엔진(Unreal) 에 맞게 변환
    void ConvertSceneToUnreal(FbxScene* Scene);

private:
    FbxManager*    Manager = { nullptr };
    FbxIOSettings* IOSettings = { nullptr };
};
