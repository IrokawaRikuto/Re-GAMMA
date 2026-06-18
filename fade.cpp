//fade.cpp
#include	"fade.h"
#include	"shader.h"
#include	"PlayerModeSwitchManager.h"

//=========================================================================================================
//�\���̐錾
//=========================================================================================================
FadeObject	g_Fade;		

//=========================================================================================================
//�O���[�o���ϐ�
//=========================================================================================================
static	ID3D11ShaderResourceView* g_Texture = NULL;	//�e�N�X�`���P����\���I�u�W�F�N�g
static ID3D11Device* g_pDevice = nullptr;
static ID3D11DeviceContext* g_pContext = nullptr;


//=========================================================================================================
//����������
//=========================================================================================================
void Fade_Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	g_pDevice = pDevice;
	g_pContext = pContext;

	//�e�N�X�`���ǂݍ���
	TexMetadata		metadata;
	ScratchImage	image;
	LoadFromWICFile(L"asset\\texture\\fade.bmp", WIC_FLAGS_NONE, &metadata, image);
	CreateShaderResourceView(pDevice, image.GetImages(), image.GetImageCount(), metadata, &g_Texture);
	assert(g_Texture);//�ǂݍ��ݎ��s���Ƀ_�C�A���O��\��

	g_Fade.fadecolor.x = 0.0f;
	g_Fade.fadecolor.y = 0.0f;
	g_Fade.fadecolor.z = 0.0f;
	g_Fade.fadecolor.w = 1.0f;
	g_Fade.frame = 60.0f;	//60�t���[���Ńt�F�[�h����
	g_Fade.state = FADE_STATE::FADE_NONE;

}

//=========================================================================================================
//�I������
//=========================================================================================================
void Fade_Finalize()
{
	if (g_Texture != NULL)
	{
		g_Texture->Release();
		g_Texture = NULL;
	}
}

//=========================================================================================================
//�X�V����
//=========================================================================================================
void Fade_Update()
{
	switch (g_Fade.state)
	{
		case FADE_STATE::FADE_NONE:
			return;
		case FADE_STATE::FADE_IN:
			g_Fade.fadecolor.w -= (1.0f / g_Fade.frame);
			if (g_Fade.fadecolor.w <= 0.0f)
			{
				g_Fade.fadecolor.w = 0.0f;
				g_Fade.state = FADE_STATE::FADE_NONE;
			}
			break;
		case FADE_STATE::FADE_OUT:
			g_Fade.fadecolor.w += (1.0f / g_Fade.frame);
			// Clamp only; do not switch scene here -- the actual transition runs
			// at the end of Fade_Draw so that the next frame's Manager_Update
			// runs the new scene's update (which positions the camera) BEFORE
			// the new scene's Draw. Doing SetScene here would race the camera
			// init -> XMMatrixLookAtLH would get Eye==At and assert.
			if (g_Fade.fadecolor.w > 1.0f) g_Fade.fadecolor.w = 1.0f;
			break;
	}
}

//=========================================================================================================
// Draw (rendering only; state update is in Fade_Update)
//=========================================================================================================
void Fade_Draw()
{
	if (g_Fade.state == FADE_STATE::FADE_NONE) return;

	Shader_Begin();

	const float SCREEN_WIDTH  = (float)Direct3D_GetBackBufferWidth();
	const float SCREEN_HEIGHT = (float)Direct3D_GetBackBufferHeight();

	Shader_SetMatrix(XMMatrixOrthographicOffCenterLH(
		0.0f, SCREEN_WIDTH, SCREEN_HEIGHT, 0.0f, 0.0f, 1.0f));

	g_pContext->PSSetShaderResources(0, 1, &g_Texture);

	SetBlendState(BLENDSTATE_ALFA);
	XMFLOAT2 pos  = { SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f };
	XMFLOAT2 size = { SCREEN_WIDTH, SCREEN_HEIGHT };
	DrawSprite(pos, size, g_Fade.fadecolor);

	// Scene transition is performed AFTER this frame's old-scene draw.
	// Fade_Draw is the last thing in Manager_Draw, so the next frame begins
	// with Manager_Update -> new scene Update (camera gets positioned)
	// -> Manager_Draw (safe). We still override fade state after SetScene
	// so the new scene's Initialize cannot clobber it with its own SetFade.
	if (g_Fade.state == FADE_STATE::FADE_OUT && g_Fade.fadecolor.w >= 1.0f)
	{
		SCENE next = g_Fade.scene;
		int   frame = g_Fade.frame;

		PlayerModeSwitchManager_ResetMode();
		SetScene(next);

		g_Fade.frame = frame;
		g_Fade.fadecolor.x = 0.0f;
		g_Fade.fadecolor.y = 0.0f;
		g_Fade.fadecolor.z = 0.0f;
		g_Fade.fadecolor.w = 1.0f;
		g_Fade.state = FADE_STATE::FADE_IN;
		g_Fade.scene = next;
	}
}

//=========================================================================================================
//�t�F�[�h��ԃZ�b�g����
//=========================================================================================================
void	SetFade(int fadeframe, XMFLOAT4 color, FADE_STATE state, SCENE scene)
{ 
	// [FIX] If a fade in the same direction is already running, do not restart.
	// Some scene Initialize functions call SetFade again right after our override
	// in Fade_Draw; without this guard, the alpha (w) jumps back to the start of
	// the fade-in each time -> visible flicker (alpha oscillates back and forth).
	if (g_Fade.state == state && state != FADE_NONE) return;

	g_Fade.frame = fadeframe;
	g_Fade.fadecolor = color;
	g_Fade.state = state;
	g_Fade.scene = scene;

	if (g_Fade.state == FADE_IN)
	{
		g_Fade.fadecolor.w = 1.0f;	//�s�����ɂ���
	}
	else
	{
		g_Fade.fadecolor.w = 0.0f;	//�����ɂ���
	}


}

void	SetFade(int fadeframe, XMFLOAT4 color, FADE_STATE state)
{
	if (g_Fade.state == state && state != FADE_NONE) return;

	g_Fade.frame = fadeframe;
	g_Fade.fadecolor = color;
	g_Fade.state = state;

	if (g_Fade.state == FADE_IN)
	{
		g_Fade.fadecolor.w = 1.0f;	//�s�����ɂ���
	}
	else
	{
		g_Fade.fadecolor.w = 0.0f;	//�����ɂ���
	}


}

//=========================================================================================================
//�t�F�[�h��Ԏ擾����
//=========================================================================================================
FADE_STATE	GetFadeState()
{
	return	g_Fade.state;
}

float GetFadeAlpha()
{
	return g_Fade.fadecolor.w;
}


