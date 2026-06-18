// game_shadow.cpp
// Ball light + shadow cubemap pass.
// Extracted from game.cpp.
#include "game_shadow.h"

#include "ShadowColliderBox.h"
#include "Collision.h"
#include "field.h"
#include "shader.h"
#include "stage_clear_state.h"

#include <vector>
#include <float.h>
#include <cstring>

static LIGHTOBJECT             g_BallLight;
static XMFLOAT3                g_LightPos       = { 0.0f, 0.0f, 0.0f };
static ShadowBuildConfig       g_ShadowConfig;
static std::vector<const ShadowPrism*> g_ActiveShadowPrisms;

// [PERF] Cubemap caching: the 6-face shadow pass dominates the per-frame cost
// on STAGE_3 (~3.5k map cells, each face does an extra full-scene draw).
// 99% of frames the world geometry that casts shadows is identical to the
// previous frame -- light is static, no boxes moved, no doors animating --
// so we can reuse the previous cubemap and skip the render entirely.
//
// Invalidation triggers tracked here:
//   1. g_LightPos changed (player moved OBJ_3 via TAB)
//   2. Any shadow caster (Field_IsShadows tile: OBJ_BOX / OBJ_2 / SEESAW /
//      MANHOLE) changed its pos or rotate (box push, seesaw rotation)
//   3. StageClearCinematic active (door scale animating)
//   4. Explicit GameShadow_MarkCubemapDirty() (scene reload etc.)
static XMFLOAT3 g_LastShadowLightPos = { FLT_MAX, FLT_MAX, FLT_MAX };
static bool     g_CubemapDirty       = true;
static std::vector<XMFLOAT3> g_PrevCasterPos;
static std::vector<XMFLOAT3> g_PrevCasterRot;

// Field_IsShadows is a static helper in ShadowColliderBox.cpp; replicate the
// same caster classification here so we know which map tiles to snapshot.
static bool IsShadowCaster(FIELD t)
{
    switch (t)
    {
    case FIELD_OBJ_2:
    case FIELD_SEESAW_1:
    case FIELD_SEESAW_2:
    case FIELD_MANHOLE:
    case FIELD_OBJ_BOX:
        return true;
    default:
        return false;
    }
}

void GameShadow_MarkCubemapDirty()
{
    g_CubemapDirty = true;
}

void GameShadow_Initialize()
{
    // Dim ambient + diffuse so the stage reads as moonlit and the in-stage
    // light source itself has visible presence. The shader treats Dir.w >= 0.5
    // as "spotlight mode"; the per-stage code in game.cpp sets the cone axis.
    g_BallLight.SetEnable(false);
    g_BallLight.SetAmbient(XMFLOAT4(0.04f, 0.04f, 0.06f, 1.0f));
    g_BallLight.SetDiffuse(XMFLOAT4(0.85f, 0.80f, 0.70f, 1.0f));
    g_BallLight.SetDirection(XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f));

    g_ShadowConfig.edgeSamples = 4;
    g_ShadowConfig.thickness   = 1.0f;
    g_ShadowConfig.maxCastDist = 100.0f;

    ShadowDebugOptions debugOpts;
    debugOpts.drawPrism    = true;
    debugOpts.drawNormal   = true;
    debugOpts.drawVertices = true;
    debugOpts.drawAABB     = false;
    debugOpts.prismColor   = IM_COL32(255, 50, 50, 220);
    debugOpts.normalColor  = IM_COL32(255, 255, 0, 255);
    debugOpts.vertexColor  = IM_COL32(0, 255, 0, 255);
    Collision_SetShadowDebugOptions(debugOpts);

    GetShadowManager()->Initialize();
    Collision_ResetShadowContactState();

    g_LightPos = XMFLOAT3(0.0f, 0.0f, 0.0f);
    g_LastShadowLightPos = XMFLOAT3(FLT_MAX, FLT_MAX, FLT_MAX);
    g_CubemapDirty = true;
    g_PrevCasterPos.clear();
    g_PrevCasterRot.clear();
}

void GameShadow_Finalize()
{
    Collision_ResetShadowContactState();
    GetShadowManager()->Finalize();
    g_ActiveShadowPrisms.clear();
}

void GameShadow_Update()
{
    // Find the active light source in the field (first FIELD_OBJ_3).
    bool found = false;
    for (const auto& mapData : GetFieldMap())
    {
        if (mapData.no == FIELD_OBJ_3)
        {
            g_BallLight.SetEnable(true);
            g_LightPos = { mapData.pos.x, mapData.pos.y + 0.25f, mapData.pos.z };
            found = true;
            break;
        }
    }
    (void)found;  // If no FIELD_OBJ_3, we keep the previous LightPos.

    const float shadowIntensity = 1.0f;
    Shader_SetShadowLightData(g_LightPos, g_ShadowRadius, shadowIntensity);

    auto& map = GetFieldMap();
    GetShadowManager()->UpdateAllShadows(g_LightPos, map, g_ShadowConfig);

    // [PERF] Decide whether the cubemap needs a rebuild this frame.
    // Light moved enough to matter?
    {
        const float dx = g_LightPos.x - g_LastShadowLightPos.x;
        const float dy = g_LightPos.y - g_LastShadowLightPos.y;
        const float dz = g_LightPos.z - g_LastShadowLightPos.z;
        if (dx * dx + dy * dy + dz * dz > 0.00001f)  // ~3mm threshold
            g_CubemapDirty = true;
    }
    // Any shadow caster moved or rotated since last frame?
    {
        std::vector<XMFLOAT3> curPos, curRot;
        curPos.reserve(g_PrevCasterPos.size() + 4);
        curRot.reserve(g_PrevCasterRot.size() + 4);
        for (const auto& md : map)
        {
            if (IsShadowCaster(md.no))
            {
                curPos.push_back(md.pos);
                curRot.push_back(md.rotate);
            }
        }
        if (curPos.size() != g_PrevCasterPos.size() ||
            (curPos.size() > 0 &&
             std::memcmp(curPos.data(), g_PrevCasterPos.data(),
                         curPos.size() * sizeof(XMFLOAT3)) != 0) ||
            (curRot.size() > 0 &&
             std::memcmp(curRot.data(), g_PrevCasterRot.data(),
                         curRot.size() * sizeof(XMFLOAT3)) != 0))
        {
            g_CubemapDirty = true;
        }
        g_PrevCasterPos = std::move(curPos);
        g_PrevCasterRot = std::move(curRot);
    }
    // Door scales animate during the stage-clear cinematic. Force a rebuild
    // every frame for the duration of the movie so the next-stage door reveal
    // (Y-scale 0 -> 1) actually appears in the shadow map.
    if (StageClearCinematic_IsActive())
        g_CubemapDirty = true;

    // Per-stage walkable-wall filter. User: "影状態になれるのは画面奥側の壁
    // のみで、左右にある壁には基本入れないようにしてほしい". For STAGE_1
    // the back wall is the -X wall (its outward normal points +X). Only push
    // prisms whose receiver normal matches that orientation so the player
    // can't enter 2D mode on the side walls. Other stages keep all shadows.
    g_ActiveShadowPrisms.clear();
    const auto& shadows = GetShadowManager()->GetShadows();
    const GAME_STAGE st = GetCurrentStage();
    for (const auto& shadow : shadows)
    {
        if (!shadow.isValid) continue;
        if (st == STAGE_1 && shadow.n.x < 0.7f) continue;
        g_ActiveShadowPrisms.push_back(&shadow);
    }
    Collision_SetShadowPrisms(g_ActiveShadowPrisms);
}

void GameShadow_RenderCubemap()
{
    // [PERF] Skip the entire 6-face pass when nothing changed -- the cubemap
    // SRV from the previous frame is still bound and still correct. Single
    // biggest stage frame-time win.
    if (!g_CubemapDirty) return;

    const float lightRadius = g_ShadowRadius;

    for (int face = 0; face < 6; face++)
    {
        Direct3D_BeginShadowPass(face);
        Shader_Begin();
        Shader_SetShadowLightData(g_LightPos, lightRadius, 1.0f, 1.0f);

        XMMATRIX lightViewProj = Direct3D_GetCubemapFaceViewProj(face, g_LightPos, lightRadius);
        Shader_SetShadowMatrix(lightViewProj);

        std::vector<MAPDATA>& Map = GetFieldMap();
        const float maxShadowDistSq = g_ShadowRadius * g_ShadowRadius;
        const float lx = g_LightPos.x, ly = g_LightPos.y, lz = g_LightPos.z;

        for (size_t i = 0; i < Map.size(); ++i)
        {
            // Skip FIELD_GROUND in the cubemap pass: ground tiles tile the
            // floor flat and sit below everything else, so casting them costs
            // hundreds of draw calls per face on big stages for no visible
            // shadow benefit. FIELD_EMPTY_BOX is a skip tile that doesn't
            // render at all in the normal pass either.
            FIELD t = Map[i].no;
            if (t == FIELD_GROUND || t == FIELD_EMPTY_BOX) continue;

            const float dx = Map[i].pos.x - lx;
            const float dy = Map[i].pos.y - ly;
            const float dz = Map[i].pos.z - lz;
            if (dx * dx + dy * dy + dz * dz > maxShadowDistSq) continue;

            // Cleared stage doors must not cast shadows once their model is
            // hidden, otherwise the arch ghost-shadows into stage_select.
            if (t == FIELD_STAGE_1 && StageClearCinematic_GetDoorScaleY(STAGE_1) <= 1e-4f) continue;
            if (t == FIELD_STAGE_2 && StageClearCinematic_GetDoorScaleY(STAGE_2) <= 1e-4f) continue;
            if (t == FIELD_STAGE_3 && StageClearCinematic_GetDoorScaleY(STAGE_3) <= 1e-4f) continue;
            if (t == FIELD_STAGE_1 && !StageClearState_IsUnlocked(STAGE_1)) continue;
            if (t == FIELD_STAGE_2 && !StageClearState_IsUnlocked(STAGE_2)) continue;
            if (t == FIELD_STAGE_3 && !StageClearState_IsUnlocked(STAGE_3)) continue;

            XMMATRIX world = Field_GetWorldMatrix((int)i);
            Field_DrawShadowMap(world, world * lightViewProj, (int)i);
        }

        Direct3D_EndShadowPass();
    }
    g_LastShadowLightPos = g_LightPos;
    g_CubemapDirty = false;
}

LIGHTOBJECT&    GameShadow_BallLight()    { return g_BallLight; }
const XMFLOAT3& GameShadow_GetLightPos()  { return g_LightPos; }
