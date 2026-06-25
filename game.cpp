//game.cpp
//
// Scene orchestrator for SCENE_GAME. After the refactor this file only:
//   - Wires the subsystems up in Initialize / Finalize
//   - Drives the per-frame update order (gates, dispatch)
//   - Composes the draw order (shadow pass -> world -> player -> UI billboards
//     -> sky -> switch light)
//
// Responsibilities that previously lived inline have been extracted:
//   - BGM lifecycle + crossfade        -> game_bgm.cpp
//   - Ball light + shadow cubemap pass -> game_shadow.cpp
//   - Stage / mode camera dispatch     -> game_camera.cpp
//
#include	"Manager.h"
#include	"sprite.h"
#include	"Game.h"
#include	"Polygon3D.h"
#include	"Player3D.h"
#include	"Player2D.h"
#include	"LightSource.h"
#include	"Switch_Light.h"
#include	"field.h"
#include	"Effect.h"
#include	"score.h"
#include	"Audio.h"
#include	"camera.h"
#include	"direct3d.h"
#include	"Collision.h"
#include	"Bill_Board.h"
#include	"SkyDome.h"

#include	"PlayerModeSwitchManager.h"
#include	"Pushing_Obj_Manager.h"

#include	"debug.h"
#include	"ShadowColliderBox.h"
#include	"stage_clear_state.h"
#include	"fade.h"
#include	"iris.h"

#include	"game_bgm.h"
#include	"game_shadow.h"
#include	"game_camera.h"

#include	<map>


static ID3D11Device*        g_pDevice  = nullptr;
static ID3D11DeviceContext* g_pContext = nullptr;

static ID3D11ShaderResourceView* g_Goal_1_Texture = nullptr;
static ID3D11ShaderResourceView* g_Goal_2_Texture = nullptr;
static ID3D11ShaderResourceView* g_Goal_3_Texture = nullptr;


void Game_Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	g_pDevice  = pDevice;
	g_pContext = pContext;

	PlayerModeSwitchManager_Init();
	PlayerPushManager_Init();

	Player3D_Initialize(pDevice, pContext);
	Player2D_Initialize(pDevice, pContext);
	{
		XMFLOAT3 start = Field_GetPlayerStartPosition();
		if (PLAYER* p3 = GetPlayer3D())
		{
			p3->Position = start;
			p3->Velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
		}
		if (PLAYER* p2 = GetPlayer2D())
		{
			p2->Position = start;
			p2->Velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
		}
	}

	field_Initialize(pDevice, pContext);
	SkyDome_Initialize(pDevice, pContext);
	Light_Initialize(pDevice, pContext);
	Camera_Initialize();
	SwitchLight_Initialize(pDevice, pContext);
	Camera_ResetLightState();
	InitializeBillBoard();

	GameShadow_Initialize();
	GameCamera_InitForStage();

	// Spotlight axis for the in-stage ball light. Dir.w >= 0.5 enables the
	// cone in the pixel shader.
	//   STAGE_SELECT: shines into the back wall (+Z) so the night sky and
	//                 the moon (FIELD_OBJ_3) read with depth.
	//   STAGE_1/2/3 : shines straight down (-Y) like a ceiling spotlight.
	LIGHTOBJECT& ball = GameShadow_BallLight();
	const GAME_STAGE currentStage = GetCurrentStage();
	if (currentStage == STAGE_SELECT)
	{
		ball.SetDirection(XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f));
	}
	else if (currentStage == STAGE_1)
	{
		// STAGE_1: light is placed at +X side of the chamber; cone shines
		// toward -X so it illuminates the back wall (which the camera also
		// faces). User: "光源で照らされている壁の方向にカメラを向けて".
		ball.SetDirection(XMFLOAT4(-1.0f, 0.0f, 0.0f, 1.0f));
	}
	else if (currentStage == STAGE_2)
	{
		// STAGE_2 mirrors STAGE_1 across X: the light sits on the -X side and
		// the cone shines +X toward the +X shadow wall (x=24), which the camera
		// also faces.
		ball.SetDirection(XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f));
	}
	else if (currentStage == STAGE_3)
	{
		// STAGE_3 vertical climb uses STAGE_1's convention: light on the +X
		// side shining -X toward the -X shadow wall (which the camera faces).
		ball.SetDirection(XMFLOAT4(-1.0f, 0.0f, 0.0f, 1.0f));
	}

	if      (currentStage == STAGE_1) LoadMapFromFile("asset\\MapData\\stage1.txt");
	else if (currentStage == STAGE_2) LoadMapFromFile("asset\\MapData\\stage2.txt");
	else if (currentStage == STAGE_3) LoadMapFromFile("asset\\MapData\\stage3.txt");
	else
	{
		LoadMapFromFile("asset\\MapData\\stage_select.txt");

		// If we just returned from a stage clear, drain the dissolve queue and
		// hand the cinematic the door position. The cinematic self-fires once
		// fade hits FADE_NONE and drives the dissolve effect internally so the
		// door collapse, CLEAR billboard and camera moves stay aligned.
		GAME_STAGE queued = StageClearState_ConsumeDissolveQueue();
		if (queued != STAGE_NONE)
		{
			FIELD want = FIELD_EMPTY_BOX;
			if      (queued == STAGE_1) want = FIELD_STAGE_1;
			else if (queued == STAGE_2) want = FIELD_STAGE_2;
			else if (queued == STAGE_3) want = FIELD_STAGE_3;
			for (const auto& md : GetFieldMap())
			{
				if (md.no == want)
				{
					StageClearCinematic_Queue(queued, md.pos);
					break;
				}
			}
			if (StageClearState_IsAllCleared())
			{
				AllClearSequence_Start();
			}
		}
	}

	GameBgm_Initialize();

	TexMetadata metadata;
	ScratchImage image;
	LoadFromWICFile(L"asset\\texture\\UI\\easy.png", WIC_FLAGS_NONE, &metadata, image);
	CreateShaderResourceView(g_pDevice, image.GetImages(), image.GetImageCount(), metadata, &g_Goal_1_Texture);

	LoadFromWICFile(L"asset\\texture\\UI\\mid.png", WIC_FLAGS_NONE, &metadata, image);
	CreateShaderResourceView(g_pDevice, image.GetImages(), image.GetImageCount(), metadata, &g_Goal_2_Texture);

	LoadFromWICFile(L"asset\\texture\\UI\\hard.png", WIC_FLAGS_NONE, &metadata, image);
	CreateShaderResourceView(g_pDevice, image.GetImages(), image.GetImageCount(), metadata, &g_Goal_3_Texture);
}


int Game_GetBgm3DId() { return GameBgm_GetBgm3DId(); }
int Game_GetBgm2DId() { return GameBgm_GetBgm2DId(); }


void Game_Finalize()
{
	SAFE_RELEASE(g_Goal_1_Texture);
	SAFE_RELEASE(g_Goal_2_Texture);
	SAFE_RELEASE(g_Goal_3_Texture);

	GameBgm_Finalize();
	GameShadow_Finalize();

	field_Finalize();
	SkyDome_Finalize();
	Polygon3D_Finalize();
	SwitchLight_Finalize();
	Light_Finalize();
	Camera_Finalize();

	PlayerPushManager_Finalize();
	PlayerModeSwitchManager_Finalize();
	Player3D_Finalize();
	Player2D_Finalize();
	FinalizeBillBoard();
}


void Game_Update()
{
	GameBgm_Update();

	// Stage-clear visuals: per-stage dissolve + all-clear banner timers +
	// per-stage cinematic (drives camera + door collapse during the movie).
	StageClearEffect_Update();
	AllClearSequence_Update();
	StageClearCinematic_Update();

	Collision_DebugClearExtraBoxes();

	SwitchLight_Update();
	Light_Update();
	field_Update();
	SkyDome_Update();

	PlayerModeSwitchManager_Update();

	// Skip box push/pull during fade so the player cannot grab/move boxes
	// mid-transition. PlayerModeSwitchManager already gates itself.
	if (PlayerModeSwitchManager_GetMode() == MODE_3D && !Input_IsGloballyLocked())
	{
		PlayerPushManager_Update();
	}

	GameShadow_Update();

	// Cinematic locks:
	//   IsLocked() spans queued+active (full input lock from goal hit through
	//     movie end).
	//   IsActive() is just the camera-owning phases — during pending/fade-in
	//     we still want the player camera to follow.
	// fadeLocked is also rolled into cineLocked so the player can't move,
	// look around or push boxes during scene transitions.
	// Note: fadeLocked is NOT added to cineActive. Freezing the per-frame
	// camera dispatch during every fade caused the new scene to fade in
	// showing whichever pose was held over from the previous scene. The
	// camera needs to update during the fade so the first visible frame
	// already shows the correct framing. Iris IS in cineActive so the camera
	// stays frozen while the door iris is closing.
	const bool fadeLocked = Input_IsGloballyLocked();
	const bool irisActive = Iris_IsActive();
	const bool cineLocked = StageClearCinematic_IsLocked() || fadeLocked || irisActive;
	const bool cineActive = StageClearCinematic_IsActive() || irisActive;

	if (!SwitchLight_IsLightMode())
	{
		const PLAYER_MODE mode = PlayerModeSwitchManager_GetMode();
		if (mode == MODE_3D)
		{
			if (!cineLocked) Player3D_Update();
		}
		else
		{
			if (!cineLocked) Player2D_Update();
		}
		if (!cineActive) GameCamera_Update();
	}
}


void Game_Draw()
{
	Camera_Draw();

	LIGHTOBJECT& ball = GameShadow_BallLight();

	ball.SetEnable(TRUE);
	Shader_SetLight(ball.Light);
	SetDepthTest(TRUE);

	GameShadow_RenderCubemap();

	const XMFLOAT3& lightPos = GameShadow_GetLightPos();
	const float lightRadius  = g_ShadowRadius;

	Shader_Begin();
	Shader_SetLight(ball.Light);
	Shader_SetShadowMap(g_pShadowCubemapSRV);
	Shader_SetShadowSampler(g_pShadowSamplerState);
	Shader_SetShadowLightData(lightPos, lightRadius, 1.0f, 0.0f);

	field_Draw();

	// Bright moon: draw FIELD_OBJ_3 unlit so it stands out against the dim
	// night sky. Leaves the GPU light DISABLED -- restore so subsequent
	// player / billboard paths can re-toggle it predictably.
	Field_DrawLightBalls();
	ball.SetEnable(TRUE);
	Shader_SetLight(ball.Light);

	if (PlayerModeSwitchManager_GetMode() == MODE_3D)
	{
		ball.SetEnable(FALSE);
		Shader_SetLight(ball.Light);

		Player3D_Draw();
		PlayerPushManager_Draw();
		PlayerModeSwitchManager_Draw();

		ball.SetEnable(TRUE);
		Shader_SetLight(ball.Light);
	}
	else
	{
		Player2D_Draw();
	}

	// Unbind shadow map / disable light before the billboard pass.
	ball.SetEnable(FALSE);
	Shader_SetLight(ball.Light);

	DEBUG_IMGUI_BEGIN({
		Collision_DebugDraw();
	});

	// Force deterministic state before the billboard loop. Previously the
	// state was inherited from the last conditional draw (PushManager /
	// ModeSwitchManager) which early-returns when no target is nearby,
	// leaking different blend/shader state into the billboards -> flicker.
	Shader_Begin();
	SetBlendState(BLENDSTATE_ALFA);
	SetDepthTest(TRUE);

	//
	// === In-stage UI billboards (stage markers + CLEAR + mist puffs) ===
	// Layout and textures are slated for a redesign with new art assets, so
	// the rendering loop is kept here in game.cpp for easy iteration.
	//
	for (const auto& mapData : GetFieldMap())
	{
		GAME_STAGE stageIdx = STAGE_NONE;
		ID3D11ShaderResourceView* origTex = nullptr;
		if      (mapData.no == FIELD_STAGE_1) { stageIdx = STAGE_1; origTex = g_Goal_1_Texture; }
		else if (mapData.no == FIELD_STAGE_2) { stageIdx = STAGE_2; origTex = g_Goal_2_Texture; }
		else if (mapData.no == FIELD_STAGE_3) { stageIdx = STAGE_3; origTex = g_Goal_3_Texture; }
		if (stageIdx == STAGE_NONE) continue;

		// Sequential unlock: hide easy/mid/hard billboards for locked stages
		// so STAGE_SELECT only shows the currently playable door + the
		// already-cleared CLEAR markers.
		if (!StageClearState_IsUnlocked(stageIdx)) continue;

		// Stage marker billboards are scaled 2x so the easy / mid / hard text
		// reads clearly from the new stage_select camera distance.
		XMFLOAT3 billPos = { mapData.pos.x, mapData.pos.y + 2.5f, mapData.pos.z };
		XMFLOAT2 size    = { 2.0f, 2.0f };

		// Stage marker billboard tracks the door's Y-scale during the
		// cinematic so a freshly-unlocked next stage's marker fades in along
		// with its door emerging from the mist (Phase 4 reveal). Cleared
		// stages keep their marker at full alpha forever -- the CLEAR image
		// renders on top at +4.0 Y.
		{
			const float doorScale = StageClearCinematic_GetDoorScaleY(stageIdx);
			const bool  isCleared = StageClearState_IsCleared(stageIdx);
			const float markerA   = isCleared ? 1.0f : doorScale;
			if (markerA > 0.02f)
			{
				g_pContext->PSSetShaderResources(0, 1, &origTex);
				XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, markerA };
				DrawBillBoard(billPos, size, color, 0, 1, 1);
			}
		}

		// CLEAR billboard above the stage marker. clear.png is 1500x500 so
		// the 3:1 aspect is preserved in `clearSize` to keep the text from
		// being vertically stretched.
		float clearA = StageClearEffect_GetClearAlpha(stageIdx);
		if (clearA > 0.0f)
		{
			ID3D11ShaderResourceView* clearTex = StageClearState_GetClearTexture();
			if (clearTex)
			{
				XMFLOAT3 clearPos  = { mapData.pos.x, mapData.pos.y + 4.0f, mapData.pos.z };
				XMFLOAT2 clearSize = { 2.0f, 2.0f / 3.0f };
				g_pContext->PSSetShaderResources(0, 1, &clearTex);
				XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, clearA };
				DrawBillBoard(clearPos, clearSize, color, 0, 1, 1);
			}
		}

		// Black mist puffs while the door collapses / next door reveals.
		StageClearEffect_DrawSparkles(stageIdx);
	}

	// In-stage goal mist: when the player hits FIELD_GOAL in a play stage,
	// Player3D_CheckGoal kicks off StageClearEffect for the current stage at
	// the goal position. The billboard loop above only fires for FIELD_STAGE_X
	// markers (= stage_select doors); play stages have no such markers, so
	// the mist would never draw without this extra call.
	{
		const GAME_STAGE cur = GetCurrentStage();
		if (cur == STAGE_1 || cur == STAGE_2 || cur == STAGE_3)
		{
			StageClearEffect_DrawSparkles(cur);
		}
	}

	{
		// Render the sky as a dim bluish night so the moon
		// (Field_DrawLightBalls) stands out. The shader multiplies the sky
		// texture by this ambient; diffuse is kept low so the dome stays
		// uniformly dark without a directional gradient.
		LIGHT night = {};
		night.Enble     = TRUE;
		night.Direction = XMFLOAT4(0.0f, -1.0f, 0.0f, 0.0f);
		night.Diffuse   = XMFLOAT4(0.10f, 0.10f, 0.18f, 1.0f);
		night.Ambient   = XMFLOAT4(0.22f, 0.22f, 0.34f, 1.0f);
		Shader_SetLight(night);
		SkyDome_Draw();
		// Leave the BallLight disabled to match the state the billboard /
		// SwitchLight passes expect.
		ball.SetEnable(FALSE);
		Shader_SetLight(ball.Light);
	}

	SwitchLight_Draw();

	SetDepthTest(FALSE);

	// Screen-space banner for the post-3rd-clear sequence.
	AllClearSequence_Draw();
}
