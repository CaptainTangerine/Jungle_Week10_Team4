#include "FbxManagerWrapper.h"

void FFbxManagerWrapper::Initialize()
{
    if (m_SdkManager == nullptr)
    {
        m_SdkManager = FbxManager::Create();
        m_IOSettings = FbxIOSettings::Create(m_SdkManager, IOSROOT);
        m_SdkManager->SetIOSettings(m_IOSettings);
    }
}

void FFbxManagerWrapper::Finalize()
{
    if (m_SdkManager != nullptr)
    {
        // 매니저를 파괴하면 IOSettings도 알아서 파괴됩니다.
        m_SdkManager->Destroy();
        m_SdkManager = nullptr;
        m_IOSettings = nullptr;
    }
}

FbxScene* FFbxManagerWrapper::LoadFbxScene(const std::string& FilePath)
{
    if (m_SdkManager == nullptr)
    {
        return nullptr;
    }

    FbxImporter* Importer = FbxImporter::Create(m_SdkManager, "");
    if (!Importer->Initialize(FilePath.c_str(), -1, m_SdkManager->GetIOSettings()))
    {
        Importer->Destroy();
        return nullptr; // 로드 실패
    }

    FbxScene* Scene = FbxScene::Create(m_SdkManager, "");
    Importer->Import(Scene);
    Importer->Destroy(); // 읽었으면 임포터는 바로 날립니다.

    // 🚨 함정 1: N-Gon (다각형) 문제 방어
    // 모든 폴리곤을 강제로 삼각형(Triangulate)으로 쪼갬
    FbxGeometryConverter GeometryConverter(m_SdkManager);
    GeometryConverter.Triangulate(Scene, true);

    // 🚨 함정 2: 좌표계(Axis System) 문제 방어
    // 엔진 좌표계에 맞게 변환 (DirectX 11 기준인 Y-Up, Left-Handed로 변환)
    FbxAxisSystem EngineAxis(FbxAxisSystem::eYAxis, FbxAxisSystem::eParityOdd, FbxAxisSystem::eLeftHanded);
    EngineAxis.ConvertScene(Scene);

    return Scene;
}