#pragma once

#include "Core/Singleton.h"
#include <fbxsdk.h>
#include <string>

class FFbxManagerWrapper : public TSingleton<FFbxManagerWrapper>
{
    friend class TSingleton<FFbxManagerWrapper>;

public:
    // SDK 초기화
    void Initialize();
    // SDK 정리
    void Finalize();

    // FBX 파일을 읽고 함정(Triangulate, 축 변환)을 거친 Scene을 반환
    FbxScene* LoadFbxScene(const std::string& FilePath);

    FbxManager* GetSdkManager() const { return m_SdkManager; }

private:
    FbxManager* m_SdkManager = nullptr;
    FbxIOSettings* m_IOSettings = nullptr;
};