//manager.cpp
#include "direct3d.h"
#include "Manager.h"
#include "keyboard.h"
#include "Game.h"
#include "Title.h"
#include "Result.h"
#include "fade.h"
#include "Input.h"
#include "Option.h"
#include "newKeyBind.h"
#include "mouse.h"
#include "stage_clear_state.h"
#include "iris.h"

#include <string>

#include "debug.h"
#ifdef _DEBUG
bool g_DebugMode = false;
#else
bool g_DebugMode = false;
#endif


//=========================================================================================================
// グローバル変数
//=========================================================================================================
static	SCENE	g_Scene = SCENE_NONE;	//現在のシーン番号
static std::string sceneText = "NULL";
static bool g_OptionMenu = false;
static bool s_PrevOptionMenu = false;



extern Controller gPad; // コントローラー

//=========================================================================================================
// 初期化処理
//=========================================================================================================
void	Manager_Initialize()
{ 
	Option_Initialize(Direct3D_GetDevice(), Direct3D_GetDeviceContext());
	////本来はtitleの初期化でフェードインをセットする
	//XMFLOAT4 color = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	//SetFade(60.0f, color, FADE_STATE::FADE_IN, SCENE_GAME);
	//SetScene(SCENE_GAME);	//最初に動かすシーンに切り替える

	//本来の形
	Fade_Initialize(Direct3D_GetDevice(), Direct3D_GetDeviceContext());
	StageClearState_Initialize(Direct3D_GetDevice(), Direct3D_GetDeviceContext());
	Iris_Initialize();
	SetScene(SCENE_TITLE);	//最初に動かすシーンに切り替える

}

//=========================================================================================================
// 終了処理
//=========================================================================================================
void	Manager_Finalize()
{
	Option_Finalize();
	Fade_Finalize();
	StageClearState_Finalize();
	Iris_Finalize();
	SetScene(SCENE_NONE);
}

//=========================================================================================================
// 更新処理
//=========================================================================================================
void	Manager_Update()
{

#ifdef _DEBUG
	if (Keyboard_IsKeyDownTrigger(KK_F1))// F1キーでデバッグモードのオンオフ切り替え
	{
		g_DebugMode = !g_DebugMode;
	}
#endif // _DEBUG

	// [FIX] Block all toggle-style input while a fade is in progress so the
	// player can't open the pause menu, swap mouse modes, etc. mid-transition.
	// Fade_Update still ticks at the bottom of this function so the fade
	// itself keeps progressing.
	const bool inputLocked = Input_IsGloballyLocked();

	static bool relativeMode = true;
	bool suppressDelta = false;
	{
		if (!inputLocked && Keyboard_IsKeyDownTrigger(KK_F2)) {
			relativeMode = !relativeMode;
			Mouse_SetMode(relativeMode ? MOUSE_POSITION_MODE_RELATIVE
				: MOUSE_POSITION_MODE_ABSOLUTE);
		}
	}


	gPad.Update();

	// Esc / Start: open or close Option. Esc-handling hierarchy:
	//   - If a confirm modal is up inside Option, Esc cancels the modal only.
	//   - Else if Option is open, Esc closes it.
	//   - Else Esc opens Option in the mode appropriate for the current scene
	//     (TITLE on title screen -> データ消去 enabled; PAUSE in game -> disabled).
	if (!inputLocked && IsInputTrigger(MenuKey,gPad))
	{
		if (Option_IsConfirmOpen())
		{
			Option_CloseConfirm();
		}
		else if (Option_IsOpen())
		{
			Option_Close();
			g_OptionMenu = false;
		}
		else if (g_Scene == SCENE_TITLE || g_Scene == SCENE_GAME)
		{
			OPTION_MODE mode = (g_Scene == SCENE_TITLE) ? OPTION_MODE_TITLE : OPTION_MODE_PAUSE;
			Option_Open(mode);
			g_OptionMenu = true;
		}
	}

	// Keep g_OptionMenu in sync if Option closed itself (e.g. Back button).
	if (g_OptionMenu && !Option_IsOpen()) g_OptionMenu = false;
	// And in sync if Option was opened externally (e.g. the title menu's
	// Option button). Without this the mouse-mode toggle below would not
	// fire and -- in scenes that use RELATIVE mouse -- the cursor would
	// stay hidden / locked when Option appears.
	if (!g_OptionMenu && Option_IsOpen()) g_OptionMenu = true;

	// 変更点：状態が変化したフレームだけモード切替
	if (g_OptionMenu != s_PrevOptionMenu)
	{
		// [FIX] Mouse_SetMode now applies cursor visibility + mode
		// synchronously, so the ShowCursor loops here are no longer needed.
		// [FIX] When closing Option from the title scene, stay in ABSOLUTE
		// because the title menu itself needs a visible cursor for button
		// clicks. Only the in-game scene wants the cursor hidden / locked.
		Mouse_PositionMode targetMode;
		if (g_OptionMenu) {
			targetMode = MOUSE_POSITION_MODE_ABSOLUTE;
		} else if (g_Scene == SCENE_TITLE) {
			targetMode = MOUSE_POSITION_MODE_ABSOLUTE;
		} else {
			targetMode = MOUSE_POSITION_MODE_RELATIVE;
		}
		Mouse_SetMode(targetMode);
		s_PrevOptionMenu = g_OptionMenu;
	}

	if (g_OptionMenu)
	{
		Option_Update();
	}

	if (!g_OptionMenu)
	{
		switch (g_Scene)	//現在シーンのアップデート関数を呼び出す
		{
		case SCENE_NONE:
			break;
		case SCENE_TITLE:
			sceneText = "TITLE";
			Title_Update();
			break;
		case SCENE_GAME:
			sceneText = "GAME";
			Game_Update();
			break;
		case SCENE_RESULT:
			sceneText = "RESULT";
			Result_Update();
			break;
		default:
			break;
		}
	}
	
	

	DEBUG_IMGUI_BEGIN({
		ImGui::Begin("Debug - han");
		ImGui::Text("SCENE: %s", sceneText.c_str());
		ImGui::End();
		});


	Iris_Update();
	Fade_Update();

}

//=========================================================================================================
// 描画処理
//=========================================================================================================
void	Manager_Draw()
{
	// Set iris cbuffer ONCE at the top so every subsequent draw sees the
	// current cutout. When iris is idle the active flag is 0 and the
	// shader short-circuits past the math.
	Iris_ApplyToShader();

	switch (g_Scene)	//現在シーンの描画関数を呼び出す
	{
		case SCENE_NONE:
			break;
		case SCENE_TITLE:
			Title_Draw();	
			break;
		case SCENE_GAME:
			Game_Draw();
			break;
		case SCENE_RESULT:
			Result_Draw();
			break;
		default:
			break;
	}

	if (g_OptionMenu)
	{
		Option_Draw();
	}

	Fade_Draw();
}

//=========================================================================================================
// シーン状態セット処理
//=========================================================================================================
void	SetScene(SCENE scene) //シーンを切り替える
{
	g_OptionMenu = false; // シーン切り替え時にオプションメニューを閉じる

	//実行中のシーンを終了させる
	switch (g_Scene)	//現在シーンの終了関数を呼び出す
	{
		case SCENE_NONE:
			break;
		case SCENE_TITLE:
			Title_Finalize();	
			break;
		case SCENE_GAME:
			Game_Finalize();
			break;
		case SCENE_RESULT:
			Result_Finalize();
			break;
		default:
			break;
	}

	g_Scene = scene;	//指定のシーンへ切り替える

	//次のシーンを初期化する
	switch (g_Scene)	//現在シーンの初期化関数を呼び出す
	{
		case SCENE_NONE:
			break;
		case SCENE_TITLE:
			Title_Initialize(Direct3D_GetDevice(), Direct3D_GetDeviceContext());
			break;
		case SCENE_GAME:
			Game_Initialize( Direct3D_GetDevice(), Direct3D_GetDeviceContext());
			break;
		case SCENE_RESULT:
			Result_Initialize(Direct3D_GetDevice(), Direct3D_GetDeviceContext());
			break;
		default:
			break;
	}

}

int		GetScene(void)
{
	return g_Scene;
}
