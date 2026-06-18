// Option.h
// Option / Pause overlay. Two modes:
//   - OPTION_MODE_TITLE : opened from title scene. データ消去 enabled, Back
//     closes Option and returns to title scene.
//   - OPTION_MODE_PAUSE : opened from game scene via Esc. データ消去 greyed
//     out (cannot wipe progress mid-play). Back resumes the game.
//
// Pages:
//   PAGE_EXPLAIN : key-binding chart with key icons in the Key column
//   PAGE_VOLUME  : BGM + SE sliders, Reset button
//   DELETE       : action only -- opens a confirm modal (TITLE mode only)
//   BACK         : action only -- closes Option
#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include "direct3d.h"

enum OPTION_MODE
{
    OPTION_MODE_TITLE,
    OPTION_MODE_PAUSE,
};

void Option_Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext);
void Option_Finalize();
void Option_Update();
void Option_Draw();

// Open Option (resets to PAGE_EXPLAIN, closes any sub-overlays). mode picks
// the visual variant and whether データ消去 is enabled.
void Option_Open(OPTION_MODE mode);
void Option_Close();
bool Option_IsOpen();

// True while the data-delete confirmation modal is open. Esc should close
// the modal first; only if no modal is up does Esc close Option entirely.
bool Option_IsConfirmOpen();
void Option_CloseConfirm();
