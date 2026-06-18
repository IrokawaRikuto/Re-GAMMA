//game.h
#pragma once

#include "direct3d.h"

//=========================================================================================================
// �v���g�^�C�v�錾
//=========================================================================================================
void Game_Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext);
void Game_Finalize();
void Game_Update();
void Game_Draw();

// Return the currently-playing 3D-mode BGM voice index, or -1 if none.
// Used by stage entry to fade out the source BGM before the iris swap.
int Game_GetBgm3DId();
int Game_GetBgm2DId();



