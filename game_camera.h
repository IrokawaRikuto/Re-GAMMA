// game_camera.h
// Per-stage / per-mode camera dispatch for the in-game scene.
// Extracted from game.cpp.
#pragma once

// Reset internal camera state at the start of a stage. Called from
// Game_Initialize after the stage transition is settled. Decides per-stage
// which rig to prime (TPS reset for STAGE_3, follow-cam reset always).
void GameCamera_InitForStage();

// Per-frame: route to the correct camera updater based on the current stage
// and player mode. Caller is responsible for gating this on cinematic state
// (only call when no cinematic / iris is owning the camera).
//
// Also consumes the debug-toggle keyboard input (KK_LEFTALT) and dispatches
// Player2DCamera_DebugUpdate while the debug mode is on.
void GameCamera_Update();
