// Option.cpp
// Title-and-pause Option overlay. See Option.h for the high-level flow.
//
// Coordinate system: every layout constant below is expressed in back-buffer
// pixels (1920x1080), which now matches the source mockup resolution exactly,
// so values are used as-is without scaling.
#include "Option.h"
#include "sprite.h"
#include "shader.h"
#include "Input.h"
#include "fade.h"
#include "mouse.h"
#include "Audio.h"
#include "Collision.h"
#include "manager.h"
#include "stage_clear_state.h"
#include "newKeyBind.h"
#include "controller.h"
#include "field.h"
#include "player3D.h"

extern Controller gPad;

// =========================================================================
// Layout
// =========================================================================
struct HitRect { float cx, cy, w, h; };
static bool RectHit(const HitRect& r, float mx, float my)
{
    const float x0 = r.cx - r.w * 0.5f;
    const float y0 = r.cy - r.h * 0.5f;
    return mx >= x0 && mx <= x0 + r.w && my >= y0 && my <= y0 + r.h;
}

// [FIX] All layout values below are now in raw source-PNG coordinates
// (1920x1080) which matches the back-buffer after the resize to 1920. The
// older /1.03125 pre-scaling for the 1980 back buffer has been undone.

// Sidebar hit rects.
static const HitRect kRectExplain = { 316.0f, 414.0f, 407.0f, 110.0f };
static const HitRect kRectVolume  = { 306.0f, 539.0f, 233.0f, 110.0f };
static const HitRect kRectDelete  = { 298.0f, 666.0f, 475.0f, 110.0f };
static const HitRect kRectBack    = { 307.0f, 890.0f, 233.0f, 110.0f };

// Volume page slider rails. Hit area is tightened to match the visible
// horizontal line (rail X extent + a thin Y band that just covers the
// handle's vertical reach).
static const float kBgmRailY = 342.0f;
static const float kSeRailY  = 531.0f;
static const float kRailMinX = 1180.0f;
static const float kRailMaxX = 1700.0f;
static const HitRect kRectBgmRail = { (kRailMinX + kRailMaxX) * 0.5f, kBgmRailY, kRailMaxX - kRailMinX, 50.0f };
static const HitRect kRectSeRail  = { (kRailMinX + kRailMaxX) * 0.5f, kSeRailY,  kRailMaxX - kRailMinX, 50.0f };

// Volume page Reset.
static const HitRect kRectReset = { 1459.0f, 792.0f, 272.0f, 80.0f };

// Confirm dialog buttons.
static const HitRect kRectNo  = { 753.0f,  641.0f, 194.0f, 100.0f };
static const HitRect kRectYes = { 1158.0f, 639.0f, 213.0f, 100.0f };

// Pause overlay (PAUSE_STAGE_BG) menu buttons. Centers / widths come from
// the pixel bboxes of the labels in pause_bg.png. 5 buttons now (Stage
// Select added between Option and Stage Exit).
static const HitRect kRectPauseResume      = { 1610.0f, 541.0f, 600.0f, 95.0f };   // Back to Game
static const HitRect kRectPauseRestart     = { 1611.0f, 641.0f, 320.0f, 90.0f };   // ReStart
static const HitRect kRectPauseOption      = { 1610.0f, 749.0f, 300.0f, 110.0f };  // Option
static const HitRect kRectPauseStageSelect = { 1609.0f, 849.0f, 425.0f, 110.0f };  // Stage Select
static const HitRect kRectPauseExit        = { 1610.0f, 938.0f, 540.0f, 95.0f };   // Stage Exit

// Slider handle size on screen (cropped texture is 44x44).
static const float kHandleSize = 44.0f;

// Explain-page key icon column geometry (1920x1080 source space = back-buffer).
// [CHANGE] 7 rows now (light-source op row added between Change and Dash):
// 移動 / ジャンプ / アクション / 影変身 / 光源操作 / ダッシュ / 選択.
static const float kRowY[7] = { 225.0f, 338.0f, 465.0f, 589.0f, 715.0f, 838.0f, 965.0f };
static const float kKeyColCx = 1331.0f;  // Key column center
static const float kPadColCx = 1597.0f;  // Pad column center

// =========================================================================
// State
// =========================================================================
static ID3D11Device*        g_pDevice  = nullptr;
static ID3D11DeviceContext* g_pContext = nullptr;

static bool        s_Open        = false;
static OPTION_MODE s_Mode        = OPTION_MODE_TITLE;
enum OptionPage { PAGE_EXPLAIN, PAGE_VOLUME };
static OptionPage  s_Page        = PAGE_EXPLAIN;

// Pause flow: pressing Esc in game opens PAUSE_STAGE_BG (pause_bg only,
// no option UI). A click then transitions to PAUSE_STAGE_OPTION (the
// regular option pages, no pause_bg). Esc closes whichever is shown.
// TITLE mode never enters PAUSE_STAGE_BG; it skips straight to the option
// pages.
enum PauseStage { PAUSE_STAGE_BG, PAUSE_STAGE_OPTION };
static PauseStage  s_PauseStage  = PAUSE_STAGE_OPTION;

static bool        s_ConfirmOpen = false;
static int         s_ConfirmHover = -1;   // 0 = NO, 1 = YES

static bool        s_BgmDrag = false;
static bool        s_SeDrag  = false;
static bool        s_PrevLeft = false;

static float       s_mouseX = 0.0f, s_mouseY = 0.0f;
static float       s_PrevMouseX = -1.0f, s_PrevMouseY = -1.0f;

// Focused button (highlighted with yellow frame). Driven by:
//  - mouse hover (whenever the cursor moves over a hit rect)
//  - gamepad D-pad / LStick (last-input wins)
enum FocusBtn
{
    BTN_NONE = -1,
    BTN_EXPLAIN = 0, BTN_VOLUME, BTN_DELETE, BTN_BACK,
    BTN_BGM, BTN_SE, BTN_RESET,
    BTN_NO, BTN_YES,
    // Pause overlay (PAUSE_STAGE_BG) menu buttons. Live in the same focus
    // list so the existing hover / nav infrastructure can drive them.
    BTN_PAUSE_RESUME,
    BTN_PAUSE_RESTART,
    BTN_PAUSE_OPTION,
    BTN_PAUSE_STAGE_SELECT,
    BTN_PAUSE_EXIT,
    BTN_COUNT
};
static FocusBtn    s_Focused = BTN_NONE;

// Page backgrounds (4 variants).
static ID3D11ShaderResourceView* s_BgExplainTitle = nullptr;
static ID3D11ShaderResourceView* s_BgExplainPause = nullptr;
static ID3D11ShaderResourceView* s_BgVolumeTitle  = nullptr;
static ID3D11ShaderResourceView* s_BgVolumePause  = nullptr;
// Pause overlay (drawn before the page bg in PAUSE mode).
static ID3D11ShaderResourceView* s_BgPause        = nullptr;
// Confirm dialog (semi-transparent dialog body with text + buttons baked in).
static ID3D11ShaderResourceView* s_TexConfirm     = nullptr;
// Slider handle (yellow diamond).
static ID3D11ShaderResourceView* s_TexHandle      = nullptr;
// Key icons for the explain table.
static ID3D11ShaderResourceView* s_KeyF      = nullptr;
static ID3D11ShaderResourceView* s_KeyW      = nullptr;
static ID3D11ShaderResourceView* s_KeyA      = nullptr;
static ID3D11ShaderResourceView* s_KeyS      = nullptr;
static ID3D11ShaderResourceView* s_KeyD      = nullptr;
static ID3D11ShaderResourceView* s_KeySpace  = nullptr;
static ID3D11ShaderResourceView* s_KeyLShift = nullptr;
static ID3D11ShaderResourceView* s_KeyMouseL = nullptr;  // Left-click icon for "選択" row
static ID3D11ShaderResourceView* s_KeyTAB    = nullptr;  // TAB key icon for "光源操作" row
// Switch-style gamepad icons for the PAD column.
static ID3D11ShaderResourceView* s_PadStickL = nullptr;
static ID3D11ShaderResourceView* s_PadA      = nullptr;
static ID3D11ShaderResourceView* s_PadB      = nullptr;
static ID3D11ShaderResourceView* s_PadX      = nullptr;  // X button for "光源操作" row
static ID3D11ShaderResourceView* s_PadY      = nullptr;
static ID3D11ShaderResourceView* s_PadDpad   = nullptr;
// 1x1 white texture used to draw flat colored rectangles (yellow focus frame).
static ID3D11ShaderResourceView* s_WhiteTex  = nullptr;

// =========================================================================
// Helpers
// =========================================================================
static ID3D11ShaderResourceView* LoadTexture(const wchar_t* path)
{
    TexMetadata md;
    ScratchImage img;
    if (FAILED(LoadFromWICFile(path, WIC_FLAGS_NONE, &md, img))) return nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    CreateShaderResourceView(g_pDevice, img.GetImages(), img.GetImageCount(), md, &srv);
    return srv;
}

static void DrawFullscreen(ID3D11ShaderResourceView* tex)
{
    if (!tex) return;
    const float W = (float)Direct3D_GetBackBufferWidth();
    const float H = (float)Direct3D_GetBackBufferHeight();
    g_pContext->PSSetShaderResources(0, 1, &tex);
    DrawSprite(XMFLOAT2(W * 0.5f, H * 0.5f), XMFLOAT2(W, H), XMFLOAT4(1, 1, 1, 1));
}

static void DrawTextureAt(ID3D11ShaderResourceView* tex, float cx, float cy, float w, float h, float alpha = 1.0f)
{
    if (!tex) return;
    g_pContext->PSSetShaderResources(0, 1, &tex);
    DrawSprite(XMFLOAT2(cx, cy), XMFLOAT2(w, h), XMFLOAT4(1, 1, 1, alpha));
}

// Draw a flat colored rect using the 1x1 white texture as a fill source.
static void DrawColorRect(float cx, float cy, float w, float h, float r, float g, float b, float a)
{
    if (!s_WhiteTex) return;
    g_pContext->PSSetShaderResources(0, 1, &s_WhiteTex);
    DrawSprite(XMFLOAT2(cx, cy), XMFLOAT2(w, h), XMFLOAT4(r, g, b, a));
}

// Yellow 4px focus frame around a hit rect.
static void DrawFocusFrame(const HitRect& r)
{
    const float th = 4.0f;
    const float R = 1.0f, G = 0.85f, B = 0.1f, A = 1.0f;
    const float x0 = r.cx - r.w * 0.5f;
    const float y0 = r.cy - r.h * 0.5f;
    // Top
    DrawColorRect(r.cx,            y0 + th * 0.5f,        r.w, th, R, G, B, A);
    // Bottom
    DrawColorRect(r.cx,            y0 + r.h - th * 0.5f,  r.w, th, R, G, B, A);
    // Left
    DrawColorRect(x0 + th * 0.5f,  r.cy,                  th,  r.h, R, G, B, A);
    // Right
    DrawColorRect(x0 + r.w - th * 0.5f, r.cy,             th,  r.h, R, G, B, A);
}

// Diamond-shaped focus outline. Used for the slider handle so the frame
// matches the shape of the yellow diamond key instead of boxing the rail.
//   cx, cy   : diamond center (= handle position on the rail).
//   half     : distance from center to each vertex along the four diagonals.
//   thickness: edge width in pixels.
// Implementation: each diamond edge is a thin rectangle rotated 45 degrees.
static void DrawDiamondFrame(float cx, float cy, float half, float thickness)
{
    if (!s_WhiteTex) return;
    const float SQRT2     = 1.41421356f;
    const float lineLen   = half * SQRT2;        // length of one edge
    const float midOffset = half * 0.5f;         // edge midpoint offset from center
    const float pi4       = 0.7853981634f;       // pi / 4
    const XMFLOAT4 yellow = { 1.0f, 0.85f, 0.1f, 1.0f };

    g_pContext->PSSetShaderResources(0, 1, &s_WhiteTex);

    // Edge midpoints sit on the diagonals through cx,cy. Rotation alternates
    // between -pi/4 and +pi/4 so the four rectangles form a closed rhombus.
    DrawSpriteExRotation(XMFLOAT2(cx + midOffset, cy - midOffset),
                         XMFLOAT2(lineLen, thickness), yellow, 0, 1, 1, -pi4);
    DrawSpriteExRotation(XMFLOAT2(cx + midOffset, cy + midOffset),
                         XMFLOAT2(lineLen, thickness), yellow, 0, 1, 1,  pi4);
    DrawSpriteExRotation(XMFLOAT2(cx - midOffset, cy + midOffset),
                         XMFLOAT2(lineLen, thickness), yellow, 0, 1, 1, -pi4);
    DrawSpriteExRotation(XMFLOAT2(cx - midOffset, cy - midOffset),
                         XMFLOAT2(lineLen, thickness), yellow, 0, 1, 1,  pi4);
}

// Look up the hit rect for a given focused-button id.
static HitRect RectForBtn(FocusBtn b)
{
    switch (b)
    {
        case BTN_EXPLAIN: return kRectExplain;
        case BTN_VOLUME:  return kRectVolume;
        case BTN_DELETE:  return kRectDelete;
        case BTN_BACK:    return kRectBack;
        case BTN_BGM:     return kRectBgmRail;
        case BTN_SE:      return kRectSeRail;
        case BTN_RESET:   return kRectReset;
        case BTN_NO:      return kRectNo;
        case BTN_YES:     return kRectYes;
        case BTN_PAUSE_RESUME:       return kRectPauseResume;
        case BTN_PAUSE_RESTART:      return kRectPauseRestart;
        case BTN_PAUSE_OPTION:       return kRectPauseOption;
        case BTN_PAUSE_STAGE_SELECT: return kRectPauseStageSelect;
        case BTN_PAUSE_EXIT:         return kRectPauseExit;
        default:          return { 0.0f, 0.0f, 0.0f, 0.0f };
    }
}

// Build the focus navigation order for the current state. Returns the count.
// kMaxNav is enough for all combinations (modal=2; main page max=7; pause=4).
static const int kMaxNav = 8;
static int BuildNavList(FocusBtn out[kMaxNav])
{
    int n = 0;
    if (s_ConfirmOpen)
    {
        out[n++] = BTN_NO;
        out[n++] = BTN_YES;
        return n;
    }
    // Pause overlay: a separate menu replaces the sidebar entirely.
    if (s_Mode == OPTION_MODE_PAUSE && s_PauseStage == PAUSE_STAGE_BG)
    {
        out[n++] = BTN_PAUSE_RESUME;
        out[n++] = BTN_PAUSE_RESTART;
        out[n++] = BTN_PAUSE_OPTION;
        out[n++] = BTN_PAUSE_STAGE_SELECT;
        out[n++] = BTN_PAUSE_EXIT;
        return n;
    }
    out[n++] = BTN_EXPLAIN;
    out[n++] = BTN_VOLUME;
    if (s_Mode == OPTION_MODE_TITLE) out[n++] = BTN_DELETE;
    out[n++] = BTN_BACK;
    if (s_Page == PAGE_VOLUME)
    {
        out[n++] = BTN_BGM;
        out[n++] = BTN_SE;
        out[n++] = BTN_RESET;
    }
    return n;
}

// Hit-test the mouse against the active button list. Returns BTN_NONE on miss.
static FocusBtn HitTestMouse()
{
    FocusBtn list[kMaxNav];
    const int n = BuildNavList(list);
    for (int i = 0; i < n; ++i)
    {
        if (RectHit(RectForBtn(list[i]), s_mouseX, s_mouseY)) return list[i];
    }
    return BTN_NONE;
}

// Activate the focused button (used by mouse click + gamepad A).
static void ActivateButton(FocusBtn b)
{
    // Play the system click SE on every option / pause button activation.
    // Sliders (BTN_BGM / BTN_SE) reach ActivateButton only via the Enter /
    // Space / A confirm path, never via the slider drag, so this stays at
    // the menu-button level even though BTN_BGM / BTN_SE are no-ops below.
    SystemSE_PlayClick();
    switch (b)
    {
        case BTN_EXPLAIN: s_Page = PAGE_EXPLAIN; break;
        case BTN_VOLUME:  s_Page = PAGE_VOLUME;  break;
        case BTN_DELETE:
            if (s_Mode == OPTION_MODE_TITLE)
            {
                s_ConfirmOpen = true;
                s_Focused = BTN_NO;  // default focus to the cancel option
            }
            break;
        case BTN_BACK:    Option_Close(); break;
        case BTN_RESET:
            SetBgmVolume(0.5f);
            SetSeVolume(0.5f);
            break;
        case BTN_NO:      s_ConfirmOpen = false; break;
        case BTN_YES:
            StageClearState_DeleteAllProgress();
            s_ConfirmOpen = false;
            break;

        // --- Pause overlay (PAUSE_STAGE_BG) buttons ---
        case BTN_PAUSE_RESUME:
            // Back to Game: close the pause overlay and resume play.
            Option_Close();
            break;
        case BTN_PAUSE_RESTART:
            // ReStart: same path as the in-game R key -- respawn the player
            // which reloads the map + resets all dynamic objects. Then close
            // the overlay so play resumes immediately.
            Player3D_Respawn();
            Option_Close();
            break;
        case BTN_PAUSE_OPTION:
            s_PauseStage = PAUSE_STAGE_OPTION;
            s_Focused = BTN_EXPLAIN;
            break;
        case BTN_PAUSE_STAGE_SELECT:
            // Same path as Stage Exit (fade back to stage_select). Distinct
            // button so the menu reads clearly; both actions end at the
            // stage select screen for now.
            if (GetFadeState() == FADE_NONE)
            {
                XMFLOAT4 black(0.0f, 0.0f, 0.0f, 1.0f);
                SetCurrentStage(STAGE_SELECT);
                SetFade(60, black, FADE_OUT, SCENE_GAME);
                Option_Close();
            }
            break;
        case BTN_PAUSE_EXIT:
            // Stage Exit: bail all the way out to the title screen.
            if (GetFadeState() == FADE_NONE)
            {
                XMFLOAT4 black(0.0f, 0.0f, 0.0f, 1.0f);
                SetFade(60, black, FADE_OUT, SCENE_TITLE);
                Option_Close();
            }
            break;
        default: break;  // sliders are not "activated" on press
    }
}

// Inset the handle's travel by half its width so its outer edge aligns with
// the rail end instead of overhanging. User: handle at t=0 / t=1 was visibly
// outside the rail line, looked broken.
static const float kHandleTravelMinX = kRailMinX + kHandleSize * 0.5f;
static const float kHandleTravelMaxX = kRailMaxX - kHandleSize * 0.5f;

static float TFromBarX(float x)
{
    if (x < kHandleTravelMinX) x = kHandleTravelMinX;
    if (x > kHandleTravelMaxX) x = kHandleTravelMaxX;
    return (x - kHandleTravelMinX) / (kHandleTravelMaxX - kHandleTravelMinX);
}
static float BarXFromT(float t)
{
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    return kHandleTravelMinX + t * (kHandleTravelMaxX - kHandleTravelMinX);
}

// Update mouse position in back-buffer pixel space (the cursor delivers
// window-client pixels but every layout above is back-buffer space).
static void UpdateMousePos(const Mouse_State& ms)
{
    float cw = 0.0f, ch = 0.0f;
    Mouse_GetClientSize(&cw, &ch);
    const float bw = (float)Direct3D_GetBackBufferWidth();
    const float bh = (float)Direct3D_GetBackBufferHeight();
    if (cw > 0.0f && ch > 0.0f)
    {
        s_mouseX = ms.x * (bw / cw);
        s_mouseY = ms.y * (bh / ch);
    }
    else
    {
        s_mouseX = (float)ms.x;
        s_mouseY = (float)ms.y;
    }
}

// =========================================================================
// Initialize / Finalize
// =========================================================================
void Option_Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
    g_pDevice  = pDevice;
    g_pContext = pContext;

    s_BgExplainTitle = LoadTexture(L"asset\\texture\\UI\\Option\\explain_title.png");
    s_BgExplainPause = LoadTexture(L"asset\\texture\\UI\\Option\\explain_pause.png");
    s_BgVolumeTitle  = LoadTexture(L"asset\\texture\\UI\\Option\\volume_title.png");
    s_BgVolumePause  = LoadTexture(L"asset\\texture\\UI\\Option\\volume_pause.png");
    s_BgPause        = LoadTexture(L"asset\\texture\\UI\\Option\\pause_bg.png");
    s_TexConfirm     = LoadTexture(L"asset\\texture\\UI\\Option\\confirm_delete.png");
    s_TexHandle      = LoadTexture(L"asset\\texture\\UI\\Option\\slider_handle.png");

    // 1x1 white texture for solid-colored rectangles (focus frame fill).
    {
        UINT32 whitePix = 0xFFFFFFFFu;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = 1; td.Height = 1;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = &whitePix;
        sd.SysMemPitch = 4;
        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(g_pDevice->CreateTexture2D(&td, &sd, &tex)) && tex)
        {
            g_pDevice->CreateShaderResourceView(tex, nullptr, &s_WhiteTex);
            tex->Release();
        }
    }

    s_KeyF      = LoadTexture(L"asset\\texture\\UI\\Keyboard\\key_F.png");
    s_KeyW      = LoadTexture(L"asset\\texture\\UI\\Keyboard\\key_W.png");
    s_KeyA      = LoadTexture(L"asset\\texture\\UI\\Keyboard\\key_A.png");
    s_KeyS      = LoadTexture(L"asset\\texture\\UI\\Keyboard\\key_S.png");
    s_KeyD      = LoadTexture(L"asset\\texture\\UI\\Keyboard\\key_D.png");
    s_KeySpace  = LoadTexture(L"asset\\texture\\UI\\Keyboard\\key_Space.png");
    s_KeyLShift = LoadTexture(L"asset\\texture\\UI\\Keyboard\\key_LShift.png");
    s_KeyMouseL = LoadTexture(L"asset\\texture\\UI\\Keyboard\\mouse_left.png");
    s_KeyTAB    = LoadTexture(L"asset\\texture\\UI\\Keyboard\\key_TAB.png");
    s_PadStickL = LoadTexture(L"asset\\texture\\UI\\Gamepad\\switch_stick_l.png");
    s_PadA      = LoadTexture(L"asset\\texture\\UI\\Gamepad\\switch_button_a.png");
    s_PadB      = LoadTexture(L"asset\\texture\\UI\\Gamepad\\switch_button_b.png");
    s_PadX      = LoadTexture(L"asset\\texture\\UI\\Gamepad\\switch_button_x.png");
    s_PadY      = LoadTexture(L"asset\\texture\\UI\\Gamepad\\switch_button_y.png");
    s_PadDpad   = LoadTexture(L"asset\\texture\\UI\\Gamepad\\switch_dpad.png");

    s_Open         = false;
    s_Mode         = OPTION_MODE_TITLE;
    s_Page         = PAGE_EXPLAIN;
    s_ConfirmOpen  = false;
    s_ConfirmHover = -1;
    s_BgmDrag      = false;
    s_SeDrag       = false;
    s_PrevLeft     = false;
}

void Option_Finalize()
{
    SAFE_RELEASE(s_BgExplainTitle);
    SAFE_RELEASE(s_BgExplainPause);
    SAFE_RELEASE(s_BgVolumeTitle);
    SAFE_RELEASE(s_BgVolumePause);
    SAFE_RELEASE(s_BgPause);
    SAFE_RELEASE(s_TexConfirm);
    SAFE_RELEASE(s_TexHandle);
    SAFE_RELEASE(s_KeyF);
    SAFE_RELEASE(s_KeyW);
    SAFE_RELEASE(s_KeyA);
    SAFE_RELEASE(s_KeyS);
    SAFE_RELEASE(s_KeyD);
    SAFE_RELEASE(s_KeySpace);
    SAFE_RELEASE(s_KeyLShift);
    SAFE_RELEASE(s_KeyMouseL);
    SAFE_RELEASE(s_KeyTAB);
    SAFE_RELEASE(s_PadStickL);
    SAFE_RELEASE(s_PadA);
    SAFE_RELEASE(s_PadB);
    SAFE_RELEASE(s_PadX);
    SAFE_RELEASE(s_PadY);
    SAFE_RELEASE(s_PadDpad);
    SAFE_RELEASE(s_WhiteTex);
}

void Option_Open(OPTION_MODE mode)
{
    s_Open         = true;
    s_Mode         = mode;
    s_Page         = PAGE_EXPLAIN;
    s_ConfirmOpen  = false;
    s_ConfirmHover = -1;
    s_BgmDrag      = false;
    s_SeDrag       = false;
    // PAUSE mode starts at the pause overlay (pause_bg only); a click opens
    // the option pages. TITLE mode skips straight to the options.
    s_PauseStage   = (mode == OPTION_MODE_PAUSE) ? PAUSE_STAGE_BG : PAUSE_STAGE_OPTION;
    // Default focus: pause overlay shows 4 buttons, so highlight the top
    // one (Back to Game) so keyboard / pad users can immediately see where
    // their input goes. Title / option pages stay un-highlighted until the
    // user hovers or navigates.
    if (s_PauseStage == PAUSE_STAGE_BG)
        s_Focused = BTN_PAUSE_RESUME;
    else
        s_Focused = BTN_NONE;
    s_PrevMouseX   = -1.0f;
    s_PrevMouseY   = -1.0f;
}

void Option_Close()
{
    s_Open         = false;
    s_ConfirmOpen  = false;
    s_BgmDrag      = false;
    s_SeDrag       = false;
}

bool Option_IsOpen()       { return s_Open; }
bool Option_IsConfirmOpen(){ return s_Open && s_ConfirmOpen; }
void Option_CloseConfirm() { s_ConfirmOpen = false; s_ConfirmHover = -1; }

// =========================================================================
// Update
// =========================================================================
void Option_Update()
{
    if (!s_Open) return;

    Mouse_State ms{};
    Mouse_GetState(&ms);
    UpdateMousePos(ms);

    const bool leftPressed  = ms.leftButton && !s_PrevLeft;
    const bool leftReleased = !ms.leftButton && s_PrevLeft;
    const bool mouseMoved = (s_mouseX != s_PrevMouseX) || (s_mouseY != s_PrevMouseY);
    s_PrevMouseX = s_mouseX;
    s_PrevMouseY = s_mouseY;

    // [CHANGE] PAUSE_STAGE_BG used to consume any click and slide into the
    // option pages. It now exposes 4 explicit buttons (Resume / Restart /
    // Option / Stage Exit) whose hit rects live in the normal nav list, so
    // we fall through to the regular focus / hover / activate path below.
    // The previous "any click anywhere" behavior is gone.

    // ---- Focus tracking ------------------------------------------------
    // Mouse hover sets focus to whatever the cursor is over (clears focus
    // when over empty space, so a stray cursor doesn't keep an item
    // highlighted while the player is using the gamepad).
    if (mouseMoved && !s_BgmDrag && !s_SeDrag)
    {
        s_Focused = HitTestMouse();
    }

    // Gamepad navigation: D-pad / LStick up/down cycles through the active
    // button list; left/right adjusts the focused slider; A activates.
    {
        FocusBtn list[kMaxNav];
        const int n = BuildNavList(list);
        int idx = -1;
        for (int i = 0; i < n; ++i) if (list[i] == s_Focused) { idx = i; break; }

        const bool up   = IsInputTrigger(UpKey,   gPad);
        const bool down = IsInputTrigger(DownKey, gPad);
        if ((up || down) && n > 0)
        {
            if (idx < 0)
            {
                // Nothing focused yet -- first DPad press picks the first item.
                s_Focused = list[0];
            }
            else if (up   && idx > 0)         s_Focused = list[idx - 1];
            else if (down && idx < n - 1)     s_Focused = list[idx + 1];
        }

        // Slider value adjustment with left/right while focused on a slider.
        // Press (not trigger) so holding the stick ramps continuously.
        if (s_Focused == BTN_BGM)
        {
            if (IsInputPress(LeftKey,  gPad)) SetBgmVolume(GetBgmVolume() - 0.01f);
            if (IsInputPress(RightKey, gPad)) SetBgmVolume(GetBgmVolume() + 0.01f);
        }
        else if (s_Focused == BTN_SE)
        {
            if (IsInputPress(LeftKey,  gPad)) SetSeVolume(GetSeVolume() - 0.01f);
            if (IsInputPress(RightKey, gPad)) SetSeVolume(GetSeVolume() + 0.01f);
        }

        // Activate: Enter / Space / gamepad A. (JumpKey is now bound to B
        // per the Switch layout, so we no longer route confirm through it.)
        bool confirmKey = Keyboard_IsKeyDownTrigger(KK_ENTER) ||
                          Keyboard_IsKeyDownTrigger(KK_SPACE);
        if (gPad.IsConnected() && gPad.IsButtonTrigger(XINPUT_GAMEPAD_A))
            confirmKey = true;
        if (confirmKey && s_Focused != BTN_NONE)
        {
            ActivateButton(s_Focused);
        }
    }

    // ---- Confirm modal: only NO/YES are interactive via mouse. ----
    if (s_ConfirmOpen)
    {
        // (Mouse hover already updated s_Focused above; the modal nav list
        // restricts to NO/YES so the focus is constrained correctly.)
        s_ConfirmHover = (s_Focused == BTN_NO)  ? 0
                       : (s_Focused == BTN_YES) ? 1
                       : -1;

        if (leftPressed && s_Focused != BTN_NONE)
        {
            ActivateButton(s_Focused);
        }
        s_PrevLeft = ms.leftButton;
        return;
    }

    // ---- Page-specific mouse interaction ----
    if (s_Page == PAGE_VOLUME)
    {
        // Slider drag start / release.
        if (leftPressed && !s_BgmDrag && !s_SeDrag)
        {
            if      (RectHit(kRectBgmRail, s_mouseX, s_mouseY)) s_BgmDrag = true;
            else if (RectHit(kRectSeRail,  s_mouseX, s_mouseY)) s_SeDrag  = true;
        }
        if (leftReleased)
        {
            s_BgmDrag = false;
            s_SeDrag  = false;
        }

        if (s_BgmDrag && ms.leftButton)
        {
            SetBgmVolume(TFromBarX(s_mouseX));
            s_Focused = BTN_BGM;  // keep frame on the rail during drag
        }
        if (s_SeDrag && ms.leftButton)
        {
            SetSeVolume(TFromBarX(s_mouseX));
            s_Focused = BTN_SE;
        }
    }

    // ---- Mouse click on a hovered button (sidebar / Reset / page switch) ----
    if (leftPressed && !s_BgmDrag && !s_SeDrag)
    {
        FocusBtn hit = HitTestMouse();
        if (hit != BTN_NONE) ActivateButton(hit);
    }

    s_PrevLeft = ms.leftButton;
}

// =========================================================================
// Draw
// =========================================================================

// Draw the key column entries for the explain table.
static void DrawExplainKeys()
{
    // Row 0: 移動 -> W A S D in a row centered on (kKeyColCx, kRowY[0])
    {
        const float keySize = 56.0f;
        const float gap     = 8.0f;
        const float totalW  = keySize * 4.0f + gap * 3.0f;
        float x = kKeyColCx - totalW * 0.5f + keySize * 0.5f;
        const float y = kRowY[0];
        ID3D11ShaderResourceView* row[4] = { s_KeyW, s_KeyA, s_KeyS, s_KeyD };
        for (int i = 0; i < 4; ++i)
        {
            DrawTextureAt(row[i], x, y, keySize, keySize);
            x += keySize + gap;
        }
    }
    // Row 1: ジャンプ -> Space (wide key, 901x500 source -> aspect ~1.8:1)
    DrawTextureAt(s_KeySpace, kKeyColCx, kRowY[1], 170.0f, 80.0f);
    // Row 2: アクション -> F
    DrawTextureAt(s_KeyF, kKeyColCx, kRowY[2], 56.0f, 56.0f);
    // Row 3: 影変身 -> F (same key as アクション currently)
    DrawTextureAt(s_KeyF, kKeyColCx, kRowY[3], 56.0f, 56.0f);
    // Row 4: 光源操作 -> TAB key (icon is ~9.3:1 source, padded to 2.125:1)
    DrawTextureAt(s_KeyTAB, kKeyColCx, kRowY[4], 170.0f, 80.0f);
    // Row 5: ダッシュ -> LShift (901x500 source)
    DrawTextureAt(s_KeyLShift, kKeyColCx, kRowY[5], 170.0f, 80.0f);
    // Row 6: 選択 -> Left mouse click. Rendered larger than the 56x56 key
    // icons (96x96) so the mouse silhouette reads clearly at this distance.
    DrawTextureAt(s_KeyMouseL, kKeyColCx, kRowY[6], 96.0f, 96.0f);

    // === Pad column (Switch layout) ===
    // Row 0 Move    -> L-stick
    // Row 1 Jump    -> B
    // Row 2 Action  -> A
    // Row 3 Change  -> A (same button as Action; ChangeKey aliased)
    // Row 4 Light   -> X (LightKey is bound to pad X)
    // Row 5 Dash    -> Y
    // Row 6 Select  -> D-pad
    const float padBtnSize   = 64.0f;
    const float padStickSize = 80.0f;
    DrawTextureAt(s_PadStickL, kPadColCx, kRowY[0], padStickSize, padStickSize);
    DrawTextureAt(s_PadB,      kPadColCx, kRowY[1], padBtnSize,   padBtnSize);
    DrawTextureAt(s_PadA,      kPadColCx, kRowY[2], padBtnSize,   padBtnSize);
    DrawTextureAt(s_PadA,      kPadColCx, kRowY[3], padBtnSize,   padBtnSize);
    DrawTextureAt(s_PadX,      kPadColCx, kRowY[4], padBtnSize,   padBtnSize);
    DrawTextureAt(s_PadY,      kPadColCx, kRowY[5], padBtnSize,   padBtnSize);
    DrawTextureAt(s_PadDpad,   kPadColCx, kRowY[6], padStickSize, padStickSize);
}

static void DrawConfirmModal()
{
    // confirm_delete.png is cropped to 800x400; center it on screen.
    const float W = (float)Direct3D_GetBackBufferWidth();
    // The original layout placed the dialog body roughly centered horizontally
    // and slightly above the vertical center (body y center ~526 in 1080px).
    DrawTextureAt(s_TexConfirm, W * 0.5f, 530.0f, 800.0f, 400.0f);
}

void Option_Draw()
{
    if (!s_Open) return;

    Shader_Begin();

    const float W = (float)Direct3D_GetBackBufferWidth();
    const float H = (float)Direct3D_GetBackBufferHeight();
    Shader_SetMatrix(XMMatrixOrthographicOffCenterLH(0.0f, W, H, 0.0f, 0.0f, 1.0f));

    SetDepthTest(FALSE);
    SetBlendState(BLENDSTATE_ALFA);

    // Pause overlay stage: pause_bg + yellow focus frame on the active
    // button. The 4 menu buttons (Resume / Restart / Option / Stage Exit)
    // are rendered as part of the texture itself; we just overlay the frame.
    if (s_PauseStage == PAUSE_STAGE_BG)
    {
        DrawFullscreen(s_BgPause);

        FocusBtn focused = s_Focused;
        if (focused == BTN_PAUSE_RESUME ||
            focused == BTN_PAUSE_RESTART ||
            focused == BTN_PAUSE_OPTION ||
            focused == BTN_PAUSE_STAGE_SELECT ||
            focused == BTN_PAUSE_EXIT)
        {
            DrawFocusFrame(RectForBtn(focused));
        }

        SetBlendState(BLENDSTATE_NONE);
        SetDepthTest(TRUE);
        return;
    }

    // Page background (mode-specific variant -- the only visual difference
    // is the データ消去 text color, baked into the texture). No pause_bg in
    // option stage; option page bgs are already semi-transparent so the
    // game scene dims through them.
    ID3D11ShaderResourceView* pageBg = nullptr;
    if (s_Page == PAGE_EXPLAIN)
    {
        pageBg = (s_Mode == OPTION_MODE_TITLE) ? s_BgExplainTitle : s_BgExplainPause;
    }
    else
    {
        pageBg = (s_Mode == OPTION_MODE_TITLE) ? s_BgVolumeTitle  : s_BgVolumePause;
    }
    DrawFullscreen(pageBg);

    // 3) Page-specific dynamic overlays.
    if (s_Page == PAGE_EXPLAIN)
    {
        DrawExplainKeys();
    }
    else
    {
        // Slider handles
        const float bgmT = GetBgmVolume();
        const float seT  = GetSeVolume();
        DrawTextureAt(s_TexHandle, BarXFromT(bgmT), kBgmRailY, kHandleSize, kHandleSize);
        DrawTextureAt(s_TexHandle, BarXFromT(seT),  kSeRailY,  kHandleSize, kHandleSize);
    }

    // 4) Confirm modal sits on top of the page.
    if (s_ConfirmOpen)
    {
        DrawConfirmModal();
    }

    // 5) Yellow focus frame on top of everything. Drawn last so it isn't
    //    occluded by page bg or modal. Skipped for BTN_NONE and for items
    //    that aren't valid in the current state (e.g. BTN_DELETE in PAUSE
    //    mode never appears in the nav list, so it can't be focused).
    //
    // [CHANGE] Sliders use the same rectangular yellow frame as the other
    // buttons, wrapping the entire rail. The earlier diamond-around-handle
    // experiment read as an X / cross at small sizes, which the user found
    // confusing.
    if (s_Focused == BTN_BGM || s_Focused == BTN_SE)
    {
        // Hit rect height is intentionally tight (50 px) so clicks above /
        // below the rail don't accidentally drag, but the visible frame is
        // bumped up so it wraps the rail visually like the older layout.
        HitRect r = RectForBtn(s_Focused);
        r.h = 80.0f;
        DrawFocusFrame(r);
    }
    else if (s_Focused != BTN_NONE)
    {
        HitRect r = RectForBtn(s_Focused);
        if (r.w > 0.0f && r.h > 0.0f) DrawFocusFrame(r);
    }

    // Restore deterministic state so the next frame's scene draw can't inherit
    // ALFA + depth=FALSE from us.
    SetBlendState(BLENDSTATE_NONE);
    SetDepthTest(TRUE);
}
