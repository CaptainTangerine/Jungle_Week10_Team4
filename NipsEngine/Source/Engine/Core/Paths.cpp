#include "Engine/Core/Paths.h"
#include <filesystem>

namespace
{
    bool IsRelativePathInsideRoot(const std::filesystem::path& RelativePath)
    {
        auto It = RelativePath.begin();
        return It != RelativePath.end() && It->wstring() != L"..";
    }
}

FWString FPaths::RootDir()
{
    static FWString Cached;
    if (Cached.empty())
    {
        WCHAR Buffer[MAX_PATH];
        GetModuleFileNameW(nullptr, Buffer, MAX_PATH);
        std::filesystem::path ExeDir = std::filesystem::path(Buffer).parent_path();

        // 1. 배포 환경: exe 파일과 같은 폴더에 바로 Shaders/ 가 있는 경우
        if (std::filesystem::exists(ExeDir / L"Shaders"))
        {
            Cached = ExeDir.generic_wstring() + L"/";
        }
        else
        {
            // 2. 개발 환경: 빌드 깊이에 무관하게 exe 상위를 루트까지 순회하며 탐색
            bool bFound = false;
            std::filesystem::path SearchDir = ExeDir;

            while (SearchDir.has_parent_path())
            {
                SearchDir = SearchDir.parent_path();

                if (std::filesystem::exists(SearchDir / L"Shaders"))
                {
                    Cached = SearchDir.generic_wstring() + L"/";
                    bFound = true;
                    break;
                }

                // 드라이브 루트까지 올라갔으면 중단
                if (SearchDir == SearchDir.root_path())
                {
                    break;
                }
            }

            // 3. 어디서도 못 찾으면 CWD Fallback
            if (!bFound)
            {
                Cached = std::filesystem::current_path().generic_wstring() + L"/";
            }
        }
    }
    return Cached;
}

// 나머지 함수는 동일 ...

FWString FPaths::ShaderDir() { return RootDir() + L"Shaders/"; }
FWString FPaths::SceneDir() { return RootDir() + L"Asset/Scene/"; }
FWString FPaths::DumpDir() { return RootDir() + L"Saves/Dump/"; }
FWString FPaths::SettingsDir() { return RootDir() + L"Settings/"; }
FWString FPaths::ShaderFilePath() { return RootDir() + L"Shaders/ShaderW0.hlsl"; }
FWString FPaths::SettingsFilePath() { return RootDir() + L"Settings/Editor.ini"; }
FWString FPaths::AssetDirectoryPath() { return RootDir() + L"Asset"; }
FWString FPaths::ResourceDefaultMaterialTexture() { return RootDir() + L"Asset/Mesh/Default.png"; }


FWString FPaths::Combine(const FWString& Base, const FWString& Child)
{
    std::filesystem::path Result(Base);
    Result /= Child;
    return Result.generic_wstring(); // backslash를 slash로 변환해서 반환한다.
}

void FPaths::CreateDir(const FWString& Path)
{
    std::filesystem::create_directories(Path);
}

FWString FPaths::ToWide(const FString& Utf8Str)
{
    if (Utf8Str.empty())
    {
        return {};
    }

    const int Utf8Length = static_cast<int>(Utf8Str.size());
    UINT CodePage = CP_UTF8;
    DWORD Flags = MB_ERR_INVALID_CHARS;
    int WideLength = MultiByteToWideChar(
        CodePage,
        Flags,
        Utf8Str.data(),
        Utf8Length,
        nullptr,
        0);

    if (WideLength == 0)
    {
        CodePage = CP_ACP;
        Flags = 0;
        WideLength = MultiByteToWideChar(
            CodePage,
            Flags,
            Utf8Str.data(),
            Utf8Length,
            nullptr,
            0);
    }

    if (WideLength == 0)
    {
        CodePage = CP_UTF8;
        Flags = 0;
        WideLength = MultiByteToWideChar(
            CodePage,
            Flags,
            Utf8Str.data(),
            Utf8Length,
            nullptr,
            0);
        if (WideLength == 0)
        {
            return {};
        }
    }

    FWString Result(static_cast<size_t>(WideLength), L'\0');
    const int ConvertedLength = MultiByteToWideChar(
        CodePage,
        Flags,
        Utf8Str.data(),
        Utf8Length,
        Result.data(),
        WideLength);
    if (ConvertedLength == 0)
    {
        return {};
    }

    return Result;
}

FString FPaths::ToUtf8(const FWString& WideStr)
{
    if (WideStr.empty())
    {
        return {};
    }

    const int WideLength = static_cast<int>(WideStr.size());
    int Utf8Length = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        WideStr.data(),
        WideLength,
        nullptr,
        0,
        nullptr,
        nullptr);

    if (Utf8Length == 0)
    {
        Utf8Length = WideCharToMultiByte(
            CP_UTF8,
            0,
            WideStr.data(),
            WideLength,
            nullptr,
            0,
            nullptr,
            nullptr);
        if (Utf8Length == 0)
        {
            return {};
        }
    }

    FString Result(static_cast<size_t>(Utf8Length), '\0');
    const int ConvertedLength = WideCharToMultiByte(
        CP_UTF8,
        0,
        WideStr.data(),
        WideLength,
        Result.data(),
        Utf8Length,
        nullptr,
        nullptr);
    if (ConvertedLength == 0)
    {
        return {};
    }

    return Result;
}

FString FPaths::ToString(const FWString& wstring)
{
    return ToUtf8(wstring);
}

//	절대경로를 입력받아서 RootDir 기준의 상대 경로로 변환한다.
FWString FPaths::ToRelative(const FWString& AbsolutePath)
{
    if (AbsolutePath.empty()) return L"";

    std::filesystem::path AbsPath(AbsolutePath);
    std::filesystem::path Root(RootDir());
    
    //	RootDir 기준으로 상대 경로를 계산 (예: Asset/Scene/map.json)
    std::filesystem::path RelPath = std::filesystem::relative(AbsPath, Root);
    if (RelPath.empty())
    {
        return AbsPath.lexically_normal().generic_wstring(); 
    }

    return RelPath.generic_wstring();
}

//	상대 경로를 입력받아서 RootDir 기준의 절대 경로로 변환한다.
FWString FPaths::ToAbsolute(const FWString& RelativePath)
{
    if (RelativePath.empty()) return L"";

    std::filesystem::path TargetPath(RelativePath);

    // 이미 C:/ 등 절대 경로라면 변환하지 않고 그대로 반환
    if (TargetPath.is_absolute())
    {
        return TargetPath.generic_wstring();
    }

    std::filesystem::path Root(RootDir());
    
    // 상대 경로라면 RootDir에 붙인 뒤, 불필요한 슬래시 등을 정리(lexically_normal)
    return (Root / TargetPath).lexically_normal().generic_wstring();
}

// 절대경로를 입력받아서 RootDir 기준의 상대 경로 string으로 변환한다.
FString FPaths::ToRelativeString(const FWString &AbsolutePath) 
{
    return ToUtf8(ToRelative(AbsolutePath));
}

// 상대 경로를 입력받아서 RootDir 기준의 절대 경로 string으로 변환한다.
FString FPaths::ToAbsoluteString(const FWString &RelativePath) 
{
    return ToUtf8(ToAbsolute(RelativePath));
}

// JSON 경로를 불러와서 FString 문자열로 변경할 때 사용하는 헬퍼 함수, 한글/한자 경로에 안전
FString FPaths::Normalize(const FString& Path)
{
    FWString WidePath = ToWide(Path);
    std::filesystem::path NormalizedPath(WidePath);
    FWString NormalizedWide = NormalizedPath.lexically_normal().generic_wstring();
    return ToUtf8(NormalizedWide);
}

FString FPaths::NormalizeProjectPath(const FString& Path)
{
    if (Path.empty())
    {
        return {};
    }

    namespace fs = std::filesystem;

    fs::path NormalizedPath(FPaths::ToWide(Path));
    NormalizedPath = NormalizedPath.lexically_normal();

    if (!NormalizedPath.is_absolute())
    {
        return FPaths::ToUtf8(NormalizedPath.generic_wstring());
    }

    fs::path RootPath(FPaths::RootDir());
    RootPath = RootPath.lexically_normal();

    std::error_code ErrorCode;
    fs::path RelativePath = fs::relative(NormalizedPath, RootPath, ErrorCode);
    if (!ErrorCode && IsRelativePathInsideRoot(RelativePath))
    {
        return FPaths::ToUtf8(RelativePath.generic_wstring());
    }

    RelativePath = NormalizedPath.lexically_relative(RootPath);
    if (!RelativePath.empty() && IsRelativePathInsideRoot(RelativePath))
    {
        return FPaths::ToUtf8(RelativePath.generic_wstring());
    }

    return FPaths::ToUtf8(NormalizedPath.generic_wstring());
}
