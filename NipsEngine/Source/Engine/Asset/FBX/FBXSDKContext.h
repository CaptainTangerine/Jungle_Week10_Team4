#pragma once
// FBX SDK의 생명주기를 관리
// 스마트 포인터를 쓸 시 Custom Deleter를 만들기엔 내부적으로 어떻게 이뤄져있는지 몰라서 RawPointer를 사용

#include <fbxsdk.h>

// LoadScene이 반환하는 RAII 래퍼
struct FFBXSceneHandle
{
    explicit FFBXSceneHandle(FbxScene* InScene = nullptr) : Scene(InScene) {}
    ~FFBXSceneHandle()
    {
        if (Scene) 
        {
            Scene->Destroy();
            Scene = nullptr;
        } 
    }

    FFBXSceneHandle(const FFBXSceneHandle&) = delete;
    FFBXSceneHandle& operator=(const FFBXSceneHandle&) = delete;

    FFBXSceneHandle(FFBXSceneHandle&& Other) noexcept : Scene(Other.Scene) { Other.Scene = nullptr; }
    FFBXSceneHandle& operator=(FFBXSceneHandle&& Other) noexcept
    {
        if (this != &Other) 
        { 
            if (Scene) Scene->Destroy(); 
            
            Scene = Other.Scene;
            Other.Scene = nullptr; 
        }
        return *this;
    }

    FbxScene* operator->() const { return Scene; }
    explicit operator bool() const { return Scene != nullptr; }
    FbxScene* Get() const { return Scene; }

    FbxScene* Scene = nullptr;
};

class FFBXSDKContext
{
public:
    FFBXSDKContext() = default;
    ~FFBXSDKContext();

    FFBXSDKContext(const FFBXSDKContext&) = delete;
    FFBXSDKContext& operator=(const FFBXSDKContext&) = delete;

public:
    static FFBXSDKContext& Get();

    bool Initialize();
    FFBXSceneHandle LoadScene(const FString& FilePath);
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
