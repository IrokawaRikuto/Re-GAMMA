//Result.cpp
#include	"Manager.h"
#include	"sprite.h"
#include	"keyboard.h"
#include	"Result.h"
#include "fade.h"
#include "shader.h"
#include"newKeyBind.h"
#include "mouse.h"
#include "Input.h"

//=========================================================================================================
//�O���[�o���ϐ�
//=========================================================================================================
static	ID3D11ShaderResourceView* g_Texture = NULL;	//�e�N�X�`���P����\���I�u�W�F�N�g
static ID3D11Device* g_pDevice = nullptr;
static ID3D11DeviceContext* g_pContext = nullptr;

//=========================================================================================================
//����������
//=========================================================================================================
void Result_Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	g_pDevice = pDevice;
	g_pContext = pContext;

	TexMetadata		metadata;
	ScratchImage	image;
	LoadFromWICFile(L"asset\\texture\\UI\\game_clear.png", WIC_FLAGS_NONE, &metadata, image);
	CreateShaderResourceView(pDevice, image.GetImages(), image.GetImageCount(), metadata, &g_Texture);
	assert(g_Texture);

	//�t�F�[�h�C���̃Z�b�g
	XMFLOAT4	color = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	SetFade(60.0f, color, FADE_IN, SCENE_GAME);

}

//=========================================================================================================
//�I������
//=========================================================================================================
void Result_Finalize()
{
	//�e�N�X�`���̉���Ȃ�
	SAFE_RELEASE(g_Texture);

}
//=========================================================================================================
//�X�V����
//=========================================================================================================
void Result_Update()
{
	if (GetFadeState() != FADE_NONE) return;

	bool advance = IsInputTrigger(EnterKey, gPad)
	             || IsInputTrigger(JumpKey, gPad)
	             || IsInputTrigger(ActionKey, gPad)
	             || IsInputTrigger(MenuKey, gPad)
	             || Keyboard_IsKeyDownTrigger(KK_SPACE);

	Mouse_State ms{};
	Mouse_PeekState(&ms);
	static bool s_PrevLeft = false;
	if (ms.leftButton && !s_PrevLeft) advance = true;
	s_PrevLeft = ms.leftButton;

	if (advance)
	{
		XMFLOAT4 color(0.0f, 0.0f, 0.0f, 1.0f);
		SetFade(40, color, FADE_OUT, SCENE_TITLE);
	}
}

//=========================================================================================================
//�`�揈��
//=========================================================================================================
void Result_Draw()
{
	// �V�F�[�_�[��`��p�C�v���C���ɐݒ�
	Shader_Begin();

	// ��ʃT�C�Y�擾
	const float SCREEN_WIDTH = (float)Direct3D_GetBackBufferWidth();
	const float SCREEN_HEIGHT = (float)Direct3D_GetBackBufferHeight();

	// ���_�V�F�[�_�[�ɕϊ��s���ݒ�
	Shader_SetMatrix(XMMatrixOrthographicOffCenterLH(
		0.0f,
		SCREEN_WIDTH,
		SCREEN_HEIGHT,
		0.0f,
		0.0f,
		1.0f));
	//---------------------------------------------------


		//�e�N�X�`�����Z�b�g
	g_pContext->PSSetShaderResources(0, 1, &g_Texture);//g_Texture���g���悤�ɐݒ肷��

	static XMFLOAT2 texcoord = { 0.0f, 0.0f };

	//�X�v���C�g�`��
	SetBlendState(BLENDSTATE_NONE);//�u�����h����
	XMFLOAT4 col = { 1.0f, 1.0f, 1.0f, 1.0f };	//�X�v���C�g�̐F
	XMFLOAT2 pos = { SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 };
	XMFLOAT2 size = { SCREEN_WIDTH, SCREEN_HEIGHT };
	DrawSprite(pos, size, col);//1���G��\��

}


