//title.cpp
#include	"Manager.h"
#include	"sprite.h"
#include	"keyboard.h"
#include	"Audio.h"
#include	"Title.h"
#include	"fade.h"
#include	"shader.h"
#include	"title_manager.h"
#include	"stage_clear_state.h"
#include	"mouse.h"
#include	"Option.h"
#include	"Input.h"
#include	<windows.h>

//=========================================================================================================
// �\���̐錾
//=========================================================================================================


//=========================================================================================================
//�O���[�o���ϐ�
//=========================================================================================================
static ID3D11Device* g_pDevice = nullptr;
static ID3D11DeviceContext* g_pContext = nullptr;

static	int	g_BgmID = NULL;

static int g_PromptFrame = 0;

// ===== Title menu (Option / Start / Exit) =====
// Layout extracted from gameyou title.png (1920x1080) by scanning the
// non-transparent pixel runs in the bottom row.
static ID3D11ShaderResourceView* g_MenuTex   = NULL;
static ID3D11ShaderResourceView* g_WhiteTex  = NULL;

enum TitleBtn { TITLE_BTN_OPTION = 0, TITLE_BTN_START, TITLE_BTN_EXIT, TITLE_BTN_COUNT };

struct TitleHitRect { float cx, cy, w, h; };
// Centers / sizes lifted from the text bounding boxes, padded ~24 px so the
// yellow focus frame doesn't crowd the glyphs.
// Centers/sizes pulled from the actual pixel bboxes of each label in
// Title_Menu.png (alpha>30, gap<250 px merges across letter holes), plus
// ~30 px padding so the yellow frame surrounds the text instead of clipping
// the descenders / ascenders. Back-buffer is 1920x1080 (same as source),
// so no X scaling is needed.
static const TitleHitRect g_BtnRects[TITLE_BTN_COUNT] = {
	{  361.0f,  955.0f, 440.0f, 160.0f },  // Option (text x:161-561)
	{  962.0f,  942.0f, 310.0f, 130.0f },  // Start  (text x:826-1098)
	{ 1561.0f,  940.0f, 265.0f, 130.0f },  // Exit   (text x:1449-1674)
};

static int  g_TitleFocus    = TITLE_BTN_START;  // default selection
static bool g_TitlePrevLeft = false;
static int  g_PadNavCool    = 0;  // frame cool-down between repeats

// Convert raw window-client mouse coords to back-buffer coords (UI lives in
// back-buffer space).
static void TitleMouseToBackBuffer(const Mouse_State& ms, float* outX, float* outY)
{
	float cw = 0.0f, ch = 0.0f;
	Mouse_GetClientSize(&cw, &ch);
	const float bw = (float)Direct3D_GetBackBufferWidth();
	const float bh = (float)Direct3D_GetBackBufferHeight();
	*outX = (cw > 1.0f) ? ((float)ms.x * bw / cw) : (float)ms.x;
	*outY = (ch > 1.0f) ? ((float)ms.y * bh / ch) : (float)ms.y;
}

static bool TitleHitContains(const TitleHitRect& r, float x, float y)
{
	const float hx = r.w * 0.5f, hy = r.h * 0.5f;
	return (x >= r.cx - hx) && (x <= r.cx + hx) && (y >= r.cy - hy) && (y <= r.cy + hy);
}

static void DrawTitleColorRect(float cx, float cy, float w, float h,
                                float r, float g, float b, float a)
{
	if (!g_WhiteTex) return;
	g_pContext->PSSetShaderResources(0, 1, &g_WhiteTex);
	DrawSprite(XMFLOAT2(cx, cy), XMFLOAT2(w, h), XMFLOAT4(r, g, b, a));
}

// 4-px thick yellow frame around the rect, same style as the Option focus.
static void DrawTitleFocusFrame(const TitleHitRect& r)
{
	const float th = 4.0f;
	const float R = 1.0f, G = 0.85f, B = 0.1f, A = 1.0f;
	const float x0 = r.cx - r.w * 0.5f;
	const float y0 = r.cy - r.h * 0.5f;
	DrawTitleColorRect(r.cx,            y0 + th * 0.5f,         r.w, th, R, G, B, A);
	DrawTitleColorRect(r.cx,            y0 + r.h - th * 0.5f,   r.w, th, R, G, B, A);
	DrawTitleColorRect(x0 + th * 0.5f,  r.cy,                   th,  r.h, R, G, B, A);
	DrawTitleColorRect(x0 + r.w - th * 0.5f, r.cy,              th,  r.h, R, G, B, A);
}

static void TitleActivate(TitleBtn b)
{
	// Play the system click SE on every menu activation. Volume scales with
	// the Option SE slider via GetSeVolume() inside the helper.
	SystemSE_PlayClick();
	switch (b)
	{
	case TITLE_BTN_START:
		Title_RequestStartAction();
		break;
	case TITLE_BTN_OPTION:
		Option_Open(OPTION_MODE_TITLE);
		break;
	case TITLE_BTN_EXIT:
		// Close the main window; main.cpp's WM_DESTROY handler posts the
		// quit message that breaks the message loop.
		PostMessage(Mouse_GetWindow(), WM_CLOSE, 0, 0);
		break;
	default: break;
	}
}
 
//=========================================================================================================
//����������
//=========================================================================================================
void Title_Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	g_pDevice = pDevice;
	g_pContext = pContext;

	// Reset per-session flags, then restore from the persistent save file
	// (1 byte: highest cleared stage). Cleared stages get everStarted=true
	// so their CLEAR billboard appears immediately in stage_select without
	// having to play the cinematic again.
	StageClearState_Reset();
	StageClearState_LoadFromSave();

	Title_Manager_Initialize(pDevice, pContext);


	g_BgmID = LoadAudio("asset\\Audio\\title.wav");	//�T�E���h���[�h
	SetAudioVolume(g_BgmID, 0.05f);
	PlayAudio(g_BgmID, true);	//�Đ��J�n�i���[�v����j

	TexMetadata		metadata;
	ScratchImage	image;

	const float SCREEN_WIDTH = (float)Direct3D_GetBackBufferWidth();
	const float SCREEN_HEIGHT = (float)Direct3D_GetBackBufferHeight();



	//�t�F�[�h�C���̃Z�b�g
	XMFLOAT4	color = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	SetFade(60.0f, color, FADE_IN, SCENE_TITLE);

	g_PromptFrame = 0;

	// Menu image (Option / Start / Exit row across the bottom). Mostly
	// transparent; only the three labels are opaque.
	if (SUCCEEDED(LoadFromWICFile(L"asset\\texture\\UI\\Title_Menu.png", WIC_FLAGS_NONE, &metadata, image)))
	{
		CreateShaderResourceView(pDevice, image.GetImages(), image.GetImageCount(), metadata, &g_MenuTex);
	}

	// 1x1 white texture for the yellow focus frame (same trick as Option).
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
		if (SUCCEEDED(pDevice->CreateTexture2D(&td, &sd, &tex)) && tex)
		{
			pDevice->CreateShaderResourceView(tex, nullptr, &g_WhiteTex);
			tex->Release();
		}
	}

	// Title menu needs absolute cursor coords for button hit-testing, and
	// the cursor should be visible. The game scene re-sets to RELATIVE on
	// scene change.
	Mouse_SetMode(MOUSE_POSITION_MODE_ABSOLUTE);

	g_TitleFocus    = TITLE_BTN_START;
	g_TitlePrevLeft = false;
	g_PadNavCool    = 0;
}

//=========================================================================================================
//�I������
//=========================================================================================================
void Title_Finalize()
{
	SAFE_RELEASE(g_MenuTex);
	SAFE_RELEASE(g_WhiteTex);

	UnloadAudio(g_BgmID);//�T�E���h�̉��

	Title_Manager_Finalize();
}

//=========================================================================================================
//�X�V����
//=========================================================================================================
void Title_Update()
{
	Title_Manager_Update();

	// Tick click prompt anim only while waiting; freeze during cinematic / fade.
	if (!Title_IsActionStarted() && GetFadeState() == FADE_NONE) {
		g_PromptFrame++;
	}

	// Menu input is locked while the cinematic is running, during fades, or
	// while Option is open (Option owns the cursor in that case).
	const bool menuLocked =
		Title_IsActionStarted() ||
		GetFadeState() != FADE_NONE ||
		Option_IsOpen();
	if (menuLocked)
	{
		g_TitlePrevLeft = false;  // forget click state so re-open doesn't re-fire
		return;
	}

	// --- Mouse hover / click ---
	Mouse_State ms{};
	Mouse_PeekState(&ms);  // peek; relative-delta consumers aren't running in title
	float mx = 0.0f, my = 0.0f;
	TitleMouseToBackBuffer(ms, &mx, &my);

	int hover = -1;
	for (int i = 0; i < TITLE_BTN_COUNT; ++i)
	{
		if (TitleHitContains(g_BtnRects[i], mx, my)) { hover = i; break; }
	}
	if (hover >= 0) g_TitleFocus = hover;

	const bool leftPressed = ms.leftButton && !g_TitlePrevLeft;
	g_TitlePrevLeft = ms.leftButton;
	if (leftPressed && hover >= 0)
	{
		TitleActivate((TitleBtn)hover);
		return;
	}

	// --- Gamepad / keyboard navigation ---
	if (g_PadNavCool > 0) --g_PadNavCool;

	int dir = 0;
	// [CHANGE] WASD keys are reserved for player movement; menu navigation
	// uses only arrow keys + gamepad DPad/LStick + mouse hover.
	if (Keyboard_IsKeyDownTrigger(KK_LEFT))  dir = -1;
	if (Keyboard_IsKeyDownTrigger(KK_RIGHT)) dir = +1;
	if (gPad.IsConnected())
	{
		if (gPad.IsButtonTrigger(XINPUT_GAMEPAD_DPAD_LEFT))  dir = -1;
		if (gPad.IsButtonTrigger(XINPUT_GAMEPAD_DPAD_RIGHT)) dir = +1;
		if (g_PadNavCool == 0)
		{
			float lx = gPad.GetLeftStickX();
			if (lx < -0.5f) { dir = -1; g_PadNavCool = 15; }
			else if (lx > 0.5f) { dir = +1; g_PadNavCool = 15; }
		}
	}
	if (dir != 0)
	{
		int n = g_TitleFocus + dir;
		if (n < 0) n = TITLE_BTN_COUNT - 1;
		if (n >= TITLE_BTN_COUNT) n = 0;
		g_TitleFocus = n;
	}

	// Confirm: Space / Enter / Pad A
	bool confirm = Keyboard_IsKeyDownTrigger(KK_SPACE) ||
	               Keyboard_IsKeyDownTrigger(KK_ENTER);
	if (gPad.IsConnected() && gPad.IsButtonTrigger(XINPUT_GAMEPAD_A))
		confirm = true;
	if (confirm)
	{
		TitleActivate((TitleBtn)g_TitleFocus);
	}
}
//=========================================================================================================
//�`�揈��
//=========================================================================================================
void Title_Draw()
{
	Title_Manager_Draw();
	// �V�F�[�_�[��`��p�C�v���C���ɐݒ�
	Shader_Begin();

	// ��ʃT�C�Y�擾
	const float SCREEN_WIDTH = (float)Direct3D_GetBackBufferWidth();
	const float SCREEN_HEIGHT = (float)Direct3D_GetBackBufferHeight();

	// ���_�V�F�[�_�[�ɕϊ��s���ݒ�
	Shader_SetMatrix(XMMatrixOrthographicOffCenterLH(
		0.0f,
		SCREEN_WIDTH,
		SCREEN_HEIGHT,
		0.0f,
		0.0f,
		1.0f));
	//---------------------------------------------------

	SetBlendState(BLENDSTATE_ALFA);

	// [CHANGE] Old click-to-start arrow prompt is hidden -- the new menu
	// (Option / Start / Exit) replaces "click to start" so the prompt would
	// be visual noise on top of the button labels.

	// === Title menu: Option / Start / Exit ===
	// Hidden during the title->game cinematic and any fade so the screen
	// reads as "in transition" cleanly.
	const bool menuVisible =
		g_MenuTex && !Title_IsActionStarted() && GetFadeState() == FADE_NONE && !Option_IsOpen();
	if (menuVisible)
	{
		// Menu image is the same size as the back-buffer, anchored center.
		g_pContext->PSSetShaderResources(0, 1, &g_MenuTex);
		SetBlendState(BLENDSTATE_ALFA);
		DrawSprite(XMFLOAT2(SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.5f),
		           XMFLOAT2(SCREEN_WIDTH, SCREEN_HEIGHT),
		           XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));

		// Yellow focus frame on the active button.
		if (g_TitleFocus >= 0 && g_TitleFocus < TITLE_BTN_COUNT)
		{
			DrawTitleFocusFrame(g_BtnRects[g_TitleFocus]);
		}
		SetBlendState(BLENDSTATE_NONE);
	}
}
