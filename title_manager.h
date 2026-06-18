#pragma once

#include "direct3d.h"

void Title_Manager_Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext);
void Title_Manager_Finalize();
void Title_Manager_Update();
void Title_Manager_Draw();

// True once the player has clicked / pressed start and the title->stage_select
// cinematic (turn + walk + fade) is running. Title scene uses this to hide
// the click-to-start prompt and freeze idle decoration.
bool Title_IsActionStarted();

// Kick off the title -> stage_select cinematic (turn left + walk + fade).
// Called by the title menu's Start button. No-op if already started.
void Title_RequestStartAction();

