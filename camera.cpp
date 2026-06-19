// camera.cpp
#include "camera.h"
#include "keyboard.h"
#include "mouse.h"
#include "player3D.h"
#include "Player2D.h"
#include "field.h"
#include "UtilMath.h"
#include "LightSource.h"
#include "Switch_Light.h"
#include "Input.h" // �ǉ�: Controller �̃O���[�o�� gPad ���g��

#include <iostream>

using namespace mu;
using namespace DirectX;

//=========================================================================================================
// ������ԁi�{�t�@�C�����̂݁j
//=========================================================================================================

// �J�����{��
static CAMERA  g_CameraObject;

// �v���C���[�ʒu�̑O�t���[���ێ��i�p�r�F�����̗h��/��ԂȂǂ̊g���p�j
XMFLOAT3 g_PlayerPosOld;

// �}�E�X��ԁi�f�o�b�O�p�r/�����݊��̂��ߎc���j
Mouse_State ms{};
float cSize = 1.0f; // �J�����̊��x/�T�C�Y�����p�i����͖��g�p�C���j

//---------------------------------------------------------------------------------------------------------
// 3D�J�����i�Ǐ] + �}�E�X����j
//---------------------------------------------------------------------------------------------------------
static bool    gCamAnglesInit = false;        // �p�x�������t���O�i����͖��g�p�����݊��ŕێ��j
static XMFLOAT3 gCamTarget = { 0, 0, 0 };     // �J���������_�i��Ԍ�j
static XMFLOAT3 gCamPos = { 0, 0, 0 };     // �J�����ʒu�i��Ԍ�j

static float   gYawDeg = 180.0f;            // ������]�ideg�j
static float   gPitchDeg = 15.0f;             // ������]�ideg�j
static float   gDistance = 8.0f;              // �^�[�Q�b�g����̋���

static const float kPitchMin = -75.0f;
static const float kPitchMax = 75.0f;

static XMFLOAT3 gTargetOffset = { 0.0f, 1.2f, 0.0f }; // �v���C���[���璍���_�ւ̃I�t�Z�b�g
static float    gFollowLerp = 0.15f;               // �Ǐ]��ԁi0..1�j

// StageSelect_Camera_Update follow-X state. File-scope so a separate
// StageSelect_Camera_Reset() can drop it back to the snap path when a
// new stage loads (otherwise the camera would visibly lerp from the
// previous map's X for ~9 frames).
static float gStageSelectCamX    = 6.0f;
static bool  gStageSelectCamInit = false;
// Transition lerp counter: when > 0, lerp g_CameraObject from its current
// value toward the desired pose instead of snapping. Used to smooth the
// hand-off back from light mode (TAB exit) so the camera doesn't teleport
// from the rigid-offset light pose to the fixed stage_select pose.
static int   gStageSelectCamTransFrames = 0;

// StageOne_Camera_Update follow-Z state (rotated 90 left from stage_select:
// camera at +X side, looking toward -X wall illuminated by the light, follows
// player along Z axis).
static float gStageOneCamZ    = 12.0f;
static float gStageOneCamAtY  = 3.0f;
static bool  gStageOneCamInit = false;
static int   gStageOneCamTransFrames = 0;

// �}�E�X���x
static float g_MouseSensYaw = 1.0f;
static float g_MouseSensPitch = 1.0f;

// �R���g���[���i�E�X�e�B�b�N�j���x�i�ǉ��j
static float g_ControllerSensYaw = 2.5f;
static float g_ControllerSensPitch = 2.5f;

//---------------------------------------------------------------------------------------------------------
// 2D�J�����i�Œ�Yaw�ŒǏ]�j
//---------------------------------------------------------------------------------------------------------
static const float kCam2D_Distance = 8.0f;
static const float kCam2D_HeightOffset = 1.5f;
static const float kCam2D_LookAtYOfs = 1.0f;
static const float kCam2D_FollowLerp = 0.12f;

static bool  g_Cam2D_Initialized = false;
static float g_Cam2D_YawDeg = 0.0f;

//---------------------------------------------------------------------------------------------------------
// ���C�g�J����
//---------------------------------------------------------------------------------------------------------
static const float kCamLight_Distance = 8.0f;
static const float kCamLight_HeightOffset = 3.0f;
static const float kCamLight_LookAtYOfs = 0.0f;
static const float kCamLight_FollowLerp = 0.12f;

static bool  g_CamLight_Initialized = false;
static float g_CamLight_YawDeg = 0.0f;

// Rigid-offset light follow: capture (camera - light) offset on TAB entry so
// the camera rides with the light from whatever pose the player camera was
// just in. Preserves visual continuity (same framing, same angle, just
// follows the light instead of the player).
static bool     g_CamLight_OffsetCaptured = false;
static XMFLOAT3 g_CamLight_PosOffset      = { 0.0f, 0.0f, 0.0f };
static XMFLOAT3 g_CamLight_AtOffset       = { 0.0f, 0.0f, 0.0f };

//---------------------------------------------------------------------------------------------------------
// �J�����Փːݒ�i���C�L���X�g�j
//---------------------------------------------------------------------------------------------------------
static const float kCameraCollisionRadius = 0.1f;  // �J���������a�i�����蔻��̊g���ʁj
static const float kCameraCollisionPadding = 0.1f;  // �ǂ��痣���ǉ�����
static const float kCameraMinDistance = 1.2f;  // �Œ዗���i�߂����h�~�j

//=========================================================================================================
// ���[�e�B���e�B
//=========================================================================================================

// 3D�x�N�g���̐��`���
static XMFLOAT3 Lerp3(const XMFLOAT3& a, const XMFLOAT3& b, float t)
{
	return {
		a.x + (b.x - a.x) * t,
		a.y + (b.y - a.y) * t,
		a.z + (b.z - a.z) * t
	};
}

// Ray vs AABB�i�X���u�@�j
// outNormal: �i���ʂ̖@���i���[�J���j
// outT     : �q�b�g�����irayO + rayD * t�j
static bool RaycastAABB(
	const XMFLOAT3& rayO,
	const XMFLOAT3& rayD_in,
	const XMFLOAT3& boxC,
	const XMFLOAT3& boxHalf,
	float maxDist,
	XMFLOAT3* outNormal,
	float* outT)
{
	XMFLOAT3 rayD = mu::Normalize(rayD_in);

	float tmin = 0.0f;
	float tmax = maxDist;
	XMFLOAT3 hitN = { 0, 0, 0 };

	auto slab = [&](float ro, float rd, float c, float h, XMFLOAT3 nNeg, XMFLOAT3 nPos) -> bool
		{
			if (fabsf(rd) < 1e-6f)
			{
				// ���C�����ɕ��s�F���_���X���u���Ȃ�OK
				return (ro >= c - h && ro <= c + h);
			}

			float inv = 1.0f / rd;
			float t1 = (c - h - ro) * inv;
			float t2 = (c + h - ro) * inv;

			XMFLOAT3 n1 = nNeg;
			XMFLOAT3 n2 = nPos;

			if (t1 > t2) { std::swap(t1, t2); std::swap(n1, n2); }

			if (t1 > tmin) { tmin = t1; hitN = n1; }
			if (t2 < tmax) { tmax = t2; }

			return tmin <= tmax;
		};

	if (!slab(rayO.x, rayD.x, boxC.x, boxHalf.x, { -1, 0, 0 }, { 1, 0, 0 })) return false;
	if (!slab(rayO.y, rayD.y, boxC.y, boxHalf.y, { 0, -1, 0 }, { 0, 1, 0 })) return false;
	if (!slab(rayO.z, rayD.z, boxC.z, boxHalf.z, { 0, 0, -1 }, { 0, 0, 1 })) return false;

	if (tmin < 0.0f || tmin > maxDist) return false;

	if (outNormal) *outNormal = hitN;
	if (outT) *outT = tmin;
	return true;
}

// Ray vs OBB�iOBB�����[�J���ɗ��Ƃ���AABB�Ƃ��Ĕ���j
static bool RaycastOBB(
	const XMFLOAT3& rayO,
	const XMFLOAT3& rayD_in,
	const XMFLOAT3& boxC,
	const XMFLOAT3& boxHalf,
	const XMFLOAT3& boxRotDeg,
	float maxDist,
	XMFLOAT3* outNormalW,
	float* outT)
{
	const XMMATRIX R = XMMatrixRotationRollPitchYaw(
		XMConvertToRadians(boxRotDeg.x),
		XMConvertToRadians(boxRotDeg.y),
		XMConvertToRadians(boxRotDeg.z));

	const XMMATRIX invR = XMMatrixTranspose(R);

	XMVECTOR O = XMLoadFloat3(&rayO);
	XMVECTOR D = XMLoadFloat3(&rayD_in);
	D = XMVector3Normalize(D);
	XMVECTOR C = XMLoadFloat3(&boxC);

	XMVECTOR Orel = O - C;
	XMVECTOR OlocV = XMVector3TransformNormal(Orel, invR);
	XMVECTOR DlocV = XMVector3TransformNormal(D, invR);

	XMFLOAT3 Oloc, Dloc;
	XMStoreFloat3(&Oloc, OlocV);
	XMStoreFloat3(&Dloc, DlocV);

	XMFLOAT3 nL = { 0, 0, 0 };
	float t = 0.0f;

	if (!RaycastAABB(Oloc, Dloc, XMFLOAT3(0, 0, 0), boxHalf, maxDist, &nL, &t))
		return false;

	XMVECTOR nW = XMVector3TransformNormal(XMLoadFloat3(&nL), R);
	nW = XMVector3Normalize(nW);

	if (outNormalW) XMStoreFloat3(outNormalW, nW);
	if (outT) *outT = t;
	return true;
}

// �J�������Փ˔���ΏۂƂ���t�B�[���h���
static bool CameraShouldCollide(FIELD type)
{
	switch (type)
	{
	case FIELD_GROUND:
	case FIELD_WALL:
		return true;
	default:
		return false;
	}
}

//=========================================================================================================
// ������ / �I��
//=========================================================================================================

void Camera_Initialize()
{
	g_CameraObject.Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
	g_CameraObject.AtPosition = XMFLOAT3(0.0f, 0.0f, 0.0f);
	g_CameraObject.UpVector = XMFLOAT3(0.0f, 1.0f, 0.0f);

	g_CameraObject.Fov = 45.0f;

	float width = (float)Direct3D_GetBackBufferWidth();
	float height = (float)Direct3D_GetBackBufferHeight();
	g_CameraObject.Aspect = (height > 1e-6f) ? (width / height) : 1.0f;

	g_CameraObject.NearClip = 0.5f;
	g_CameraObject.FarClip = 1000.0f;

	g_PlayerPosOld = GetPlayer3DPosition();

	// 3D�����z�肵�A���΃��[�h����{�Ƃ���
	Mouse_SetMode(MOUSE_POSITION_MODE_RELATIVE);
}

void Camera_Finalize()
{
	// ����͓��I���\�[�X����
	return;
}

//=========================================================================================================
// �X�V
//=========================================================================================================

void Player3DCamera_Update()
{
	// [CHANGE] Mouse look + right-stick look + wheel zoom all removed.
	// The game does not need free camera rotation -- the player only ever
	// looks forward through the puzzle area -- so yaw/pitch/distance stay
	// at the values seeded by Player3DCamera_Reset (180 / 15 / 8). The
	// remaining logic just resolves a TPS pose around the player and
	// snaps on the first frame after a stage load.

	// [FIX] On first call after a stage load, snap the camera straight to the
	// desired pose (no lerp from stale STAGE_SELECT/cinematic state). Without
	// this, gCamTarget/gCamPos linger at the previous stage's pose and the
	// player camera takes ~1s to converge, which looks "broken" at the start
	// of every actual stage.
	const bool snapThisFrame = !gCamAnglesInit;

	// �^�[�Q�b�g�i�v���C���[�ʒu + �I�t�Z�b�g�j
	XMFLOAT3 playerPos = GetPlayer3DPosition();
	XMFLOAT3 desiredTarget = {
		playerPos.x + gTargetOffset.x,
		playerPos.y + gTargetOffset.y,
		playerPos.z + gTargetOffset.z
	};

	// ���ʍ��W���Ɂu�w�ʕ����v���Z�o���ăJ�����ʒu�����߂�
	float yaw = XMConvertToRadians(gYawDeg);
	float pitch = XMConvertToRadians(gPitchDeg);

	float cp = cosf(pitch), sp = sinf(pitch);
	float cy = cosf(yaw), sy = sinf(yaw);

	XMFLOAT3 back = { sy * cp, sp, cy * cp };
	XMFLOAT3 desiredPos = {
		desiredTarget.x - back.x * gDistance,
		desiredTarget.y - back.y * gDistance,
		desiredTarget.z - back.z * gDistance
	};

	// �ǂ�����ꍇ�͎�O�ɋl�߂�i�Փˉ���j
	XMFLOAT3 finalPos;
	Camera_CheckCollision(desiredTarget, desiredPos, finalPos);

	// �Ǐ]��ԁi�J�����̋}�ȗh���}����j
	if (snapThisFrame)
	{
		// First frame after stage load: skip lerp, snap to desired pose.
		gCamTarget = desiredTarget;
		gCamPos = finalPos;
		gCamAnglesInit = true;
	}
	else
	{
		gCamTarget = Lerp3(gCamTarget, desiredTarget, gFollowLerp);
		gCamPos = Lerp3(gCamPos, finalPos, gFollowLerp);
	}

	g_CameraObject.AtPosition = gCamTarget;
	g_CameraObject.Position = gCamPos;
	g_CameraObject.UpVector = { 0, 1, 0 };
}

void Player2DCamera_Update()
{
	PLAYER* p2 = GetPlayer2D();
	if (!p2) return;

	bool snapThisFrame = false;
	if (!g_Cam2D_Initialized)
	{
		g_Cam2D_YawDeg = p2->Rotation.y;
		g_Cam2D_Initialized = true;
		snapThisFrame = true;
	}

	XMFLOAT3 targetAt = {
		p2->Position.x,
		p2->Position.y + kCam2D_LookAtYOfs,
		p2->Position.z
	};

	float yawRad = XMConvertToRadians(g_Cam2D_YawDeg);
	float fwdX = sinf(yawRad);
	float fwdZ = cosf(yawRad);

	XMFLOAT3 targetPos = {
		p2->Position.x - fwdX * kCam2D_Distance,
		p2->Position.y + kCam2D_HeightOffset,
		p2->Position.z - fwdZ * kCam2D_Distance
	};

	if (snapThisFrame)
	{
		g_CameraObject.AtPosition = targetAt;
		g_CameraObject.Position   = targetPos;
	}
	else
	{
		g_CameraObject.AtPosition = Lerp3(g_CameraObject.AtPosition, targetAt, kCam2D_FollowLerp);
		g_CameraObject.Position   = Lerp3(g_CameraObject.Position,   targetPos, kCam2D_FollowLerp);
	}
	g_CameraObject.UpVector = { 0.0f, 1.0f, 0.0f };
}

//---------------------------------------------------------------------------------------------------------
// ���C�g�J�����X�V
//---------------------------------------------------------------------------------------------------------
void LightCamera_Update()
{
	// [CHANGE] Focus directly on the light source using the SAME camera
	// formula the player uses for the current stage, just substituting the
	// light's position for the player's. That keeps the visual framing
	// consistent with normal play -- camera angle, distance, follow axis --
	// while the look-at re-centers on the light. User: "光源にフォーカスして
	// 行ける範囲はプレイヤーと同じ".
	XMFLOAT3 lightPos = SwitchLight_GetLightObjPosition();
	GAME_STAGE st = GetCurrentStage();
	g_CamLight_Initialized   = true;
	g_CamLight_OffsetCaptured = true;  // keep flag set so Reset still trips it

	if (st == STAGE_1)
	{
		// StageOne formula (camera at +X side, Z follows target).
		const float kCamX = 22.0f, kCamY = 3.5f;
		const float kAtX  =  6.0f, kAtY  = 3.0f;
		const float kZMin =  4.0f, kZMax = 28.0f;
		float z = lightPos.z;
		if (z < kZMin) z = kZMin;
		if (z > kZMax) z = kZMax;
		g_CameraObject.Position   = XMFLOAT3(kCamX, kCamY, z);
		g_CameraObject.AtPosition = XMFLOAT3(kAtX,  kAtY,  z);
	}
	else if (st == STAGE_SELECT || st == STAGE_2)
	{
		// StageSelect formula (camera at -Z side, X follows target).
		const float kCamY = 3.5f, kCamZ = -7.5f;
		const float kAtY  = 3.0f, kAtZ  =  6.5f;
		const float kXMin = 4.5f, kXMax =  8.5f;
		float x = lightPos.x;
		if (x < kXMin) x = kXMin;
		if (x > kXMax) x = kXMax;
		g_CameraObject.Position   = XMFLOAT3(x, kCamY, kCamZ);
		g_CameraObject.AtPosition = XMFLOAT3(x, kAtY,  kAtZ);
	}
	else
	{
		// STAGE_3 etc: TPS-style around the light. Distance / angle match
		// the player camera defaults (yaw 180, pitch 15, distance 8).
		const float yaw   = XMConvertToRadians(180.0f);
		const float pitch = XMConvertToRadians(15.0f);
		const float dist  = 8.0f;
		const float cp = cosf(pitch), sp = sinf(pitch);
		const float cy = cosf(yaw),   sy = sinf(yaw);
		XMFLOAT3 back = { sy * cp, sp, cy * cp };
		XMFLOAT3 at   = { lightPos.x, lightPos.y + 1.2f, lightPos.z };
		g_CameraObject.Position   = XMFLOAT3(at.x - back.x * dist,
		                                      at.y - back.y * dist,
		                                      at.z - back.z * dist);
		g_CameraObject.AtPosition = at;
	}
	g_CameraObject.UpVector = { 0.0f, 1.0f, 0.0f };
}

void Camera_ResetLightState()
{
	g_CamLight_Initialized = false;
	g_CamLight_YawDeg = 0.0f;
	g_CamLight_OffsetCaptured = false;
}

// [FIX] Seed Player3D camera internal lerp state from the live g_CameraObject
// so that returning from light mode lerps smoothly out of the light camera
// pose rather than snapping to wherever gCamTarget/gCamPos were left.
void Camera_SeedPlayer3DFromCurrent()
{
	gCamTarget = g_CameraObject.AtPosition;
	gCamPos    = g_CameraObject.Position;
}

float GetLightCameraYaw()
{
	return g_CamLight_YawDeg;
}

void Player2DCamera_DebugUpdate()
{
	// �f�o�b�O�p�F3D�Ɠ�������n��2D�v���C���[��ǂ�
	Mouse_State msLocal{};
	Mouse_GetState(&msLocal);

	if (msLocal.positionMode == MOUSE_POSITION_MODE_RELATIVE)
	{
		gYawDeg += msLocal.x * 1.0f;
		gPitchDeg -= msLocal.y * 1.0f;
		if (gPitchDeg < kPitchMin) gPitchDeg = kPitchMin;
		if (gPitchDeg > kPitchMax) gPitchDeg = kPitchMax;
	}

	// �ǉ�: �f�o�b�O���͉E�X�e�B�b�N�ł�����\
	if (gPad.IsConnected())
	{
		float rsx = gPad.GetRightStickX();
		float rsy = gPad.GetRightStickY();
		const float deadzone = 0.20f;
		if (fabsf(rsx) < deadzone) rsx = 0.0f;
		if (fabsf(rsy) < deadzone) rsy = 0.0f;

		if (fabsf(rsx) > 1e-6f || fabsf(rsy) > 1e-6f)
		{
			gYawDeg += rsx * g_ControllerSensYaw;
			gPitchDeg -= rsy * g_ControllerSensPitch;
			if (gPitchDeg < kPitchMin) gPitchDeg = kPitchMin;
			if (gPitchDeg > kPitchMax) gPitchDeg = kPitchMax;
		}
	}

	XMFLOAT3 playerPos = GetPlayer2DPosition();
	XMFLOAT3 desiredTarget = {
		playerPos.x + gTargetOffset.x,
		playerPos.y + gTargetOffset.y,
		playerPos.z + gTargetOffset.z
	};

	float yaw = XMConvertToRadians(gYawDeg);
	float pitch = XMConvertToRadians(gPitchDeg);

	float cp = cosf(pitch), sp = sinf(pitch);
	float cy = cosf(yaw), sy = sinf(yaw);

	XMFLOAT3 back = { sy * cp, sp, cy * cp };
	XMFLOAT3 desiredPos = {
		desiredTarget.x - back.x * gDistance,
		desiredTarget.y - back.y * gDistance,
		desiredTarget.z - back.z * gDistance
	};

	gCamTarget = Lerp3(gCamTarget, desiredTarget, gFollowLerp);
	gCamPos = Lerp3(gCamPos, desiredPos, gFollowLerp);

	g_CameraObject.AtPosition = gCamTarget;
	g_CameraObject.Position = gCamPos;
	g_CameraObject.UpVector = { 0, 1, 0 };
}

void Title_Camera_Update()
{
	// �^�C�g���F�Œ�V�l�}�e�B�b�N
	g_CameraObject.Position = XMFLOAT3(4.0f, 3.0f, -5.0f);
	g_CameraObject.AtPosition = XMFLOAT3(4.0f, 1.0f, 0.0f);
	g_CameraObject.UpVector = XMFLOAT3(0.0f, 1.0f, 0.0f);
}

// File-scope state for camera mode (player vs light) so the same updater
// can be reused with different zoom / tracking parameters.
static bool g_LightCameraMode = false;
void Camera_SetLightMode(bool b) { g_LightCameraMode = b; }

void StageSelect_Camera_Update()
{
	// Light mode keeps the wide field overview (original framing). Player
	// mode pulls in tighter and lightly tracks player Z so the camera
	// "breathes" forward/back as the player walks into / out of the scene.
	const float kCamY     = 3.5f;
	const float kCamZ     = g_LightCameraMode ? -7.5f : -6.0f;
	const float kAtY      = 3.0f;
	const float kAtZ      = g_LightCameraMode ?  6.5f : 7.0f;
	const float kXMin     = 4.5f;
	const float kXMax     = 8.5f;
	const float kFollowK  = 0.08f;

	XMFLOAT3 playerPos = GetPlayer3DPosition();
	float targetX = playerPos.x;
	if (targetX < kXMin) targetX = kXMin;
	if (targetX > kXMax) targetX = kXMax;

	if (!gStageSelectCamInit)
	{
		gStageSelectCamX = targetX;
		gStageSelectCamInit = true;
	}
	else
	{
		gStageSelectCamX += (targetX - gStageSelectCamX) * kFollowK;
	}

	// Light mode: hold camera Z fixed. Player mode: drift the camera Z and
	// AtZ with player.z (small range) so depth movement reads on screen.
	float camZOfs = 0.0f;
	float atZOfs  = 0.0f;
	if (!g_LightCameraMode)
	{
		float pz = playerPos.z - 4.0f;  // bias around mid-stage z
		if (pz < -3.0f) pz = -3.0f;
		if (pz >  3.0f) pz =  3.0f;
		camZOfs = pz * 0.20f;
		atZOfs  = pz * 0.20f;
	}

	XMFLOAT3 desiredPos = XMFLOAT3(gStageSelectCamX, kCamY, kCamZ + camZOfs);
	XMFLOAT3 desiredAt  = XMFLOAT3(gStageSelectCamX, kAtY,  kAtZ + atZOfs);

	if (gStageSelectCamTransFrames > 0)
	{
		// Smooth hand-off from a foreign pose (e.g., the rigid-offset light
		// camera after TAB exit). Lerp toward the desired pose for a few
		// frames so the camera glides instead of teleporting.
		const float kTransLerp = 0.15f;
		g_CameraObject.Position   = Lerp3(g_CameraObject.Position,   desiredPos, kTransLerp);
		g_CameraObject.AtPosition = Lerp3(g_CameraObject.AtPosition, desiredAt,  kTransLerp);
		--gStageSelectCamTransFrames;
	}
	else
	{
		g_CameraObject.Position   = desiredPos;
		g_CameraObject.AtPosition = desiredAt;
	}
	g_CameraObject.UpVector = XMFLOAT3(0.0f, 1.0f, 0.0f);
}

// STAGE_1 camera: 90deg left of stage_select. Camera at +X side, looking
// toward the -X wall (the wall illuminated by the spotlight). Z follows the
// player along the long axis of the chamber, clamped to stage bounds.
//
// Stage extends roughly z=1..24 (24 rows tall, 18 cols wide). Camera placed
// outside the +X opening so it sees the U-shape interior. Player walks along
// Z; camera scrolls in Z to follow.
void StageOne_Camera_Update()
{
    // Light mode keeps the original wide framing. Player mode pulls the
    // camera in (smaller kCamX) and lightly tracks player.x so the depth
    // axis moves a bit as the player walks toward / away from the wall.
    const float kCamX  = g_LightCameraMode ? 22.0f : 18.0f;
    const float kCamY  = 3.5f;
    const float kAtX   = 6.0f;
    const float kAtY   = 3.0f;
    const float kZMin     = 4.0f;
    const float kZMax     = 28.0f;
    const float kFollowK  = 0.08f;

    XMFLOAT3 playerPos = GetPlayer3DPosition();
    float targetZ = playerPos.z;
    if (targetZ < kZMin) targetZ = kZMin;
    if (targetZ > kZMax) targetZ = kZMax;

    if (!gStageOneCamInit)
    {
        gStageOneCamZ = targetZ;
        gStageOneCamInit = true;
    }
    else
    {
        gStageOneCamZ += (targetZ - gStageOneCamZ) * kFollowK;
    }

    // Slight depth tracking in player mode: camera X drifts a touch as the
    // player walks toward the back wall, giving forward/back feedback.
    float camXOfs = 0.0f;
    if (!g_LightCameraMode)
    {
        float px = playerPos.x - 6.0f;
        if (px < -4.0f) px = -4.0f;
        if (px >  6.0f) px =  6.0f;
        camXOfs = px * -0.20f;  // closer when player approaches camera
    }

    // [FIX] Vertical follow: tilt the look-at toward the player's height so
    // they stay framed -- low on the floor the camera tilts down to show them,
    // high on the cliff top it tilts up. Smoothed like the Z follow.
    float targetAtY = playerPos.y + 0.8f;
    if (targetAtY < kAtY - 1.5f) targetAtY = kAtY - 1.5f;
    if (targetAtY > kAtY + 2.5f) targetAtY = kAtY + 2.5f;
    gStageOneCamAtY += (targetAtY - gStageOneCamAtY) * kFollowK;

    XMFLOAT3 desiredPos = XMFLOAT3(kCamX + camXOfs, kCamY, gStageOneCamZ);
    XMFLOAT3 desiredAt  = XMFLOAT3(kAtX,             gStageOneCamAtY,  gStageOneCamZ);

    if (gStageOneCamTransFrames > 0)
    {
        const float kTransLerp = 0.15f;
        g_CameraObject.Position   = Lerp3(g_CameraObject.Position,   desiredPos, kTransLerp);
        g_CameraObject.AtPosition = Lerp3(g_CameraObject.AtPosition, desiredAt,  kTransLerp);
        --gStageOneCamTransFrames;
    }
    else
    {
        g_CameraObject.Position   = desiredPos;
        g_CameraObject.AtPosition = desiredAt;
    }
    g_CameraObject.UpVector = XMFLOAT3(0.0f, 1.0f, 0.0f);
}

void StageOne_Camera_Reset()
{
    gStageOneCamInit = false;
    gStageOneCamZ    = 12.0f;
}

// Begin a few-frame lerp into the desired pose on the next StageXxx_Camera_Update
// calls. Use when returning from a foreign camera (e.g., light mode) so the
// transition glides instead of snapping.
void StageOne_Camera_BeginTransition(int frames)
{
    if (frames < 0) frames = 0;
    gStageOneCamTransFrames = frames;
}

void StageSelect_Camera_BeginTransition(int frames)
{
    if (frames < 0) frames = 0;
    gStageSelectCamTransFrames = frames;
}

// Drop the cached follow X so the next StageSelect_Camera_Update call snaps
// to the current player position. Use when transitioning between maps that
// use this camera so it doesn't lerp visibly from the previous stage's pose.
void StageSelect_Camera_Reset()
{
	gStageSelectCamInit = false;
	gStageSelectCamX    = 6.0f;
}

// Reset Player3D camera internal state so the next Player3DCamera_Update
// snaps to the desired pose on its first call (no lerp from stale STAGE_SELECT
// or cinematic pose). Call this when loading a fresh STAGE_X map.
void Player3DCamera_Reset()
{
	gCamAnglesInit = false;
	gYawDeg = 180.0f;
	gPitchDeg = 15.0f;
	gDistance = 8.0f;
	gCamTarget = XMFLOAT3(0.0f, 0.0f, 0.0f);
	gCamPos = XMFLOAT3(0.0f, 0.0f, 0.0f);
}

//=========================================================================================================
// �s��X�V
//=========================================================================================================

void Camera_Draw()
{
	float w = (float)Direct3D_GetBackBufferWidth();
	float h = (float)Direct3D_GetBackBufferHeight();
	if (h > 1e-6f) g_CameraObject.Aspect = w / h;

	g_CameraObject.Projection = XMMatrixPerspectiveFovLH(
		XMConvertToRadians(g_CameraObject.Fov),
		g_CameraObject.Aspect,
		g_CameraObject.NearClip,
		g_CameraObject.FarClip);

	XMVECTOR vPos = XMVectorSet(g_CameraObject.Position.x, g_CameraObject.Position.y, g_CameraObject.Position.z, 0.0f);
	XMVECTOR vAt = XMVectorSet(g_CameraObject.AtPosition.x, g_CameraObject.AtPosition.y, g_CameraObject.AtPosition.z, 0.0f);
	XMVECTOR vUp = XMVectorSet(g_CameraObject.UpVector.x, g_CameraObject.UpVector.y, g_CameraObject.UpVector.z, 0.0f);

	// [FIX] Guard against degenerate Eye == AtPosition (happens on the first frame
	// after a scene transition, before the scene's camera update has run).
	// XMMatrixLookAtLH asserts in debug and returns NaN in release for zero look
	// direction -- causing rendering glitches/flicker. Use forward as fallback.
	XMVECTOR vDir = XMVectorSubtract(vAt, vPos);
	if (XMVectorGetX(XMVector3LengthSq(vDir)) < 1e-8f)
	{
		vAt = XMVectorAdd(vPos, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
	}

	g_CameraObject.View = XMMatrixLookAtLH(vPos, vAt, vUp);
}

//=========================================================================================================
// �Z�b�^�[ / �Q�b�^�[
//=========================================================================================================

void SetCameraFov(float fov) { g_CameraObject.Fov = fov; }
void SetCameraAspect(float asp) { g_CameraObject.Aspect = asp; }
void SetCameraClip(float n, float f) { g_CameraObject.NearClip = n; g_CameraObject.FarClip = f; }
void SetCameraPosition(XMFLOAT3 pos) { g_CameraObject.Position = pos; }
void SetCameraAtPosition(XMFLOAT3 atpos) { g_CameraObject.AtPosition = atpos; }
void SetCameraUpVector(XMFLOAT3 up) { g_CameraObject.UpVector = up; }

XMMATRIX GetViewMatrix() { return g_CameraObject.View; }
XMMATRIX GetProjectionMatrix() { return g_CameraObject.Projection; }
XMFLOAT3 GetCameraAtPosition() { return g_CameraObject.AtPosition; }
XMFLOAT3 GetCameraPosition() { return g_CameraObject.Position; }

void Camera_Reset2DState()
{
	g_Cam2D_Initialized = false;
	g_Cam2D_YawDeg = 0.0f;
}

void SetCameraMouseSensitivity(float yaw, float pitch)
{
	g_MouseSensYaw = yaw;
	g_MouseSensPitch = pitch;
}

float GetMouseSensYaw() { return g_MouseSensYaw; }
float GetMouseSensPitch() { return g_MouseSensPitch; }

//=========================================================================================================
// �J�����Փ�
//=========================================================================================================

void Camera_CheckCollision(XMFLOAT3 targetPos, XMFLOAT3 desiredCamPos, XMFLOAT3& outCamPos)
{
	outCamPos = desiredCamPos;

	// ���C�i�^�[�Q�b�g -> �ړI�J�����ʒu�j
	XMFLOAT3 rayDir = {
		desiredCamPos.x - targetPos.x,
		desiredCamPos.y - targetPos.y,
		desiredCamPos.z - targetPos.z
	};

	float rayLength = sqrtf(rayDir.x * rayDir.x + rayDir.y * rayDir.y + rayDir.z * rayDir.z);
	if (rayLength < 0.001f) return;

	rayDir.x /= rayLength;
	rayDir.y /= rayLength;
	rayDir.z /= rayLength;

	std::vector<MAPDATA>& map = GetFieldMap();

	float closestHit = rayLength;
	bool  hitSomething = false;

	const float cameraRadius = kCameraCollisionRadius;

	for (size_t i = 0; i < map.size(); i++)
	{
		if (!CameraShouldCollide(map[i].no)) continue;

		// field.cpp �͐����ΏۊO�̂��߁A�����ł͊������W�b�N��ێ�
		XMFLOAT3 boxHalf;
		if (map[i].useCustomCollider) boxHalf = map[i].colliderHalf;
		else
		{
			boxHalf = XMFLOAT3(
				BOX_RADIUS * map[i].scale.x,
				BOX_RADIUS * map[i].scale.y,
				BOX_RADIUS * map[i].scale.z
			);
		}

		// �J�������a������������𑾂点��
		boxHalf.x += cameraRadius;
		boxHalf.y += cameraRadius;
		boxHalf.z += cameraRadius;

		float hitDist = 0.0f;
		XMFLOAT3 hitNormal{};

		if (RaycastOBB(targetPos, rayDir, map[i].pos, boxHalf, map[i].rotate, rayLength, &hitNormal, &hitDist))
		{
			if (hitDist < closestHit)
			{
				closestHit = hitDist;
				hitSomething = true;
			}
		}
	}

	if (hitSomething)
	{
		// �ǂ̏�����O�ɃJ�������񂹂�
		float safeDistance = closestHit - cameraRadius - kCameraCollisionPadding;
		if (safeDistance < kCameraMinDistance) safeDistance = kCameraMinDistance;

		outCamPos.x = targetPos.x + rayDir.x * safeDistance;
		outCamPos.y = targetPos.y + rayDir.y * safeDistance;
		outCamPos.z = targetPos.z + rayDir.z * safeDistance;
	}
}