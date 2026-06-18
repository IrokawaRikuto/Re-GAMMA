//KeyBind.cpp
#include "newKeyBind.h"
#include "Controller.h" // �K�v�Ȓ�`���C���N���[�h

InputKey UpKey = { KK_W,			XINPUT_GAMEPAD_DPAD_UP };	//�O�i
InputKey RightKey = { KK_D,			XINPUT_GAMEPAD_DPAD_RIGHT };//�E�ړ�
InputKey DownKey = { KK_S,			XINPUT_GAMEPAD_DPAD_DOWN };	//���
InputKey LeftKey = { KK_A,			XINPUT_GAMEPAD_DPAD_LEFT };	//���ړ�

// Switch-layout pad mapping (user spec): A = Action, B = Jump, Y = Dash,
// L-stick = move, D-pad = selection. Y was previously the light-toggle slot;
// it has been remapped to X so Dash can take Y.
InputKey JumpKey = { KK_SPACE,		XINPUT_GAMEPAD_B };			//�W�����v
InputKey DashKey = { KK_LEFTSHIFT,	XINPUT_GAMEPAD_Y };			//�_�b�V��
InputKey ActionKey = { KK_F,			XINPUT_GAMEPAD_A };			//�A�N�V����
InputKey ChangeKey = { KK_F,			XINPUT_GAMEPAD_A };			//�e�ϐg

InputKey ResetKey = { KK_R,			XINPUT_GAMEPAD_BACK };		//���Z�b�g
InputKey MenuKey = { KK_ESCAPE,	XINPUT_GAMEPAD_START };		//���j���[
InputKey LightKey = { KK_TAB,	XINPUT_GAMEPAD_X };

InputKey EnterKey = { KK_ENTER,	XINPUT_GAMEPAD_A };		//����

//�����Ă��鎞
bool IsInputPress(const InputKey& key, const Controller& pad)
{
	// �L�[�{�[�h
	if (key.keyboard != KK_NONE)
	{
		if (Keyboard_IsKeyDown(key.keyboard))
			return true;
	}

	// �R���g���[���[
	if (key.gamepad != 0 && pad.IsConnected())
	{
		if (pad.IsButtonPressed(key.gamepad))
			return true;
	}

	return false;
}

//�����ꂽ��
bool IsInputTrigger(const InputKey& key, const Controller& pad)
{
	// �L�[�{�[�h
	if (key.keyboard != KK_NONE)
	{
		if (Keyboard_IsKeyDownTrigger(key.keyboard))
			return true;
	}
	// �R���g���[���[
	if (key.gamepad != 0 && pad.IsConnected())
	{
		if (pad.IsButtonTrigger(key.gamepad))
			return true;
	}
	return false;
}

//�������Ƃ�
bool IsInputUp(const InputKey& key, const Controller& pad)
{
	// �L�[�{�[�h
	if (key.keyboard != KK_NONE)
	{
		if (!Keyboard_IsKeyUp(key.keyboard))
			return true;
	}
	// �R���g���[���[
	if (key.gamepad != 0 && pad.IsConnected())
	{
		if (pad.IsButtonReleased(key.gamepad))
			return true;
	}
	return false;
}