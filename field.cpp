//field.cpp
#include "field.h"
#include "camera.h"
#include "model.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <cmath>

#include "debug.h"
#include "player3D.h"
#include "FieldSeesaw.h"
#include "FieldManhole.h"
#include "FieldPortal.h"
#include "FieldFountain.h"
#include "stage_clear_state.h"
#include "manager.h"

//=========================================================================================================
// �}�N����`
//=========================================================================================================
#define BOX_NUM_VERTEX (36)

//=========================================================================================================
//�\���̒�`�E��`
//=========================================================================================================

static Vertex3D Box_vdata[BOX_NUM_VERTEX] = {
	// +Z (Front)
	{{ 0.5f,  0.5f,  0.5f}, { 0,  0,  1}, {1,1,1,1}, {0,0}},
	{{-0.5f,  0.5f,  0.5f}, { 0,  0,  1}, {1,1,1,1}, {1,0}},
	{{ 0.5f, -0.5f,  0.5f}, { 0,  0,  1}, {1,1,1,1}, {0,1}},
	{{-0.5f, -0.5f,  0.5f}, { 0,  0,  1}, {1,1,1,1}, {1,1}},

	// -Z (Back)
	{{-0.5f,  0.5f, -0.5f}, { 0,  0, -1}, {1,1,1,1}, {0,0}},
	{{ 0.5f,  0.5f, -0.5f}, { 0,  0, -1}, {1,1,1,1}, {1,0}},
	{{-0.5f, -0.5f, -0.5f}, { 0,  0, -1}, {1,1,1,1}, {0,1}},
	{{ 0.5f, -0.5f, -0.5f}, { 0,  0, -1}, {1,1,1,1}, {1,1}},

	// +X (Right)
	{{ 0.5f,  0.5f, -0.5f}, { 1,  0,  0}, {1,1,1,1}, {0,0}},
	{{ 0.5f,  0.5f,  0.5f}, { 1,  0,  0}, {1,1,1,1}, {1,0}},
	{{ 0.5f, -0.5f, -0.5f}, { 1,  0,  0}, {1,1,1,1}, {0,1}},
	{{ 0.5f, -0.5f,  0.5f}, { 1,  0,  0}, {1,1,1,1}, {1,1}},

	// -X (Left)
	{{-0.5f,  0.5f,  0.5f}, {-1,  0,  0}, {1,1,1,1}, {0,0}},
	{{-0.5f,  0.5f, -0.5f}, {-1,  0,  0}, {1,1,1,1}, {1,0}},
	{{-0.5f, -0.5f,  0.5f}, {-1,  0,  0}, {1,1,1,1}, {0,1}},
	{{-0.5f, -0.5f, -0.5f}, {-1,  0,  0}, {1,1,1,1}, {1,1}},

	// +Y (Top)
	{{-0.5f,  0.5f,  0.5f}, { 0,  1,  0}, {1,1,1,1}, {0,0}},
	{{ 0.5f,  0.5f,  0.5f}, { 0,  1,  0}, {1,1,1,1}, {1,0}},
	{{-0.5f,  0.5f, -0.5f}, { 0,  1,  0}, {1,1,1,1}, {0.0f,0.25f}},
	{{ 0.5f,  0.5f, -0.5f}, { 0,  1,  0}, {1,1,1,1}, {1.0f,0.25f}},

	// -Y (Bottom)
	{{-0.5f, -0.5f, -0.5f}, { 0, -1,  0}, {1,1,1,1}, {0.0f,0.75f}},
	{{ 0.5f, -0.5f, -0.5f}, { 0, -1,  0}, {1,1,1,1}, {1.0f,0.75f}},
	{{-0.5f, -0.5f,  0.5f}, { 0, -1,  0}, {1,1,1,1}, {0,1}},
	{{ 0.5f, -0.5f,  0.5f}, { 0, -1,  0}, {1,1,1,1}, {1,1}},
};

//static Vertex3D Box_vdata[BOX_NUM_VERTEX] = {
//	//���W                  Normal               �F                    �e�N�X�`�����W
//
//	//�O�� (Z+)
//	//���_0 LEFT-TOP
//	{{ 0.5f,  0.5f, 0.5f},{ 0.5f,  0.5f, 0.5f}, {1.0f,1.0f,1.0f,1.0f} ,{0.0f,0.0f}},
//	//���_1 RIGHT-TOP						  
//	{{-0.5f,  0.5f, 0.5f},{ 0.5f,  0.5f, 0.5f}, {1.0f,1.0f,1.0f,1.0f} ,{1.0f,0.0f}},
//	//���_2 LEFT-BOTTOM						  
//	{{ 0.5f, -0.5f, 0.5f},{ 0.5f,  0.5f, 0.5f}, {1.0f,1.0f,1.0f,1.0f} ,{0.0f,1.0f}},
//	//���_3 RIGHT-BOTTOM
//	{{-0.5f, -0.5f, 0.5f},{ 0.5f,  0.5f, 0.5f}, {1.0f,1.0f,1.0f,1.0f}, {1.0f,1.0f}},
//
//	//�O�� (Z-)
//	//���_4 LEFT-TOP
//	{{-0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f, 0.5f},{1.0f,1.0f,1.0f,1.0f} ,{0.0f,0.0f}},
//	//���_5 RIGHT-TOP		
//	{{ 0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f, 0.5f},{1.0f,1.0f,1.0f,1.0f} ,{1.0f,0.0f}},
//	//���_6 LEFT-BOTTOM	 
//	{{-0.5f, -0.5f, -0.5f}, { 0.5f,  0.5f, 0.5f},{1.0f,1.0f,1.0f,1.0f} ,{0.0f,1.0f}},
//	//���_7 RIGHT-BOTTOM
//	{{ 0.5f, -0.5f, -0.5f}, { 0.5f,  0.5f, 0.5f}, {1.0f,1.0f,1.0f,1.0f}, {1.0f,1.0f}},
//
//	//�O�� (X+)
//	//���_8 LEFT-TOP
//	{{ 0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f, 0.5f}, {1.0f,1.0f,1.0f,1.0f} ,{0.0f,0.0f}},
//	//���_9 RIGHT-TOP
//	{{ 0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f, 0.5f}, {1.0f,1.0f,1.0f,1.0f} ,{1.0f,0.0f}},
//	//���_10 LEFT-BOTTOM
//	{{ 0.5f, -0.5f, -0.5f}, { 0.5f,  0.5f, 0.5f}, {1.0f,1.0f,1.0f,1.0f} ,{0.0f,1.0f}},
//	//���_11 RIGHT-BOTTOM
//	{{ 0.5f, -0.5f,  0.5f}, { 0.5f,  0.5f, 0.5f}, {1.0f,1.0f,1.0f,1.0f}, {1.0f,1.0f}},
//
//	//�O�� (X-)
//	//���_12 LEFT-TOP
//	{{-0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f, 0.5f}, {1.0f,1.0f,1.0f,1.0f} ,{0.0f,0.0f}},
//	//���_13 RIGHT-TOP
//	{{-0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f, 0.5f}, {1.0f,1.0f,1.0f,1.0f} ,{1.0f,0.0f}},
//	//���_14 LEFT-BOTTOM
//	{{-0.5f, -0.5f,  0.5f}, { 0.5f,  0.5f, 0.5f}, {1.0f,1.0f,1.0f,1.0f} ,{0.0f,1.0f}},
//	//���_15 RIGHT-BOTTOM
//	{{-0.5f, -0.5f, -0.5f}, { 0.5f,  0.5f, 0.5f}, {1.0f,1.0f,1.0f,1.0f}, {1.0f,1.0f}},
//
//	//�O�� (Y+)
//	//���_16 LEFT-TOP
//	{{-0.5f,  0.5f,  0.5f}, { 0.0f,  1.0f, 0.0f}, {1.0f,1.0f,1.0f,1.0f} ,{0.0f,0.0f}},
//	//���_17 RIGHT-TOP
//	{{ 0.5f,  0.5f,  0.5f}, { 0.0f,  1.0f, 0.0f}, {1.0f,1.0f,1.0f,1.0f} ,{1.0f,0.0f}},
//	//���_18 LEFT-BOTTOM
//	{{-0.5f,  0.5f, -0.5f}, { 0.0f,  1.0f, 0.0f}, {1.0f,1.0f,1.0f,1.0f} ,{0.0f,0.25f}},
//	//���_19 RIGHT-BOTTOM
//	{{ 0.5f,  0.5f, -0.5f}, { 0.0f,  1.0f, 0.0f}, {1.0f,1.0f,1.0f,1.0f}, {1.0f,0.25f}},
//
//	//�O�� (Y-)
//	//���_20 LEFT-TOP
//	{{-0.5f, -0.5f, -0.5f}, { 0.5f,  0.5f, 0.5f}, {1.0f,1.0f,1.0f,1.0f} ,{0.0f,0.75f}},
//	//���_21 RIGHT-TOP
//	{{ 0.5f, -0.5f, -0.5f}, { 0.5f,  0.5f, 0.5f}, {1.0f,1.0f,1.0f,1.0f} ,{1.0f,0.75f}},
//	//���_22 LEFT-BOTTOM
//	{{-0.5f, -0.5f,  0.5f}, { 0.5f,  0.5f, 0.5f}, {1.0f,1.0f,1.0f,1.0f} ,{0.0f,1.0f}},
//	//���_23 RIGHT-BOTTOM
//	{{ 0.5f, -0.5f,  0.5f}, { 0.5f,  0.5f, 0.5f}, {1.0f,1.0f,1.0f,1.0f}, {1.0f,1.0f}},
//};

static UINT Box_idxdata[6 * 6] =
{
	0,  1,  2,  2,  1,  3,//-Z
	4,  5,  6,  6,  5,  7,//+Z
	8,  9, 10, 10,  9, 11,//+X
   12, 13, 14, 14, 13, 15,//-X
   16, 17, 18, 18, 17, 19,//+Y
   20, 21, 22, 22, 21, 23,//-Y

};

//=========================================================================================================
// �O���[�o���ϐ�
//=========================================================================================================
static ID3D11Device* g_pDevice = NULL;
static ID3D11DeviceContext* g_pContext = NULL;
static ID3D11ShaderResourceView* g_Texture;		//�e�N�X�`���ϐ�
static ID3D11Buffer* g_VertexBuffer = NULL;		// ���_�o�b�t�@
static ID3D11Buffer* g_IndexBuffer = NULL;		// �C���f�b�N�X�o�b�t�@
// �ǉ�: �v���C���[�����ʒu�iR�̓ǂݎ�茋�ʁj��ێ�
static XMFLOAT3 g_PlayerStartPos = XMFLOAT3(0.0f, 0.0f, 0.0f);

MODEL* Model[FIELD_MAX] = { NULL };
static std::vector<MAPDATA> g_MapData;

static GAME_STAGE g_CurrentStage = STAGE_NONE;

static void EnsureBoxCreated()
{
	if (g_VertexBuffer && g_IndexBuffer) return;
	CreateBox();
}

//=========================================================================================================
// ������
//=========================================================================================================
void field_Initialize(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	g_pDevice = pDevice;
	g_pContext = pContext;

	Seesaw_Initialize();
	Manhole_Initialize();
	Portal_Initialize();
	Fountain_Initialize();

	// �e�N�X�`��
	TexMetadata metadata;
	ScratchImage image;
	LoadFromWICFile(L"asset\\Texture\\renga.png", WIC_FLAGS_NONE, &metadata, image);
	CreateShaderResourceView(pDevice, image.GetImages(), image.GetImageCount(), metadata, &g_Texture);
	assert(g_Texture);

	if (GetScene() == SCENE_TITLE)
	{
		if (!LoadMapFromFile("asset\\MapData\\stage_title.txt"))
		{
			// Error:  could not load map
			MessageBox(nullptr, "Failed to load map file! Error", "�G���[", MB_OK);
		}
	}

	

	bool hasGround = false;
	bool hasWall = false;
	for (const auto& m : g_MapData) {
		if (m.no == FIELD_GROUND) hasGround = true;
		if (m.no == FIELD_WALL)   hasWall = true;
	}

	if (hasGround || hasWall || !Model[FIELD_OBJ_1] || !Model[FIELD_OBJ_2] || !Model[FIELD_OBJ_3] 
		|| !Model[FIELD_EMPTY_BOX]) EnsureBoxCreated();

	if (!Model[FIELD_GROUND])
	{
		Model[FIELD_GROUND] = ModelLoad("asset\\model\\Yuka.fbx");

		//Model[FIELD_GROUND] = ModelLoad("asset\\model\\Building_B.fbx");
		//Model[FIELD_GROUND] = ModelLoad("asset\\model\\test.fbx");
	}
	if (!Model[FIELD_WALL])
	{
		Model[FIELD_WALL] = ModelLoad("asset\\model\\Kabe.fbx");
	}
	if (!Model[FIELD_OBJ_BOX])
	{
		Model[FIELD_OBJ_BOX] = ModelLoad("asset\\model\\Box_test_08.fbx",true);
	}
	if (!Model[FIELD_OBJ_1])
	{
		Model[FIELD_OBJ_1] = ModelLoad("asset\\model\\Building_A.fbx");
	}
	if (!Model[FIELD_OBJ_2])
	{
		Model[FIELD_OBJ_2] = ModelLoad("asset\\model\\pole.fbx");
		//CreateBox();
	}
	if (!Model[FIELD_OBJ_3])
	{
		Model[FIELD_OBJ_3] = ModelLoad("asset\\model\\ball.fbx");
	}
	if (!Model[FIELD_BENCH])
	{
		Model[FIELD_BENCH] = ModelLoad("asset\\model\\bench.fbx");
	}
	if (!Model[FIELD_DUSTBOX])
	{
		//Model[FIELD_DUSTBOX] = ModelLoad("asset\\model\\Dustbox.fbx");
	}
	if (!Model[FIELD_FOUNTAIN])
	{
		Model[FIELD_FOUNTAIN] = ModelLoad("asset\\model\\fountain.fbx");
	}
	if (!Model[FIELD_POLE])
	{
		Model[FIELD_POLE] = ModelLoad("asset\\model\\pole.fbx");
	}
	if (!Model[FIELD_FENCE])
	{
		//Model[FIELD_FENCE] = ModelLoad("asset\\model\\fence.fbx");
	}
	if (!Model[FIELD_GOAL])
	{
		Model[FIELD_GOAL] = ModelLoad("asset\\model\\arch.fbx");
	}

	if (!Model[FIELD_STAGE_1])
	{
		Model[FIELD_STAGE_1] = ModelLoad("asset\\model\\arch.fbx");
	}													
	if (!Model[FIELD_STAGE_2])							
	{													
		Model[FIELD_STAGE_2] = ModelLoad("asset\\model\\arch.fbx");
	}													
	if (!Model[FIELD_STAGE_3])							
	{													
		Model[FIELD_STAGE_3] = ModelLoad("asset\\model\\arch.fbx");
	}													
														
	if (!Model[FIELD_PORTAL_K])							
	{													
		Model[FIELD_PORTAL_K] = ModelLoad("asset\\model\\ball_k.fbx");
	}

	if (!Model[FIELD_PORTAL_J])
	{
		Model[FIELD_PORTAL_J] = ModelLoad("asset\\model\\ball_j.fbx");
	}


	if (!Model[FIELD_SEESAW_1])
	{
		Model[FIELD_SEESAW_1] = ModelLoad("asset\\model\\Seesaw_dodai.fbx");
	}
	if (!Model[FIELD_SEESAW_2])
	{
		Model[FIELD_SEESAW_2] = ModelLoad("asset\\model\\Seesaw_siso.fbx");
	}
	
}


//=========================================================================================================
// �I��
//=========================================================================================================
void field_Finalize(void)
{
	Seesaw_Finalize();
	Manhole_Finalize();
	Portal_Finalize();
	Fountain_Finalize();

	g_MapData.clear();  // Clear the vector

	for (int i = 0; i < FIELD_MAX; i++)
	{
		if (Model[i] != NULL) {
			ModelRelease(Model[i]);
			Model[i] = NULL;
		}
	}
	SAFE_RELEASE(g_VertexBuffer);
	SAFE_RELEASE(g_IndexBuffer);
	SAFE_RELEASE(g_Texture);
}

//=========================================================================================================
// �X�V
//=========================================================================================================
// Box gravity: FIELD_OBJ_BOX cells fall straight down (no rotation) until they
// rest on a solid surface or another box. STAGE_2 drops boxes off ledges to
// build a 3-box bridge to the goal. Velocity is stored per-cell in MAPDATA.fallVelY.
static bool IsBoxSupport(FIELD t)
{
    switch (t)
    {
    case FIELD_GROUND:
    case FIELD_WALL:
    case FIELD_EMPTY_BOX:
    case FIELD_OBJ_BOX:
    case FIELD_MANHOLE:
    case FIELD_SEESAW_1:
    case FIELD_SEESAW_2:
        return true;
    default:
        return false;
    }
}

static void Box_UpdateGravity()
{
    const float kGrav    = 9.8f / 600.0f * 0.5f;  // ~0.0082, matches player gravity
    const float kMaxFall = 0.5f;
    for (size_t i = 0; i < g_MapData.size(); ++i)
    {
        MAPDATA& box = g_MapData[i];
        if (box.no != FIELD_OBJ_BOX) continue;

        const XMFLOAT3 bh = Field_GetCollisionHalfSize(box);
        const float boxBottom = box.pos.y - bh.y;

        // Highest support top directly below the box (box center over it).
        float supportTop = -1000.0f;
        for (size_t j = 0; j < g_MapData.size(); ++j)
        {
            if (j == i) continue;
            const MAPDATA& o = g_MapData[j];
            if (!IsBoxSupport(o.no)) continue;
            const XMFLOAT3 oh = Field_GetCollisionHalfSize(o);
            if (std::fabs(box.pos.x - o.pos.x) > oh.x + 0.05f) continue;
            if (std::fabs(box.pos.z - o.pos.z) > oh.z + 0.05f) continue;
            const float oTop = o.pos.y + oh.y;
            if (oTop <= boxBottom + 0.05f && oTop > supportTop) supportTop = oTop;
        }

        if (boxBottom > supportTop + 0.02f)
        {
            box.fallVelY -= kGrav;
            if (box.fallVelY < -kMaxFall) box.fallVelY = -kMaxFall;
            if (boxBottom + box.fallVelY <= supportTop)   // would pass through -> land
            {
                box.pos.y = supportTop + bh.y;
                box.fallVelY = 0.0f;
            }
            else
            {
                box.pos.y += box.fallVelY;
            }
        }
        else
        {
            box.fallVelY = 0.0f;
            if (supportTop > -999.0f) box.pos.y = supportTop + bh.y;  // rest exactly
        }
        box.rotate = XMFLOAT3(0.0f, 0.0f, 0.0f);  // straight fall, no rotation
    }
}

void field_Update(void)
{
	DEBUG_IMGUI_BEGIN({
		ImGui::Begin("Debug - han");
		if (ImGui::TreeNode("Filed.cpp"))
		{

			ImGui::TreePop();
		}
		ImGui::End();
		});

	const float deltaTime = 1.0f / 60.0f;
	Seesaw_UpdateAll(deltaTime);
	Manhole_UpdateAll(deltaTime);
	Box_UpdateGravity();
	Fountain_UpdateAll(deltaTime);


	for (size_t i = 0; i < g_MapData.size(); ++i)
	{
		if (g_MapData[i].no == FIELD_OBJ_1)
		{
			//g_MapData[i].scale = XMFLOAT3(1.0f, 4.0f, 10.0f);
			//g_MapData[i].rotate = XMFLOAT3(0.0f, 20.0f, 0.0f);
		}
		else if (g_MapData[i].no == FIELD_SEESAW_2)
		{
			//g_MapData[i].colliderHalf = XMFLOAT3(0.37f, 0.2f, 2.7f);
			//g_MapData[i].rotate = XMFLOAT3(0.0f, 20.0f, 0.0f);
		}

		else if (g_MapData[i].no == FIELD_OBJ_2)
		{
			g_MapData[i].scale = XMFLOAT3(0.01f, 0.01f, 0.01f);
			g_MapData[i].rotate = XMFLOAT3(0.0f, 0.0f, 0.0f);
		}

		else if (g_MapData[i].no == FIELD_BENCH)
		{
			g_MapData[i].scale = XMFLOAT3(0.01f, 0.01f, 0.01f);
			g_MapData[i].rotate = XMFLOAT3(90.0f, 0.0f, 90.0f);
		}

		else if (g_MapData[i].no == FIELD_DUSTBOX)
		{
			g_MapData[i].scale = XMFLOAT3(1.0f, 1.0f, 1.0f);
			g_MapData[i].rotate = XMFLOAT3(0.0f, 0.0f, 0.0f);
		}

		else if (g_MapData[i].no == FIELD_POLE)
		{
			g_MapData[i].scale = XMFLOAT3(0.01f, 0.01f, 0.01f);
			g_MapData[i].rotate = XMFLOAT3(90.0f, 0.0f, 90.0f);
		}

		else if (g_MapData[i].no == FIELD_FENCE)
		{
			g_MapData[i].scale = XMFLOAT3(0.01f, 0.01f, 0.01f);
			g_MapData[i].rotate = XMFLOAT3(90.0f, 0.0f, 90.0f);
		}
	}
}

//=========================================================================================================
// �`��
//=========================================================================================================
// [PERF] Draw a merged FIELD_WALL block as a unit-Box geometry with per-face
// UVs scaled by the world extent so the wall texture TILES instead of
// stretching. Visual matches a row of unmerged 1x1x1 walls; no perf cost
// versus a single FBX draw (still one DrawIndexed call), and the dynamic VB
// write is 36 vertices = trivial.
//
// Box_vdata layout from top of this file (face order, 4 verts each):
//   0..3   +Z front  : u along +X, v along -Y -> uvScale = (sx, sy)
//   4..7   -Z back   : u along -X, v along -Y -> uvScale = (sx, sy)
//   8..11  +X right  : u along -Z, v along -Y -> uvScale = (sz, sy)
//   12..15 -X left   : u along +Z, v along -Y -> uvScale = (sz, sy)
//   16..19 +Y top    : original tex Y range is 0..0.25 (atlas); multiplying
//                      by (sx, sz) tiles the same atlas slot.
//   20..23 -Y bottom : same as top; atlas Y 0.75..1
static void DrawTiledWallBox(const XMMATRIX& WorldMatrix,
                              const XMMATRIX& WVP,
                              float sx, float sy, float sz)
{
	const float faceScaleU[6] = { sx, sx, sz, sz, sx, sx };
	const float faceScaleV[6] = { sy, sy, sy, sy, sz, sz };

	D3D11_MAPPED_SUBRESOURCE msr;
	if (FAILED(g_pContext->Map(g_VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr)))
		return;
	Vertex3D* v = (Vertex3D*)msr.pData;
	for (int i = 0; i < BOX_NUM_VERTEX; ++i)
	{
		v[i] = Box_vdata[i];
		const int face = i / 4;  // 0..5
		v[i].texCoord.x = Box_vdata[i].texCoord.x * faceScaleU[face];
		v[i].texCoord.y = Box_vdata[i].texCoord.y * faceScaleV[face];
	}
	g_pContext->Unmap(g_VertexBuffer, 0);

	Shader_SetWorldMatrix(WorldMatrix);
	Shader_SetMatrix(WVP);
	g_pContext->PSSetShaderResources(0, 1, &g_Texture);

	UINT stride = sizeof(Vertex3D);
	UINT offset = 0;
	g_pContext->IASetVertexBuffers(0, 1, &g_VertexBuffer, &stride, &offset);
	g_pContext->IASetIndexBuffer(g_IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
	g_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	g_pContext->DrawIndexed(6 * 6, 0, 0);
}

void field_Draw(void)
{
	Shader_Begin();
	XMMATRIX Projection = GetProjectionMatrix();
	XMMATRIX View = GetViewMatrix();
	XMMATRIX VP = View * Projection;

	for (size_t i = 0; i < g_MapData.size(); ++i)
	{
		// [FIX] Apply stage-clear cinematic Y-collapse to the matching
		// door arch so it visibly shrinks down to nothing during the
		// movie. Non-target stages and non-arches keep Y-scale 1.0.
		// [FEAT] Sequential unlock: hide locked stages' doors entirely. Plus
		// the existing cinematic Y-collapse for the just-cleared stage.
		float doorScaleY = 1.0f;
		if (g_MapData[i].no == FIELD_STAGE_1)
		{
			if (!StageClearState_IsUnlocked(STAGE_1)) continue;
			doorScaleY = StageClearCinematic_GetDoorScaleY(STAGE_1);
		}
		else if (g_MapData[i].no == FIELD_STAGE_2)
		{
			if (!StageClearState_IsUnlocked(STAGE_2)) continue;
			doorScaleY = StageClearCinematic_GetDoorScaleY(STAGE_2);
		}
		else if (g_MapData[i].no == FIELD_STAGE_3)
		{
			if (!StageClearState_IsUnlocked(STAGE_3)) continue;
			doorScaleY = StageClearCinematic_GetDoorScaleY(STAGE_3);
		}
		if (doorScaleY <= 1e-4f) continue;  // fully collapsed: skip draw
		// Skip light ball -- drawn separately unlit by Field_DrawLightBalls.
		if (g_MapData[i].no == FIELD_OBJ_3) continue;
		XMMATRIX ScalingMatrix = XMMatrixScaling(
			g_MapData[i].scale.x,
			g_MapData[i].scale.y * doorScaleY,
			g_MapData[i].scale.z);
		XMMATRIX TranslationMatrix = XMMatrixTranslation(
			g_MapData[i].pos.x,
			g_MapData[i].pos.y,
			g_MapData[i].pos.z);
		XMMATRIX RotationMatrix = XMMatrixRotationRollPitchYaw(
			XMConvertToRadians(g_MapData[i].rotate.x),
			XMConvertToRadians(g_MapData[i].rotate.y),
			XMConvertToRadians(g_MapData[i].rotate.z));

		XMMATRIX WorldMatrix = ScalingMatrix * RotationMatrix * TranslationMatrix;
		XMMATRIX WVP = WorldMatrix * VP;

		// [PERF] Merged FIELD_WALL blocks (scale != 1) use the tiled-Box
		// path so the texture repeats per world unit instead of stretching.
		// Visually matches a run of unmerged 1x1x1 walls.
		if (g_MapData[i].no == FIELD_WALL)
		{
			const float sx = g_MapData[i].scale.x;
			const float sy = g_MapData[i].scale.y;
			const float sz = g_MapData[i].scale.z;
			const bool isMerged =
				std::fabs(sx - 1.0f) > 0.01f ||
				std::fabs(sy - 1.0f) > 0.01f ||
				std::fabs(sz - 1.0f) > 0.01f;
			if (isMerged)
			{
				DrawTiledWallBox(WorldMatrix, WVP, sx, sy, sz);
				continue;
			}
		}

		Shader_SetWorldMatrix(WorldMatrix);
		Shader_SetMatrix(WVP);



		if (Model[g_MapData[i].no])
			ModelDraw(Model[g_MapData[i].no]);
		else if (g_MapData[i].no == FIELD_EMPTY_BOX)
		{
			continue;
		}
		else {
			g_pContext->PSSetShaderResources(0, 1, &g_Texture);
			UINT stride = sizeof(Vertex3D);
			UINT offset = 0;
			g_pContext->IASetVertexBuffers(0, 1, &g_VertexBuffer, &stride, &offset);
			g_pContext->IASetIndexBuffer(g_IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
			g_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			g_pContext->DrawIndexed(6 * 6, 0, 0);
		}
	}

	DEBUG_IMGUI_BEGIN({
		Seesaw_DebugDraw();
		Manhole_DebugDraw();
		Portal_DebugDraw();
		Fountain_DebugDraw();
		});
}

//=========================================================================================================
// Box�쐬
// Draw all FIELD_OBJ_3 light balls with lighting disabled so they appear
// full-bright (moon look). Caller is responsible for restoring its scene
// LIGHT state afterwards.
void Field_DrawLightBalls(void)
{
	if (!Model[FIELD_OBJ_3]) return;

	LIGHT off = {};
	off.Enble = FALSE;
	Shader_SetLight(off);

	Shader_Begin();
	XMMATRIX Projection = GetProjectionMatrix();
	XMMATRIX View = GetViewMatrix();
	XMMATRIX VP = View * Projection;

	for (size_t i = 0; i < g_MapData.size(); ++i)
	{
		if (g_MapData[i].no != FIELD_OBJ_3) continue;

		XMMATRIX S = XMMatrixScaling(g_MapData[i].scale.x, g_MapData[i].scale.y, g_MapData[i].scale.z);
		XMMATRIX R = XMMatrixRotationRollPitchYaw(
			XMConvertToRadians(g_MapData[i].rotate.x),
			XMConvertToRadians(g_MapData[i].rotate.y),
			XMConvertToRadians(g_MapData[i].rotate.z));
		XMMATRIX T = XMMatrixTranslation(g_MapData[i].pos.x, g_MapData[i].pos.y, g_MapData[i].pos.z);
		XMMATRIX W = S * R * T;
		XMMATRIX WVP = W * VP;

		Shader_SetWorldMatrix(W);
		Shader_SetMatrix(WVP);
		ModelDraw(Model[FIELD_OBJ_3]);
	}
}

//=========================================================================================================
void CreateBox()
{
	// ���_�o�b�t�@
	D3D11_BUFFER_DESC bd = {};
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.ByteWidth = sizeof(Vertex3D) * BOX_NUM_VERTEX;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	g_pDevice->CreateBuffer(&bd, NULL, &g_VertexBuffer);

	D3D11_MAPPED_SUBRESOURCE msr;
	g_pContext->Map(g_VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
	Vertex3D* vertex = (Vertex3D*)msr.pData;
	CopyMemory(vertex, Box_vdata, sizeof(Vertex3D) * BOX_NUM_VERTEX);
	g_pContext->Unmap(g_VertexBuffer, 0);

	// �C���f�b�N�X�o�b�t�@
	{
		//���_�o�b�t�@�쐬
		D3D11_BUFFER_DESC bd;
		ZeroMemory(&bd, sizeof(bd));//0�ŃN���A
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.ByteWidth = sizeof(UINT) * BOX_NUM_VERTEX;//�i�[�ł��钸�_��*���_�T�C�Y
		bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		g_pDevice->CreateBuffer(&bd, NULL, &g_IndexBuffer);

		//index buffer read in 
		D3D11_MAPPED_SUBRESOURCE msr;
		g_pContext->Map(g_IndexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
		UINT* index = (UINT*)msr.pData;

		//copy index buffer
		CopyMemory(&index[0], &Box_idxdata[0], sizeof(UINT) * BOX_NUM_VERTEX);
		g_pContext->Unmap(g_IndexBuffer, 0);
	}
}

XMMATRIX Field_GetWorldMatrix(int i)
{
	const auto& m = g_MapData[i];

	XMMATRIX ScalingMatrix = XMMatrixScaling(m.scale.x, m.scale.y, m.scale.z);
	XMMATRIX TranslationMatrix = XMMatrixTranslation(m.pos.x, m.pos.y, m.pos.z);
	XMMATRIX RotationMatrix = XMMatrixRotationRollPitchYaw(
		XMConvertToRadians(m.rotate.x),
		XMConvertToRadians(m.rotate.y),
		XMConvertToRadians(m.rotate.z));
	return ScalingMatrix * RotationMatrix * TranslationMatrix;
}

void Field_DrawShadowMap(const XMMATRIX& world, const XMMATRIX& matrix, int i)
{
	Shader_SetWorldMatrix(world);
	Shader_SetMatrix(matrix);
	// Set vertex and index buffers for box

	if (g_MapData[i].no == FIELD_GROUND)
		return; // skip shadow

	if (g_MapData[i].no == FIELD_EMPTY_BOX)
		return;

	if (g_MapData[i].no == FIELD_FOUNTAIN)
		return;

	if (g_MapData[i].no == FIELD_PORTAL_K)
		return;

	if (g_MapData[i].no == FIELD_PORTAL_J)
		return;

	if (g_MapData[i].no == FIELD_OBJ_3)
		return;


	if (Model[g_MapData[i].no])
	{
		ModelDraw(Model[g_MapData[i].no]);
		return;
	}

	UINT stride = sizeof(Vertex3D);
	UINT offset = 0;
	g_pContext->IASetVertexBuffers(0, 1, &g_VertexBuffer, &stride, &offset);
	g_pContext->IASetIndexBuffer(g_IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
	g_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	g_pContext->DrawIndexed(6 * 6, 0, 0);

}





//=========================================================================================================
// txt���[�h
//=========================================================================================================
std::vector<MAPDATA>& GetFieldMap()
{
	return g_MapData;
}

// [PERF] Greedy-merge contiguous cells of the given FIELD type into larger
// axis-aligned blocks. Each unmerged cell costs one FBX draw call (and one
// per cubemap face when the shadow rebuild fires); STAGE_1 has 767 wall +
// 380 ground cells. After merging, a single back-wall column at
// (x=0, z=0..23, y=1) becomes ONE MAPDATA with scale.z=24 -- 24 draws
// collapse to 1.
//
// Strategy (1D-first greedy, simple and good enough):
//   For each 1-unit cell (in sorted Y/X/Z order):
//     1. Extend along +Z as far as contiguous cells exist.
//     2. If the Z-run is just 1, extend along +X instead.
//     3. If both are 1, extend along +Y.
//   Emit one merged MAPDATA per block, removing consumed cells.
//
// Texture stretches across the block (user explicitly approved this trade
// for walls; ground is a top-down floor whose stretch is barely visible).
// Collision uses scale (Field_GetCollisionHalfSize) so resolution stays
// correct. Camera collision also uses scale.
//
// Only operates on plain 1x1x1 cells with no rotation / custom collider so
// it does not touch hand-tuned entries (e.g., stage doors which scale 2x).
static void MergeContiguousField(FIELD targetType)
{
    auto isMergeable = [targetType](const MAPDATA& d) {
        return d.no == targetType
            && std::fabs(d.scale.x - 1.0f) < 0.01f
            && std::fabs(d.scale.y - 1.0f) < 0.01f
            && std::fabs(d.scale.z - 1.0f) < 0.01f
            && std::fabs(d.rotate.x) < 0.01f
            && std::fabs(d.rotate.y) < 0.01f
            && std::fabs(d.rotate.z) < 0.01f
            && !d.useCustomCollider;
    };

    // Encode integer cell coords into a 64-bit key. Offsets keep negative
    // coordinates (-2 minimum from map load) in the positive range.
    auto encode = [](int x, int y, int z) -> long long {
        const long long OFS = 1024;
        return ((long long)(x + OFS) << 40) | ((long long)(y + OFS) << 20) | (long long)(z + OFS);
    };

    struct Cell { int x, y, z; };
    std::vector<Cell> cells;
    std::unordered_set<long long> set;
    cells.reserve(g_MapData.size());

    for (const auto& d : g_MapData)
    {
        if (!isMergeable(d)) continue;
        int ix = (int)std::lround(d.pos.x);
        int iy = (int)std::lround(d.pos.y);
        int iz = (int)std::lround(d.pos.z);
        // Sanity: cell pos must be integer (we only emit at integer grid).
        if (std::fabs(d.pos.x - (float)ix) > 0.01f) continue;
        if (std::fabs(d.pos.y - (float)iy) > 0.01f) continue;
        if (std::fabs(d.pos.z - (float)iz) > 0.01f) continue;
        long long k = encode(ix, iy, iz);
        if (set.insert(k).second) cells.push_back({ ix, iy, iz });
    }
    if (cells.empty()) return;

    // Strip eligible cells from g_MapData; merged blocks are appended below.
    g_MapData.erase(
        std::remove_if(g_MapData.begin(), g_MapData.end(), isMergeable),
        g_MapData.end());

    // Deterministic emit order so block centers are stable across runs.
    std::sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) {
        if (a.y != b.y) return a.y < b.y;
        if (a.x != b.x) return a.x < b.x;
        return a.z < b.z;
    });

    auto has = [&](int x, int y, int z) {
        return set.find(encode(x, y, z)) != set.end();
    };
    auto take = [&](int x, int y, int z) {
        set.erase(encode(x, y, z));
    };

    for (const auto& start : cells)
    {
        if (!has(start.x, start.y, start.z)) continue;

        int zLen = 1;
        while (has(start.x, start.y, start.z + zLen)) ++zLen;

        int xLen = 1;
        if (zLen == 1)
        {
            while (has(start.x + xLen, start.y, start.z)) ++xLen;
        }

        int yLen = 1;
        if (zLen == 1 && xLen == 1)
        {
            while (has(start.x, start.y + yLen, start.z)) ++yLen;
        }

        for (int dy = 0; dy < yLen; ++dy)
        for (int dx = 0; dx < xLen; ++dx)
        for (int dz = 0; dz < zLen; ++dz)
            take(start.x + dx, start.y + dy, start.z + dz);

        MAPDATA md;
        md.no    = targetType;
        md.pos   = XMFLOAT3(
            (float)start.x + (xLen - 1) * 0.5f,
            (float)start.y + (yLen - 1) * 0.5f,
            (float)start.z + (zLen - 1) * 0.5f);
        md.scale = XMFLOAT3((float)xLen, (float)yLen, (float)zLen);
        md.rotate = XMFLOAT3(0.0f, 0.0f, 0.0f);
        md.useCustomCollider = false;
        g_MapData.push_back(md);
    }
}

bool LoadMapFromFile(const char* filename)
{
	std::ifstream file(filename);
	if (!file.is_open()) return false;

	// Detect which stage we're loading
	if (strstr(filename, "stage_select") != nullptr)
	{
		g_CurrentStage = STAGE_SELECT;
	}
	else if (strstr(filename, "stage1") != nullptr)
	{
		g_CurrentStage = STAGE_1;
	}
	else if (strstr(filename, "stage2") != nullptr)
	{
		g_CurrentStage = STAGE_2;
	}
	else if (strstr(filename, "stage3") != nullptr)
	{
		g_CurrentStage = STAGE_3;
	}

	bool isMap1 = false;
	if (strstr(filename, "stage1") != nullptr)
	{
		isMap1 = true;
	}

	bool isMapTitle = false;
	if (strstr(filename, "title") != nullptr)
	{
		isMapTitle = true;
	}

	// Stage select: walls render at 4x Y so the room feels tall and
	// the night sky / moon read as a scenic backdrop. Visual only --
	// collision still uses the original 1x size.
	bool isStageSelect = false;
	if (strstr(filename, "stage_select") != nullptr)
	{
		isStageSelect = true;
	}

	g_MapData.clear();
	Seesaw_ClearAll();
	Manhole_ClearAll();
	Portal_ClearAll();
	Fountain_ClearAll();

	// �ǉ�: �ǂݍ��݊J�n���ɏ����ʒu���f�t�H���g��
	g_PlayerStartPos = XMFLOAT3(0.0f, 0.0f, 0.0f);

	std::string line;
	int y = 0;  // height layer
	int z = 0;  // depth

	while (std::getline(file, line))
	{
		if (line.empty() || line[0] == '#')
		{
			if (line.find("Layer") != std::string::npos) {
				y++;
				z = 0;
			}
			continue;
		}

		for (int x = 0; x < (int)line.length(); x++)
		{
			char c = line[x];// ������ǂݎ��

			// �ǉ�: 'S' �̓V�[�\�[
			if (c == 'S')
			{
				// �}�b�v���W�n�Ɠ��l�̃I�t�Z�b�g�Őݒ�
				Seesaw_Create(
					(float)(x - 2),
					(float)(y - 2),
					(float)z,
					g_MapData
				);
				continue;
			}

			// �ǉ�: 'M' �̓}���z�[��
			if (c == 'M')
			{
				// �}�b�v���W�n�Ɠ��l�̃I�t�Z�b�g�Őݒ�
				Manhole_Create(
					(float)(x - 2),
					(float)(y - 1),
					(float)z,
					g_MapData
				);
				continue;
			}


			// �ǉ�: 'R' �̓v���C���[�����ʒu�}�[�J�[
			if (c == 'R')
			{
				// [FIX] Subtract 0.5 from Y so the player's collision bottom
				// rests on the ground top from frame 1. Ground boxes are 1 unit
				// tall with half-size 0.5 anchored at pos.y; placing R directly
				// at (y-2) put the player 0.5 above the ground top and gravity
				// closed the gap over a few visible frames -> player looked to
				// be floating briefly at every scene transition.
				g_PlayerStartPos = XMFLOAT3(
					(float)(x - 2),
					(float)(y - 2) - 0.5f,
					(float)z
				);
				Player3D_setposition(g_PlayerStartPos);
				Player3D_InitAt(g_PlayerStartPos, XMFLOAT3(0.0f, 180.0f, 0.0f));
				// �t�B�[���h�I�u�W�F�N�g�Ƃ��Ă͒ǉ����Ȃ�
				continue;
			}

			if (c == 'F')
			{
				Fountain_Create(
					(float)(x - 2),
					(float)(y - 2),
					(float)z,
					g_MapData
				);
				continue;
			}

			FIELD type;
			bool valid = true;

			switch (c)
			{
			case 'G': type = FIELD_GROUND;   break;
			case 'W': type = FIELD_WALL;     break;
			case 'B': type = FIELD_OBJ_BOX;  break;
			case 'E': type = FIELD_EMPTY_BOX; break;
			case '1': type = FIELD_GOAL;     break;
			case 'O': type = FIELD_OBJ_1;    break;
			case '2': type = FIELD_OBJ_2;    break;
			case '3': type = FIELD_OBJ_3;    break;
			case 'C': type = FIELD_BENCH;    break;
			case 'D': type = FIELD_DUSTBOX;    break;
			case 'P': type = FIELD_POLE;    break;
			case 'H': type = FIELD_FENCE;    break;
			case '7': type = FIELD_STAGE_1;    break;
			case '8': type = FIELD_STAGE_2;    break;
			case '9': type = FIELD_STAGE_3;    break;
			case 'K': type = FIELD_PORTAL_K;    break;
			case 'J': type = FIELD_PORTAL_J;    break;
			case 'X': type = FIELD_SWITCH;    break;

			case '.': valid = false;         break;
			case ' ': valid = false;         break;
			default:  valid = false;         break;
			}

			if (valid)
			{
				MAPDATA data;

				if (isMapTitle)
				{
					data.pos = XMFLOAT3(
						(float)(x - 2),
						(float)(y - 2),
						(float)z
					);

					data.no = type;
					data.scale = XMFLOAT3(1.0f, 1.0f, 1.0f);
					data.rotate = XMFLOAT3(0.0f, 0.0f, 0.0f);
					g_MapData.push_back(data);
				}
				else
				{
					data.pos = XMFLOAT3(
						(float)(x - 2),
						(float)(y - 2),
						(float)z
					);

					data.no = type;
					// [TUNE] Stage doors render at 2x scale for stronger
					// presence on stage_select. Other field objects unchanged.
					float doorScale = (type == FIELD_STAGE_1 || type == FIELD_STAGE_2 || type == FIELD_STAGE_3) ? 2.0f : 1.0f;
					data.scale = XMFLOAT3(doorScale, doorScale, doorScale);
					data.rotate = XMFLOAT3(0.0f, 0.0f, 0.0f);
					g_MapData.push_back(data);
				}


				// �|�[�^�������iK�j�� mapIndex ��o�^
				if (type == FIELD_PORTAL_K)
				{
					Portal_RegisterEntranceMapIndex((int)g_MapData.size() - 1);
				}
				// �|�[�^���o���iJ�j�� mapIndex ��o�^
				if (type == FIELD_PORTAL_J)
				{
					Portal_RegisterExitMapIndex((int)g_MapData.size() - 1);
				}
			}
		}
		z++;
	}

	Portal_BuildPairs();

	// Stage select: replicate every wall block at +3, +6, +9 in Y so the
	// original 3 wall layers become 12 (4x). Box size stays 1; only the
	// count of stacked blocks grows. Visual only; collision unchanged.
	if (isStageSelect)
	{
		const size_t baseCount = g_MapData.size();
		for (size_t i = 0; i < baseCount; ++i)
		{
			if (g_MapData[i].no != FIELD_WALL) continue;
			MAPDATA base = g_MapData[i];
			for (int k = 1; k <= 3; ++k)
			{
				MAPDATA d = base;
				d.pos.y = base.pos.y + (float)(k * 3);
				g_MapData.push_back(d);
			}
		}
	}

	// [PERF] After all wall placement is finalized (including stage_select
	// stacking), merge contiguous 1x1x1 cells into larger blocks. Title
	// scene is excluded because its layout is hand-tuned for a tiny fixed
	// shot and gains nothing from merging.
	// FIELD_GROUND is intentionally not merged: ground draws via Yuka.fbx
	// which uses a different texture than the wall path; merging would
	// stretch the texture across the whole floor with no equivalent tiled
	// fallback yet. Walls have the DrawTiledWallBox path so they keep the
	// same look post-merge.
	if (!isMapTitle)
	{
		MergeContiguousField(FIELD_WALL);
		// MergeContiguousField erases/reorders g_MapData, invalidating the
		// absolute part indices Seesaw_Create stored. Re-resolve them so the
		// seesaw board keeps a working collider (player can stand / tilt it).
		Seesaw_RebindIndices();
		Manhole_RebindIndices();
	}

	file.close();
	return true;
}

XMFLOAT3 Field_GetPlayerStartPosition()
{
	return g_PlayerStartPos;
}

void SetCurrentStage(GAME_STAGE stage)
{
	g_CurrentStage = stage;
}

GAME_STAGE GetCurrentStage()
{
	return g_CurrentStage;
}

XMFLOAT3 Field_GetCollisionHalfSize(const MAPDATA& m)
{
	if (m.useCustomCollider)
	{
		return m.colliderHalf;
	}
	return XMFLOAT3{
		BOX_RADIUS * m.scale.x,
		BOX_RADIUS * m.scale.y,
		BOX_RADIUS * m.scale.z
	};
}

void Field_ReloadCurrentMap()
{
	GAME_STAGE currentStage = GetCurrentStage();

	switch (currentStage)
	{
	case STAGE_SELECT:
		LoadMapFromFile("asset\\MapData\\stage_select.txt");
		break;
	case STAGE_1:
		LoadMapFromFile("asset\\MapData\\stage1.txt");
		break;
	case STAGE_2:
		LoadMapFromFile("asset\\MapData\\stage2.txt");
		break;
	case STAGE_3:
		LoadMapFromFile("asset\\MapData\\stage3.txt");
		break;
	default:
		LoadMapFromFile("asset\\MapData\\stage_select.txt");
		break;
	}
}