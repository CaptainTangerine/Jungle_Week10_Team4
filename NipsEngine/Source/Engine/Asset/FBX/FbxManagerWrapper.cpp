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
    
    // 모든 폴리곤을 강제로 삼각형(Triangulate)으로 쪼갬
    FbxGeometryConverter GeometryConverter(m_SdkManager);
    GeometryConverter.Triangulate(Scene, true);

    // 좌표계(Axis System) 문제 방어
    FbxAxisSystem EngineAxis;
    if (FbxAxisSystem::ParseAxisSystem("yzx", EngineAxis))
    {
        EngineAxis.ConvertScene(Scene);
    }

    return Scene;
}