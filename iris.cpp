// iris.cpp
// Iris-out transition. See iris.h for the API contract.
//
// State machine:
//   Idle      : nothing rendered, shader iris disabled.
//   Shrinking : timer ticks; radius lerps from kStartRadius to 0.
//   FullClose : radius=0, iris ACTIVE, holds for kFullCloseFrames frames
//               so the user clearly sees a fully black screen BEFORE the
//               (potentially slow) SetScene runs and the buffer freezes
//               on the previous frame. Also where BGM cross-fades land.
//   BlackHold : SetScene fired, FADE_IN active. Iris still kept on for
//               one frame so the new scene's first draw cannot leak
//               through before the fade overlay takes the wheel.
//
// The shader applies the cutout to every draw. Initial radius is the
// back-buffer diagonal so the cutout covers the whole screen on frame 1.

#include "iris.h"
#include "shader.h"
#include "direct3d.h"
#include "fade.h"
#include "Audio.h"
#include <math.h>

using namespace DirectX;

namespace {

enum class IrisPhase { Idle, Shrinking, FullClose, BlackHold };

XMFLOAT2  g_Center        = { 0.0f, 0.0f };
float     g_RadiusCurrent = 0.0f;
float     g_RadiusStart   = 0.0f;
int       g_TimerFrames   = 0;
int       g_DurationFrames = 120;
IrisPhase g_Phase         = IrisPhase::Idle;
int       g_FullCloseCounter = 0;

SCENE      g_TargetScene  = SCENE_GAME;
GAME_STAGE g_TargetStage  = STAGE_NONE;

// FADE_IN duration for the new scene reveal.
const int kFadeFrames = 60;
// Frames to hold at radius=0 BEFORE firing SetScene. Guarantees the user
// sees fully black for ~0.25s instead of catching the small-radius frame
// while SetScene's Game_Initialize blocks (model/texture load) and the
// previous frame stays on the buffer.
const int kFullCloseFrames = 15;
// Audio fade duration for the source (stage_select) BGM. 90 frames = 1.5s
// at 60 fps as the user requested.
const float kBgmFadeOutSec = 1.5f;

// Track which BGM voice (if any) we kicked into a fade-out so we don't
// re-issue the fade every frame.
int  g_FadingBgmId = -1;

float ComputeStartRadius()
{
    float w = (float)Direct3D_GetBackBufferWidth();
    float h = (float)Direct3D_GetBackBufferHeight();
    return sqrtf(w * w + h * h);
}

} // namespace

void Iris_Initialize()
{
    g_Phase = IrisPhase::Idle;
    g_RadiusCurrent = 0.0f;
    g_TimerFrames = 0;
    g_FullCloseCounter = 0;
    g_FadingBgmId = -1;
}

void Iris_Finalize()
{
    g_Phase = IrisPhase::Idle;
}

void Iris_Start(const XMFLOAT2& centerScreenPx,
                int durationFrames,
                SCENE targetScene,
                GAME_STAGE targetStage)
{
    g_Center = centerScreenPx;
    g_DurationFrames = (durationFrames > 0) ? durationFrames : 120;
    g_TimerFrames = 0;
    g_FullCloseCounter = 0;
    g_RadiusStart = ComputeStartRadius();
    g_RadiusCurrent = g_RadiusStart;
    g_TargetScene = targetScene;
    g_TargetStage = targetStage;
    g_Phase = IrisPhase::Shrinking;
    g_FadingBgmId = -1;
}

void Iris_StartBgmFadeOut(int bgmId)
{
    if (bgmId < 0) return;
    if (g_FadingBgmId == bgmId) return; // already fading
    FadeOutAndStopAudio(bgmId, kBgmFadeOutSec);
    g_FadingBgmId = bgmId;
}

void Iris_Update()
{
    if (g_Phase == IrisPhase::Shrinking)
    {
        g_TimerFrames++;
        float t = (float)g_TimerFrames / (float)g_DurationFrames;
        if (t > 1.0f) t = 1.0f;

        // Linear shrink. Cubic ease-in was visually nice but the snap at
        // t=1 combined with SetScene's blocking work made the user see the
        // last small-radius frame frozen on screen for the duration of
        // Initialize. Linear pairs naturally with the new FullClose hold.
        g_RadiusCurrent = g_RadiusStart * (1.0f - t);

        if (g_TimerFrames >= g_DurationFrames)
        {
            g_RadiusCurrent = 0.0f;
            g_FullCloseCounter = 0;
            g_Phase = IrisPhase::FullClose;
        }
        return;
    }

    if (g_Phase == IrisPhase::FullClose)
    {
        g_FullCloseCounter++;
        if (g_FullCloseCounter >= kFullCloseFrames)
        {
            // Screen has been fully black long enough. Now swap and start
            // the fade-in reveal. Iris stays ACTIVE through one more frame
            // in BlackHold so the new scene's first draw can't peek.
            SetCurrentStage(g_TargetStage);
            SetScene(g_TargetScene);
            XMFLOAT4 black(0.0f, 0.0f, 0.0f, 1.0f);
            SetFade(kFadeFrames, black, FADE_IN, g_TargetScene);
            g_Phase = IrisPhase::BlackHold;
        }
        return;
    }

    if (g_Phase == IrisPhase::BlackHold)
    {
        // Iris keeps the screen solid black for exactly one frame after
        // the SetScene swap so the new scene's first Draw cannot show
        // through before the fade overlay takes over. After that the
        // fade module's FADE_IN alpha drives the reveal.
        FADE_STATE fs = GetFadeState();
        if (fs == FADE_IN || fs == FADE_NONE)
        {
            g_Phase = IrisPhase::Idle;
            g_RadiusCurrent = 0.0f;
            g_FadingBgmId = -1;
        }
        return;
    }
}

void Iris_ApplyToShader()
{
    bool active = (g_Phase == IrisPhase::Shrinking)
               || (g_Phase == IrisPhase::FullClose)
               || (g_Phase == IrisPhase::BlackHold);
    Shader_SetIris(g_Center, g_RadiusCurrent, active ? 1.0f : 0.0f);
}

bool Iris_IsActive()
{
    return g_Phase != IrisPhase::Idle;
}

float Iris_GetPlayerAlpha()
{
    // While the iris is closing, the player dissolves into the mist over
    // the first 70% of the shrink. When the iris is fully closed / holding
    // black, the player is invisible. On the new scene's FADE_IN, ramp 0
    // -> 1 in sync with the fade alpha so the player re-materializes from
    // the mist on stage entry.
    if (g_Phase == IrisPhase::Shrinking)
    {
        float t = (float)g_TimerFrames / (float)g_DurationFrames;
        if (t > 1.0f) t = 1.0f;
        float a = 1.0f - (t / 0.70f);
        if (a < 0.0f) a = 0.0f;
        return a;
    }
    if (g_Phase == IrisPhase::FullClose || g_Phase == IrisPhase::BlackHold)
    {
        return 0.0f;
    }
    if (GetFadeState() == FADE_IN)
    {
        // FADE_IN starts at alpha=1 (fully black) and decays to 0. Match the
        // player alpha curve so the player emerges as the screen brightens.
        return 1.0f - GetFadeAlpha();
    }
    return 1.0f;
}
