#pragma once

#include "Asset/RawMeshTypes.h"

/*
 * Raw data produced by the OBJ parser.
 * Common surface geometry lives in FRawMeshGeometry; this type keeps OBJ-specific metadata.
 */

struct FObjRawData
{
    FRawMeshGeometry Geometry;
    FString ReferencedMtlPath;
};

/*
 * Key used to reuse vertices with the same OBJ position, uv, and normal indices.
 */

struct FObjVertexKey
{
    FRawMeshIndex RawMeshIndex;

    bool operator==(const FObjVertexKey& Other) const
    {
        return RawMeshIndex == Other.RawMeshIndex;
    }
};

namespace std
{
    template<>
    struct hash<FObjVertexKey>
    {
        size_t operator()(const FObjVertexKey& Key) const noexcept
        {
            return std::hash<FRawMeshIndex>()(Key.RawMeshIndex);
        }
    };
}
