//PlayerStatus.h
#pragma once

#include "Keyboard.h"
#include "controller.h"
#include "Input.h"
#include "model.h"
#include "direct3d.h"

// �R���g���[���[
extern Controller gPad;

enum PLAYER_ANIM {
	PLAYER_ANIM_IDLE = 0,
	PLAYER_ANIM_WALK,
	PLAYER_ANIM_DASH,
	PLAYER_ANIM_JUMP,
	PLAYER_ANIM_PUSH,

	PLAYER_ANIM_MAX
};

// �v���C���[�X�e�[�g
enum PLAYER_STATE
{
	PLAYER_STATE_IDLE = 0,		//�������Ȃ�
	PLAYER_STATE_MOVE,			//�ړ���
	PLAYER_STATE_FALL,			//������
	PLAYER_STATE_JUMP,			//�㏸��
	PLAYER_STATE_DASH,			//�_�b�V����
	PLAYER_STATE_RESPAWN,		//�ϐg��
	PLAYER_STATE_ACTION,		//�A�N�V������(3D����)
	PLAYER_STATE_AUTO_WALK,		// �^�C�g����ʂ̎������s

	PLAYER_STATE_MAX,
};


// �����ʒu
class PLAYER
{
public:
	XMFLOAT3		Firstposition;
	XMFLOAT3		FirstRotation;
	XMFLOAT3		FirstScaling;
	XMFLOAT3		FirstVelocity;
	XMFLOAT3		FirstAcceleration;
	PLAYER_STATE	FirstState;
	PLAYER_ANIM		FirstAnim;
	float			FirstStopTime;
	XMVECTOR		FirstQuaternion;

	XMFLOAT3 Position;			//���W
	XMFLOAT3 Rotation;			//��]
	XMFLOAT3 Scaling;			//�T�C�Y
	XMFLOAT3 Velocity;			//����
	XMFLOAT3 Acceleration;		//�����x
	PLAYER_STATE state;			//���
	PLAYER_ANIM CurrentAnimIndex;
	MODEL* Model[PLAYER_ANIM_MAX];	//���f���f�[�^
	XMVECTOR Quaternion;		//�N�H�[�^�j�I����]

	// �v���C���[�X�e�[�^�X
	float moveSpeed = 0.009f;			//�ړ����x
	float maxMoveSpeed = 0.5f;			//�ő�ړ����x
	float maxFallSpeed = -0.5f;			//�ő嗎�����x
	float dampingXZ = 0.925f;			//���C�W��
	float gravityPower = -1.0f;			//�d�͉����x�i������������g���\��j
	float jumpPower = 0.175f;			//�W�����v��
	float dashMoveSpeed = 2.0f;			//�_�b�V���ړ����x�{��	
	bool isGround = false;				//�ڒn����
	int  coyoteFrames = 0;
	bool isDash = false;				//�_�b�V������
	bool isPushing = false;
	bool isAuto = false;
	bool Active = true;
	float FirstMaxMoveSpeed = maxMoveSpeed;

	bool fountainJumped = false;

	// �v���C���[�ϐg�t���O
	bool isChange = false;
	//  true: 2D���
	//  false:3D���

	bool isTitleScene = false;

	bool blockMovement = false; // �ړ��֎~�t���O

	bool ControllerMode = false;

};

struct InputKey
{
	Keyboard_Keys keyboard; // KK_*
	WORD          gamepad;  // XINPUT_GAMEPAD_*
};

