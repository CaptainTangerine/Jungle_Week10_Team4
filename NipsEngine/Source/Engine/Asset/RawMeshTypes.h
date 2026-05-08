#pragma once

#include "Core/CoreTypes.h"
#include "Core/Containers/Array.h"
#include "Core/Containers/String.h"
#include "Math/Vector.h"
#include "Math/Vector2.h"

#include <functional>

/*
 * Surface geometry raw data shared by static and skeletal mesh importers.
 * Skinned mesh data such as bones, weights, and animations should live in a separate raw type.
 */

struct FRawMeshIndex
{
    int32 PositionIndex = -1;
    int32 UVIndex = -1;
    int32 NormalIndex = -1;

    bool operator==(const FRawMeshIndex& Other) const
    {
        return PositionIndex == Other.PositionIndex && UVIndex == Other.UVIndex && NormalIndex == Other.NormalIndex;
    }
};

struct FRawMeshFace
{
    TArray<FRawMeshIndex> Vertices;
    FString MaterialName;
};

struct FRawMeshGeometry
{
    TArray<FVector> Positions;
    TArray<FRawMeshFace> Faces;
    TArray<FVector> Normals;
    TArray<FVector2> UVs;
};

namespace std
{
    template<>
    struct hash<FRawMeshIndex>
    {
        size_t operator()(const FRawMeshIndex& Key) const noexcept
        {
            size_t h1 = std::hash<int32>()(Key.PositionIndex);
            size_t h2 = std::hash<int32>()(Key.UVIndex);
            size_t h3 = std::hash<int32>()(Key.NormalIndex);

            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
}
