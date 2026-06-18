#include "Player3D.h"
#include "PlayerStatus.h"
#include "Keyboard.h"
#include "Camera.h"
#include "shader.h"
#include "Collision.h"
#include "manager.h"
#include "Input.h"
#include "fade.h"

#include "field.h"
#include "debug.h"
#include "UtilMath.h"
using namespace mu;

#include "FieldSeesaw.h"
#include "FieldPortal.h"
#include "FieldFountain.h"
#include "stage_clear_state.h"
#include "iris.h"
#include "direct3d.h"
#include "game.h"




//=========================================================================================================
// �O���[�o���ϐ�
//=========================================================================================================
PLAYER g_Player3D;
ID3D11Device* g_pDevice;
ID3D11DeviceContext* g_pContext;
static float g_StopTime = 0.0f;

// �R���g���[���[
extern Controller gPad;
static XMFLOAT3 inputDir(0.0f, 0.0f, 0.0f);
float FirstMaxMoveSpeed = g_Player3D.maxMoveSpeed;
static XMFLOAT3 g_SolidHalfSize = XMFLOAT3(
	PLAYER3D_SOLID_HALF_X,
	PLAYER3D_SOLID_HALF_Y,
	PLAYER3D_SOLID_HALF_Z
);
static XMFLOAT3 g_TriggerHalfSize = XMFLOAT3(
	PLAYER3D_TRIGGER_HALF_X,
	PLAYER3D_TRIGGER_HALF_Y,
	PLAYER3D_TRIGGER_HALF_Z
);
static bool isTrigger = false;


//=========================================================================================================
// ����������
//=========================================================================================================
void Player3D_Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	g_pDevice = pDevice;
	g_pContext = pContext;

	for (int i = 0; i < PLAYER_ANIM_MAX; i++) {
		g_Player3D.Model[i] = NULL;
	}

	g_Player3D.FirstAnim = g_Player3D.CurrentAnimIndex = PLAYER_ANIM_IDLE;


	//g_Player3D.Model[PLAYER_ANIM_IDLE] = ModelLoad("asset\\model\\Idle.fbx");
	//g_Player3D.Model[PLAYER_ANIM_IDLE] = ModelLoad("asset\\model\\Chara_test_02.fbx");
	g_Player3D.Model[PLAYER_ANIM_IDLE] = ModelLoad("asset\\model\\anim\\Idle.fbx");
	//g_Player3D.Model[PLAYER_ANIM_IDLE] = ModelLoad("asset\\model\\Chara_test_02.fbx",true);
	g_Player3D.Model[PLAYER_ANIM_WALK] = ModelLoad("asset\\model\\anim\\Walking.fbx");
	g_Player3D.Model[PLAYER_ANIM_DASH] = ModelLoad("asset\\model\\anim\\Running.fbx");
	g_Player3D.Model[PLAYER_ANIM_PUSH] = ModelLoad("asset\\model\\anim\\Push.fbx");

	g_Player3D.Model[PLAYER_ANIM_JUMP] = ModelLoad("asset\\model\\anim\\Jump.fbx");
	g_Player3D.Model[PLAYER_ANIM_JUMP]->LoopAnimation = false;
	g_Player3D.Model[PLAYER_ANIM_JUMP]->CustomEndTime = 53.0f; // --> use if need to cut the animation short/�A�j���[�V������Z���J�b�g����K�v������ꍇ�Ɏg�p���܂�



	{
		MODEL* jm = g_Player3D.Model[PLAYER_ANIM_JUMP];
		if (jm && jm->AiScene && jm->AiScene->HasAnimations())
		{
			const aiAnimation* anim = jm->AiScene->mAnimations[0];
			float tps = (float)anim->mTicksPerSecond;
			float dur = (float)anim->mDuration;
			float seconds = (tps > 0.0f) ? dur / tps : -1.0f;

			char buf[256];
			sprintf(buf,
				"JUMP ANIM: duration=%.4f ticks, ticksPerSec=%.4f, seconds=%.4f, channels=%d",
				dur, tps, seconds, anim->mNumChannels);
			OutputDebugStringA(buf);
			// Also show in ImGui if you prefer:
			// hal::dout << buf << std::endl;
		}
	}

	g_Player3D.Firstposition = g_Player3D.Position;
	g_Player3D.FirstRotation = g_Player3D.Rotation = XMFLOAT3(0.0f, 180.0f, 0.0f);
	//g_Player3D.FirstScaling = g_Player3D.Scaling = XMFLOAT3(0.1f, 0.1f, 0.1f);
	//g_Player3D.FirstScaling = g_Player3D.Scaling = XMFLOAT3(0.01f, 0.01f, 0.01f);
	g_Player3D.FirstScaling = g_Player3D.Scaling = XMFLOAT3(1.5f, 1.5f, 1.5f);


	g_Player3D.FirstVelocity = g_Player3D.Velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
	g_Player3D.FirstAcceleration = g_Player3D.Acceleration = XMFLOAT3(0.0f, -9.8f / 600.0f * 0.5f, 0.0f);
	g_Player3D.FirstState = g_Player3D.state = PLAYER_STATE_IDLE;
	g_Player3D.FirstStopTime = g_StopTime = 0.0f;
	g_Player3D.FirstQuaternion = g_Player3D.Quaternion = XMQuaternionIdentity();

	FirstMaxMoveSpeed = g_Player3D.maxMoveSpeed;

	g_Player3D.FirstMaxMoveSpeed = g_Player3D.maxMoveSpeed;
}

//=========================================================================================================
// �I������
//=========================================================================================================
void Player3D_Finalize(void)
{
	for (int i = 0; i < PLAYER_ANIM_MAX; i++) {
		if (g_Player3D.Model[i] != NULL) {
			ModelRelease(g_Player3D.Model[i]);
			g_Player3D.Model[i] = NULL;
		}
	}
}

//=========================================================================================================
// �X�V����
//=========================================================================================================
void Player3D_Update()
{
	DEBUG_IMGUI_BEGIN({
		ImGui::Begin("Debug - han");
		if (ImGui::TreeNode("Player3d.cpp"))
		{
			ImGui::Text("PosX: %.2f", g_Player3D.Position.x);
			ImGui::Text("PosY: %.2f", g_Player3D.Position.y);
			ImGui::Text("PosZ: %.2f", g_Player3D.Position.z);
			ImGui::Text("State: %d", g_Player3D.state);
			ImGui::TreePop();
		}
		ImGui::End();

	});

	g_Player3D.fountainJumped = false;

	if (!g_Player3D.Active) return;

	if (g_Player3D.state == PLAYER_STATE_AUTO_WALK)
	{
		Player3D_Gravity();
		Player3D_AutoWalk();
		return;  // No player input allowed during auto-walk
	}

	if (g_Player3D.Position.y < -10.0f)
	{
		Player3D_Respawn();
	}
	
	Player3D_Gravity();
	Player3D_CheckGoal();

	switch (g_Player3D.state)
	{
	case PLAYER_STATE_IDLE:
		Player3D_Idle();
		break;
	case PLAYER_STATE_MOVE:
		if (!g_Player3D.isAuto)
		Player3D_Move();	//�ړ�
		break;
	case PLAYER_STATE_JUMP:
		Player3D_Jump();	//�W�����v
		break;
	case PLAYER_STATE_DASH:
		Player3D_Dash();	//�_�b�V��
		break;
	case PLAYER_STATE_ACTION:
		Player3D_Action();	//�A�N�V����
		break;
	default:
		break;
	}

}

//=========================================================================================================
// �d�͏���
//=========================================================================================================
void Player3D_Gravity()
{
	//// ���������x�E���x����i���̃u���b�N�͋�������Ő����\�j
	if (g_Player3D.Velocity.x >= g_Player3D.maxMoveSpeed) g_Player3D.Velocity.x = g_Player3D.maxMoveSpeed;
	else g_Player3D.Velocity.x += g_Player3D.Acceleration.x;

	if (g_Player3D.Velocity.z >= g_Player3D.maxMoveSpeed) g_Player3D.Velocity.z = g_Player3D.maxMoveSpeed;
	else g_Player3D.Velocity.z += g_Player3D.Acceleration.z;

	g_Player3D.Velocity.x *= 0.925f;
	g_Player3D.Velocity.z *= 0.925f;

	// Coyote frames: while grounded, keep the counter topped up. When the
	// player walks off a ledge, isGround drops to false but coyoteFrames
	// stays positive for a short grace window so a slightly-late jump
	// press still triggers a normal jump.
	const int kCoyoteFramesMax = 8;  // ~0.13s at 60fps
	if (g_Player3D.isGround)
	{
		g_Player3D.coyoteFrames = kCoyoteFramesMax;
	}
	else if (g_Player3D.coyoteFrames > 0)
	{
		g_Player3D.coyoteFrames--;
	}

	if (!g_Player3D.isGround)
	{
		g_Player3D.Velocity.y += g_Player3D.Acceleration.y;

		if (g_Player3D.Velocity.y < g_Player3D.maxFallSpeed)
			g_Player3D.Velocity.y = g_Player3D.maxFallSpeed;
	}
	else
	{
		if (g_Player3D.Velocity.y < 0.0f)
			g_Player3D.Velocity.y = 0.0f;
	}

	// �ēx�����ɉ����x�ƌ�����K�p�i�K�v�Ȃ琮���j
	g_Player3D.Velocity.x += g_Player3D.Acceleration.x;
	g_Player3D.Velocity.z += g_Player3D.Acceleration.z;

	g_Player3D.Velocity.x *= g_Player3D.dampingXZ;
	g_Player3D.Velocity.z *= g_Player3D.dampingXZ;

	// �ʒu�X�V
	g_Player3D.Position.x += g_Player3D.Velocity.x;
	g_Player3D.Position.y += g_Player3D.Velocity.y;
	g_Player3D.Position.z += g_Player3D.Velocity.z;

	int hit = Player3DField_Collision();

	Seesaw_PlayerCollision();
}

void Player3D_Respawn()
{
	g_Player3D.Position = g_Player3D.Firstposition;
	g_Player3D.Rotation = g_Player3D.FirstRotation;
	g_Player3D.Scaling = g_Player3D.FirstScaling;
	g_Player3D.Velocity = g_Player3D.FirstVelocity;
	g_Player3D.Acceleration = g_Player3D.FirstAcceleration;
	g_Player3D.state = g_Player3D.FirstState;
	g_Player3D.CurrentAnimIndex = g_Player3D.FirstAnim;
	g_StopTime = g_Player3D.FirstStopTime;
	g_Player3D.Quaternion = g_Player3D.FirstQuaternion;

	g_Player3D.maxMoveSpeed = g_Player3D.FirstMaxMoveSpeed;
	// [FIX] Clear dash flag on respawn. If the player was holding Shift
	// when they fell, isDash stayed true after respawn so movement kept
	// running at dash speed even with no key held. State is reset to
	// FirstState above, which skips the PLAYER_STATE_DASH branch where
	// the released-key check would have cleared isDash.
	g_Player3D.isDash = false;
	Field_ReloadCurrentMap();
	Collision_ResetShadowContactState();
	return;
}

void Player3D_Move()
{
	XMFLOAT3 inputDir(0.0f, 0.0f, 0.0f);
	bool isMoving = false;

	// [FIX] Clear dash if Shift was released. Player3D_Dash() (which does
	// the release check normally) only runs while state == PLAYER_STATE_DASH.
	// As soon as a jump triggers, the dispatcher moves through PLAYER_STATE_JUMP
	// and then sets state back to PLAYER_STATE_MOVE in Player3D_Jump(), so
	// Player3D_Dash() never runs again. Without this check, isDash stays true
	// forever after a Shift-then-jump and the dash animation is stuck on.
	if (g_Player3D.isDash && !IsInputPress(DashKey, gPad))
	{
		g_Player3D.isDash = false;
		g_Player3D.maxMoveSpeed = g_Player3D.FirstMaxMoveSpeed;
	}

	if (IsInputTrigger(JumpKey, gPad))
	{
		if (!g_Player3D.fountainJumped && (g_Player3D.isGround || g_Player3D.coyoteFrames > 0))
		{
			g_Player3D.state = PLAYER_STATE_JUMP;
		}
	}

	MODEL* jumpModel = g_Player3D.Model[PLAYER_ANIM_JUMP];
	if (g_Player3D.CurrentAnimIndex == PLAYER_ANIM_JUMP &&
		jumpModel != NULL && jumpModel->AnimationFinished)
	{
		g_Player3D.CurrentAnimIndex = PLAYER_ANIM_IDLE;
	}


	g_Player3D.ControllerMode = gPad.IsConnected();

	// �L�[�{�[�h�̑��݂����\�I�ȑ���L�[���`�F�b�N�i������Ă���Ȃ�L�[�{�[�h�D��j
	if (Keyboard_IsKeyDown(KK_W) || Keyboard_IsKeyDown(KK_S) ||
		Keyboard_IsKeyDown(KK_A) || Keyboard_IsKeyDown(KK_D) ||
		Keyboard_IsKeyDown(KK_SPACE) || Keyboard_IsKeyDown(KK_F) ||
		Keyboard_IsKeyDown(KK_LEFTSHIFT) || Keyboard_IsKeyDown(KK_R) ||
		Keyboard_IsKeyDown(KK_ESCAPE))
	{
		g_Player3D.ControllerMode = false;
	}

	// �܂��L�[�{�[�h���͂��擾�i��Ɏ擾���Ă����j
	XMFLOAT3 kbDir = XMFLOAT3(0.0f, 0.0f, 0.0f);
	if (Keyboard_IsKeyDown(KK_W))
	{
		kbDir.z += +1.0f;
	}
	if (Keyboard_IsKeyDown(KK_S))
	{
		kbDir.z += -1.0f;
	}
	if (Keyboard_IsKeyDown(KK_D))
	{
		kbDir.x += +1.0f;
	}
	if (Keyboard_IsKeyDown(KK_A))
	{
		kbDir.x += -1.0f;
	}

	// �R���g���[���[���́i�ڑ�����Ă���Ύ擾�j
	XMFLOAT3 padDir = XMFLOAT3(0.0f, 0.0f, 0.0f);
	bool padActive = false;
	if (gPad.IsConnected())
	{
		float lx = gPad.GetLeftStickX();
		float ly = gPad.GetLeftStickY();

		const float deadzone = 0.20f;
		if (fabsf(lx) < deadzone) lx = 0.0f;
		if (fabsf(ly) < deadzone) ly = 0.0f;

		padDir.x = lx;
		padDir.y = 0.0f;
		padDir.z = ly;

		if (Length2D(padDir) > 1e-6f)
			padActive = true;
	}

	// �D�惋�[���FControllerMode �ɉ����ē��̓\�[�X�����肷��
	// �iControllerMode �͏�� gPad.IsConnected() ����ɂ��A�L�[�{�[�h���͂������ false �ɂ��Ă���j
	if (g_Player3D.ControllerMode && padActive)
	{
		inputDir = padDir;
		isMoving = true;
	}
	else
	{
		inputDir = kbDir;
		if (Length2D(kbDir) > 1e-6f) isMoving = true;
	}

	// �A�j���ؑ�
	if (!g_Player3D.isPushing && !g_Player3D.isAuto)
	{
		if (g_Player3D.CurrentAnimIndex != PLAYER_ANIM_JUMP)
		{
			if (g_Player3D.isDash)
			{
				// Dashing always shows dash animation, even when pushing
				g_Player3D.CurrentAnimIndex = PLAYER_ANIM_DASH;
			}
			else if (!g_Player3D.isPushing)
			{
				if (isMoving) {
					g_Player3D.CurrentAnimIndex = PLAYER_ANIM_WALK;
				}
				else {
					g_Player3D.CurrentAnimIndex = PLAYER_ANIM_IDLE;
				}
			}
		}
	}

	// �J������ړ�
	float len = Length2D(inputDir);
	if (len > 1e-6f)
	{
		inputDir = Normalize2D(inputDir);

		XMFLOAT3 camPos = GetCameraPosition();
		XMFLOAT3 camAt = GetCameraAtPosition();

		XMFLOAT3 camFwd = XMFLOAT3(
			camAt.x - camPos.x,
			0.0f,
			camAt.z - camPos.z
		);
		camFwd = Normalize2D(camFwd);
		XMFLOAT3 camRight = XMFLOAT3(
			camFwd.z,
			0.0f,
			-camFwd.x
		);
		XMFLOAT3 moveWorld = XMFLOAT3(
			camFwd.x * inputDir.z + camRight.x * inputDir.x,
			0.0f,
			camFwd.z * inputDir.z + camRight.z * inputDir.x
		);
		moveWorld = Normalize2D(moveWorld);

		if (g_Player3D.isDash)
		{
			g_Player3D.Velocity.x += moveWorld.x * (g_Player3D.moveSpeed * g_Player3D.dashMoveSpeed);
			g_Player3D.Velocity.z += moveWorld.z * (g_Player3D.moveSpeed * g_Player3D.dashMoveSpeed);
		}
		else
		{
			g_Player3D.Velocity.x += moveWorld.x * g_Player3D.moveSpeed;
			g_Player3D.Velocity.z += moveWorld.z * g_Player3D.moveSpeed;
		}

		// �����̕��
		float targetYawRad = atan2f(moveWorld.x, moveWorld.z);
		float targetYawDeg = XMConvertToDegrees(targetYawRad);

		const float yawOffset = g_Player3D.FirstRotation.y;
		targetYawDeg += yawOffset;

		float currentYaw = g_Player3D.Rotation.y;
		float delta = targetYawDeg - currentYaw;
		while (delta > 180.0f) delta -= 360.0f;
		while (delta < -180.0f) delta += 360.0f;

		const float rotateLerp = 0.2f;
		g_Player3D.Rotation.y = currentYaw + delta * rotateLerp;
	}

	// �ő呬�x�N�����v�i�������W�b�N�����̂܂܎g�p�j
	if (g_Player3D.Velocity.x >= g_Player3D.maxMoveSpeed)
	{
		g_Player3D.Velocity.x = g_Player3D.maxMoveSpeed;
	}
	else if (g_Player3D.Velocity.x <= -g_Player3D.maxMoveSpeed)
	{
		g_Player3D.Velocity.x = -g_Player3D.maxMoveSpeed;
	}
	if (g_Player3D.Velocity.z >= g_Player3D.maxMoveSpeed)
	{
		g_Player3D.Velocity.z = g_Player3D.maxMoveSpeed;
	}
	else if (g_Player3D.Velocity.z <= -g_Player3D.maxMoveSpeed)
	{
		g_Player3D.Velocity.z = -g_Player3D.maxMoveSpeed;
	}

	// Dash�J�n
	if (IsInputTrigger(DashKey, gPad))
	{
		g_Player3D.state = PLAYER_STATE_DASH;
		g_Player3D.isDash = true;
		g_Player3D.maxMoveSpeed = g_Player3D.FirstMaxMoveSpeed * g_Player3D.dashMoveSpeed;
	}
}

void Player3D_Jump()
{
	if (g_Player3D.isGround || g_Player3D.coyoteFrames > 0)
	{
		ModelResetAnimation(g_Player3D.Model[PLAYER_ANIM_JUMP]);
		g_Player3D.CurrentAnimIndex = PLAYER_ANIM_JUMP;
		g_Player3D.Velocity.y = g_Player3D.jumpPower;
		g_Player3D.isGround = false;
		g_Player3D.coyoteFrames = 0;
	}
	// Always return control to MOVE so the state machine cannot latch in
	// JUMP and re-trigger on landing.
	g_Player3D.state = PLAYER_STATE_MOVE;
}

void Player3D_Change()
{
	// �ϐg����������Ȃ炱����
}

void Player3D_Dash()
{
	if (IsInputPress(DashKey, gPad))
	{
		g_Player3D.isDash = true;
		g_Player3D.maxMoveSpeed = g_Player3D.FirstMaxMoveSpeed * g_Player3D.dashMoveSpeed;

		// Use Move() to handle input and velocity
		Player3D_Move();

		// Only show dash animation if actually moving
		if (g_Player3D.CurrentAnimIndex != PLAYER_ANIM_JUMP)
		{
			bool isMoving = (fabsf(g_Player3D.Velocity.x) > 0.01f ||
				fabsf(g_Player3D.Velocity.z) > 0.01f);

			if (isMoving)
			{
				g_Player3D.CurrentAnimIndex = PLAYER_ANIM_DASH;
			}
			else
			{
				g_Player3D.CurrentAnimIndex = PLAYER_ANIM_IDLE;
			}
		}
	}
	else
	{
		g_Player3D.isDash = false;
		g_Player3D.maxMoveSpeed = g_Player3D.FirstMaxMoveSpeed;
		g_Player3D.state = PLAYER_STATE_MOVE;
	}
}

void Player3D_Action()
{

	if (IsInputTrigger(ActionKey, gPad))
	{

	}
	
	isTrigger = false;

	TRIGGER_HIT hit;
	if (!Collision_PlayerTrigger(&hit, 0.2f)) return;


	if (hit.side != TRIGGER_SIDE_FRONT) return;
	switch (hit.type)
	{
	case FIELD_GOAL:
		// �S�[������
		break;
	case FIELD_OBJ_1:
		isTrigger = true;
		break;
	default:
		break;
	}
}

void Player3D_Draw(void)
{
	if (!g_Player3D.Active) return;

	// Hide the player while the iris is closed / nearly closed so they
	// appear to dissolve into the mist on stage entry. Same gate applies
	// to the stage-clear cinematic emerge: while the mist is dense the
	// player stays hidden, then re-appears as the mist clears.
	if (Iris_GetPlayerAlpha() < 0.10f) return;
	if (StageClearCinematic_GetPlayerAlpha() < 0.10f) return;

	DEBUG_IMGUI_BEGIN({
		 ImGui::Begin("Debug - han");
		 if (ImGui::TreeNode("Player3D.cpp"))
		 {
			 ImGui::Text("Trigger: %s", isTrigger ? "true" : "false");

			 // Jump animation debug
			 MODEL* jm = g_Player3D.Model[PLAYER_ANIM_JUMP];
			 if (jm) {
				 ImGui::Text("Jump AnimTime: %.2f / 73.00", jm->AnimationTime);
				 ImGui::Text("Jump Finished: %s", jm->AnimationFinished ? "YES" : "NO");
			 }
			 ImGui::Text("CurrentAnim: %d (0=IDLE 1=WALK 2=JUMP)", g_Player3D.CurrentAnimIndex);
			 ImGui::Text("State: %d", g_Player3D.state);
			 ImGui::Text("isGround: %s", g_Player3D.isGround ? "YES" : "NO");
			 ImGui::Text("VelY: %.4f", g_Player3D.Velocity.y);

			 ImGui::TreePop();
		 }
		 ImGui::End();
		});


	XMMATRIX scale = XMMatrixScaling
	(
		g_Player3D.Scaling.x,
		g_Player3D.Scaling.y,
		g_Player3D.Scaling.z
	);

	XMMATRIX rotation = XMMatrixRotationRollPitchYaw
	(
		XMConvertToRadians(g_Player3D.Rotation.x),
		XMConvertToRadians(g_Player3D.Rotation.y),
		XMConvertToRadians(g_Player3D.Rotation.z)

	);

	XMMATRIX translation = XMMatrixTranslation
	(
		g_Player3D.Position.x,
		g_Player3D.Position.y,
		g_Player3D.Position.z
	);

	XMMATRIX world = scale * rotation * translation;

	XMMATRIX view = GetViewMatrix();
	XMMATRIX projection = GetProjectionMatrix();
	XMMATRIX wvp = world * view * projection;

	Shader_SetWorldMatrix(world);
	Shader_SetMatrix(wvp);

	if (g_Player3D.CurrentAnimIndex == PLAYER_ANIM_JUMP)
	{
		MODEL* jumpModel = g_Player3D.Model[PLAYER_ANIM_JUMP];
		if (jumpModel != NULL && jumpModel->AnimationFinished)
		{
			g_Player3D.CurrentAnimIndex = PLAYER_ANIM_IDLE;
		}
	}

	MODEL* currentModel = g_Player3D.Model[g_Player3D.CurrentAnimIndex];

	if (currentModel != NULL)
	{
		// Faster animation speed for jump, normal for everything else
		float animSpeed = 10.0f / 600.0f;  // default speed

		//if (g_Player3D.CurrentAnimIndex == PLAYER_ANIM_JUMP)
		//{
		//	animSpeed = 20.0f / 600.0f;  // ~2.5x faster for jump
		//}

		ModelUpdateAnimation(currentModel, animSpeed);
		Shader_SetBones(currentModel);
	}

	ModelDraw(currentModel);
}

XMFLOAT3 Player3D_GetForward()
{
	const float pitch = XMConvertToRadians(g_Player3D.Rotation.x);
	const float yaw = XMConvertToRadians(g_Player3D.Rotation.y);
	const float roll = XMConvertToRadians(g_Player3D.Rotation.z);

	XMMATRIX R = XMMatrixRotationRollPitchYaw(pitch, yaw, roll);

	XMVECTOR f = XMVector3TransformNormal(XMVectorSet(0, 0, -1, 0), R);
	f = XMVector3Normalize(f);

	XMFLOAT3 out;
	XMStoreFloat3(&out, f);
	return out;
}

void Player3D_InitAt(const XMFLOAT3& pos, const XMFLOAT3& rot)
{
	g_Player3D.Position = pos;
	g_Player3D.Rotation = rot;

	g_Player3D.Velocity = XMFLOAT3(0, 0, 0);
	g_Player3D.Acceleration = XMFLOAT3(0.0f, -9.8f / 600.0f * 0.5f, 0.0f);

	g_Player3D.state = PLAYER_STATE_MOVE;
	g_Player3D.isGround = false;
	g_StopTime = 0.0f;

	// Clear dash carry-over. Entering a new stage (or being re-placed by
	// the wall-switch path) must not inherit Shift-held dash from the
	// previous stage / mode. Same root cause as Player3D_Respawn.
	g_Player3D.isDash = false;
	g_Player3D.maxMoveSpeed = g_Player3D.FirstMaxMoveSpeed;
}

void Player3D_SetActive(bool active)
{
	g_Player3D.Active = active;
}

//=========================================================================================================
// map�̓ǂݍ��ݎ��̏����ʒu�ݒ�p
//=========================================================================================================
void Player3D_setposition(XMFLOAT3 pos)
{
	//g_Player3D.Firstposition = pos;

	//if (PLAYER* p = GetPlayer3D())
	//{
	//	g_Player3D.Firstposition = p->Position;
	//	// ���� player3D.cpp ���� Firstposition �ϐ������ɂ���ꍇ�͂�����ɂ�������Ă��������B
	//	// ��: Firstposition = p->Position;
	//}

	g_Player3D.Firstposition = pos;
	g_Player3D.Position = pos;
}

//=========================================================================================================
// �Q�b�^�[
//=========================================================================================================
XMFLOAT3 GetPlayer3DPosition()
{
	return g_Player3D.Position;
}

PLAYER* GetPlayer3D()
{
	return &g_Player3D;
}

XMFLOAT3 Player3D_GetSolidHalfSize()
{
	return g_SolidHalfSize;
}

XMFLOAT3 Player3D_GetTriggerHalfSize()
{
	return g_TriggerHalfSize;
}


void Player3D_Idle()
{
	if (IsInputTrigger(JumpKey, gPad))
	{
		if (!g_Player3D.fountainJumped)
		{
			g_Player3D.state = PLAYER_STATE_JUMP;
		}
		return;
	}

	if (IsInputPress(UpKey, gPad)|| IsInputPress(RightKey, gPad) || IsInputPress(DownKey, gPad) || IsInputPress(LeftKey, gPad))
	{
		g_Player3D.state = PLAYER_STATE_MOVE;
	}

	// Only set IDLE animation if not pushing and not auto and not jumping
	if (!g_Player3D.isPushing && !g_Player3D.isAuto && !g_Player3D.isDash &&
		g_Player3D.CurrentAnimIndex != PLAYER_ANIM_JUMP)
	{
		g_Player3D.CurrentAnimIndex = PLAYER_ANIM_IDLE;
	}

	// �������͊ɂ₩�Ɍ���
	g_Player3D.Velocity.x *= g_Player3D.dampingXZ;
	g_Player3D.Velocity.z *= g_Player3D.dampingXZ;
}

void Player3D_CheckGoal()
{
	TRIGGER_HIT hit;
	if (!Collision_PlayerTrigger(&hit, 0.2f)) return;


	if (hit.type == FIELD_PORTAL_K || hit.type == FIELD_PORTAL_J)
	{
		// ActionKey ���������u�Ԃ̂ݓ]�����s
		if (IsInputTrigger(ActionKey, gPad))
		{
			XMFLOAT3 dest;
			if (Portal_GetDestForTrigger((int)hit.mapIndex, &dest))
			{
				// K��J �̓]�����A���̃y�A����������iJ��K �����j
				if (hit.type == FIELD_PORTAL_K)
				{
					Portal_ActivatePair((int)hit.mapIndex);
				}

				// �v���C���[��]��
				Player3D_setposition(dest);
				g_Player3D.Velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
			}
		}

		return; // �|�[�^��������� Goal/Stage ������X�L�b�v
	}

	if (hit.type == FIELD_FOUNTAIN)
	{
		Fountain_PlayerInteraction();
		return;
	}


	if (hit.type == FIELD_GOAL)
	{
		if (GetFadeState() == FADE_NONE)
		{
			GAME_STAGE cur = GetCurrentStage();
			StageClearState_MarkCleared(cur);

			// Spawn black mist at the goal so the clear has a visible cue
			// before the fade-out. Uses the cleared stage's effect slot; the
			// stage-select cinematic on return uses the same slot for the
			// arrival mist, which is fine because that fires after the fade.
			{
				XMFLOAT3 mistBillPos = GetFieldMap()[hit.mapIndex].pos;
				mistBillPos.y += 2.0f;  // billboard center is door pos + 2
				StageClearEffect_Start(cur, mistBillPos);
			}

			XMFLOAT4 color(0.0f, 0.0f, 0.0f, 1.0f);

			// [FIX] STAGE3 is the final stage: skip stage-select entirely and
			// fade straight to SCENE_RESULT. STAGE1/STAGE2 still return to
			// stage_select so the per-stage cinematic can play.
			if (cur == STAGE_3)
			{
				SetFade(60, color, FADE_OUT, SCENE_RESULT);
			}
			else
			{
				SetCurrentStage(STAGE_SELECT);
				SetFade(60, color, FADE_OUT, SCENE_GAME);
			}
		}
	}
	else if (hit.type == FIELD_STAGE_1 || hit.type == FIELD_STAGE_2 || hit.type == FIELD_STAGE_3)
	{
		// Map the door type to its stage id + lock gate. Cleared stages are
		// re-enterable; only locked stages refuse the trigger.
		GAME_STAGE stage = STAGE_NONE;
		if      (hit.type == FIELD_STAGE_1) stage = STAGE_1;
		else if (hit.type == FIELD_STAGE_2) stage = STAGE_2;
		else                                stage = STAGE_3;

		if (!StageClearState_IsUnlocked(stage)) return;

		// [CHANGE] Replaced SetFade with an iris-out transition. The pixel
		// shader paints solid black outside a shrinking circle centered on
		// the door's screen position; once the circle closes the iris
		// module fires the actual scene swap. This both hides the camera
		// rig switching (StageSelect -> TPS) and gives a more cinematic
		// "diving into the door" feel than a plain fade-to-black.
		if (GetFadeState() == FADE_NONE && !Iris_IsActive() && IsInputTrigger(ActionKey, gPad))
		{
			const auto& map = GetFieldMap();
			XMFLOAT3 doorPos = map[hit.mapIndex].pos;
			// [TUNE] Center the iris on the doorway itself, not the arch top.
			// +1.5 placed the circle visibly above the door; +0.5 lines up
			// with the visible doorway center, where the player walks through.
			doorPos.y += 0.5f;

			XMMATRIX vp = GetViewMatrix() * GetProjectionMatrix();
			XMVECTOR clip = XMVector3TransformCoord(XMLoadFloat3(&doorPos), vp);
			XMFLOAT3 ndc; XMStoreFloat3(&ndc, clip);
			float bw = (float)Direct3D_GetBackBufferWidth();
			float bh = (float)Direct3D_GetBackBufferHeight();
			XMFLOAT2 screen(
				(ndc.x * 0.5f + 0.5f) * bw,
				(1.0f - (ndc.y * 0.5f + 0.5f)) * bh);

			// Spawn black mist at the door so the entry reads as a portal
			// of smoke. The mist runs in stage_select for ~0.7s; by then
			// the iris is well underway so the lingering puffs are hidden
			// behind the closing iris.
			{
				// Mist spawns at door body center so the cloud sits low and
				// the puff radius wraps around the player rather than above
				// them. The mist effect's own DoorYOffset still lifts the
				// puff cloud's center; passing doorPos here (instead of +1.5)
				// places that center near the doorway, not the arch top.
				XMFLOAT3 mistBillPos = doorPos;
				StageClearEffect_Start(stage, mistBillPos);
			}

			// [CHANGE] Start a 1.5s fade-out on the active stage_select BGM
			// so it doesn't snap to silence the instant SetScene fires. The
			// next stage's Game_Initialize fades its own BGM in.
			Iris_StartBgmFadeOut(Game_GetBgm3DId());

			// [TUNE] 120 frames (2s) so the close has room to read instead
			// of snapping past the player. Iris module also enters BlackHold
			// after this, holding solid black until the new scene's FADE_IN
			// alpha takes over -- so the transition stays seamless even
			// though the actual scene swap happens mid-blackout.
			Iris_Start(screen, 120, SCENE_GAME, stage);
		}
	}
}

// =========================================================================================================
// Auto-walk variables (add near the top with other statics)
// =========================================================================================================
static bool  g_AutoWalking = false;
static float g_AutoWalkTargetYaw = 0.0f;     // Target yaw in degrees
static bool  g_AutoWalkTurning = true;        // Currently turning phase
static const float kAutoTurnSpeed = 3.0f;     // Degrees per frame for turning
static const float kAutoWalkSpeed = 0.005f;   // Walk speed during auto-walk

// =========================================================================================================
// �^�C�g����ʗp�F�������s�J�n�i�w������Ɍ�����ς��Ă܂����������j
// =========================================================================================================
void Player3D_StartAutoWalk(float targetYawDeg)
{
	g_AutoWalking = true;
	g_AutoWalkTargetYaw = targetYawDeg;
	g_AutoWalkTurning = true;
	g_Player3D.state = PLAYER_STATE_AUTO_WALK;
	g_Player3D.CurrentAnimIndex = PLAYER_ANIM_WALK;
}

// =========================================================================================================
// �������s�����`�F�b�N
// =========================================================================================================
bool Player3D_IsAutoWalking()
{
	return g_AutoWalking;
}

// =========================================================================================================
// �������s�X�V����
// =========================================================================================================
void Player3D_AutoWalk()
{
	if (!g_AutoWalking) return;

	// Phase 1: Turn towards target yaw
	if (g_AutoWalkTurning)
	{
		float currentYaw = g_Player3D.Rotation.y;
		float delta = g_AutoWalkTargetYaw - currentYaw;

		// Shortest path
		while (delta > 180.0f) delta -= 360.0f;
		while (delta < -180.0f) delta += 360.0f;

		if (fabsf(delta) < kAutoTurnSpeed)
		{
			// Close enough, snap and start walking
			g_Player3D.Rotation.y = g_AutoWalkTargetYaw;
			g_AutoWalkTurning = false;
		}
		else
		{
			// Smoothly rotate
			float sign = (delta > 0.0f) ? 1.0f : -1.0f;
			g_Player3D.Rotation.y += sign * kAutoTurnSpeed;
		}
	}

	// Phase 2: Walk forward in the direction the player is facing
	// The model faces -Z in local space (initial rotation is 180 degrees),
	// so forward = (sin(yaw), 0, cos(yaw)) when yaw includes the 180 offset
	float yawRad = XMConvertToRadians(g_Player3D.Rotation.y - g_Player3D.FirstRotation.y);
	XMFLOAT3 forward = { sinf(yawRad), 0.0f, cosf(yawRad) };

	g_Player3D.Velocity.x += forward.x * kAutoWalkSpeed;
	g_Player3D.Velocity.z += forward.z * kAutoWalkSpeed;

	// Keep walk animation
	g_Player3D.CurrentAnimIndex = PLAYER_ANIM_WALK;
}