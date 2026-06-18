//PlayerModeSwitchManager.h
#pragma once
#include <DirectXMath.h>

// プレイヤーモード
enum PLAYER_MODE
{
	MODE_3D = 0,
	MODE_2D
};

// ボックスの面
enum BOX_FACE
{
	FACE_NONE = 0,
	FACE_POS_X, FACE_NEG_X,
	FACE_POS_Z, FACE_NEG_Z,
	FACE_POS_Y, FACE_NEG_Y
};

// モード切り替えターゲット情報
struct SWITCH_TARGET
{
	int     fieldIndex = -1;
	BOX_FACE face = FACE_NONE;
	DirectX::XMFLOAT3 normal = { 0,0,0 };
};

void PlayerModeSwitchManager_Init();// 初期化
void PlayerModeSwitchManager_Finalize();
void PlayerModeSwitchManager_Update();// 更新
void PlayerModeSwitchManager_Draw();
PLAYER_MODE PlayerModeSwitchManager_GetMode();// モード取得
void PlayerModeSwitchManager_ResetMode();  // Force mode back to MODE_3D
// Force-deactivate Player2D and respawn Player3D at its start position.
// Use when the 2D player has fallen off the supported shadow area so the
// game does not soft-lock with the player in shadow space with no platform.
void PlayerModeSwitchManager_ForceTo3D();

bool PlayerModeSwitchManager_ShouldShowBillBoard();
DirectX::XMFLOAT3 PlayerModeSwitchManager_GetBillBoardPosition();
