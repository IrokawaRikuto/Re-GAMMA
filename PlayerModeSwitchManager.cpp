//PlayerModeSwitchManager.cpp
#include "PlayerModeSwitchManager.h"
#include <cfloat>

#include "field.h"
#include "collision.h"
#include "player3D.h"
#include "Player2D.h"
#include "keyboard.h"
#include "UtilMath.h"
#include "manager.h"
using namespace mu;

#include "debug.h"
#include "camera.h"

#include "Bill_Board.h"
#include "fade.h"

//=========================================================================================================
// デバッグ
//=========================================================================================================

static bool debugMode;

//=========================================================================================================
// 定数（切り替えパラメータ）
//=========================================================================================================

static const float kSwitchMaxDist = 2.0f;   // 3D→2D: 前方レイの最大距離
static const float kPlayer2DThickness = 0.05f;  // 2Dプレイヤーの壁からの押し出し厚み
static const float kWallOffset = 0.02f;  // 壁面に少し浮かせる
static const float kTo3DFrontOffset = -1.0f;  // 2D→3D: 壁から前方へ戻す量（符号は現状仕様に合わせる）

static const float kGroundSearchUp = 3.0f;   // 2D→3D: 着地地点の探索開始Y（上方向）
static const float kGroundSearchDown = 10.0f;  // 2D→3D: 探索の下方向距離

static bool g_ShowBillBoard = false;
static XMFLOAT3 g_BillBoardPosition = { 0.0f, 0.0f, 0.0f };

static ID3D11Device* g_pDevice = nullptr;
static ID3D11DeviceContext* g_pContext = nullptr;
static ID3D11ShaderResourceView* g_BillBoardTexture = nullptr;

PLAYER* p = GetPlayer3D();


//=========================================================================================================
// 状態
//=========================================================================================================

static PLAYER_MODE g_Mode = MODE_3D;

// 3D⇔2D切り替えキー
static const auto TABKey = KK_TAB;

//=========================================================================================================
// 内部ユーティリティ：壁判定
//=========================================================================================================

// 2D化できる「壁」かどうか
static bool Field_IsWall(FIELD t)
{
	if (GetScene() == SCENE_GAME)
	{
		switch (t)
		{
		case FIELD_OBJ_1:
		case FIELD_WALL:
			return true;
		default:
			return false;
		}
	}
	return false;
}

//=========================================================================================================
// 内部ユーティリティ：Raycast（AABB/OBB）+ 面情報
//=========================================================================================================

// ローカルAABBへのレイ判定（どの面に当たったかも返す）
static bool RaycastAABB_Face(
	const XMFLOAT3& rayO,
	const XMFLOAT3& rayD_in,
	const XMFLOAT3& boxC,
	const XMFLOAT3& boxHalf,
	float maxDist,
	BOX_FACE* outFace,
	XMFLOAT3* outNormal,
	float* outT)
{
	const XMFLOAT3 rayD = Normalize(rayD_in);

	float tmin = 0.0f;
	float tmax = maxDist;

	BOX_FACE hitFace = FACE_NONE;
	XMFLOAT3 hitN = { 0,0,0 };

	auto slab = [&](float ro, float rd, float c, float h,
		BOX_FACE negFace, BOX_FACE posFace,
		XMFLOAT3 nNeg, XMFLOAT3 nPos) -> bool
		{
			if (fabsf(rd) < 1e-6f)
				return (ro >= c - h && ro <= c + h);

			float inv = 1.0f / rd;
			float t1 = (c - h - ro) * inv;
			float t2 = (c + h - ro) * inv;

			BOX_FACE f1 = negFace;
			BOX_FACE f2 = posFace;
			XMFLOAT3 n1 = nNeg;
			XMFLOAT3 n2 = nPos;

			if (t1 > t2)
			{
				std::swap(t1, t2);
				std::swap(f1, f2);
				std::swap(n1, n2);
			}

			if (t1 > tmin)
			{
				tmin = t1;
				hitFace = f1;
				hitN = n1;
			}
			if (t2 < tmax) tmax = t2;

			return tmin <= tmax;
		};

	if (!slab(rayO.x, rayD.x, boxC.x, boxHalf.x, FACE_NEG_X, FACE_POS_X, { -1,0,0 }, { 1,0,0 })) return false;
	if (!slab(rayO.y, rayD.y, boxC.y, boxHalf.y, FACE_NEG_Y, FACE_POS_Y, { 0,-1,0 }, { 0,1,0 })) return false;
	if (!slab(rayO.z, rayD.z, boxC.z, boxHalf.z, FACE_NEG_Z, FACE_POS_Z, { 0,0,-1 }, { 0,0,1 })) return false;

	if (tmin < 0.0f || tmin > maxDist) return false;

	if (outFace)   *outFace = hitFace;
	if (outNormal) *outNormal = hitN;
	if (outT)      *outT = tmin;
	return true;
}

// 任意回転OBBへのレイ判定（当たり面も返す）
// ※Field_GetCollisionHalfSize は field.cpp 側の既存実装をそのまま使用
static bool RaycastOBB_Face(
	const XMFLOAT3& rayO,
	const XMFLOAT3& rayD_in,
	const MAPDATA& box,
	float maxDist,
	BOX_FACE* outFace,
	XMFLOAT3* outNormalW,
	float* outT)
{
	const XMFLOAT3 half = Field_GetCollisionHalfSize(box);

	const XMMATRIX R = XMMatrixRotationRollPitchYaw(
		XMConvertToRadians(box.rotate.x),
		XMConvertToRadians(box.rotate.y),
		XMConvertToRadians(box.rotate.z));
	const XMMATRIX invR = XMMatrixTranspose(R);

	XMVECTOR O = XMLoadFloat3(&rayO);
	XMVECTOR D = XMLoadFloat3(&rayD_in);
	D = XMVector3Normalize(D);

	XMVECTOR C = XMLoadFloat3(&box.pos);

	XMVECTOR Orel = O - C;
	XMVECTOR OlocV = XMVector3TransformNormal(Orel, invR);
	XMVECTOR DlocV = XMVector3TransformNormal(D, invR);

	XMFLOAT3 Oloc{}, Dloc{};
	XMStoreFloat3(&Oloc, OlocV);
	XMStoreFloat3(&Dloc, DlocV);

	BOX_FACE faceL = FACE_NONE;
	XMFLOAT3 nL = { 0,0,0 };
	float t = 0.0f;

	if (!RaycastAABB_Face(Oloc, Dloc, XMFLOAT3(0, 0, 0), half, maxDist, &faceL, &nL, &t))
		return false;

	XMVECTOR nW = XMVector3TransformNormal(XMLoadFloat3(&nL), R);
	nW = XMVector3Normalize(nW);

	if (outFace) *outFace = faceL;
	if (outNormalW) XMStoreFloat3(outNormalW, nW);
	if (outT) *outT = t;
	return true;
}

//=========================================================================================================
// 内部ユーティリティ：壁面→2D回転/位置
//=========================================================================================================

// 壁面に応じたYawオフセット
static float FaceYawDeg(BOX_FACE face)
{
	switch (face)
	{
	case FACE_POS_Z: return 0.0f;
	case FACE_NEG_Z: return 180.0f;
	case FACE_POS_X: return 90.0f;
	case FACE_NEG_X: return -90.0f;
	default:         return 0.0f;
	}
}

// 壁面に貼り付く2Dプレイヤーの回転（Yawのみ）を作る
static XMFLOAT3 Calc2DRotationFromFace(const MAPDATA& box, BOX_FACE face)
{
	// +180 は「モデル正面/2D正面」の合わせ込み（現状仕様）
	const float yawDeg = box.rotate.y + FaceYawDeg(face) + 180.0f;
	return { 0.0f, yawDeg, 0.0f };
}

// 3Dプレイヤー位置を基準に、壁面上の2D位置を計算する
static XMFLOAT3 Calc2DPositionOnWallSurface(
	const MAPDATA& box,
	BOX_FACE face,
	const XMFLOAT3& p3Pos,
	const XMFLOAT3& faceNormalW)
{
	const XMFLOAT3 half = Field_GetCollisionHalfSize(box);

	const XMMATRIX R = XMMatrixRotationRollPitchYaw(
		XMConvertToRadians(box.rotate.x),
		XMConvertToRadians(box.rotate.y),
		XMConvertToRadians(box.rotate.z));
	const XMMATRIX invR = XMMatrixTranspose(R);

	XMVECTOR C = XMLoadFloat3(&box.pos);
	XMVECTOR P = XMLoadFloat3(&p3Pos);

	// ワールド→ローカル
	XMVECTOR PlocV = XMVector3TransformNormal(P - C, invR);
	XMFLOAT3 local{};
	XMStoreFloat3(&local, PlocV);

	// 壁面から少し浮かせる（厚み + 誤差対策）
	const float offset = kPlayer2DThickness + kWallOffset;

	switch (face)
	{
	case FACE_POS_X: local.x = half.x + offset;  break;
	case FACE_NEG_X: local.x = -half.x - offset; break;
	case FACE_POS_Z: local.z = half.z + offset;  break;
	case FACE_NEG_Z: local.z = -half.z - offset; break;
	case FACE_POS_Y: local.y = half.y + offset;  break;
	case FACE_NEG_Y: local.y = -half.y - offset; break;
	default: break;
	}

	// ローカル→ワールド
	XMVECTOR L = XMLoadFloat3(&local);
	XMVECTOR Pw = XMVector3TransformNormal(L, R) + C;

	XMFLOAT3 out{};
	XMStoreFloat3(&out, Pw);

	// 壁法線がほぼ水平（側面）なら、Yは3DのYを維持して「高さが飛ぶ」のを抑える
	const float kKeepYWhenWallNY = 0.20f;
	if (fabsf(faceNormalW.y) < kKeepYWhenWallNY)
		out.y = p3Pos.y;

	return out;
}

//=========================================================================================================
// 内部ユーティリティ：2D→3D復帰時の着地点探索
//=========================================================================================================

// 指定XZにおける「地面上面Y」を探索する（AABB上面）
// startY から maxDown の範囲で、最も高い上面を探す
static bool FindGroundTopY_OnXZ(float x, float z, float startY, float maxDown, float* outTopY)
{
	std::vector<MAPDATA>& map = GetFieldMap();
	if (map.empty()) return false;

	bool found = false;
	float bestTop = -FLT_MAX;

	for (size_t i = 0; i < map.size(); ++i)
	{
		const MAPDATA& m = map[i];

		const XMFLOAT3 half = Field_GetCollisionHalfSize(m);
		if (x < m.pos.x - half.x || x > m.pos.x + half.x) continue;
		if (z < m.pos.z - half.z || z > m.pos.z + half.z) continue;

		const float top = m.pos.y + half.y;

		if (top <= startY && top >= startY - maxDown)
		{
			if (top > bestTop) { bestTop = top; found = true; }
		}
	}

	if (found && outTopY) *outTopY = bestTop;
	return found;
}

//=========================================================================================================
// 内部ユーティリティ：3D前方の切り替え対象取得
//=========================================================================================================

static bool GetSwitchTargetFromPlayer3D(SWITCH_TARGET* outT)
{
	PLAYER* p3 = GetPlayer3D();
	if (!p3) return false;

	const XMFLOAT3 rayO = p3->Position;
	const XMFLOAT3 rayD = Player3D_GetForward();

	std::vector<MAPDATA>& map = GetFieldMap();
	if (map.empty()) return false;

	float bestT = FLT_MAX;
	SWITCH_TARGET best{};

	for (size_t i = 0; i < map.size(); ++i)
	{
		const MAPDATA& m = map[i];
		if (!Field_IsWall(m.no)) continue;

		BOX_FACE face{};
		XMFLOAT3 normal{};
		float t = 0.0f;

		if (RaycastOBB_Face(rayO, rayD, m, kSwitchMaxDist, &face, &normal, &t))
		{
			if (t < bestT)
			{
				bestT = t;
				best.fieldIndex = (int)i;
				best.face = face;
				best.normal = normal;
			}
		}
	}

	if (best.fieldIndex < 0) return false;
	if (outT) *outT = best;
	return true;
}

//=========================================================================================================
// 切り替え本体：3D→2D
//=========================================================================================================

static bool TrySwitch3DTo2D()
{
	SWITCH_TARGET tgt;
	if (!GetSwitchTargetFromPlayer3D(&tgt))
	{
		if (debugMode)
		{
			ImGui::Begin("Debug - han");
			if (ImGui::TreeNode("SwitchFail"))
			{
				ImGui::Text("No wall found in front");
				ImGui::TreePop();
			}
			ImGui::End();
		}
		return false;
	}

	std::vector<MAPDATA>& map = GetFieldMap();
	const MAPDATA& box = map[tgt.fieldIndex];

	PLAYER* p3 = GetPlayer3D();

	// 壁面上の2D初期位置/回転
	const XMFLOAT3 p2Pos = Calc2DPositionOnWallSurface(box, tgt.face, p3->Position, tgt.normal);
	const XMFLOAT3 p2Rot = Calc2DRotationFromFace(box, tgt.face);

	if (debugMode)
	{
		ImGui::Begin("Debug - han");
		if (ImGui::TreeNode("SwitchOK"))
		{
			ImGui::Text("Hit fieldIndex: %d", tgt.fieldIndex);
			ImGui::Text("Hit face: %d", (int)tgt.face);
			ImGui::Text("3D pos: (%.2f, %.2f, %.2f)", p3->Position.x, p3->Position.y, p3->Position.z);
			ImGui::Text("2D pos: (%.2f, %.2f, %.2f)", p2Pos.x, p2Pos.y, p2Pos.z);
			ImGui::Text("2D rot: (%.2f, %.2f, %.2f)", p2Rot.x, p2Rot.y, p2Rot.z);
			ImGui::TreePop();
		}
		ImGui::End();
	}

	Player2D_InitAt(p2Pos, p2Rot);
	Player2D_SetActive(true);
	Player3D_SetActive(false);

	g_Mode = MODE_2D;
	return true;
}

//=========================================================================================================
// 切り替え本体：2D→3D
//=========================================================================================================

static bool TrySwitch2DTo3D()
{
	PLAYER* p2 = GetPlayer2D();
	if (!p2) return false;

	// 2Dプレイヤーの向き（Yaw）から前方を計算
	const float yawRad = XMConvertToRadians(p2->Rotation.y);
	const XMFLOAT3 fwd = { sinf(yawRad), 0.0f, cosf(yawRad) };

	// 壁から少し前方（3D側）に戻す
	const XMFLOAT3 landing = p2->Position + fwd * kTo3DFrontOffset;

	// 着地Yを探索
	const float startY = p2->Position.y + kGroundSearchUp;

	float topY = 0.0f;
	if (!FindGroundTopY_OnXZ(landing.x, landing.z, startY, kGroundSearchDown, &topY))
		return false;

	// 3Dプレイヤーは中心Y = 地面上面 + 半径Y
	const XMFLOAT3 p3Half = Player3D_GetSolidHalfSize();
	const XMFLOAT3 p3Pos = { landing.x, topY + p3Half.y, landing.z };
	const XMFLOAT3 p3Rot = { 0.0f, p2->Rotation.y, 0.0f };

	Player3D_InitAt(p3Pos, p3Rot);
	Player3D_SetActive(true);

	Player2D_SetActive(false);
	Player2D_Uninit();

	// 2Dカメラ状態をクリア（2D→3D復帰で角度を初期化）
	Camera_Reset2DState();

	g_Mode = MODE_3D;
	return true;
}

//=========================================================================================================
// 外部公開：初期化 / 状態取得 / 更新
//=========================================================================================================

void PlayerModeSwitchManager_Init()
{
	g_Mode = MODE_3D;	

	g_pDevice = Direct3D_GetDevice();

	TexMetadata metadata;
	ScratchImage image;
	// Use a texture for the mode switch prompt (change path to your actual texture)
	LoadFromWICFile(L"asset\\texture\\UI\\Keyboard\\KeyboardChange.png", WIC_FLAGS_NONE, &metadata, image);
	CreateShaderResourceView(g_pDevice, image.GetImages(), image.GetImageCount(), metadata, &g_BillBoardTexture);

	g_ShowBillBoard = false;
}

void PlayerModeSwitchManager_Finalize()
{
	SAFE_RELEASE(g_BillBoardTexture);
	g_ShowBillBoard = false;
}

PLAYER_MODE PlayerModeSwitchManager_GetMode()
{
	return g_Mode;
}

void PlayerModeSwitchManager_ResetMode()
{
	g_Mode = MODE_3D;
}

// [FEAT] Auto-revert to 3D from shadow mode when no platform supports
// the 2D player (e.g., fell off into wallless space). Mirrors the
// shutdown half of TrySwitch2DTo3D but skips the "find a 3D landing"
// step: just respawn the 3D player at the map start position.
void PlayerModeSwitchManager_ForceTo3D()
{
	if (g_Mode == MODE_3D) return;

	XMFLOAT3 spawn = Field_GetPlayerStartPosition();
	XMFLOAT3 rot = { 0.0f, 0.0f, 0.0f };
	if (PLAYER* p2 = GetPlayer2D()) rot.y = p2->Rotation.y;

	XMFLOAT3 half = Player3D_GetSolidHalfSize();
	XMFLOAT3 p3Pos = { spawn.x, spawn.y + half.y, spawn.z };

	Player3D_InitAt(p3Pos, rot);
	Player3D_SetActive(true);

	// [FIX] Clear dash flag on auto-revert so a Shift-held fall in
	// shadow mode does not carry the dash state into the respawned
	// Player3D. Same root cause as Player3D_Respawn -- state machine
	// does not pass through PLAYER_STATE_DASH branch on respawn so the
	// release check that normally clears isDash never runs.
	if (PLAYER* p3 = GetPlayer3D())
	{
		p3->isDash = false;
		p3->maxMoveSpeed = p3->FirstMaxMoveSpeed;
	}

	Player2D_SetActive(false);
	Player2D_Uninit();

	Camera_Reset2DState();

	// [FIX] Falls/auto-reverts should also restore stage object positions
	// (the box/seesaw the player was puzzling with). Without this they
	// stay wherever they ended up before the fall, so a retry attempt is
	// in a different state than the original.
	Field_ReloadCurrentMap();
	Collision_ResetShadowContactState();

	g_Mode = MODE_3D;
}

void PlayerModeSwitchManager_Update()
{
	if (debugMode)
	{
		ImGui::Begin("Debug - han");
		if (ImGui::TreeNode("PlSwitch.cpp"))
		{
			ImGui::Text("PlayerMode : %s", (g_Mode == MODE_3D) ? "3D" : "2D");
			ImGui::TreePop();
		}
		ImGui::End();
	}

	// [FIX] Allow mode switching only during in-game stages
	// (block title, stage select, result). Also block during any
	// active fade: after F is pressed on a stage trigger, SetCurrentStage
	// flips to STAGE_X immediately while fade-out is still running, which
	// previously let the prompt flash in for ~60 frames.
	if (GetScene() != SCENE_GAME
		|| GetCurrentStage() == STAGE_SELECT
		|| GetFadeState() != FADE_NONE)
	{
		g_ShowBillBoard = false;
		return;
	}

	if (g_Mode == MODE_3D)
	{
		SWITCH_TARGET tgt;
		if (GetSwitchTargetFromPlayer3D(&tgt))
		{
			g_ShowBillBoard = true;

			PLAYER* p3 = GetPlayer3D();
			if (p3)
			{
				XMFLOAT3 halfSize = Player3D_GetSolidHalfSize();
				g_BillBoardPosition = XMFLOAT3(
					p3->Position.x,
					p3->Position.y + 2.0f,
					p3->Position.z
				);
			}
		}
		else
		{
			g_ShowBillBoard = false;
		}
	}
	else
	{
		PLAYER* p2 = GetPlayer2D();
		if (p2)
		{
			g_ShowBillBoard = true;
			g_BillBoardPosition = XMFLOAT3(
				p2->Position.x,
				p2->Position.y + 0.5f,
				p2->Position.z
			);
		}
		else
		{
			g_ShowBillBoard = false;
		}
	}

	//test
	// タイトルシーン中は Player3D::isTitleScene フラグで切り替えを禁止（早期リターン）
	PLAYER* p3 = GetPlayer3D();
	if (p3 && p3->isTitleScene)
	{
		// 必要ならデバッグ出力
		DEBUG_IMGUI_BEGIN({
			ImGui::Begin("Debug - PlSwitch");
			ImGui::Text("Mode switch blocked: Player3D::isTitleScene == true");
			ImGui::End();
			});
		return;
	}

	// 切り替えキー（このファイルでは TABKey を使用）
	if (!IsInputTrigger(ChangeKey, gPad)) return;

	if (g_Mode == MODE_3D)
		TrySwitch3DTo2D();
	else
		TrySwitch2DTo3D();

}

void PlayerModeSwitchManager_Draw()
{
	if (!g_ShowBillBoard || !g_BillBoardTexture) return;

	g_pContext = Direct3D_GetDeviceContext();

	Shader_Begin();
	// [FIX] Force deterministic draw state. Inheriting from Player3D_Draw
	// made this billboard flicker once Option / other paths perturbed
	// blend / depth.
	SetBlendState(BLENDSTATE_ALFA);
	SetDepthTest(TRUE);

	g_pContext->PSSetShaderResources(0, 1, &g_BillBoardTexture);

	XMFLOAT2 size = { 2.0f, 2.0f };  // Billboard size
	XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

	DrawBillBoard(g_BillBoardPosition, size, color, 0, 1, 1);
}

bool PlayerModeSwitchManager_ShouldShowBillBoard()
{
	return g_ShowBillBoard;
}

XMFLOAT3 PlayerModeSwitchManager_GetBillBoardPosition()
{
	return g_BillBoardPosition;
}