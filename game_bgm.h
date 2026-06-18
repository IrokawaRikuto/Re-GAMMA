// game_bgm.h
// Owns 3D/2D BGM lifecycle and the mode-switch crossfade.
// Extracted from game.cpp to slim it down.
#pragma once

void GameBgm_Initialize();
void GameBgm_Finalize();
void GameBgm_Update();

int  GameBgm_GetBgm3DId();
int  GameBgm_GetBgm2DId();
