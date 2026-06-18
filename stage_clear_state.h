// stage_clear_state.h
// Session-only per-stage clear flags + one-shot dissolve / CLEAR appear effect
// + all-clear sequence. Reset on title screen entry (no save data).
#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include "field.h"

// ---- Lifecycle -----------------------------------------------------------
void StageClearState_Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext);
void StageClearState_Finalize();
void StageClearState_Reset();  // wipe clear flags + queues (call on title entry)

// Player-triggered "delete all progress" (Option screen, title mode).
// Currently aliases StageClearState_Reset since progress is session-only.
// When a save system is added, this should also clear the persisted data;
// StageClearState_Reset will keep its session-only semantics for use by
// the title scene's per-launch wipe.
void StageClearState_DeleteAllProgress();

// ---- Clear flags (session-only) ------------------------------------------
void  StageClearState_MarkCleared(GAME_STAGE stage);
bool  StageClearState_IsCleared(GAME_STAGE stage);
bool  StageClearState_IsAllCleared();
int   StageClearState_GetClearCount();

void  StageClearState_LoadFromSave();
void  StageClearState_SaveProgress();
// Sequential unlock: STAGE_1 is always playable, STAGE_2 requires STAGE_1
// cleared, STAGE_3 requires STAGE_2 cleared. Locked stages hide their door
// and billboard and refuse the F-enter trigger.
bool  StageClearState_IsUnlocked(GAME_STAGE stage);

// ---- Dissolve queue (handoff: goal handler -> stage_select Initialize) ---
// MarkCleared also queues a one-shot dissolve for that stage. The next
// stage_select Initialize consumes the queue and starts the effect.
GAME_STAGE StageClearState_ConsumeDissolveQueue();

// ---- Per-stage dissolve / CLEAR-appear effect (3D billboard space) ------
// Total duration is fixed at kDissolveTotalSec (defined in .cpp).
// Phase A (0 .. 0.7s): original easy/mid/hard billboard alpha 1 -> 0,
//                      8 sparkle billboards radiate outward.
// Phase B (0.6 .. 1.5s): CLEAR billboard alpha 0 -> 1 (slight overlap).
void StageClearEffect_Start(GAME_STAGE stage, DirectX::XMFLOAT3 worldPos);
void StageClearEffect_Update();
bool StageClearEffect_IsRunning(GAME_STAGE stage);
float StageClearEffect_GetOriginalAlpha(GAME_STAGE stage); // 1.0 if not running
float StageClearEffect_GetClearAlpha(GAME_STAGE stage);    // 1.0 if not running and cleared, else 0
void  StageClearEffect_DrawSparkles(GAME_STAGE stage);     // small white billboards
// Screen-space "CLEAR" text overlay rendered via ImGui foreground draw list
// at the world-projected position of the cleared stage door. Replaces the
// gold-tinted placeholder billboard so the text actually reads "CLEAR".
// Pass the door floor world position; the overlay positions itself above it.
void  StageClearEffect_DrawClearTextOverlay(GAME_STAGE stage, DirectX::XMFLOAT3 doorWorldPos);
ID3D11ShaderResourceView* StageClearState_GetClearTexture();
ID3D11ShaderResourceView* StageClearState_GetSparkleTexture();

// ---- All-clear sequence (post-3rd-stage) ---------------------------------
// Started when the 3rd stage's goal is hit; runs in stage_select for ~3s
// then triggers fade-out to SCENE_RESULT.
void AllClearSequence_Start();
void AllClearSequence_Update();   // ticks timer; on finish, calls SetFade -> SCENE_RESULT
bool AllClearSequence_IsActive();
void AllClearSequence_Draw();     // screen-space banner overlay

// ---- Per-stage clear cinematic (movie) -----------------------------------
// Played once on return to stage_select after a stage clear. Five phases:
//   Phase 1 (0.0 - 1.0s): camera lerps from snapshotted player camera pose
//                          to a framing pose looking at the cleared door
//   Phase 2 (1.0 - 2.5s): camera holds on the cleared door while
//                          StageClearEffect plays (mist + door Y-collapse +
//                          CLEAR billboard fade-in)
//   Phase 3 (2.5 - 3.5s): camera pans across to the next stage's door
//   Phase 4 (3.5 - 5.0s): camera holds on the next door, fresh mist puffs
//                          spawn, and the next door / stage marker emerge
//                          from inside the mist (Y-scale 0 -> 1)
//   Phase 5 (5.0 - 6.0s): camera lerps back to the snapshotted start pose,
//                          then control returns to Player3DCamera_Update
// While active: player movement, mode switch, and light TAB are all locked.
// Queue a cinematic to fire once fade has finished (FADE_NONE). This avoids
// snapshotting a stale camera pose during the cross-scene black frame.
void StageClearCinematic_Queue(GAME_STAGE stage, DirectX::XMFLOAT3 doorWorldPos);
void StageClearCinematic_Start(GAME_STAGE stage, DirectX::XMFLOAT3 doorWorldPos);
void StageClearCinematic_Update();
// True only while the active camera-control phases are running. Player
// camera updates skip when this is true so the cinematic owns the camera.
bool StageClearCinematic_IsActive();

// Player visibility (0..1) during the cinematic Phase 2 emerge. Returns
// 1.0 outside the cinematic. During the mist build-up the player is hidden
// (0.0) and gradually appears as the mist dissipates. Player3D_Draw skips
// when this is below 0.1.
float StageClearCinematic_GetPlayerAlpha();
// True from the moment the cinematic is queued (Game_Initialize after a
// clear) through the end of the active phases. Use this to lock player
// input/mode switch so the player can't move during fade-in or the movie.
bool StageClearCinematic_IsLocked();
// Y-scale multiplier for stage doors during the cinematic.
//  - Cleared (target) stage: 1.0 default, fades to 0 during Phase 2 collapse.
//    After the cinematic, stays at 0 because the cleared door is gone.
//  - Next stage (just unlocked): 0 during Phases 1-3, ramps 0 -> 1 during
//    Phase 4 reveal, then 1.0 forever after. Lets the next door appear to
//    materialize from the mist instead of popping in instantly.
//  - All other cases: 1.0 if not cleared, 0.0 if cleared.
float StageClearCinematic_GetDoorScaleY(GAME_STAGE stage);
