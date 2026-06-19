//Player2D.cpp
#include "Player2D.h"
#include "PlayerStatus.h"
#include "Camera.h"
#include "shader.h"
#include "sprite.h"
#include "UtilMath.h"
using namespace mu;


#include "debug.h"
#include "collision.h"
#include "field.h"
#include "PlayerModeSwitchManager.h"




PLAYER g_Player2D;
static ID3D11Device* g_pDevice = NULL;
static ID3D11DeviceContext* g_pContext = NULL;
static ID3D11Buffer* g_VertexBuffer = NULL;

static float g_StopTime = 0.0f;

static Player2DAnimeDef g_AnimDefs[PLAYER2D_ANIME_MAX] = {
	//                  texturePath                           cols rows start count speed loop
	/* IDLE */ { L"asset\\Texture\\Player2D\\2D_idol.png",     5,   5,    0,    25,  4.5f, true  },
	/* WALK */ { L"asset\\Texture\\Player2D\\2D_walk.png",     5,   5,    0,    25,  2.0f, true  },
	/* JUMP */ { L"asset\\Texture\\Player2D\\2D_jump.png",     5,   5,    0,    9,  2.0f, false },
	/* JUMP */ { L"asset\\Texture\\Player2D\\2D_jump.png",     5,   5,    9,    8,  2.0f, false },
	/* JUMP */ { L"asset\\Texture\\Player2D\\2D_jump.png",     5,   5,    17,    8,  3.0f, false },

};

static ID3D11ShaderResourceView* g_AnimTextures[PLAYER2D_ANIME_MAX] = { NULL };

static PLAYER2D_ANIME g_CurrentAnim = PLAYER2D_ANIME_IDLE;
static int   g_AnimFrame = 0;
static float g_AnimTimer = 0.0f;
static bool  g_AnimFinished = false;
static bool  g_FacingRight = true;

static bool g_JustJumped = false;
static bool g_JustLanded = false;

static Vertex3D Player2DVertex[4] = {
	{
		XMFLOAT3(-1.0f, 1.0f, 0.0f),		
		XMFLOAT3(0.0f, 1.0f, 0.0f),			
		XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),	
		XMFLOAT2(0.0f,0.0f)					
	},

	{
		XMFLOAT3(1.0f, 1.0f, 0.0f),		
		XMFLOAT3(0.0f, 1.0f, 0.0f),			
		XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),	
		XMFLOAT2(1.0f,0.0f)					
	},

	{
		XMFLOAT3(-1.0f, 0.0f, 0.0f),		
		XMFLOAT3(0.0f, 1.0f, 0.0f),			
		XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),	
		XMFLOAT2(0.0f,1.0f)					
	},

	{
		XMFLOAT3(1.0f, 0.0f, 0.0f),	
		XMFLOAT3(0.0f, 1.0f, 0.0f),			
		XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),	
		XMFLOAT2(1.0f,1.0f)					
	},
};

// �v���C���[�̓����蔻��̔����̃T�C�Y
static XMFLOAT3 g_SolidHalfSize_2d = XMFLOAT3(
	PLAYER2D_SOLID_HALF_X,
	PLAYER2D_SOLID_HALF_Y,
	PLAYER2D_SOLID_HALF_Z
);

// �A�j���[�V������؂�ւ���
void Player2D_SetAnime(PLAYER2D_ANIME anim)
{
	if (anim < 0 || anim >= PLAYER2D_ANIME_MAX) return;

	if (anim == g_CurrentAnim) return;

	if (g_AnimDefs[anim].cols <= 0 || g_AnimDefs[anim].rows <= 0)
		return;

	g_CurrentAnim = anim;
	g_AnimFrame = 0;
	g_AnimTimer = 0.0f;
	g_AnimFinished = false;
}
// ���݂̃A�j���[�V�������擾����
PLAYER2D_ANIME Player2D_GetAnime()
{
	return g_CurrentAnim;
}
// �A�j���[�V�������X�V����
static void Player2D_UpdateAnime()
{
	const float moveEpsSq = 0.0001f;
	const float vyEps = 0.001f;

	float yawRad = XMConvertToRadians(g_Player2D.Rotation.y);
	float rightX = cosf(yawRad);
	float rightZ = -sinf(yawRad);
	float rightDot = g_Player2D.Velocity.x * rightX + g_Player2D.Velocity.z * rightZ;
	if (rightDot > 0.001f) g_FacingRight = true;
	else if (rightDot < -0.001f) g_FacingRight = false;

	float speedSq = g_Player2D.Velocity.x * g_Player2D.Velocity.x +
		g_Player2D.Velocity.z * g_Player2D.Velocity.z;

	if (g_JustLanded)
	{
		Player2D_SetAnime(PLAYER2D_ANIME_FALL);
		g_JustLanded = false;
	}
	else if (g_JustJumped)
	{
		Player2D_SetAnime(PLAYER2D_ANIME_JUMP);
		g_JustJumped = false;
	}
	else
	{
		if ((g_CurrentAnim == PLAYER2D_ANIME_JUMP || g_CurrentAnim == PLAYER2D_ANIME_FALL) && !g_AnimFinished)
		{

		}
		else
		{
			if (!g_Player2D.isGround)
			{
				if (g_Player2D.Velocity.y < -vyEps)
					Player2D_SetAnime(PLAYER2D_ANIME_FALL);
				else
					Player2D_SetAnime(PLAYER2D_ANIME_JUMP_AIR);
			}
			else
			{
				Player2D_SetAnime((speedSq > moveEpsSq) ? PLAYER2D_ANIME_WALK : PLAYER2D_ANIME_IDLE);
			}
		}
	}

	const Player2DAnimeDef& def = g_AnimDefs[g_CurrentAnim];
	int frameCount = (def.frameCount > 0) ? def.frameCount : 1;
	float speed = (def.frameSpeed > 0.0f) ? def.frameSpeed : 1.0f;

	g_AnimTimer += 1.0f;
	if (g_AnimTimer >= speed)
	{
		g_AnimTimer = 0.0f;
		g_AnimFrame++;

		if (g_AnimFrame >= frameCount)
		{
			g_AnimFrame = def.loop ? 0 : (frameCount - 1);
			g_AnimFinished = !def.loop;
		}
	}

	DEBUG_IMGUI_BEGIN({
		ImGui::Begin("Debug - han");
				if (ImGui::TreeNode("2DMoAnim.cpp"))
				{
					ImGui::Text("isG: %s", g_Player2D.isGround? "true" : "false");
					ImGui::Text("Anim: %d", g_CurrentAnim);
					ImGui::TreePop();
				}
				ImGui::End();

		});
}
// �A�j���[�V�������X�V����
static void Player2D_UpdateUV()
{
	if (g_CurrentAnim < 0 || g_CurrentAnim >= PLAYER2D_ANIME_MAX)
	{
		g_CurrentAnim = PLAYER2D_ANIME_IDLE;
	}

	const Player2DAnimeDef& def = g_AnimDefs[g_CurrentAnim];

	int cols = def.cols;
	int rows = def.rows;
	if (cols <= 0) cols = 1;
	if (rows <= 0) rows = 1;

	float uvW = 1.0f / cols;
	float uvH = 1.0f / rows;


	int frameCount = def.frameCount;
	if (frameCount <= 0) frameCount = 1;

	int animFrame = g_AnimFrame;
	if (animFrame < 0) animFrame = 0;
	if (animFrame >= frameCount) animFrame = frameCount - 1;

	int sheetFrame = def.startFrame + animFrame;

	int totalFrames = cols * rows;
	if (totalFrames <= 0) totalFrames = 1;
	if (sheetFrame >= totalFrames) sheetFrame = totalFrames - 1;
	if (sheetFrame < 0) sheetFrame = 0;

	int col = sheetFrame % cols;
	int row = sheetFrame / cols;

	float u0 = col * uvW;
	float v0 = row * uvH;
	float u1 = u0 + uvW;
	float v1 = v0 + uvH;

	if (!g_FacingRight)
	{
		float temp = u0;
		u0 = u1;
		u1 = temp;
	}

	Player2DVertex[0].texCoord = XMFLOAT2(u0, v0);
	Player2DVertex[1].texCoord = XMFLOAT2(u1, v0);
	Player2DVertex[2].texCoord = XMFLOAT2(u0, v1);
	Player2DVertex[3].texCoord = XMFLOAT2(u1, v1);
}





//=========================================================================================================
// ������
//=========================================================================================================
void Player2D_Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	// �f�o�C�X�ƃR���e�L�X�g��ۑ�
	g_pDevice = pDevice;
	g_pContext = pContext;

	// �A�j���[�V�����̃e�N�X�`����ǂݍ���
	for (int i = 0; i < PLAYER2D_ANIME_MAX; i++)
	{
		if (g_AnimDefs[i].texturePath == NULL) continue;

		TexMetadata metadata;
		ScratchImage image;
		HRESULT hr = LoadFromWICFile(g_AnimDefs[i].texturePath, WIC_FLAGS_NONE, &metadata, image);

		if (SUCCEEDED(hr))
		{
			CreateShaderResourceView(pDevice, image.GetImages(),
				image.GetImageCount(), metadata, &g_AnimTextures[i]);
		}
		else
		{
			char msg[256];
			sprintf_s(msg, "Warning: Failed to load texture for anim %d\n", i);
			OutputDebugStringA(msg);
			g_AnimTextures[i] = NULL;
		}
	}
	// �A�j���[�V�����̃e�N�X�`�����Ȃ��ꍇ�́AIDLE�̃e�N�X�`�����g�p����
	for (int i = 0; i < PLAYER2D_ANIME_MAX; i++)
	{
		if (g_AnimTextures[i] == NULL && g_AnimTextures[PLAYER2D_ANIME_IDLE] != NULL)
		{
			g_AnimTextures[i] = g_AnimTextures[PLAYER2D_ANIME_IDLE];
			g_AnimTextures[i]->AddRef();
		}
	}

	// ���_�o�b�t�@�̍쐬
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.ByteWidth = sizeof(Vertex3D) * 4;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	g_pDevice->CreateBuffer(&bd, NULL, &g_VertexBuffer);
	if (!g_VertexBuffer) return;
	// ���_�o�b�t�@�ɏ����f�[�^����������
	D3D11_MAPPED_SUBRESOURCE msr;
	g_pContext->Map(g_VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
	Vertex3D* vertex = (Vertex3D*)msr.pData;
	CopyMemory(&vertex[0], &Player2DVertex[0], sizeof(Vertex3D) * 4);
	g_pContext->Unmap(g_VertexBuffer, 0);
}

//=========================================================================================================
// �I��
//=========================================================================================================
void Player2D_Finalize(void)
{
	SAFE_RELEASE(g_VertexBuffer);
	for (int i = 0; i < PLAYER2D_ANIME_MAX; i++)// �A�j���[�V�����̃e�N�X�`�������
	{
		if (g_AnimTextures[i] != NULL)
		{
			for (int j = i + 1; j < PLAYER2D_ANIME_MAX; j++)
			{
				if (g_AnimTextures[j] == g_AnimTextures[i])
				{
					g_AnimTextures[j]->Release();
					g_AnimTextures[j] = NULL;
				}
			}
			g_AnimTextures[i]->Release();
			g_AnimTextures[i] = NULL;
		}
	}
}

//=========================================================================================================
// �X�V
//=========================================================================================================
void Player2D_Update()
{
	if (!g_Player2D.Active) return;

	

	// ����������
	Player2D_Respawn();



	Player2D_Move();		// �ړ�����
	Player2D_Jump();		// �W�����v����

	Player2D_Gravity();		// �d�͏���
	
	Player2D_UpdateAnime();	// �A�j���[�V�����̍X�V
}

//=========================================================================================================
// �Q�b�^�[
//=========================================================================================================
// �v���C���[��2D���W���擾����
XMFLOAT3 GetPlayer2DPosition()
{
	return g_Player2D.Position;
}
// �v���C���[�̍\���̂��擾����
PLAYER * GetPlayer2D()
{
	return &g_Player2D;
}


Capsule2D Player2D_GetCapsule()
{
	Capsule2D cap;

	float totalHeight = PLAYER2D_CAPSULE_HEIGHT + PLAYER2D_CAPSULE_RADIUS * 2.0f;
	cap.center = XMFLOAT3(
		g_Player2D.Position.x,
		g_Player2D.Position.y + totalHeight * 0.5f,
		g_Player2D.Position.z
	);

	cap.radius = PLAYER2D_CAPSULE_RADIUS;
	cap.halfHeight = PLAYER2D_CAPSULE_HEIGHT * 0.5f;
	cap.halfZ = PLAYER2D_CAPSULE_HALF_Z;
	cap.rotationZ = XMConvertToRadians(g_Player2D.Rotation.z);

	return cap;
}

XMFLOAT3 Player2D_GetSolidHalfSize()
{
	Capsule2D cap = Player2D_GetCapsule();
	return cap.GetBoundingHalfSize();
}


//=========================================================================================================
// ����
//=========================================================================================================
void Player2D_Gravity()
{
	g_Player2D.blockMovement = false;

	bool wasGround = g_Player2D.isGround;

	if (!wasGround)
	{
		g_Player2D.Velocity.y += g_Player2D.Acceleration.y;

		if (g_Player2D.Velocity.y < g_Player2D.gravityPower)
		{
			g_Player2D.Velocity.y = g_Player2D.gravityPower;
		}
	}
	else
	{
		if (g_Player2D.Velocity.y < 0.0f)
		{
			g_Player2D.Velocity.y = 0.0f;
		}
	}

	g_Player2D.Velocity.x *= 0.925f;
	g_Player2D.Velocity.z *= 0.925f;

	
	int hit = Collision_Player2D_MoveAndCollision();

	g_JustLanded = (!wasGround && g_Player2D.isGround);

	if (g_Player2D.isGround && g_Player2D.Velocity.y < 0.0f)
	{
		g_Player2D.Velocity.y = 0.0f;
	}

	// �ڒn��Ԃ̍X�V
	if (g_Player2D.isGround)
	{
		if (g_Player2D.state == PLAYER_STATE_FALL)
		{
			g_Player2D.state = PLAYER_STATE_MOVE;
		}
	}
	else
	{
		if (g_Player2D.state != PLAYER_STATE_FALL)
		{
			g_Player2D.state = PLAYER_STATE_FALL;
		}
	}


	float len = g_Player2D.Velocity.x * g_Player2D.Velocity.x +
		g_Player2D.Velocity.y * g_Player2D.Velocity.y +
		g_Player2D.Velocity.z * g_Player2D.Velocity.z;

	if (len <= 0.0002f)
	{
		g_StopTime++;
		if (g_StopTime > (60.0f * 2))
		{
			g_Player2D.Velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
			g_Player2D.state = PLAYER_STATE_IDLE;
			g_StopTime = 0.0f;
		}
	}
	else
	{
		g_StopTime = 0.0f;
	}
}

void Player2D_Respawn()
{
	static int s_FallFrameCount = 0;
	const int FALL_FRAME_THRESHOLD = 30;

	// Only force back to 3D when the player has clearly fallen off the
	// supported shadow area (Y well below world floor). The previous
	// no-ground-time check fired during normal jumps -- 90 frames with
	// !isGround was easy to hit -- and looked like a phantom restart.
	if (g_Player2D.Position.y < -10.0f)
	{
		s_FallFrameCount++;
		if (s_FallFrameCount >= FALL_FRAME_THRESHOLD)
		{
			PlayerModeSwitchManager_ForceTo3D();
			s_FallFrameCount = 0;
		}
	}
	else
	{
		s_FallFrameCount = 0;
	}
}

// �ړ�����
void Player2D_Move()
{
	if (g_Player2D.blockMovement)
	{
		return;
	}

	XMFLOAT3 inputDir = XMFLOAT3(0.0f, 0.0f, 0.0f);

	if (IsInputPress(RightKey, gPad)) inputDir.x += +1.0f;
	if (IsInputPress(LeftKey, gPad))  inputDir.x += -1.0f;

	if (fabsf(inputDir.x) > 0.0001f)
	{
		// ���͕��������[���h���W�ɕϊ����邽�߂ɁA�v���C���[�̉�]���l������
		// �v���C���[�̉�]��Y���݂̂Ɖ��肷��
		float yawRad = XMConvertToRadians(g_Player2D.Rotation.y);

		// �v���C���[�̉E�����x�N�g�����v�Z����
		float rightX = cosf(yawRad);
		float rightZ = -sinf(yawRad);

		// ���͕��������[���h���W�ɕϊ�����
		float worldX = inputDir.x * rightX;
		float worldZ = inputDir.x * rightZ;

		// ���[���h���W�̓��͕����Ɋ�Â��đ��x�����Z����
		g_Player2D.Velocity.x += worldX * g_Player2D.moveSpeed;
		g_Player2D.Velocity.z += worldZ * g_Player2D.moveSpeed;
	}

	// ���x�̏�����`�F�b�N����
	float speedSq = g_Player2D.Velocity.x * g_Player2D.Velocity.x +
		g_Player2D.Velocity.z * g_Player2D.Velocity.z;
	float maxSpeed = g_Player2D.maxMoveSpeed;
	if (speedSq > maxSpeed * maxSpeed)
	{
		float speed = sqrtf(speedSq);
		g_Player2D.Velocity.x = (g_Player2D.Velocity.x / speed) * maxSpeed;
		g_Player2D.Velocity.z = (g_Player2D.Velocity.z / speed) * maxSpeed;
	}


	DEBUG_IMGUI_BEGIN({
		ImGui::Begin("Debug - han");
				if (ImGui::TreeNode("pla2DMo.cpp"))
				{
					ImGui::Text("g_Player2DblockMovement: %s", g_Player2D.blockMovement ? "true" : "false");
					ImGui::Text("g_Player2DVelocityX: %.2f", g_Player2D.Velocity.x);
					ImGui::Text("g_Player2DVelocityZ: %.2f", g_Player2D.Velocity.z);
					ImGui::TreePop();
				}
				ImGui::End();

		});

}

//=========================================================================================================
// �W�����v����
//=========================================================================================================


// �W�����v�̏�Ԃ��Ǘ����邽�߂̕ϐ�
static bool g_IsJumping = false;
static bool g_JumpKeyReleased = true;
static float g_JumpHoldTime = 0.0f;
static float g_CoyoteTime = 0.0f;
static float g_JumpBufferTime = 0.0f;

static const float JUMP_INITIAL_VELOCITY = 0.18f;	// �W�����v�̏���
static const float JUMP_HOLD_BONUS = 0.008f;		// �W�����v�z�[���h�ɂ��ǉ��㏸��
static const float JUMP_HOLD_MAX_TIME = 12.0f;		// �W�����v�z�[���h�̍ő厞��
static const float COYOTE_TIME_MAX = 6.0f;			// �R���[�e�^�C���̍ő厞��
static const float JUMP_BUFFER_MAX = 8.0f;			// �W�����v�o�b�t�@�̍ő厞��
static const float JUMP_CUT_MULTIPLIER = 0.5f;		// �W�����v�J�b�g�̑��x�{��


void Player2D_Jump()
{
	bool jumpTriggered = IsInputTrigger(JumpKey, gPad);
	bool jumpPressed   = IsInputPress(JumpKey, gPad);

	if (jumpTriggered && g_Player2D.isGround && !g_IsJumping)
	{
		g_Player2D.Velocity.y = JUMP_INITIAL_VELOCITY;
		g_JustJumped = true;
		g_Player2D.isGround = false;
		g_IsJumping = true;
		g_JumpHoldTime = 0.0f;
		g_Player2D.state = PLAYER_STATE_FALL;
	}

	if (g_IsJumping && jumpPressed && g_Player2D.Velocity.y > 0.0f)
	{
		g_JumpHoldTime += 1.0f;
		if (g_JumpHoldTime <= JUMP_HOLD_MAX_TIME)
		{
			float holdBonus = JUMP_HOLD_BONUS * (1.0f - g_JumpHoldTime / JUMP_HOLD_MAX_TIME);
			g_Player2D.Velocity.y += holdBonus;
		}
	}
	else if (g_IsJumping && !jumpPressed)
	{
		// [FIX] Always release the jump latch on key-up, even while falling.
		// The old Velocity.y>0 guard left g_IsJumping stuck true after a mid-air
		// side hit, blocking all jumps until a 3D<->2D re-entry reset it.
		if (g_Player2D.Velocity.y > 0.0f) g_Player2D.Velocity.y *= JUMP_CUT_MULTIPLIER;
		g_IsJumping = false;
	}

	if (g_Player2D.isGround)
	{
		g_IsJumping = false;
		g_JumpHoldTime = 0.0f;
	}
}

// �v���C���[�̏�Ԃ�������ԂɃ��Z�b�g����
void Player2D_Reset()
{
	g_Player2D.Position = g_Player2D.Firstposition;
	g_Player2D.Rotation = g_Player2D.FirstRotation;
	g_Player2D.Scaling = g_Player2D.FirstScaling;
	g_Player2D.Velocity = g_Player2D.FirstVelocity;
	g_Player2D.Acceleration = g_Player2D.FirstAcceleration;
	g_Player2D.state = g_Player2D.FirstState;
	g_StopTime = g_Player2D.FirstStopTime;
	g_Player2D.Quaternion = g_Player2D.FirstQuaternion;

	// �W�����v�֘A�̏�ԂɃ��Z�b�g����
	g_IsJumping = false;
	g_JumpKeyReleased = true;
	g_JumpHoldTime = 0.0f;
	g_CoyoteTime = 0.0f;
	g_JumpBufferTime = 0.0f;

	// �A�j���[�V������������ԂɃ��Z�b�g����
	g_CurrentAnim = PLAYER2D_ANIME_IDLE;
	g_AnimFrame = 0;
	g_AnimTimer = 0.0f;
	g_AnimFinished = false;
	g_FacingRight = true;

	// [FIX] Falls/resets should also reset stage object positions
	// (boxes, seesaw, light) so the puzzle restarts from a fresh state.
	Field_ReloadCurrentMap();
	Collision_ResetShadowContactState();

	Field_ReloadCurrentMap();
	Collision_ResetShadowContactState();
}


//=========================================================================================================
// �`��
//=========================================================================================================
void Player2D_Draw(void)
{

	if (!g_Player2D.Active) return;

	ID3D11ShaderResourceView* currentTexture = g_AnimTextures[g_CurrentAnim];
	if (!currentTexture) return;

	Player2D_UpdateUV();

	D3D11_MAPPED_SUBRESOURCE msr;
	g_pContext->Map(g_VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
	Vertex3D* vertex = (Vertex3D*)msr.pData;
	CopyMemory(&vertex[0], &Player2DVertex[0], sizeof(Vertex3D) * 4);
	g_pContext->Unmap(g_VertexBuffer, 0);

	XMMATRIX scale = XMMatrixScaling
	(
		g_Player2D.Scaling.x,
		g_Player2D.Scaling.y,
		g_Player2D.Scaling.z
	);

	XMMATRIX rotation = XMMatrixRotationRollPitchYaw
	(
		XMConvertToRadians(g_Player2D.Rotation.x),
		XMConvertToRadians(g_Player2D.Rotation.y),
		XMConvertToRadians(g_Player2D.Rotation.z)
	);

	XMMATRIX translation = XMMatrixTranslation
	(
		g_Player2D.Position.x,
		g_Player2D.Position.y,
		g_Player2D.Position.z
	);

	XMMATRIX world = scale * rotation * translation;

	XMMATRIX view = GetViewMatrix();
	XMMATRIX projection = GetProjectionMatrix();
	XMMATRIX wvp = world * view * projection;

	// �V�F�[�_�[�ɍs��ƃe�N�X�`�����Z�b�g���ĕ`��
	Shader_SetWorldMatrix(world);
	Shader_SetMatrix(wvp);


	g_pContext->PSSetShaderResources(0, 1, &currentTexture);
	UINT stride = sizeof(Vertex3D);
	UINT offset = 0;
	g_pContext->IASetVertexBuffers(0, 1, &g_VertexBuffer, &stride, &offset);
	g_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	SetBlendState(BLENDSTATE_ALFA);
	g_pContext->Draw(4, 0);

}

// �v���C���[�̏�Ԃ�������ԂɃ��Z�b�g����
void Player2D_InitAt(const XMFLOAT3& pos, const XMFLOAT3& rot)
{
	g_Player2D.Firstposition = g_Player2D.Position = pos;
	g_Player2D.FirstRotation = g_Player2D.Rotation = rot;

	g_Player2D.Scaling = XMFLOAT3(1.0f, 2.0f, 1.0f);

	g_Player2D.Velocity = XMFLOAT3(0, 0, 0);
	g_Player2D.Acceleration = XMFLOAT3(0.0f, -9.8f / 600.0f * 0.5f, 0.0f);

	g_Player2D.state = PLAYER_STATE_MOVE;
	g_StopTime = 0.0f;

	// Reset jump state so the player can immediately jump after entering
	// 2D mode (previously g_IsJumping could be left stale = true from the
	// last 2D session, blocking the first jump).
	g_IsJumping = false;
	g_JumpHoldTime = 0.0f;
	g_Player2D.isGround = true;  // entered on a shadow surface

	g_Player2D.Quaternion = XMQuaternionIdentity();

	g_Player2D.FirstScaling = g_Player2D.Scaling;
	g_Player2D.FirstVelocity = g_Player2D.Velocity;
	g_Player2D.FirstAcceleration = g_Player2D.Acceleration;
	g_Player2D.FirstState = g_Player2D.state;
	g_Player2D.FirstStopTime = g_StopTime;
	g_Player2D.FirstQuaternion = g_Player2D.Quaternion;

	g_Player2D.Active = true;
}

// �v���C���[�̏�Ԃ�������ԂɃ��Z�b�g����
void Player2D_Uninit()
{
	g_Player2D.Active = false;

	g_Player2D.Velocity = XMFLOAT3(0, 0, 0);
	g_Player2D.state = PLAYER_STATE_IDLE;
	g_StopTime = 0.0f;
}

// �v���C���[�̃A�N�e�B�u��Ԃ�ݒ肷��
void Player2D_SetActive(bool active)
{
	g_Player2D.Active = active;
}
