#include	"title_manager.h"
#include	"sprite.h"
#include	"keyboard.h"
#include	"Player3D.h"
#include	"Player2D.h"
#include	"LightSource.h"
#include	"field.h"
#include	"Audio.h"
#include	"camera.h"
#include	"Collision.h"
#include	"PlayerModeSwitchManager.h"
#include	"ShadowColliderBox.h"
#include	"fade.h"
#include	"manager.h"
#include	"SkyDome.h"
#include "newKeyBind.h"
#include "mouse.h"

#include	 <map>

LIGHTOBJECT g_TitleLight;

static ID3D11Device* g_pDevice = nullptr;
static ID3D11DeviceContext* g_pContext = nullptr;

static std::map<int, ShadowPrism> g_ShadowPrisms;
static ShadowBuildConfig g_ShadowConfig;

static std::vector<const ShadowPrism*> g_ActiveShadowPrisms;

// ===== Title Action: Press Enter -> turn left & walk straight =====
static bool g_TitleActionStarted = false;
static int  g_TitleActionTimer = 0;

static const int kTitleTurnDuration = 30;    // ~0.5 seconds to turn left
static const int kTitleWalkDuration = 240;   // ~3 seconds walking before transition

static float g_TitleTargetYaw = 0.0f;        // target yaw after turning left
static float g_TitleStartYaw = 0.0f;         // yaw when action started

static const int kTitleStopDuration = 45;   // ~0.75 sec pause
static const int kTitleFadeDuration = 90;   // ~1.5 sec fade

static void TitleAction_Start()
{
	PLAYER* p = GetPlayer3D();
	if (!p) return;

	g_TitleActionStarted = true;
	g_TitleActionTimer = 0;

	// Hold the player in place (no walking cinematic): the only thing
	// Start does now is wait ~1 second then fade into the game.
	p->blockMovement = true;
	p->isAuto = false;
	p->Velocity.x = 0.0f;
	p->Velocity.z = 0.0f;
}

static void TitleAction_Update()
{
	if (!g_TitleActionStarted) return;

	PLAYER* p = GetPlayer3D();
	if (!p) return;

	g_TitleActionTimer++;
	p->blockMovement = true;
	p->Velocity.x = 0.0f;
	p->Velocity.z = 0.0f;

	const int kStartHoldFrames = 60;   // 1.0s before fade
	const int kStartFadeFrames = 60;   // 1.0s fade-out

	if (g_TitleActionTimer >= kStartHoldFrames && GetFadeState() == FADE_NONE)
	{
		XMFLOAT4 color(0.0f, 0.0f, 0.0f, 1.0f);
		SetFade(kStartFadeFrames, color, FADE_OUT, SCENE_GAME);
		SetCurrentStage(STAGE_SELECT);
	}
}

// Expose action-started flag to title.cpp so it can hide the click prompt
// and freeze the logo idle animation once the cinematic begins.
bool Title_IsActionStarted() { return g_TitleActionStarted; }

// Public entry point used by the title menu's Start button (in title.cpp).
// The click-on-anything detection was previously here; now Start is routed
// explicitly so Option / Exit clicks don't accidentally trigger the
// title->game transition.
void Title_RequestStartAction()
{
	if (g_TitleActionStarted) return;
	if (Input_IsGloballyLocked()) return;
	TitleAction_Start();
	PLAYER* p = GetPlayer3D();
	if (p) p->isTitleScene = false;
}

void Title_Manager_Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	g_pDevice = pDevice;
	g_pContext = pContext;

	// Reset title action state
	g_TitleActionStarted = false;
	g_TitleActionTimer = 0;

	PlayerModeSwitchManager_Init();
	Player3D_Initialize(pDevice, pContext);
	Player2D_Initialize(pDevice, pContext);

	field_Initialize(pDevice, pContext);
	Light_Initialize(pDevice, pContext);
	SkyDome_Initialize(pDevice, pContext);
	Camera_Initialize();

	g_TitleLight.SetEnable(false);
	g_TitleLight.SetAmbient(XMFLOAT4(0.08f, 0.08f, 0.1f, 1.0f));
	g_TitleLight.SetDiffuse(XMFLOAT4(1.0f, 0.95f, 0.85f, 1.0f));

	g_ShadowConfig.edgeSamples = 4;
	g_ShadowConfig.thickness = 1.0f;
	g_ShadowConfig.maxCastDist = 100.0f;

	// �O���[�o���ϐ��𒼐ڒ�`�����APlayer3D �̃G�N�X�|�[�g�֐���ʂ��ăt���O��ݒ�
	PLAYER* p = GetPlayer3D();
	if (p) p->isTitleScene = true;
}

void Title_Manager_Finalize()
{
	// Reset title action state on finalize
	g_TitleActionStarted = false;
	g_TitleActionTimer = 0;

	g_ShadowPrisms.clear();
	g_ActiveShadowPrisms.clear();

	field_Finalize();
	Light_Finalize();
	SkyDome_Finalize();
	Camera_Finalize();

	Player3D_Finalize();
	Player2D_Finalize();
}

void Title_Manager_Update()
{
	Title_Camera_Update();
	Light_Update();
	field_Update();
	SkyDome_Update();

	// [CHANGE] Start trigger moved to title.cpp's menu button handling.
	// Title_RequestStartAction() is now the public entry; it fires only
	// when the user actually clicks / activates the Start button (no more
	// "any click anywhere starts the game" behavior).
	if (!g_TitleActionStarted)
	{
		// Normal mode switching only when action hasn't started
		PlayerModeSwitchManager_Update();
	}

	// Update the title action (turn + walk)
	TitleAction_Update();

	XMFLOAT3 LightPos = GetLight_Position();
	g_TitleLight.SetDirection(XMFLOAT4(LightPos.x, LightPos.y, LightPos.z, 0.0f));
	g_ShadowLightPos = LightPos;

	float shadowIntensity = 1.0f;
	Shader_SetShadowLightData(LightPos, g_ShadowRadius, shadowIntensity);

	auto& map = GetFieldMap();

	g_ActiveShadowPrisms.clear();
	std::vector<int> casterIndices;
	for (int i = 0; i < (int)map.size(); ++i)
	{
		if (map[i].no == FIELD_OBJ_2)
		{
			casterIndices.push_back(i);
		}
	}

	std::vector<int> keysToRemove;
	for (auto& pair : g_ShadowPrisms)
	{
		bool found = false;
		for (int idx : casterIndices)
		{
			if (pair.first == idx)
			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			keysToRemove.push_back(pair.first);
		}
	}
	for (int key : keysToRemove)
	{
		g_ShadowPrisms.erase(key);
	}

	for (int casterIdx : casterIndices)
	{
		if (g_ShadowPrisms.find(casterIdx) == g_ShadowPrisms.end())
		{
			g_ShadowPrisms[casterIdx] = ShadowPrism();
		}

		ShadowPrism& prism = g_ShadowPrisms[casterIdx];
		const MAPDATA& caster = map[casterIdx];

		bool needsRebuild = Shadow_NeedsRebuild(prism, LightPos, caster, 0.01f);

		if (needsRebuild)
		{
			bool buildSuccess = Shadow_Build(
				prism,
				caster,
				LightPos,
				map,
				g_ShadowConfig
			);

			if (!buildSuccess)
			{
				prism.isValid = false;
			}
		}

		if (prism.isValid)
		{
			g_ActiveShadowPrisms.push_back(&prism);
		}
	}

	Collision_SetShadowPrisms(g_ActiveShadowPrisms);

	if (PlayerModeSwitchManager_GetMode() == MODE_3D)
	{
		Player3D_Update();
	}
	else
	{
		Player2D_Update();
	}
}

void Title_Manager_Draw()
{
	Camera_Draw();
	Light_Draw();

	g_TitleLight.SetEnable(TRUE);
	Shader_SetLight(g_TitleLight.Light);
	SetDepthTest(TRUE);

	XMFLOAT3 lightPos = GetLight_Position();
	float lightRadius = g_ShadowRadius;

	for (int face = 0; face < 6; face++)
	{
		Direct3D_BeginShadowPass(face);
		Shader_Begin();
		Shader_SetShadowLightData(lightPos, lightRadius, 1.0f, 1.0f);

		XMMATRIX lightViewProj = Direct3D_GetCubemapFaceViewProj(face, lightPos, lightRadius);
		Shader_SetShadowMatrix(lightViewProj);

		{
			XMMATRIX world = Light_GetWorldMatrix();
			Light_DrawRaw(world, world * lightViewProj);
		}

		{
			std::vector<MAPDATA>& Map = GetFieldMap();
			float maxShadowDist = g_ShadowRadius;

			for (size_t i = 0; i < Map.size(); ++i)
			{
				XMVECTOR v = XMLoadFloat3(&Map[i].pos) - XMLoadFloat3(&lightPos);
				if (XMVectorGetX(XMVector3LengthSq(v)) > maxShadowDist * maxShadowDist)
					continue;

				XMMATRIX world = Field_GetWorldMatrix((int)i);
				Field_DrawShadowMap(world, world * lightViewProj, (int)i);
			}
		}
	}

	Direct3D_EndShadowPass();

	Shader_Begin();
	Shader_SetLight(g_TitleLight.Light);
	Shader_SetShadowMap(g_pShadowCubemapSRV);
	Shader_SetShadowSampler(g_pShadowSamplerState);
	Shader_SetShadowLightData(lightPos, lightRadius, 1.0f, 0.0f);

	{
		field_Draw();
	}
	{
		Light_Draw();
	}
	{
		SkyDome_Draw();
	}

	if (PlayerModeSwitchManager_GetMode() == MODE_3D)
	{
		Player3D_Draw();
	}
	else
	{
		Player2D_Draw();
	}

	g_TitleLight.SetEnable(FALSE);
	Shader_SetLight(g_TitleLight.Light);

	SetDepthTest(FALSE);
}