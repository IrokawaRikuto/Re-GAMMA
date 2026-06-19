#include "Switch_Light.h"
#include "field.h"
#include "Camera.h"
#include "keyboard.h"
#include "Collision.h"
#include "player3D.h"
#include "PlayerModeSwitchManager.h"
#include "Bill_Board.h"
#include "newKeyBind.h"
#include "stage_clear_state.h"
#include "fade.h"

#include "debug.h"

using namespace DirectX;

extern Controller gPad;

static bool g_LightMode = false;
static bool g_TabWasDown = false;

// Index into g_MapData for the OBJ_3 we're controlling (-1 = not found)
static int g_LightObjIndex = -1;

// OBJ_3 collision half-size (ball radius as ellipsoid)
static const XMFLOAT3 kOBJ3_CollisionHalf = { 0.25f, 0.25f, 0.25f };

// Billboard prompt distance
static const float kPromptDistance = 3.0f;    // how close player needs to be
static const float kBillboardYOffset = 1.0f;  // height above OBJ_3

// Billboard texture
static ID3D11Device* g_pDevice = nullptr;
static ID3D11DeviceContext* g_pContext = nullptr;
static ID3D11ShaderResourceView* g_TabPromptTexture = nullptr;

// Is player near the light?
static bool g_PlayerNearLight = false;

//=========================================================================================================
// Which field types should OBJ_3 collide with?
//=========================================================================================================
static bool OBJ3_IsSolid(FIELD type)
{
	switch (type)
	{
	case FIELD_GROUND:
	case FIELD_WALL:
	case FIELD_OBJ_BOX:
	case FIELD_OBJ_1:
	case FIELD_OBJ_2:
	case FIELD_BENCH:
	case FIELD_DUSTBOX:
	case FIELD_FOUNTAIN:
	case FIELD_POLE:
	case FIELD_FENCE:
	case FIELD_SEESAW_1:
	case FIELD_EMPTY_BOX:
		return true;
	default:
		return false;
	}
}

//=========================================================================================================
// extern from CollisionGeometry.cpp (same ones used by Player3D)
//=========================================================================================================
extern bool Resolve_Ellipsoid_OBB_Yaw(
	const XMFLOAT3& ellC, const XMFLOAT3& ellR,
	const XMFLOAT3& boxC, const XMFLOAT3& boxH, float boxYaw,
	XMFLOAT3* outPush, XMFLOAT3* outNormal);

//=========================================================================================================
// OBJ_3 vs all field objects collision
//=========================================================================================================
static void SwitchLight_CheckCollision(XMFLOAT3& objPos)
{
	std::vector<MAPDATA>& map = GetFieldMap();

	XMFLOAT3 ellC = objPos;
	XMFLOAT3 ellR = kOBJ3_CollisionHalf;

	// Multiple passes for corner resolution
	for (int pass = 0; pass < 3; pass++)
	{
		for (size_t i = 0; i < map.size(); i++)
		{
			// Don't collide with itself
			if ((int)i == g_LightObjIndex) continue;

			if (!OBJ3_IsSolid(map[i].no)) continue;

			// Get box yaw (same logic as Player3D collision)
			float boxYaw = (map[i].no == FIELD_OBJ_1) ? map[i].rotate.y : 0.0f;

			XMFLOAT3 boxHalf = Field_GetCollisionHalfSize(map[i]);

			XMFLOAT3 push, norm;
			if (!Resolve_Ellipsoid_OBB_Yaw(ellC, ellR, map[i].pos, boxHalf, boxYaw, &push, &norm))
				continue;

			// Push OBJ_3 out of the collision
			ellC.x += push.x;
			ellC.y += push.y;
			ellC.z += push.z;
		}
	}

	objPos = ellC;
}

//=========================================================================================================
// Get distance between player and OBJ_3
//=========================================================================================================
static float GetPlayerToLightDistance()
{
	if (g_LightObjIndex < 0) return 9999.0f;

	std::vector<MAPDATA>& map = GetFieldMap();
	if (g_LightObjIndex >= (int)map.size()) return 9999.0f;

	XMFLOAT3 lightPos = map[g_LightObjIndex].pos;
	XMFLOAT3 playerPos = { 0, 0, 0 };

	PLAYER* p3 = GetPlayer3D();
	if (!p3) return 9999.0f;

	playerPos = p3->Position;

	float dx = playerPos.x - lightPos.x;
	float dy = playerPos.y - lightPos.y;
	float dz = playerPos.z - lightPos.z;

	return sqrtf(dx * dx + dy * dy + dz * dz);
}

void SwitchLight_Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	g_pDevice = pDevice;
	g_pContext = pContext;

	g_LightMode = false;
	g_TabWasDown = false;
	g_LightObjIndex = -1;

	// Load the Tab prompt texture
	TexMetadata metadata;
	ScratchImage image;
	LoadFromWICFile(L"asset\\texture\\UI\\Keyboard\\TAB.png", WIC_FLAGS_NONE, &metadata, image);
	CreateShaderResourceView(g_pDevice, image.GetImages(), image.GetImageCount(), metadata, &g_TabPromptTexture);
}

void SwitchLight_Finalize(void)
{
	SAFE_RELEASE(g_TabPromptTexture);
}

// Find the first FIELD_OBJ_3 in the map data
static int FindLightObjIndex()
{
	std::vector<MAPDATA>& map = GetFieldMap();
	for (size_t i = 0; i < map.size(); i++)
	{
		if (map[i].no == FIELD_OBJ_3)
		{
			return (int)i;
		}
	}
	return -1;
}

void SwitchLight_Update(void)
{
	// Always find the OBJ_3 index (map may reload)
	g_LightObjIndex = FindLightObjIndex();

	// Check if player is near the light (for billboard only)
	float dist = GetPlayerToLightDistance();
	g_PlayerNearLight = (dist <= kPromptDistance);

	// Tab toggle works from anywhere
	bool tabIsDown = IsInputPress(LightKey, gPad);
	// During the stage-clear cinematic, swallow the TAB toggle so the camera
	// stays locked to the cinematic and the player can't yank control mid-shot.
	// [FIX] Also swallow during any active fade so the player cannot enter or
	// leave light mode mid-transition.
	if (StageClearCinematic_IsActive()) tabIsDown = false;
	if (Input_IsGloballyLocked()) tabIsDown = false;
	if (tabIsDown && !g_TabWasDown)
	{
		g_LightMode = !g_LightMode;
		if (g_LightMode)
		{
			PLAYER* p3 = GetPlayer3D();
			if (p3)
			{
				p3->Velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
				p3->CurrentAnimIndex = PLAYER_ANIM_IDLE;
			}
		}
		else
		{
			// [FIX] Returning from light mode: seed Player3D camera lerp state
			// from the current g_CameraObject (which is at the light camera pose).
			// Without this seed, gCamTarget/gCamPos still hold the pre-TAB player
			// camera, so Player3DCamera_Update snaps from light pose to that
			// stale pose on the first frame, producing a teleport.
			Camera_SeedPlayer3DFromCurrent();
			// Drop the rigid-offset capture so the next TAB entry re-captures
			// from whatever the player camera pose is at that time instead of
			// reusing the stale offset from the last entry.
			Camera_ResetLightState();
			// Stage-specific cameras (STAGE_1 / STAGE_SELECT) directly assign
			// g_CameraObject every frame without lerping, so they would snap
			// from the light pose to their fixed pose. Kick a short lerp
			// transition so the hand-back is as smooth as the entry.
			StageOne_Camera_BeginTransition(30);
			StageSelect_Camera_BeginTransition(30);
			StageTwo_Camera_BeginTransition(30);
		}
	}
	g_TabWasDown = tabIsDown;

	if (g_LightMode && PlayerModeSwitchManager_GetMode() == MODE_2D)
	{
		g_LightMode = false;
	}

	if (!g_LightMode) return;
	if (g_LightObjIndex < 0) return;
	// [FIX] Light source movement also locked during fade -- player should not
	// be able to drag the light around while the scene is transitioning.
	if (Input_IsGloballyLocked()) return;

	std::vector<MAPDATA>& map = GetFieldMap();
	if (g_LightObjIndex >= (int)map.size()) return;

	MAPDATA& obj = map[g_LightObjIndex];

	// Light source moves only along the camera-right axis (screen left/right).
	// Dash key (Shift / pad Y) speeds it up so the player isn't stuck waiting
	// for the light to crawl across the chamber.
	const float kBaseSpeed = 8.0f / 60.0f;
	const float kDashMul   = 2.2f;
	float speed = kBaseSpeed;
	if (IsInputPress(DashKey, gPad)) speed *= kDashMul;

	float inputX = 0.0f;
	if (IsInputPress(RightKey, gPad)) inputX += 1.0f;
	if (IsInputPress(LeftKey,  gPad)) inputX -= 1.0f;
	if (gPad.IsConnected())
	{
		float lx = gPad.GetLeftStickX();
		if (fabsf(lx) >= 0.20f) inputX = lx;
	}

	if (fabsf(inputX) > 1e-6f)
	{
		if (inputX >  1.0f) inputX =  1.0f;
		if (inputX < -1.0f) inputX = -1.0f;

		// Camera right vector = cross(up, forward). Project light movement
		// onto this so left/right on screen always moves the light along
		// the screen's horizontal axis regardless of which stage we're in.
		XMFLOAT3 camPos = GetCameraPosition();
		XMFLOAT3 camAt  = GetCameraAtPosition();
		XMFLOAT3 fwd = { camAt.x - camPos.x, 0.0f, camAt.z - camPos.z };
		float fl = sqrtf(fwd.x * fwd.x + fwd.z * fwd.z);
		if (fl > 1e-6f) { fwd.x /= fl; fwd.z /= fl; }
		// right = cross(up=+Y, fwd) = (fwd.z, 0, -fwd.x)
		float rightX = fwd.z;
		float rightZ = -fwd.x;
		obj.pos.x += rightX * inputX * speed;
		obj.pos.z += rightZ * inputX * speed;
	}

	if (Keyboard_IsKeyDown(KK_NUMPAD4)) obj.pos.y -= speed;
	if (Keyboard_IsKeyDown(KK_NUMPAD6)) obj.pos.y += speed;

	// --- Collision check and resolution (same system as Player3D) ---
	SwitchLight_CheckCollision(obj.pos);

	// Per-stage hard clamp so the light stays one cell short of the chamber
	// walls. Collision alone leaves slight drift; this gives a deterministic
	// stop one step before the wall.
	{
		GAME_STAGE st = GetCurrentStage();
		if (st == STAGE_1)
		{
			if (obj.pos.x < 0.0f)  obj.pos.x = 0.0f;
			if (obj.pos.x > 14.0f) obj.pos.x = 14.0f;
			if (obj.pos.z < 2.0f)  obj.pos.z = 2.0f;
			if (obj.pos.z > 21.0f) obj.pos.z = 21.0f;
		}
			else if (st == STAGE_2)
			{
				// STAGE_2: light lives on the -X side (x~-1) and moves along Z.
				if (obj.pos.x < -2.0f) obj.pos.x = -2.0f;
				if (obj.pos.x > 14.0f) obj.pos.x = 14.0f;
				if (obj.pos.z <  2.0f) obj.pos.z =  2.0f;
				if (obj.pos.z > 31.0f) obj.pos.z = 31.0f;
			}
		else if (st == STAGE_SELECT)
		{
			if (obj.pos.x < 0.0f)  obj.pos.x = 0.0f;
			if (obj.pos.x > 14.0f) obj.pos.x = 14.0f;
			if (obj.pos.z < 1.0f)  obj.pos.z = 1.0f;
			if (obj.pos.z > 22.0f) obj.pos.z = 22.0f;
		}
	}

	// Camera follows the OBJ_3 (with wide field-overview framing).
	Camera_SetLightMode(true);
	LightCamera_Update();
}

void SwitchLight_Draw(void)
{
	// Only show billboard when player is near AND not already controlling the light
	if (!g_PlayerNearLight) return;
	if (g_LightMode) return;
	if (g_LightObjIndex < 0) return;
	if (!g_TabPromptTexture) return;

	std::vector<MAPDATA>& map = GetFieldMap();
	if (g_LightObjIndex >= (int)map.size()) return;

	XMFLOAT3 lightPos = map[g_LightObjIndex].pos;

	// Billboard position: above the OBJ_3
	XMFLOAT3 billPos = {
		lightPos.x,
		lightPos.y + kBillboardYOffset,
		lightPos.z
	};

	XMFLOAT2 size = { 2.0f, 2.0f };
	XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

	g_pContext->PSSetShaderResources(0, 1, &g_TabPromptTexture);
	DrawBillBoard(billPos, size, color, 0, 1, 1);
}

bool SwitchLight_IsLightMode(void)
{
	return g_LightMode;
}
// Getter so camera.cpp can read the OBJ_3 position
XMFLOAT3 SwitchLight_GetLightObjPosition(void)
{
	if (g_LightObjIndex >= 0)
	{
		std::vector<MAPDATA>& map = GetFieldMap();
		if (g_LightObjIndex < (int)map.size())
		{
			return map[g_LightObjIndex].pos;
		}
	}
	return XMFLOAT3(0, 0, 0);
}
