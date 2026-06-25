// FieldManhole.cpp
#include "FieldManhole.h"

#include "player3D.h"
#include "Collision.h"
#include "camera.h"
#include "direct3d.h"
#include "UtilDebug.h"

#include <cmath>

using namespace DirectX;

//=========================================================================================================
// 内部設定（モデル/当たりの定義）
//=========================================================================================================

struct ManholePartConfig
{
    FIELD   partType;
    XMFLOAT3 offsetPos; // 配置オフセット（マップ座標->実座標）
    XMFLOAT3 scale;     // 見た目/当たりのスケール（BOX_RADIUS基準）
    XMFLOAT3 rotate;    // 初期回転
};

static const ManholePartConfig MANHOLE_CONFIG =
{
    FIELD_MANHOLE,
    XMFLOAT3(0.0f, -0.5f, 0.0f),
    XMFLOAT3(1.0f, 0.3f, 1.0f),
    XMFLOAT3(0.0f, 0.0f, 0.0f),
};

//=========================================================================================================
// 内部状態
//=========================================================================================================

static std::vector<ManholeData> g_Manholes;
static float g_TotalTime = 0.0f; // デバッグ/将来拡張用（全体時間）
static bool  g_ManholeActivated = false; // switch starts the platform (one-way)

//=========================================================================================================
// 初期化 / 破棄
//=========================================================================================================

void Manhole_Initialize()
{
    g_ManholeActivated = false;
    g_Manholes.clear();
    g_TotalTime = 0.0f;
}

void Manhole_Finalize()
{
    g_Manholes.clear();
}

void Manhole_ClearAll()
{
    g_Manholes.clear();
    g_TotalTime = 0.0f;
}

//=========================================================================================================
// 生成（MapDataへ MAPDATA を追加して mapIndex を保持）
//=========================================================================================================

int Manhole_Create(float x, float y, float z, std::vector<MAPDATA>& mapData)
{
    ManholeData manhole;

    // 振動の基準位置（見た目の中心）
    manhole.basePos = XMFLOAT3(
        x + MANHOLE_CONFIG.offsetPos.x,
        y + MANHOLE_CONFIG.offsetPos.y,
        z + MANHOLE_CONFIG.offsetPos.z
    );
    manhole.currentPos = XMFLOAT3(x, y, z);

    MAPDATA data;
    data.pos = XMFLOAT3(x, y, z);
    data.no = MANHOLE_CONFIG.partType;
    data.scale = MANHOLE_CONFIG.scale;
    data.rotate = MANHOLE_CONFIG.rotate;

    manhole.mapIndex = (int)mapData.size();
    mapData.push_back(data);

    int index = (int)g_Manholes.size();
    g_Manholes.push_back(manhole);
    return index;
}

void Manhole_Activate() { g_ManholeActivated = true; }

// Rebind mapIndex after MergeContiguousField reorders g_MapData (same issue as
// the seesaw). Match each manhole to the nearest FIELD_MANHOLE entry.
void Manhole_RebindIndices()
{
    std::vector<MAPDATA>& mapData = GetFieldMap();
    for (auto& m : g_Manholes)
    {
        int best = -1; float bd = 1e30f;
        for (int i = 0; i < (int)mapData.size(); ++i)
        {
            if (mapData[i].no != FIELD_MANHOLE) continue;
            float dx = mapData[i].pos.x - m.basePos.x;
            float dz = mapData[i].pos.z - m.basePos.z;
            float d2 = dx*dx + dz*dz;
            if (d2 < bd) { bd = d2; best = i; }
        }
        if (best >= 0) m.mapIndex = best;
    }
}

//=========================================================================================================
// 参照
//=========================================================================================================

std::vector<ManholeData>& Manhole_GetAll() { return g_Manholes; }

ManholeData* Manhole_Get(int index)
{
    if (index < 0 || index >= (int)g_Manholes.size())
        return nullptr;
    return &g_Manholes[index];
}

int Manhole_GetCount()
{
    return (int)g_Manholes.size();
}

//=========================================================================================================
// 更新
//=========================================================================================================

static void UpdateSingleManhole(int index, float deltaTime)
{
    if (index < 0 || index >= (int)g_Manholes.size())
        return;

    ManholeData& manhole = g_Manholes[index];
    ManholeParams& params = manhole.params;

    if (!g_ManholeActivated)
    {
        // Not switched on yet: hold at the base (rest) position.
        std::vector<MAPDATA>& md = GetFieldMap();
        if (manhole.mapIndex >= 0 && manhole.mapIndex < (int)md.size())
            md[manhole.mapIndex].pos.y = manhole.basePos.y;
        return;
    }

    // 位相更新（cos波）
    params.phase += params.speed * deltaTime;

    const float PI2 = 6.28318530718f;
    while (params.phase > PI2)
        params.phase -= PI2;

    // 0..amplitude の範囲で上下
    float offset = params.amplitude * (1.0f - cosf(params.phase + params.phaseOffset)) * 0.5f;
    manhole.currentPos.y = manhole.basePos.y + offset;

    // MapDataに反映（field.cppは整理対象外なので、既存設計のまま直接更新）
    std::vector<MAPDATA>& mapData = GetFieldMap();
    if (manhole.mapIndex >= 0 && manhole.mapIndex < (int)mapData.size())
    {
        mapData[manhole.mapIndex].pos.y = manhole.currentPos.y;
    }
}

void Manhole_UpdateAll(float deltaTime)
{
    g_TotalTime += deltaTime;

    for (int i = 0; i < (int)g_Manholes.size(); ++i)
    {
        UpdateSingleManhole(i, deltaTime);
    }
}

//=========================================================================================================
// デバッグ描画
//=========================================================================================================

void Manhole_DebugDraw()
{
    if (g_Manholes.empty()) return;

    std::vector<MAPDATA>& mapData = GetFieldMap();

    for (int i = 0; i < (int)g_Manholes.size(); ++i)
    {
        const ManholeData& manhole = g_Manholes[i];

        if (manhole.mapIndex < 0 || manhole.mapIndex >= (int)mapData.size())
            continue;

        const MAPDATA& data = mapData[manhole.mapIndex];

        // NOTE: Field_GetCollisionHalfSize() は field.cpp 側だが整理対象外のため触らない
        XMFLOAT3 half = {
            MANHOLE_CONFIG.scale.x * BOX_RADIUS,
            MANHOLE_CONFIG.scale.y * BOX_RADIUS,
            MANHOLE_CONFIG.scale.z * BOX_RADIUS
        };

        DebugDrawAABB(data.pos, half, IM_COL32(255, 200, 50, 255));

        // 基準位置と現在位置
        DrawPoint3D(manhole.basePos, IM_COL32(0, 255, 255, 255), 5.0f);
        DrawPoint3D(data.pos, IM_COL32(255, 255, 0, 255), 4.0f);

        // 可動範囲の可視化（base->base+amplitude）
        XMFLOAT3 lowPos = manhole.basePos;
        XMFLOAT3 highPos = manhole.basePos;
        highPos.y += manhole.params.amplitude;
        DrawLine3D(lowPos, highPos, IM_COL32(100, 100, 255, 180), 1.0f);
    }
}