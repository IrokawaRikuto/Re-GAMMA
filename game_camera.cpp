// game_camera.cpp
// Per-stage / per-mode camera dispatch.
// Extracted from game.cpp Game_Initialize / Game_Update.
#include "game_camera.h"

#include "camera.h"
#include "field.h"                // GetCurrentStage, GAME_STAGE
#include "PlayerModeSwitchManager.h"
#include "keyboard.h"

#ifdef _DEBUG
static bool s_2DPlayerDebugMode = false;
#endif

void GameCamera_InitForStage()
{
    const GAME_STAGE st = GetCurrentStage();

    // Reset the Player3D TPS camera's internal lerp state so the first frame
    // of a real stage snaps to the correct pose instead of lerping out of the
    // previous STAGE_SELECT / cinematic pose. STAGE_SELECT itself uses the
    // fixed StageSelect_Camera_Update which writes the pose directly each
    // frame, so the reset is only required for the live stages.
    if (st == STAGE_1 || st == STAGE_2 || st == STAGE_3)
    {
        Player3DCamera_Reset();
    }

    // STAGE_1/2 (and STAGE_SELECT itself) use stage_select-style follow
    // cameras (X-follow for stage_select/STAGE_2, Z-follow for STAGE_1 which
    // is rotated 90 left). Reset both so whichever fires snaps to the
    // player's spawn position instead of lerping from the previous map.
    StageSelect_Camera_Reset();
    StageOne_Camera_Reset();
}

void GameCamera_Update()
{
#ifdef _DEBUG
    if (Keyboard_IsKeyDownTrigger(KK_LEFTALT))
    {
        s_2DPlayerDebugMode = !s_2DPlayerDebugMode;
    }
#endif

    if (PlayerModeSwitchManager_GetMode() == MODE_3D)
    {
        Camera_SetLightMode(false);
        const GAME_STAGE st = GetCurrentStage();
        if (st == STAGE_1)
        {
            StageOne_Camera_Update();
        }
        else if (st == STAGE_SELECT || st == STAGE_2)
        {
            StageSelect_Camera_Update();
        }
        else
        {
            Player3DCamera_Update();
        }
    }
    else
    {
        Player2DCamera_Update();
#ifdef _DEBUG
        if (s_2DPlayerDebugMode)
        {
            Player2DCamera_DebugUpdate();
        }
#endif
    }
}
