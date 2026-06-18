// iris.h
// Iris-out transition for stage entry. Pixel shader paints solid black
// outside a circle centered on the door's screen position; the circle
// shrinks to zero, then the actual scene swap fires.
#pragma once

#include <DirectXMath.h>
#include "Manager.h"
#include "field.h"

void Iris_Initialize();
void Iris_Finalize();

// Start the iris animation. centerScreenPx is the door's projected screen
// position in back-buffer pixels. durationFrames controls the shrink
// length. On completion the iris module performs the SetCurrentStage +
// SetScene swap. While the iris is active all gameplay input must be
// gated (use Iris_IsActive).
void Iris_Start(const DirectX::XMFLOAT2& centerScreenPx,
                int durationFrames,
                SCENE targetScene,
                GAME_STAGE targetStage);

void Iris_Update();

// Push the current iris parameters into the pixel-shader cbuffer. Call
// once per frame BEFORE any draws (so all subsequent draws see the right
// state). Safe to call always -- when iris is inactive it writes
// active=0.0 and the shader short-circuits.
void Iris_ApplyToShader();

bool Iris_IsActive();

// Player visibility alpha tracked by the iris module. Returns 1.0 when
// idle, ramps 1 -> 0 as the iris closes (player dissolves into mist),
// stays 0 during the black hold + initial FADE_IN, then 0 -> 1 as the
// new scene's fade-in completes. Player3D_Draw multiplies its model
// color by this so the player appears to fade behind the mist on stage
// entry and fade back in on stage start.
float Iris_GetPlayerAlpha();

// Optionally tell the iris that the given audio voice should fade out
// during the close. Use this from the goal handler / stage-entry trigger
// before Iris_Start so the BGM doesn't snap to silence on the scene swap.
void Iris_StartBgmFadeOut(int bgmId);
