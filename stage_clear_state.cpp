// stage_clear_state.cpp
#include "stage_clear_state.h"

#include <cmath>
#include <cstdio>
#include "direct3d.h"
#include "Bill_Board.h"
#include "sprite.h"
#include "shader.h"
#include "fade.h"
#include "manager.h"
#include "camera.h"
#include "player3D.h"
#include "imgui-docking/imgui.h"

using namespace DirectX;

// ---- Tunables ------------------------------------------------------------
static const float kDissolvePhaseAEndSec   = 0.7f;  // original billboard fade-out completes
static const float kDissolvePhaseBStartSec = 0.6f;  // CLEAR billboard fade-in starts (overlap)
static const float kDissolveTotalSec       = 1.5f;
// [FIX] Black mist tuning. Two-layer scheme so the cloud reads as volumetric
// rather than a ring of identical puffs:
//   - Core layer (indices 0..kSparkleCore-1): dense, dark, near the door
//   - Drift layer (indices kSparkleCore..kSparkleCount-1): larger, lighter,
//     slower-expanding puffs that also drift upward like rising smoke
// Tightened to hug the door instead of spreading out. Higher puff count with
// smaller individual sizes -> dense, fine, smoke-like cloud that conceals the
// door appearance/disappearance without obvious granularity.
static const int   kSparkleCount           = 280;  // denser cloud
static const int   kSparkleCore            = 180;
static const float kSparkleRadiusMax       = 2.6f;  // wider, covers middle area
static const float kSparkleSize            = 1.1f;
static const float kSparkleSizeOuter       = 1.6f;
static const float kSparkleDoorYOffset     = 0.3f;  // lower, hugs the door body
static const float kSparkleHeightSpread    = 2.2f;
static const float kSparkleUpDrift         = 0.7f;

static const float kAllClearTotalSec       = 3.0f;
static const float kAllClearFadeOutSec     = 0.5f;  // last 0.5s: kick off SetFade to RESULT

// Cinematic phase timing.
// [CHANGE] Phase 1 (zoom-in from initial stage_select pose -> cleared door)
// removed by setting kCineZoomInSec = 0. The cinematic now snaps to the
// cleared-door framing on its first frame (Start), so the user never sees
// the wide stage_select pose during fade-in. The remaining four phases:
//   Phase 2: hold cleared door + player emerge + CLEAR billboard
//   Phase 3: pan camera to next-stage door
//   Phase 4: hold + next-door reveal (mist + Y-scale ramp)
//   Phase 5: zoom out to stage_select default pose at emerged player X
static const float kCineZoomInSec     = 0.0f;
static const float kCineHoldClearSec  = 2.0f;  // walk + brief idle pause
static const float kCinePanSec        = 1.0f;
static const float kCineHoldRevealSec = 1.5f;
static const float kCineZoomOutSec    = 1.0f;
// Phase 2 emerge tuning. Walk duration < HoldClearSec so the player visibly
// stops at the emerge end before Phase 3 pans away. Shared by Start (to
// predict the final X for the Phase 5 zoom-out target) and the Phase 2
// walk loop so they stay in sync.
static const float kCineEmergeWalkSec = 1.2f;
static const float kCineEmergeSpeed   = 1.8f;
// Absolute phase boundaries against g_CineTimer for legibility.
static const float kPhase1End = kCineZoomInSec;                             // 1.0
static const float kPhase2End = kPhase1End + kCineHoldClearSec;             // 2.5
static const float kPhase3End = kPhase2End + kCinePanSec;                   // 3.5
static const float kPhase4End = kPhase3End + kCineHoldRevealSec;            // 5.0
static const float kCineTotalSec = kPhase4End + kCineZoomOutSec;            // 6.0
// Total when there is no next stage to reveal (STAGE_3 case, defensive).
static const float kCineTotalSec_NoNext = kPhase2End + kCineZoomOutSec;     // 3.5
// Next-door reveal window inside Phase 4 (offsets from Phase 4 start).
// Mist runs 0..kDissolvePhaseAEndSec (=0.7s); door reveal must start AFTER
// the mist completely finishes so the player sees mist disperse THEN the
// door materialize, not the door visible while mist is still active.
static const float kRevealStart = 0.75f;
static const float kRevealEnd   = 1.35f;
// Framing offsets when looking at a stage door (cleared or next).
// Slightly pulled back + raised vs. the old (4.0, 1.5, 1.0) so the door
// arch plus the mist envelope around it both fit in frame without the
// mist's outer ring clipping out of view. LookAtY raised to door body.
// [TUNE] Door framing constants. With the door now scaled 2x, the camera
// needs a bit more distance and a higher look-at point so the arch fits
// in frame and stays centered while the player walks out underneath.
static const float kCineDoorBackDist = 7.5f;
static const float kCineDoorHeight   = 2.4f;
static const float kCineLookAtYOfs   = 1.8f;

// ---- State ---------------------------------------------------------------
static bool g_Cleared[STAGE_MAX] = {};            // indexed by GAME_STAGE
static bool g_DissolveQueued[STAGE_MAX] = {};     // set on MarkCleared, drained by ConsumeDissolveQueue

struct EffectState
{
    bool      running;
    // [FIX] Tracks whether Effect_Start was ever called for this stage during
    // the session. GetClearAlpha needs to distinguish "effect already played,
    // CLEAR settled to 1.0" from "effect never started yet, do not show
    // CLEAR". Without this, deferring Effect_Start to Phase 2 of the
    // cinematic (so mist aligns with door collapse) would leave the CLEAR
    // billboard visible at full alpha during the Phase 1 zoom-in.
    bool      everStarted;
    float     timer;          // seconds since start
    XMFLOAT3  pos;            // billboard world pos (door + Y2)
    XMFLOAT3  doorPos;        // door floor world pos (for door-centered sparkles)
    float     sparkleAngle[kSparkleCount];  // base angles for sparkles
    float     sparkleHeight[kSparkleCount]; // vertical jitter per sparkle
    float     sparkleSpeed[kSparkleCount];  // radius growth multiplier per sparkle
};
static EffectState g_Effect[STAGE_MAX] = {};

static bool  g_AllClearActive   = false;
static float g_AllClearTimer    = 0.0f;
static bool  g_AllClearFadeFired = false;

// Cinematic state
static bool       g_CineActive          = false;
static float      g_CineTimer           = 0.0f;
static GAME_STAGE g_CineStage           = STAGE_NONE;
static XMFLOAT3   g_CineDoorPos         = { 0.0f, 0.0f, 0.0f };
static XMFLOAT3   g_CineStartPos        = { 0.0f, 0.0f, 0.0f };  // camera snapshot at start
static XMFLOAT3   g_CineStartAt         = { 0.0f, 0.0f, 0.0f };
static XMFLOAT3   g_CineDoorCamPos      = { 0.0f, 0.0f, 0.0f };  // framing pose, cleared door
static XMFLOAT3   g_CineDoorCamAt       = { 0.0f, 0.0f, 0.0f };
// Next stage that should be revealed in Phase 4 (STAGE_NONE if there is
// none, e.g. STAGE_3 clear -- though STAGE_3 routes to SCENE_RESULT and
// never actually plays the cinematic).
static GAME_STAGE g_CineNextStage        = STAGE_NONE;
static XMFLOAT3   g_CineNextDoorPos      = { 0.0f, 0.0f, 0.0f };
static XMFLOAT3   g_CineNextDoorCamPos   = { 0.0f, 0.0f, 0.0f };  // framing pose, next door
static XMFLOAT3   g_CineNextDoorCamAt    = { 0.0f, 0.0f, 0.0f };
// Mist start latches: fire StageClearEffect_Start exactly once per stage
// when the relevant cinematic phase entry happens. Deferred so the mist
// envelope (peaks at effect t=0.42s, ends at 0.7s) lines up with the door
// collapse / reveal window inside the phase, not the cinematic start.
static bool       g_CineClearMistStarted = false;   // trigger at Phase 2 entry
static bool       g_CineNextMistStarted  = false;   // trigger at Phase 4 entry

// Player emerges from the cleared door during Phase 2. We teleport the
// player to the door's front face, drive a forward walk for ~1.5 s, then
// settle to idle. Cinematic owns Position / Rotation / Anim until Phase 2
// ends (Player3D_Update is gated by cineLocked elsewhere).
static bool       g_CineEmergeStarted    = false;
static XMFLOAT3   g_CineEmergeFromPos    = { 0.0f, 0.0f, 0.0f };  // door front pos
static XMFLOAT3   g_CineEmergeDir        = { 0.0f, 0.0f, -1.0f }; // unit forward
static float      g_CineEmergeYawDeg     = 0.0f;

// Pending state: queued in Game_Initialize, fires once fade is FADE_NONE
static bool       g_CinePending      = false;
static GAME_STAGE g_CinePendingStage = STAGE_NONE;
static XMFLOAT3   g_CinePendingDoor  = { 0.0f, 0.0f, 0.0f };

static ID3D11Device*        g_pDevice   = nullptr;
static ID3D11DeviceContext* g_pContext  = nullptr;
static ID3D11ShaderResourceView* g_ClearTex   = nullptr;
static ID3D11ShaderResourceView* g_SparkleTex = nullptr;
static ID3D11ShaderResourceView* g_AllClearTex = nullptr;

// ---- helpers -------------------------------------------------------------
static bool TryLoadTexture(const wchar_t* path, ID3D11ShaderResourceView** outSrv)
{
    TexMetadata meta;
    ScratchImage img;
    HRESULT hr = LoadFromWICFile(path, WIC_FLAGS_NONE, &meta, img);
    if (FAILED(hr)) return false;
    hr = CreateShaderResourceView(g_pDevice, img.GetImages(), img.GetImageCount(), meta, outSrv);
    return SUCCEEDED(hr);
}

static bool IsStageIndexValid(GAME_STAGE s)
{
    return (s == STAGE_1 || s == STAGE_2 || s == STAGE_3);
}

// =========================================================================
// Lifecycle
// =========================================================================
void StageClearState_Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
    g_pDevice  = pDevice;
    g_pContext = pContext;

    // Try to load proper CLEAR / sparkle / ALL CLEAR textures.
    // If user has not provided them, fall back to existing UI textures so the
    // build still runs and visuals are recognizable as placeholders.
    if (!TryLoadTexture(L"asset\\texture\\UI\\clear.png", &g_ClearTex))
    {
        // Placeholder: tint easy.png gold via vertex color in the draw call.
        TryLoadTexture(L"asset\\texture\\UI\\easy.png", &g_ClearTex);
    }
    // [FIX] Use a plain soft-black puff (radial alpha falloff) for the
    // dispersal mist. The old fallback chain ended at "+.png", which is
    // actually the pause-menu legend bar (a black banner with the pause
    // glyph baked in) -- so when sparkle.png was missing the door cloud
    // rendered as a swarm of pause-button icons instead of smoke.
    if (!TryLoadTexture(L"asset\\texture\\UI\\smoke_puff.png", &g_SparkleTex))
    {
        // Last-resort: leave g_SparkleTex null so DrawSparkles becomes a
        // no-op. Better to render no mist than to render legend bars.
        g_SparkleTex = nullptr;
    }
    if (!TryLoadTexture(L"asset\\texture\\UI\\all_clear.png", &g_AllClearTex))
    {
        // Placeholder: reuse logo / easy if logo missing
        if (!TryLoadTexture(L"asset\\texture\\logo.png", &g_AllClearTex))
        {
            TryLoadTexture(L"asset\\texture\\UI\\easy.png", &g_AllClearTex);
        }
    }

    StageClearState_Reset();
}

void StageClearState_Finalize()
{
    if (g_ClearTex)    { g_ClearTex->Release();    g_ClearTex    = nullptr; }
    if (g_SparkleTex)  { g_SparkleTex->Release();  g_SparkleTex  = nullptr; }
    if (g_AllClearTex) { g_AllClearTex->Release(); g_AllClearTex = nullptr; }
}

// Save file lives next to the executable. Format: 1 byte representing the
// highest stage cleared (0 = none, 1 = STAGE_1, 2 = STAGE_2, 3 = STAGE_3).
static const char* kSavePath = "save.dat";

void StageClearState_SaveProgress()
{
    unsigned char highest = 0;
    if (g_Cleared[STAGE_1]) highest = 1;
    if (g_Cleared[STAGE_2]) highest = 2;
    if (g_Cleared[STAGE_3]) highest = 3;

    FILE* fp = nullptr;
    if (fopen_s(&fp, kSavePath, "wb") != 0 || !fp) return;
    fwrite(&highest, sizeof(highest), 1, fp);
    fclose(fp);
}

void StageClearState_LoadFromSave()
{
    FILE* fp = nullptr;
    if (fopen_s(&fp, kSavePath, "rb") != 0 || !fp) return;
    unsigned char highest = 0;
    fread(&highest, sizeof(highest), 1, fp);
    fclose(fp);
    if (highest > 3) highest = 3;

    for (int s = STAGE_1; s <= (int)highest && s <= STAGE_3; ++s)
    {
        g_Cleared[s] = true;
        g_Effect[s].everStarted = true;
        g_DissolveQueued[s] = false;
    }
}

void StageClearState_DeleteAllProgress()
{
    StageClearState_Reset();
    std::remove(kSavePath);
}

void StageClearState_Reset()
{
    for (int i = 0; i < STAGE_MAX; ++i)
    {
        g_Cleared[i] = false;
        g_DissolveQueued[i] = false;
        g_Effect[i].running     = false;
        g_Effect[i].everStarted = false;
        g_Effect[i].timer       = 0.0f;
    }
    g_AllClearActive    = false;
    g_AllClearTimer     = 0.0f;
    g_AllClearFadeFired = false;

    g_CineActive = false;
    g_CineTimer  = 0.0f;
    g_CineStage  = STAGE_NONE;
    g_CineNextStage        = STAGE_NONE;
    g_CineClearMistStarted = false;
    g_CineNextMistStarted  = false;
    g_CinePending      = false;
    g_CinePendingStage = STAGE_NONE;
}

// Helper: stage that should be revealed after `s` clears.
// STAGE_1 -> STAGE_2, STAGE_2 -> STAGE_3, otherwise none.
static GAME_STAGE NextStageOf(GAME_STAGE s)
{
    if (s == STAGE_1) return STAGE_2;
    if (s == STAGE_2) return STAGE_3;
    return STAGE_NONE;
}

// Helper: find a stage marker (FIELD_STAGE_1/2/3) in the current map and
// return its floor position + Y rotation. rotation matters because the
// camera framing places the camera in front of the door's facing direction,
// not on whatever side the previous camera happened to be on. Returns false
// if the marker isn't in the loaded map (e.g., not in stage_select).
static bool FindStageDoor(GAME_STAGE stage, XMFLOAT3* outPos, float* outYawDeg)
{
    FIELD want = FIELD_EMPTY_BOX;
    if      (stage == STAGE_1) want = FIELD_STAGE_1;
    else if (stage == STAGE_2) want = FIELD_STAGE_2;
    else if (stage == STAGE_3) want = FIELD_STAGE_3;
    else return false;

    for (const auto& md : GetFieldMap())
    {
        if (md.no == want)
        {
            if (outPos)    *outPos = md.pos;
            if (outYawDeg) *outYawDeg = md.rotate.y;
            return true;
        }
    }
    return false;
}

// Back-compat shim for the position-only callers.
static bool FindStageDoorPos(GAME_STAGE stage, XMFLOAT3* outPos)
{
    float yaw = 0.0f;
    return FindStageDoor(stage, outPos, &yaw);
}

// Helper: compute camera framing pose looking at `doorPos`, picking the
// "behind" direction from `fromAt` so the camera approaches from the side
// the player was on (or pre-pan camera position).
static void ComputeDoorFraming(const XMFLOAT3& doorPos,
                                const XMFLOAT3& fromAt,
                                XMFLOAT3* outCamPos,
                                XMFLOAT3* outCamAt)
{
    float dx = doorPos.x - fromAt.x;
    float dz = doorPos.z - fromAt.z;
    float len = sqrtf(dx * dx + dz * dz);
    float bx, bz;
    if (len > 1e-3f) { bx = dx / len; bz = dz / len; }
    else             { bx = 0.0f;     bz = -1.0f; }

    if (outCamPos)
    {
        outCamPos->x = doorPos.x - bx * kCineDoorBackDist;
        outCamPos->y = doorPos.y + kCineDoorHeight;
        outCamPos->z = doorPos.z - bz * kCineDoorBackDist;
    }
    if (outCamAt)
    {
        outCamAt->x = doorPos.x;
        outCamAt->y = doorPos.y + kCineLookAtYOfs;
        outCamAt->z = doorPos.z;
    }
}

// Helper: compute camera framing pose that places the camera directly in
// front of the door's facing direction. Used for the next-stage reveal so
// the player sees the door's front face, never the side. The "from-at"
// variant above produces a 90-deg side view when the previous camera was
// off to one side (e.g. STAGE_1 -> STAGE_2 pan, where both doors sit on
// the same Z line and the lateral direction is +X).
//
// Door front (rotate.y = 0) = -Z. Rotating around Y by yawDeg rotates the
// front vector accordingly. Camera is placed `kCineDoorBackDist` in front,
// raised by `kCineDoorHeight`, looking back at the door.
static void ComputeDoorFramingFront(const XMFLOAT3& doorPos,
                                     float yawDeg,
                                     XMFLOAT3* outCamPos,
                                     XMFLOAT3* outCamAt)
{
    const float yawRad = yawDeg * (3.14159265358979323846f / 180.0f);
    const float s = sinf(yawRad), c = cosf(yawRad);
    // front = (-sin(yaw), 0, -cos(yaw)) so yaw=0 gives -Z (player side).
    const float fx = -s, fz = -c;

    if (outCamPos)
    {
        outCamPos->x = doorPos.x + fx * kCineDoorBackDist;
        outCamPos->y = doorPos.y + kCineDoorHeight;
        outCamPos->z = doorPos.z + fz * kCineDoorBackDist;
    }
    if (outCamAt)
    {
        outCamAt->x = doorPos.x;
        outCamAt->y = doorPos.y + kCineLookAtYOfs;
        outCamAt->z = doorPos.z;
    }
}

// =========================================================================
// Clear flags
// =========================================================================
void StageClearState_MarkCleared(GAME_STAGE stage)
{
    if (!IsStageIndexValid(stage)) return;
    if (g_Cleared[stage]) return;
    g_Cleared[stage]        = true;
    g_DissolveQueued[stage] = true;
    StageClearState_SaveProgress();
}

bool StageClearState_IsCleared(GAME_STAGE stage)
{
    if (!IsStageIndexValid(stage)) return false;
    return g_Cleared[stage];
}

int StageClearState_GetClearCount()
{
    int n = 0;
    if (g_Cleared[STAGE_1]) ++n;
    if (g_Cleared[STAGE_2]) ++n;
    if (g_Cleared[STAGE_3]) ++n;
    return n;
}

bool StageClearState_IsAllCleared()
{
    return StageClearState_GetClearCount() >= 3;
}

bool StageClearState_IsUnlocked(GAME_STAGE stage)
{
    switch (stage)
    {
    case STAGE_1: return true;
    case STAGE_2: return g_Cleared[STAGE_1];
    case STAGE_3: return g_Cleared[STAGE_2];
    default:      return false;
    }
}

GAME_STAGE StageClearState_ConsumeDissolveQueue()
{
    // Drain the first queued stage; caller is expected to start the effect.
    for (int s = STAGE_1; s <= STAGE_3; ++s)
    {
        if (g_DissolveQueued[s])
        {
            g_DissolveQueued[s] = false;
            return (GAME_STAGE)s;
        }
    }
    return STAGE_NONE;
}

// =========================================================================
// Per-stage dissolve / CLEAR-appear effect
// =========================================================================
void StageClearEffect_Start(GAME_STAGE stage, XMFLOAT3 worldPos)
{
    if (!IsStageIndexValid(stage)) return;
    EffectState& e = g_Effect[stage];
    e.running     = true;
    e.everStarted = true;
    e.timer       = 0.0f;
    e.pos         = worldPos;
    // Door floor pos = billboard pos - (0, 2, 0) (matches the +2 offset used
    // by StageClearCinematic_Start when constructing billPos from doorPos).
    e.doorPos = { worldPos.x, worldPos.y - 2.0f, worldPos.z };

    // Seed sparkle pattern: uniform angles + deterministic per-index jitter
    // for height and speed so each shower frame stays consistent but varied.
    const float twoPi = 6.2831853f;
    for (int i = 0; i < kSparkleCount; ++i)
    {
        e.sparkleAngle[i] = (twoPi / kSparkleCount) * i;
        // Cheap deterministic "random" from index -- gives each sparkle a
        // unique height offset and growth speed without an RNG dependency.
        float h = sinf((float)i * 12.9898f) * 43758.5453f;
        h = h - floorf(h);  // fract -> [0,1)
        e.sparkleHeight[i] = (h - 0.5f) * kSparkleHeightSpread;

        float s = sinf((float)i * 78.233f) * 43758.5453f;
        s = s - floorf(s);
        e.sparkleSpeed[i] = 0.7f + s * 0.6f;  // [0.7, 1.3)
    }
}

void StageClearEffect_Update()
{
    const float dt = 1.0f / 60.0f;
    for (int s = STAGE_1; s <= STAGE_3; ++s)
    {
        EffectState& e = g_Effect[s];
        if (!e.running) continue;
        e.timer += dt;
        if (e.timer >= kDissolveTotalSec)
        {
            e.running = false;
            e.timer   = kDissolveTotalSec;
        }
    }
}

bool StageClearEffect_IsRunning(GAME_STAGE stage)
{
    if (!IsStageIndexValid(stage)) return false;
    return g_Effect[stage].running;
}

float StageClearEffect_GetOriginalAlpha(GAME_STAGE stage)
{
    if (!IsStageIndexValid(stage)) return 1.0f;
    if (!g_Cleared[stage]) return 1.0f;            // not cleared -> full
    const EffectState& e = g_Effect[stage];
    if (!e.running) return 0.0f;                   // cleared and effect done -> hidden
    // Phase A fade 1 -> 0 from t=0 to t=kDissolvePhaseAEndSec
    float t = e.timer / kDissolvePhaseAEndSec;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return 1.0f - t;
}

float StageClearEffect_GetClearAlpha(GAME_STAGE stage)
{
    if (!IsStageIndexValid(stage)) return 0.0f;
    if (!g_Cleared[stage]) return 0.0f;
    const EffectState& e = g_Effect[stage];
    if (!e.running)
    {
        // Settled state: 1.0 only if the effect has actually played once
        // already. If it has never started (e.g., Phase 1 zoom-in of the
        // cinematic before the Phase 2 mist trigger fires), keep CLEAR
        // hidden so it does not appear prematurely.
        return e.everStarted ? 1.0f : 0.0f;
    }
    // Phase B fade 0 -> 1 from t=kDissolvePhaseBStartSec to t=kDissolveTotalSec
    float t = (e.timer - kDissolvePhaseBStartSec) / (kDissolveTotalSec - kDissolvePhaseBStartSec);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t;
}

void StageClearEffect_DrawSparkles(GAME_STAGE stage)
{
    if (!IsStageIndexValid(stage)) return;
    const EffectState& e = g_Effect[stage];
    if (!e.running) return;
    if (!g_SparkleTex || !g_pContext) return;

    // Mist lifecycle: build up, hold near peak through the front half of
    // Phase A, then dissipate over the back half.
    float t = e.timer / kDissolvePhaseAEndSec;
    if (t >= 1.0f) return;

    // Alpha envelope: faster ramp-in (so the door is hidden in time for
    // the Y-collapse window 0.4..0.7s) and a softer, wider hold at peak.
    // Peak raised from 0.85 to 0.96 for actual occlusion of the arch.
    const float kPeak = 0.96f;
    float alpha;
    if (t < 0.35f)
    {
        // Smooth ease-in instead of linear so the cloud appears to bloom
        float u = t / 0.35f;
        alpha = u * u * (3.0f - 2.0f * u) * kPeak;
    }
    else if (t < 0.75f)
    {
        alpha = kPeak;  // hold at peak through collapse window
    }
    else
    {
        float u = (t - 0.75f) / 0.25f;
        // Ease-out so the dispersal feels like smoke thinning, not a cut.
        alpha = kPeak * (1.0f - u * u * (3.0f - 2.0f * u));
    }
    if (alpha <= 0.01f) return;

    g_pContext->PSSetShaderResources(0, 1, &g_SparkleTex);

    const XMFLOAT3 center = {
        e.doorPos.x,
        e.doorPos.y + kSparkleDoorYOffset,
        e.doorPos.z
    };

    // Ease-out radius growth: puffs accelerate then settle at the rim.
    const float radiusEase = 1.0f - (1.0f - t) * (1.0f - t);
    // Drift layer rises as smoke does: 0 at start, peak by t=1.
    const float upDrift = kSparkleUpDrift * radiusEase;

    for (int i = 0; i < kSparkleCount; ++i)
    {
        const bool isCore = (i < kSparkleCore);

        // Per-puff size pulses so neighbours don't look stamped.
        const float baseSize  = isCore ? kSparkleSize : kSparkleSizeOuter;
        const float sizePulse = 0.85f + 0.30f * e.sparkleSpeed[i];
        // Grow over lifetime: 1.0x -> ~1.5x core, ~1.7x outer.
        const float grow = 1.0f + t * (isCore ? 0.5f : 0.7f);
        const float s    = baseSize * sizePulse * grow;
        XMFLOAT2 size = { s, s };

        // Color: core is near-black, drift is slightly lighter / warmer
        // so the cloud has depth instead of being a flat silhouette.
        XMFLOAT4 color = isCore
            ? XMFLOAT4{ 0.04f, 0.04f, 0.06f, alpha }
            : XMFLOAT4{ 0.18f, 0.17f, 0.20f, alpha * 0.75f };

        const float a = e.sparkleAngle[i];
        // Core puffs stay closer; drift layer pushes out further and rises.
        const float radius =
            (isCore ? kSparkleRadiusMax * 0.65f : kSparkleRadiusMax)
            * radiusEase * e.sparkleSpeed[i];
        const float heightJitter = e.sparkleHeight[i];
        const float rise = isCore ? upDrift * 0.25f : upDrift;

        XMFLOAT3 p = {
            center.x + cosf(a) * radius,
            center.y + heightJitter + sinf(a) * radius * 0.4f + rise,
            center.z + sinf(a) * radius
        };
        DrawBillBoard(p, size, color, 0, 1, 1);
    }
}

ID3D11ShaderResourceView* StageClearState_GetClearTexture()
{
    return g_ClearTex;
}

void StageClearEffect_DrawClearTextOverlay(GAME_STAGE stage, XMFLOAT3 doorWorldPos)
{
    if (!IsStageIndexValid(stage)) return;

    const float alpha = StageClearEffect_GetClearAlpha(stage);
    if (alpha <= 0.0f) return;

    // Anchor the text above the door (matches old billboard position at y+2).
    XMFLOAT3 worldPos = { doorWorldPos.x, doorWorldPos.y + 2.0f, doorWorldPos.z };

    // World -> clip -> NDC -> screen.
    XMMATRIX vp = GetViewMatrix() * GetProjectionMatrix();
    XMVECTOR v = XMLoadFloat3(&worldPos);
    XMVECTOR clip = XMVector3TransformCoord(v, vp);
    XMFLOAT3 ndc{};
    XMStoreFloat3(&ndc, clip);

    // Behind camera: skip.
    if (ndc.z < 0.0f || ndc.z > 1.0f) return;

    const float SW = (float)Direct3D_GetBackBufferWidth();
    const float SH = (float)Direct3D_GetBackBufferHeight();
    const float screenX = (ndc.x * 0.5f + 0.5f) * SW;
    const float screenY = (1.0f - (ndc.y * 0.5f + 0.5f)) * SH;

    // Drop shadow + main text via ImGui overlay. Foreground draw list renders
    // on top of everything else after Manager_Draw.
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl) return;

    const char* text = "CLEAR";
    ImFont* font = ImGui::GetFont();
    const float fontSize = 64.0f;
    ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);

    ImVec2 pos = { screenX - textSize.x * 0.5f, screenY - textSize.y * 0.5f };

    const int ai = (int)(alpha * 255.0f);
    const ImU32 colShadow = IM_COL32(0, 0, 0, ai);
    const ImU32 colMain   = IM_COL32(255, 215, 0, ai);  // bright gold

    // Drop shadow (offset 3px) then main text.
    dl->AddText(font, fontSize, ImVec2(pos.x + 3.0f, pos.y + 3.0f), colShadow, text);
    dl->AddText(font, fontSize, pos, colMain, text);
}

ID3D11ShaderResourceView* StageClearState_GetSparkleTexture()
{
    return g_SparkleTex;
}

// =========================================================================
// All-clear sequence
// =========================================================================
void AllClearSequence_Start()
{
    g_AllClearActive    = true;
    g_AllClearTimer     = 0.0f;
    g_AllClearFadeFired = false;
}

void AllClearSequence_Update()
{
    if (!g_AllClearActive) return;
    g_AllClearTimer += 1.0f / 60.0f;

    // Kick off SetFade -> SCENE_RESULT during the last kAllClearFadeOutSec
    // so the screen fades smoothly into Result.
    const float fadeStart = kAllClearTotalSec - kAllClearFadeOutSec;
    if (!g_AllClearFadeFired && g_AllClearTimer >= fadeStart)
    {
        if (GetFadeState() == FADE_NONE)
        {
            XMFLOAT4 black(0.0f, 0.0f, 0.0f, 1.0f);
            int frames = (int)(kAllClearFadeOutSec * 60.0f);
            SetFade(frames, black, FADE_OUT, SCENE_RESULT);
            g_AllClearFadeFired = true;
        }
    }

    if (g_AllClearTimer >= kAllClearTotalSec)
    {
        g_AllClearActive = false;
    }
}

bool AllClearSequence_IsActive()
{
    return g_AllClearActive;
}

void AllClearSequence_Draw()
{
    if (!g_AllClearActive) return;
    if (!g_AllClearTex || !g_pContext) return;

    // Banner alpha: fade in over first 0.4s, hold, fade out over last fadeOut window
    float a = 1.0f;
    const float fadeInSec = 0.4f;
    if (g_AllClearTimer < fadeInSec)
    {
        a = g_AllClearTimer / fadeInSec;
    }
    else
    {
        const float fadeStart = kAllClearTotalSec - kAllClearFadeOutSec;
        if (g_AllClearTimer > fadeStart)
        {
            float u = (g_AllClearTimer - fadeStart) / kAllClearFadeOutSec;
            if (u > 1.0f) u = 1.0f;
            a = 1.0f - u;
        }
    }

    Shader_Begin();

    const float SW = (float)Direct3D_GetBackBufferWidth();
    const float SH = (float)Direct3D_GetBackBufferHeight();

    Shader_SetMatrix(XMMatrixOrthographicOffCenterLH(
        0.0f, SW, SH, 0.0f, 0.0f, 1.0f));

    g_pContext->PSSetShaderResources(0, 1, &g_AllClearTex);
    SetBlendState(BLENDSTATE_ALFA);

    // Subtle pulse on banner scale
    float pulse = 1.0f + 0.05f * sinf(g_AllClearTimer * 6.0f);

    XMFLOAT2 pos  = { SW * 0.5f, SH * 0.45f };
    XMFLOAT2 size = { SW * 0.6f * pulse, SH * 0.2f * pulse };
    XMFLOAT4 color = { 1.0f, 0.92f, 0.4f, a };  // gold tint for placeholder

    DrawSprite(pos, size, color);
}

// =========================================================================
// Per-stage clear cinematic
// =========================================================================
static float SmoothStep01(float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);  // ease in/out
}

void StageClearCinematic_Queue(GAME_STAGE stage, XMFLOAT3 doorWorldPos)
{
    if (!IsStageIndexValid(stage)) return;
    g_CinePending      = true;
    g_CinePendingStage = stage;
    g_CinePendingDoor  = doorWorldPos;
}

void StageClearCinematic_Start(GAME_STAGE stage, XMFLOAT3 doorWorldPos)
{
    if (!IsStageIndexValid(stage)) return;

    g_CineActive           = true;
    g_CineTimer            = 0.0f;
    g_CineStage            = stage;
    g_CineDoorPos          = doorWorldPos;
    g_CineClearMistStarted = false;
    g_CineNextMistStarted  = false;

    // [CHANGE] Frame the cleared door from in FRONT of its arch (using the
    // door's own yaw) so the player always sees the arched face during the
    // hold + emerge.
    float clearedYaw = 0.0f;
    {
        XMFLOAT3 dummy;
        FindStageDoor(stage, &dummy, &clearedYaw);
    }
    ComputeDoorFramingFront(doorWorldPos, clearedYaw,
                            &g_CineDoorCamPos, &g_CineDoorCamAt);

    // Stash the yaw so the Phase 2 emergence walks in the correct direction.
    g_CineEmergeYawDeg  = clearedYaw;
    g_CineEmergeStarted = false;

    // Look up the next stage's door so Phase 3 can pan to it and Phase 4
    // can mist + reveal. If no next stage (STAGE_3 case) or the marker is
    // missing from the map, mark as none and the cinematic skips Phases 3/4.
    g_CineNextStage = NextStageOf(stage);
    if (g_CineNextStage != STAGE_NONE)
    {
        XMFLOAT3 nextDoor;
        float    nextYaw = 0.0f;
        if (FindStageDoor(g_CineNextStage, &nextDoor, &nextYaw))
        {
            g_CineNextDoorPos = nextDoor;
            ComputeDoorFramingFront(nextDoor, nextYaw,
                                    &g_CineNextDoorCamPos,
                                    &g_CineNextDoorCamAt);
        }
        else
        {
            g_CineNextStage = STAGE_NONE;
        }
    }

    // [CHANGE] End pose for Phase 5 zoom-out = the stage_select default
    // pose at the player's predicted emerged X. After the 1.5 s emerge walk
    // at 1.8 u/s, the player ends up doorPos + emergeDir * 2.7. Stage_select
    // camera clamps to x in [4.5, 8.5]. Snapping to this exact pose at the
    // end of Phase 5 + calling StageSelect_Camera_Reset means the hand-off
    // to StageSelect_Camera_Update is seamless.
    {
        const float yawRad = clearedYaw * (3.14159265358979323846f / 180.0f);
        const float dirX = -sinf(yawRad);
        // dirZ not needed for the X-only camera. Kept for clarity.
        const float emergeWalkDist = kCineEmergeWalkSec * kCineEmergeSpeed;
        float endCamX = doorWorldPos.x + dirX * emergeWalkDist;
        if (endCamX < 4.5f) endCamX = 4.5f;
        if (endCamX > 8.5f) endCamX = 8.5f;
        g_CineStartPos = XMFLOAT3(endCamX, 3.5f, -7.5f);
        g_CineStartAt  = XMFLOAT3(endCamX, 3.0f, 6.5f);
    }

    // [NEW] Snap the camera to the cleared-door framing on the first frame
    // so the fade-in shows the door, not the wide stage_select pose. Also
    // teleport the player to the door so they don't briefly appear at the
    // stage_select default spawn position (visible in frame edges if the
    // door happens to be near the spawn).
    SetCameraPosition(g_CineDoorCamPos);
    SetCameraAtPosition(g_CineDoorCamAt);
    {
        PLAYER* p = GetPlayer3D();
        if (p)
        {
            p->Position = doorWorldPos;
            p->Position.y = doorWorldPos.y - 0.5f;
            p->Velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
            p->Rotation.y = -clearedYaw;
            p->CurrentAnimIndex = PLAYER_ANIM_IDLE;
            p->isAuto = true;
            p->blockMovement = true;
            p->isGround = true;
        }
    }
}

static XMFLOAT3 CineLerp(const XMFLOAT3& a, const XMFLOAT3& b, float t)
{
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

void StageClearCinematic_Update()
{
    // [CHANGE] Fire the cinematic IMMEDIATELY on the first Update after Queue
    // so the camera is locked to the cleared-door framing from frame 1 of
    // stage_select (during fade-in). Old behavior waited for FADE_NONE,
    // which let the user see the wide stage_select pose (with all doors
    // visible) for the full fade-in duration before zooming in.
    if (g_CinePending && !g_CineActive)
    {
        StageClearCinematic_Start(g_CinePendingStage, g_CinePendingDoor);
        g_CinePending      = false;
        g_CinePendingStage = STAGE_NONE;
    }

    if (!g_CineActive) return;

    // [NEW] Hold the cleared-door framing during fade-in without advancing
    // the timer. The emerge animation should only start once the player can
    // actually see it; otherwise the player would walk most of the way out
    // behind the black overlay and the reveal would feel anticlimactic.
    if (GetFadeState() != FADE_NONE)
    {
        SetCameraPosition(g_CineDoorCamPos);
        SetCameraAtPosition(g_CineDoorCamAt);
        return;
    }

    g_CineTimer += 1.0f / 60.0f;

    const bool hasNext = (g_CineNextStage != STAGE_NONE);
    // When there's no next stage to reveal, skip Phases 3/4 entirely: total
    // collapses to zoom-in + hold-cleared + zoom-out, just like before.
    const float zoomOutStart = hasNext ? kPhase4End : kPhase2End;
    const float totalSec     = hasNext ? kCineTotalSec : kCineTotalSec_NoNext;

    XMFLOAT3 camPos = g_CineStartPos;
    XMFLOAT3 camAt  = g_CineStartAt;

    // Phase 1 (zoom-in) removed: cinematic starts already framed on the
    // cleared door, so there is no need to lerp into it. kPhase1End = 0
    // would otherwise make the original "if (timer < kPhase1End)" branch a
    // divide-by-zero (C4723) -- handled by simply not having the branch.
    if (g_CineTimer < kPhase2End)
    {
        // Phase 2: hold on cleared door while the player emerges from it.
        if (!g_CineClearMistStarted)
        {
            // Latch CLEAR billboard reveal.
            g_Effect[g_CineStage].everStarted = true;
            g_CineClearMistStarted = true;

            // [NEW] Spawn mist at door foot so the emergence reads as the
            // player stepping out of a portal of black smoke.
            XMFLOAT3 mistPos = {
                g_CineDoorPos.x,
                g_CineDoorPos.y + 1.0f,
                g_CineDoorPos.z
            };
            StageClearEffect_Start(g_CineStage, mistPos);
        }

        if (!g_CineEmergeStarted)
        {
            // [NEW] Teleport player to door's front face and start a
            // forward walk toward camera. Front vector matches the one
            // used by ComputeDoorFramingFront so the player walks straight
            // at the camera.
            float yawDeg = g_CineEmergeYawDeg;
            const float yawRad = yawDeg * (3.14159265358979323846f / 180.0f);
            const float s = sinf(yawRad), c = cosf(yawRad);
            g_CineEmergeDir = XMFLOAT3(-s, 0.0f, -c);
            g_CineEmergeFromPos = g_CineDoorPos;

            PLAYER* p = GetPlayer3D();
            if (p)
            {
                p->Position = g_CineEmergeFromPos;
                // Door cell pos.y is the cell center; ground top is half a
                // cell below (matches the R-spawn offset in field.cpp). Without
                // this the player floats half a unit above the ground.
                p->Position.y = g_CineDoorPos.y - 0.5f;
                p->Velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
                // Face along emerge direction so the player walks forward
                // toward the camera, not moonwalks. GetForward returns
                // rotate(0,0,-1) by R_y, so forward = (sin Ry, 0, -cos Ry).
                // We need forward = (-sin yawDeg, 0, -cos yawDeg) (= door
                // front), so Ry = -yawDeg.
                p->Rotation.y = -yawDeg;
                p->CurrentAnimIndex = PLAYER_ANIM_WALK;
                p->isAuto = true;
                p->blockMovement = true;
                p->isGround = true;
            }
            g_CineEmergeStarted = true;
        }

        // Walk for kCineEmergeWalkSec, then settle to idle until Phase 3.
        float phase2Elapsed = g_CineTimer - kPhase1End;
        PLAYER* p = GetPlayer3D();
        if (p)
        {
            if (phase2Elapsed < kCineEmergeWalkSec)
            {
                float dt = 1.0f / 60.0f;
                p->Position.x += g_CineEmergeDir.x * kCineEmergeSpeed * dt;
                p->Position.z += g_CineEmergeDir.z * kCineEmergeSpeed * dt;
                p->CurrentAnimIndex = PLAYER_ANIM_WALK;
            }
            else
            {
                p->Velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
                p->CurrentAnimIndex = PLAYER_ANIM_IDLE;
            }
        }

        camPos = g_CineDoorCamPos;
        camAt  = g_CineDoorCamAt;
    }
    else if (hasNext && g_CineTimer < kPhase3End)
    {
        // Phase 3: pan camera from cleared door to next door.
        float t = SmoothStep01((g_CineTimer - kPhase2End) / kCinePanSec);
        camPos = CineLerp(g_CineDoorCamPos, g_CineNextDoorCamPos, t);
        camAt  = CineLerp(g_CineDoorCamAt,  g_CineNextDoorCamAt,  t);
    }
    else if (hasNext && g_CineTimer < kPhase4End)
    {
        // Phase 4: hold on next door. Kick off the mist effect for the next
        // stage exactly once at phase entry so it builds up just as the
        // door is about to emerge.
        if (!g_CineNextMistStarted)
        {
            XMFLOAT3 nextBillPos = {
                g_CineNextDoorPos.x,
                g_CineNextDoorPos.y + 2.0f,
                g_CineNextDoorPos.z
            };
            StageClearEffect_Start(g_CineNextStage, nextBillPos);
            g_CineNextMistStarted = true;
        }
        camPos = g_CineNextDoorCamPos;
        camAt  = g_CineNextDoorCamAt;
    }
    else if (g_CineTimer < totalSec)
    {
        // Phase 5: zoom out back to the original player-follow pose.
        // Start pose is whichever door we were holding on last.
        XMFLOAT3 fromPos = hasNext ? g_CineNextDoorCamPos : g_CineDoorCamPos;
        XMFLOAT3 fromAt  = hasNext ? g_CineNextDoorCamAt  : g_CineDoorCamAt;
        float t = SmoothStep01((g_CineTimer - zoomOutStart) / kCineZoomOutSec);
        camPos = CineLerp(fromPos, g_CineStartPos, t);
        camAt  = CineLerp(fromAt,  g_CineStartAt,  t);
    }
    else
    {
        // Finished: settle on the start pose, hand camera back to player.
        camPos = g_CineStartPos;
        camAt  = g_CineStartAt;
        g_CineActive = false;
        // Seed the player camera's internal lerp state so the handoff is
        // glitch-free (same trick used for TAB return from light mode).
        Camera_SeedPlayer3DFromCurrent();
        // [NEW] Phase 5 end pose matches the stage_select default at the
        // emerged player X. Resetting the stage_select camera makes its next
        // call snap to gStageSelectCamX = playerX (clamped), so the camera
        // does not lerp through a stale X value on the first post-cinematic
        // frame.
        StageSelect_Camera_Reset();

        // [NEW] Release the player from cinematic-driven emergence so
        // normal Player3D_Update controls them once the camera hands back.
        PLAYER* p = GetPlayer3D();
        if (p)
        {
            p->isAuto = false;
            p->blockMovement = false;
            p->Velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
            p->CurrentAnimIndex = PLAYER_ANIM_IDLE;
        }
    }

    SetCameraPosition(camPos);
    SetCameraAtPosition(camAt);
}

bool StageClearCinematic_IsActive()
{
    return g_CineActive;
}

float StageClearCinematic_GetPlayerAlpha()
{
    if (!g_CineActive) return 1.0f;
    // Only Phase 2 owns the player emerge; outside that the player is
    // either off-screen (camera elsewhere) or hands back to game control.
    if (g_CineTimer < kPhase1End || g_CineTimer >= kPhase2End) return 1.0f;
    float phase2Elapsed = g_CineTimer - kPhase1End;
    // Mist runs ~0.7s after Phase 2 entry (kDissolvePhaseAEndSec). Hide
    // the player while the mist is dense; fade them in over the next 0.4s
    // as the mist dissipates so they materialize out of the smoke.
    if (phase2Elapsed < 0.45f) return 0.0f;
    if (phase2Elapsed > 0.85f) return 1.0f;
    return (phase2Elapsed - 0.45f) / 0.40f;
}

bool StageClearCinematic_IsLocked()
{
    // Input lock spans both queued and active phases: from goal hit through
    // the end of the movie. Camera follow is allowed during the queued phase
    // so the fade-in shows a normal player-follow shot, then the cinematic
    // snapshot fires for a clean Phase 1 sweep.
    return g_CineActive || g_CinePending;
}

float StageClearCinematic_GetDoorScaleY(GAME_STAGE stage)
{
    if (!IsStageIndexValid(stage)) return 1.0f;

    // [CHANGE] Cleared doors stay at full size so the player can re-enter
    // the stage. The door disappearance / collapse animation has been
    // removed; only the reveal animation for newly-unlocked NEXT stages
    // still ramps Y-scale 0 -> 1.
    const bool cleared = g_Cleared[stage];
    (void)cleared;

    if (!g_CineActive) return 1.0f;

    // Next stage (not yet cleared): hidden through Phases 1-3 and the
    // mist-build-up portion of Phase 4, then ramps 0 -> 1 as the mist clears
    // and the door appears to materialize.
    if (stage == g_CineNextStage && !g_Cleared[stage])
    {
        const float revealStartAbs = kPhase3End + kRevealStart;
        const float revealEndAbs   = kPhase3End + kRevealEnd;
        if (g_CineTimer < revealStartAbs) return 0.0f;
        if (g_CineTimer >= revealEndAbs)  return 1.0f;
        float t = (g_CineTimer - revealStartAbs) / (revealEndAbs - revealStartAbs);
        return SmoothStep01(t);
    }

    // Any other stage (including the just-cleared one): full size.
    return 1.0f;
}
