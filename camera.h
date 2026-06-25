// camera.h
#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include "direct3d.h"

//=========================================================================================================
// �J�����\���́i�V���v���ȃr���[�E�v���W�F�N�V�����Ǘ��j
// �� �{�v���W�F�N�g�� C API ���̊֐��Q�� CameraObject �𑀍삷��݌v
//=========================================================================================================
class CAMERA
{
public:
	DirectX::XMFLOAT3 Position;     // �J�����ʒu�iWorld�j
	DirectX::XMFLOAT3 AtPosition;   // �����_�iWorld�j
	DirectX::XMFLOAT3 UpVector;     // ������x�N�g���iWorld�j

	DirectX::XMMATRIX View;         // �r���[�s��
	DirectX::XMMATRIX Projection;   // �v���W�F�N�V�����s��

	float Fov;                      // ����p�ideg�j
	float Aspect;                   // �A�X�y�N�g��
	float NearClip;                 // �߃N���b�v
	float FarClip;                  // ���N���b�v
};

//=========================================================================================================
// ���C�t�T�C�N��
//=========================================================================================================
void Camera_Initialize();
void Camera_Finalize();

//=========================================================================================================
// �X�V�i�v���C���[���_�j
//=========================================================================================================
void Player3DCamera_Update();
void Player2DCamera_Update();
void Player2DCamera_DebugUpdate();
void Title_Camera_Update();
void StageSelect_Camera_Update();
void StageSelect_Camera_Reset();
void StageSelect_Camera_BeginTransition(int frames);
void StageOne_Camera_Update();
void StageOne_Camera_Reset();
void StageOne_Camera_BeginTransition(int frames);
void StageTwo_Camera_Update();
void StageTwo_Camera_Reset();
void StageTwo_Camera_BeginTransition(int frames);
void StageThree_Camera_Update();
void StageThree_Camera_Reset();
void StageThree_Camera_BeginTransition(int frames);
void Camera_SetLightMode(bool b);
void Player3DCamera_Reset();
void LightCamera_Update();

//=========================================================================================================
// �`��i�s��X�V�j
//=========================================================================================================
void Camera_Draw();

//=========================================================================================================
// �Z�b�^�[ / �Q�b�^�[
//=========================================================================================================
void SetCameraFov(float fov);
void SetCameraAspect(float asp);
void SetCameraClip(float nearClip, float farClip);
void SetCameraPosition(DirectX::XMFLOAT3 pos);
void SetCameraAtPosition(DirectX::XMFLOAT3 atpos);
void SetCameraUpVector(DirectX::XMFLOAT3 up);

DirectX::XMMATRIX GetViewMatrix();
DirectX::XMMATRIX GetProjectionMatrix();
DirectX::XMFLOAT3 GetCameraAtPosition();
DirectX::XMFLOAT3 GetCameraPosition();

//=========================================================================================================
// 2D�J����������Ԃ̃��Z�b�g�i2D->3D�؂�ւ����Ŏg�p�j
//=========================================================================================================
void Camera_Reset2DState();

//=========================================================================================================
// �J�����Փˁi�^�[�Q�b�g->�J�����̃��C�ŕǂɓ�����ꍇ�A�J�����ʒu����O�ɋl�߂�j
//=========================================================================================================
void Camera_CheckCollision(
	DirectX::XMFLOAT3 targetPos,
	DirectX::XMFLOAT3 desiredCamPos,
	DirectX::XMFLOAT3& outCamPos
);

//=========================================================================================================
// �}�E�X���x�iYaw/Pitch�j
//=========================================================================================================
void SetCameraMouseSensitivity(float yaw, float pitch);
float GetMouseSensYaw();
float GetMouseSensPitch();

// Add alongside Camera_Reset2DState()
void Camera_ResetLightState();
float GetLightCameraYaw();

// Seed the internal Player3D camera lerp state (gCamTarget/gCamPos) from
// the current g_CameraObject. Call this when leaving light mode so the
// player camera lerps smoothly from the light camera pose instead of
// snapping to the stale pre-TAB player camera pose.
void Camera_SeedPlayer3DFromCurrent();