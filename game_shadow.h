// game_shadow.h
// Owns the in-game ball light, the shadow-pass light position tracking,
// the shadow build configuration, and the cubemap shadow render pass.
// Extracted from game.cpp to slim it down.
#pragma once

#include "LightSource.h"
#include "direct3d.h"

void GameShadow_Initialize();
void GameShadow_Finalize();

// Per-frame: scan field for the active light caster (FIELD_OBJ_3), update
// LightPos, rebuild shadow prisms, and publish them to the collision system.
void GameShadow_Update();

// Render the 6 cubemap faces (depth-only) using the current LightPos.
// Caller should already have set up the back-buffer state it wants restored
// afterwards (this function does not restore RT bindings beyond the existing
// Direct3D_EndShadowPass contract).
void GameShadow_RenderCubemap();

// Accessors for the in-game ball light. Returned by reference because the
// draw composition in game.cpp toggles Enble on/off around individual passes.
LIGHTOBJECT& GameShadow_BallLight();
const XMFLOAT3& GameShadow_GetLightPos();

// Force the next cubemap render to actually run instead of being cached.
// Call when external state changes the shadow scene in a way the snapshot
// check can't detect (e.g., scene reload, manual door open/close). Normal
// caster movement and light movement are detected automatically.
void GameShadow_MarkCubemapDirty();
