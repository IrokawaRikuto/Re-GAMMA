// game_bgm.cpp
// 3D/2D BGM lifecycle + mode-switch crossfade.
// Extracted from game.cpp Game_Initialize / Game_Finalize / Game_Update.
#include "game_bgm.h"
#include "Audio.h"
#include "PlayerModeSwitchManager.h"

static int          g_Bgm3D = -1;
static int          g_Bgm2D = -1;
static PLAYER_MODE  g_PrevMode = MODE_3D;
static const float  CROSSFADE_DURATION = 2.0f;

void GameBgm_Initialize()
{
    g_Bgm3D = LoadAudio("asset/Audio/stage.wav");
    g_Bgm2D = LoadAudio("asset/Audio/stageR.wav");

    // Set volume BEFORE PlayAudio: a freshly created SourceVoice has default
    // volume 1.0, so Start() first would play one audio quantum at full volume
    // before SetAudioVolume(0) silences it (audible blip at stage start).
    if (g_Bgm3D >= 0)
    {
        // Start at silence and fade in over 1.5 s so the iris-out swap
        // cross-fades smoothly with the previous scene's BGM fade-out.
        SetAudioVolume(g_Bgm3D, 0.0f);
        PlayAudio(g_Bgm3D, true);
        FadeInAudio(g_Bgm3D, 1.5f, 1.0f);
    }
    if (g_Bgm2D >= 0)
    {
        SetAudioVolume(g_Bgm2D, 0.0f);
        PlayAudio(g_Bgm2D, true);
    }

    g_PrevMode = MODE_3D;
}

void GameBgm_Finalize()
{
    if (g_Bgm3D >= 0) { UnloadAudio(g_Bgm3D); g_Bgm3D = -1; }
    if (g_Bgm2D >= 0) { UnloadAudio(g_Bgm2D); g_Bgm2D = -1; }
}

void GameBgm_Update()
{
    UpdateAudio();

    PLAYER_MODE currentMode = PlayerModeSwitchManager_GetMode();
    if (currentMode == g_PrevMode) return;

    if (currentMode == MODE_3D)
    {
        if (g_Bgm3D >= 0) FadeInAudio (g_Bgm3D, CROSSFADE_DURATION, 1.0f);
        if (g_Bgm2D >= 0) FadeOutAudio(g_Bgm2D, CROSSFADE_DURATION);
    }
    else if (currentMode == MODE_2D)
    {
        if (g_Bgm2D >= 0) FadeInAudio (g_Bgm2D, CROSSFADE_DURATION, 1.0f);
        if (g_Bgm3D >= 0) FadeOutAudio(g_Bgm3D, CROSSFADE_DURATION);
    }
    g_PrevMode = currentMode;
}

int GameBgm_GetBgm3DId() { return g_Bgm3D; }
int GameBgm_GetBgm2DId() { return g_Bgm2D; }
