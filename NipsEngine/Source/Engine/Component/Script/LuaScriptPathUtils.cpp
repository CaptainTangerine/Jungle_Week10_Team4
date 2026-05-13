#include "Core/EnginePCH.h"
#include "Engine/Component/Script/LuaScriptPathUtils.h"

#include "Component/StaticMeshComponent.h"
#include "Core/ActorTags.h"
#include "Engine/Core/Paths.h"
#include "Engine/GameFramework/Actor.h"

#include <cctype>
#include <cstring>
#include <filesystem>

namespace
{
    constexpr const char* ActorTypeScriptRoot = "Asset/Scripts/ActorTypes/";

    bool IsGenericActorClassName(const FString& ClassName)
    {
        return ClassName == "AActor"
            || ClassName == "ASceneActor"
            || ClassName == "AStaticMeshActor"
            || ClassName == "ASkeletalMeshActor"
            || ClassName == "APawn";
    }

    FString StripNativeActorPrefix(const FString& ClassName)
    {
        if (ClassName.size() > 1
            && ClassName[0] == 'A'
            && std::isupper(static_cast<unsigned char>(ClassName[1])) != 0)
        {
            return ClassName.substr(1);
        }

        return ClassName;
    }

    bool EndsWithInsensitive(const FString& Value, const char* Suffix)
    {
        const size_t SuffixLength = std::strlen(Suffix);
        if (Value.size() < SuffixLength)
        {
            return false;
        }

        const size_t Offset = Value.size() - SuffixLength;
        for (size_t Index = 0; Index < SuffixLength; ++Index)
        {
            if (std::tolower(static_cast<unsigned char>(Value[Offset + Index]))
                != std::tolower(static_cast<unsigned char>(Suffix[Index])))
            {
                return false;
            }
        }

        return true;
    }

    FString AppendActorSuffixIfNeeded(const FString& Name)
    {
        if (Name.empty() || EndsWithInsensitive(Name, "Actor"))
        {
            return Name;
        }

        return Name + "Actor";
    }

    FString StripGeneratedSuffix(const FString& Name)
    {
        if (Name.empty())
        {
            return Name;
        }

        size_t End = Name.size();
        while (End > 0 && std::isdigit(static_cast<unsigned char>(Name[End - 1])) != 0)
        {
            --End;
        }

        if (End == Name.size())
        {
            return Name;
        }

        if (End > 0 && (Name[End - 1] == '_' || Name[End - 1] == '-' || Name[End - 1] == ' '))
        {
            --End;
        }

        return End > 0 ? Name.substr(0, End) : Name;
    }

    UStaticMeshComponent* FindOwnedStaticMeshComponent(const AActor* Actor)
    {
        if (Actor == nullptr)
        {
            return nullptr;
        }

        if (UStaticMeshComponent* RootMesh = Cast<UStaticMeshComponent>(Actor->GetRootComponent()))
        {
            return RootMesh;
        }

        for (UActorComponent* Component : Actor->GetComponents())
        {
            if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
            {
                return StaticMeshComponent;
            }
        }

        return nullptr;
    }

    FString GetStaticMeshDerivedTypeName(const AActor* Actor)
    {
        const UStaticMeshComponent* StaticMeshComponent = FindOwnedStaticMeshComponent(Actor);
        const UStaticMesh* StaticMesh = StaticMeshComponent ? StaticMeshComponent->GetStaticMesh() : nullptr;
        if (StaticMesh == nullptr)
        {
            return {};
        }

        const FString& AssetPath = StaticMesh->GetAssetPathFileName();
        if (AssetPath.empty())
        {
            return {};
        }

        const std::filesystem::path MeshPath(FPaths::ToWide(AssetPath));
        const FString MeshStem = FPaths::ToUtf8(MeshPath.stem().wstring());
        return MeshStem.empty() ? FString() : AppendActorSuffixIfNeeded(MeshStem);
    }
}

namespace LuaScriptPathUtils
{
    FString SanitizeFileName(const FString& Name)
    {
        FWString WideName = FPaths::ToWide(Name);
        if (WideName.empty())
        {
            return "Actor";
        }

        FWString Sanitized;
        Sanitized.reserve(WideName.size());
        for (wchar_t Character : WideName)
        {
            const bool bInvalidCharacter =
                Character < 32 ||
                Character == L'<' ||
                Character == L'>' ||
                Character == L':' ||
                Character == L'"' ||
                Character == L'/' ||
                Character == L'\\' ||
                Character == L'|' ||
                Character == L'?' ||
                Character == L'*';

            Sanitized.push_back((bInvalidCharacter || Character == L' ') ? L'_' : Character);
        }

        while (!Sanitized.empty() && (Sanitized.back() == L'.' || Sanitized.back() == L' '))
        {
            Sanitized.pop_back();
        }

        return Sanitized.empty() ? FString("Actor") : FPaths::ToUtf8(Sanitized);
    }

    FString GetActorScriptTypeName(const AActor* Actor)
    {
        if (Actor == nullptr)
        {
            return "Actor";
        }

        const FTypeInfo* TypeInfo = Actor->GetTypeInfo();
        const FString NativeClassName = TypeInfo ? TypeInfo->name : FString();
        if (!NativeClassName.empty() && !IsGenericActorClassName(NativeClassName))
        {
            return SanitizeFileName(StripNativeActorPrefix(NativeClassName));
        }

        const FString ActorTag = Actor->GetTag();
        if (!ActorTag.empty() && ActorTag != ActorTags::Untagged)
        {
            return SanitizeFileName(AppendActorSuffixIfNeeded(ActorTag));
        }

        const FString MeshDerivedTypeName = GetStaticMeshDerivedTypeName(Actor);
        if (!MeshDerivedTypeName.empty())
        {
            return SanitizeFileName(MeshDerivedTypeName);
        }

        const FString ActorName = StripGeneratedSuffix(Actor->GetFName().ToString());
        if (!ActorName.empty())
        {
            return SanitizeFileName(ActorName);
        }

        return "Actor";
    }

    FString GetActorTypeScriptPath(const AActor* Actor)
    {
        return FPaths::Normalize(FString(ActorTypeScriptRoot) + GetActorScriptTypeName(Actor) + ".lua");
    }
}
