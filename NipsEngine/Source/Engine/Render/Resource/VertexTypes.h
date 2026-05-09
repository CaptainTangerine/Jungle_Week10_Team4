#pragma once

#include "Core/CoreTypes.h"
#include "Core/Containers/Array.h"
#include "Math/Color.h"
#include "Math/Vector.h"
#include "Math/Vector2.h"

/*
    Vertex 구조체들을 정의하는 Header입니다.
    추후에 다양한 Vertex 구조체들을 추가할 수 있습니다.
*/

struct FVertex
{
    FVector Position;
    FColor Color;
    int SubID;
};

struct FNormalVertex
{
    FVector Position;
    FColor Color;
    FVector Normal;
    FVector2 UVs;	//	TexCoord
    FVector Tangent;
    FVector Bitangent;
};

struct FOverlayVertex
{
    float X, Y;
};

// Position + TexCoord 범용 버텍스 (FontBatcher, SubUVBatcher 등 텍스처 기반 배처 공용)
struct FTextureVertex
{
    FVector  Position;
    FVector2 TexCoord;
};

struct FMeshData
{
    TArray<FVertex> Vertices;
    TArray<uint32> Indices;
};

struct FUIVertex
{
    FVector2 XY; // 스크린 좌표 
    FVector2 UV;
    FVector4 Color;
};

struct FbxVertex
{
    FVector Position;
    FVector Normal;
    FVector2 UV;
    
    //only top 4 Bone lefts
    int BoneIndices[4];
    float BoneWeights[4];
};
