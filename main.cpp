//main.cpp
//�E�B���h�E�̕\��
#include <SDKDDKVer.h>	//���p�ł���ł���ʂ� Windows �v���b�g�t�H�[������`�����
#define WIN32_LEAN_AND_MEAN	//32bit�A�v���ɂ͕s�v�ȏ���}�~���ăR���p�C�����Ԃ�Z�k
#include	<windows.h>
#include	"debug_ostream.h"	//�f�o�b�O�\��
#include <algorithm>			
#include "direct3d.h"			
#include "shader.h"
#include "polygon.h"
#include "field.h"
#include "sprite.h"
#include "keyboard.h"
#include "Player3D.h"
#include "Effect.h"
#include "score.h"
#include "Manager.h"
#include "Audio.h"	
#include "Mouse.h"
#include <shellapi.h>

#include "debug.h"

//==================================
//�O���[�o���ϐ�
//==================================
#ifdef _DEBUG	//�f�o�b�O�r���h�̎������ϐ��������
int		g_CountFPS;			//FPS�J�E���^�[
char	g_DebugStr[2048];	//FPS�\��������
#endif
#pragma comment(lib, "winmm.lib")

//=================================
//�}�N����`
//=================================
#define		CLASS_NAME	"DX21 Window"
#define		WINDOW_CAPTION	"�|���S���`��"
#define		SCREEN_WIDTH	(1920)
#define		SCREEN_HEIGHT	(1080)

//===================================
//�v���g�^�C�v�錾
//===================================

// ImGui�p�E�B���h�E�v���V�[�W���n���h��
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


//�E�B���h�E�v���V�[�W��
//�R�[���o�b�N�֐��������l���Ăяo���Ă����֐�
LRESULT	CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int APIENTRY WinMain(HINSTANCE hInstance,
	HINSTANCE hPrevInstance, LPSTR lpCmd, int nCmdShow)
{

	//�����̏�����
	srand(timeGetTime());

	//�t���[�����[�g�v���p�ϐ�
	DWORD	dwExecLastTime;
	DWORD	dwFPSLastTime;
	DWORD	dwCurrentTime;
	DWORD	dwFrameCount;

	HRESULT hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED);

	// �v���C�}�����j�^�[�̉𑜓x���擾
	int screenWidth = GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = GetSystemMetrics(SM_CYSCREEN);

	//�E�B���h�E�N���X�̓o�^�i�E�B���h�E�̎d�l�I�ȕ������߂�Windows�փZ�b�g����j
	WNDCLASS	wc;	//�\���̂�����
	ZeroMemory(&wc, sizeof(WNDCLASS));//���e���O�ŏ�����
	wc.lpfnWndProc = WndProc;	//�R�[���o�b�N�֐��̃|�C���^�[
	wc.lpszClassName = CLASS_NAME;	//���̎d�l���̖��O
	wc.hInstance = hInstance;	//���̃A�v���P�[�V�����̂���
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);//�J�[�\���̎��
	wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND + 1);//�E�B���h�E�̔w�i�F�͍�
	RegisterClass(&wc);	//�\���̂�Windows�փZ�b�g

	//�N���C�A���g�̈�̃T�C�Y��\����` (������left, top, right, bottom)
	RECT window_rect = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
	//�E�B���h�E�̃X�^�C���i�E�B���h�E�g�ƍő剻�{�^�����폜�j
	//DWORD window_style = WS_OVERLAPPEDWINDOW;

	// �t���X�N���[���p�̃E�B���h�E�X�^�C���i�g�Ȃ��A�^�C�g���o�[�Ȃ��j
#ifdef _DEBUG
	// [FIX] Debug build: windowed mode (title bar + border) for easier dev/debug
	DWORD window_style = WS_OVERLAPPEDWINDOW;
#else
	DWORD window_style = WS_POPUP | WS_VISIBLE;
#endif


	//DWORD window_style = WS_OVERLAPPEDWINDOW ^ (WS_THICKFRAME | WS_MAXIMIZEBOX);
	//�w�肵���N���C�A���g�̈���m�ۂ��邽�߂ɐV���ȋ�`���W���v�Z
	AdjustWindowRect(&window_rect, window_style, FALSE);
	//�������ꂽ��`�̉��Əc�̃T�C�Y���v�Z
	int window_width = window_rect.right - window_rect.left;
	int window_height = window_rect.bottom - window_rect.top;

	//�E�B���h�E�̍쐬
	/*HWND	hWnd = CreateWindow(
		CLASS_NAME,
		WINDOW_CAPTION,
		window_style,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		0,
		0,
		NULL,
		NULL,
		hInstance,
		NULL
	);*/

	// �E�B���h�E�̍쐬�i��ʑS�̂��J�o�[�j
#ifdef _DEBUG
	// [FIX] Debug: smaller window so VS/IDE remains accessible alongside
	HWND hWnd = CreateWindow(
		CLASS_NAME,
		WINDOW_CAPTION,
		window_style,
		CW_USEDEFAULT, CW_USEDEFAULT,
		1280, 720,
		NULL,
		NULL,
		hInstance,
		NULL
	);
#else
	HWND hWnd = CreateWindow(
		CLASS_NAME,
		WINDOW_CAPTION,
		window_style,
		0,                  // X�ʒu: ��ʍ��[
		0,                  // Y�ʒu: ��ʏ�[
		screenWidth,        // ��: ���j�^�[�S��
		screenHeight,       // ����: ���j�^�[�S��
		NULL,
		NULL,
		hInstance,
		NULL
	);

	// �E�B���h�E���őO�ʂɕ\��
	SetWindowPos(hWnd, HWND_TOP, 0, 0, window_width, window_height, SWP_SHOWWINDOW);
#endif
	//// �^�X�N�o�[���B��
	//APPBARDATA abd = {};
	//abd.cbSize = sizeof(APPBARDATA);
	//abd.hWnd = FindWindowA("Shell_TrayWnd", NULL); // FindWindow �� FindWindowA �ɕύX���A������ "Shell_TrayWnd" ��
	//if (abd.hWnd)
	//{
	//	ShowWindow(abd.hWnd, SW_HIDE);
	//}

	//�쐬�����E�B���h�E��\������
#ifdef _DEBUG
	ShowWindow(hWnd, SW_SHOW);
#else
	ShowWindow(hWnd, SW_MAXIMIZE);//�����ɏ]���ĕ\���A�܂��͔�\��
#endif
	//�E�B���h�E�����̍X�V�v��
	UpdateWindow(hWnd);


	Direct3D_Initialize(hWnd);
	Mouse_Initialize(hWnd);
	Keyboard_Initialize();

	Shader_Initialize(Direct3D_GetDevice(), Direct3D_GetDeviceContext()); // �V�F�[�_�̏�����
	InitializeSprite();//�X�v���C�g�̏�����

	InitAudio();	//�T�E���h�̏�����

	{// ImGui ������
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();

		// io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

		ImGui::StyleColorsDark();

		ImGuiStyle& style = ImGui::GetStyle();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}

		// ���{��t�H���g�ݒ�
		io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", 18.0f, nullptr,
			io.Fonts->GetGlyphRangesChineseFull());


		// ImGui �p�� DirectX11 �� Win32 �̏�����
		ImGui_ImplWin32_Init(hWnd);
		ImGui_ImplDX11_Init(Direct3D_GetDevice(), Direct3D_GetDeviceContext());
	}



	Manager_Initialize();


	/////////////////////////////////

	//���b�Z�[�W���[�v
	MSG	msg;
	ZeroMemory(&msg, sizeof(MSG));

	//�t���[�����[�g�v��������
	timeBeginPeriod(1);	//�^�C�}�[�̐��x��ݒ�
	dwExecLastTime = dwFPSLastTime = timeGetTime();//���݂̃^�C�}�[�l
	dwCurrentTime = dwFrameCount = 0;

	//�I�����b�Z�[�W������܂Ń��[�v����
	do {
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{ // �E�B���h�E���b�Z�[�W�����Ă�����
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{ // �Q�[���̏���
			dwCurrentTime = timeGetTime();
			if ((dwCurrentTime - dwFPSLastTime) >= 1000)
			{
#ifdef _DEBUG
				g_CountFPS = dwFrameCount;
#endif
				dwFPSLastTime = dwCurrentTime;//���݂̃^�C�}�[�l�ۑ�
				dwFrameCount = 0;
			}

			if ((dwCurrentTime - dwExecLastTime) >= ((float)1000 / 60))
			{// 1/60s �o�߂���
				dwExecLastTime = dwCurrentTime;//���݂̃^�C�}�[�ƕۑ�
#ifdef _DEBUG
				//�E�B���h�E�L���v�V�����֌��݂�FPS��\��
				wsprintf(g_DebugStr, "GAMMA ");
				wsprintf(&g_DebugStr[strlen(g_DebugStr)],
					" FPS : %d", g_CountFPS);
				SetWindowText(hWnd, g_DebugStr);
#endif


				ImGui_ImplWin32_NewFrame();
				ImGui_ImplDX11_NewFrame();
				ImGui::NewFrame();


				//�X�V����
				Manager_Update();

				//�`�揈��
				Direct3D_Clear();
				Manager_Draw();

				ImGui::Render();
				ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

				{
					ImGuiIO& io = ImGui::GetIO();
					if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
					{
						ImGui::UpdatePlatformWindows();
						ImGui::RenderPlatformWindowsDefault();
					}
				}


				Direct3D_Present();
				keycopy();
				Mouse_ResetScrollWheelValue();

				dwFrameCount++;		//�����񐔍X�V
			}

		}
	} while (msg.message != WM_QUIT);

	Manager_Finalize();
	
	// �^�X�N�o�[���ĕ\��
	/*abd.cbSize = sizeof(APPBARDATA);
	abd.hWnd = FindWindowA("Shell_TrayWnd", NULL);
	if (abd.hWnd)
	{
		ShowWindow(abd.hWnd, SW_SHOW);
	}*/

	Mouse_Finalize();
	UninitAudio();		//�T�E���h�̏I��

	Shader_Finalize(); // �V�F�[�_�̏I������
	FinalizeSprite();	//�X�v���C�g�̏I������
	/////////////////////////////////////////
	Direct3D_Finalize();


	//�I������
	return (int)msg.wParam;

}

//=========================================
//�E�B���h�E�v���V�[�W��
// ���b�Z�[�W���[�v���ŌĂяo�����
//=========================================
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	HGDIOBJ hbrWhite, hbrGray;

	HDC		hdc;	//�E�B���h�E��ʂ�\�����i�f�o�C�X�R���e�L�X�g ���o�͐�j
	PAINTSTRUCT	ps;	//�E�B���h�E��ʂ̑傫���ȂǕ`��֘A�̏��


#ifdef _DEBUG
	if (ImGui::GetCurrentContext() != nullptr) {
		if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
			return 0;
	}
#endif // _DEBUG


	switch (uMsg)
	{
	case WM_ACTIVATEAPP:
	case WM_SYSKEYDOWN:
	case WM_KEYUP:
	case WM_SYSKEYUP:
		Keyboard_ProcessMessage(uMsg, wParam, lParam);
		break;


	case WM_SIZING:
	{
		const float targetAspect = (float)SCREEN_WIDTH / (float)SCREEN_HEIGHT;

		RECT* rc = reinterpret_cast<RECT*>(lParam);
		int w = rc->right - rc->left;
		int h = rc->bottom - rc->top;

		float currentAspect = (float)w / (float)h;

		switch (wParam)
		{
		case WMSZ_LEFT:
		case WMSZ_RIGHT:
			h = (int)(w / targetAspect + 0.5f);
			if (wParam == WMSZ_LEFT)
				rc->top = rc->bottom - h;
			else
				rc->bottom = rc->top + h;
			break;

		case WMSZ_TOP:
		case WMSZ_BOTTOM:
			w = (int)(h * targetAspect + 0.5f);
			if (wParam == WMSZ_TOP)
				rc->left = rc->right - w;
			else
				rc->right = rc->left + w;
			break;

		default:
			if (currentAspect > targetAspect)
			{
				w = (int)(h * targetAspect + 0.5f);
			}
			else
			{
				h = (int)(w / targetAspect + 0.5f);
			}

			if (wParam == WMSZ_TOPLEFT)
			{
				rc->left = rc->right - w;
				rc->top = rc->bottom - h;
			}
			else if (wParam == WMSZ_TOPRIGHT)
			{
				rc->right = rc->left + w;
				rc->top = rc->bottom - h;
			}
			else if (wParam == WMSZ_BOTTOMLEFT)
			{
				rc->left = rc->right - w;
				rc->bottom = rc->top + h;
			}
			else if (wParam == WMSZ_BOTTOMRIGHT)
			{
				rc->right = rc->left + w;
				rc->bottom = rc->top + h;
			}
			break;
		}

		return TRUE;
	}

	case WM_KEYDOWN:	//�L�[�������ꂽ
		//if (wParam == VK_ESCAPE)//�����ꂽ�̂�ESC�L�[
		//{
		//	//�E�B���h�E����������N�G�X�g��Windows�ɑ���
		//	SendMessage(hWnd, WM_CLOSE, 0, 0);
		//}

		Keyboard_ProcessMessage(uMsg, wParam, lParam);
		break;
	//case WM_CLOSE:	//�E�B���h�E����Ȃ�������				
	//	//if (
	//	//	MessageBox(hWnd, "�{���ɏI�����Ă�낵���ł����H",
	//	//		"�m�F", MB_OKCANCEL | MB_DEFBUTTON2) == IDOK
	//	//	)
	//	//{//OK�������ꂽ�Ƃ�
	//	//	DestroyWindow(hWnd);//�I������葱����Windows�փ��N�G�X�g
	//	//}
	//	//else
	//	//{
	//	//	return 0;	//����ς�I���Ȃ�
	//	//}

	//	//break;
	case WM_DESTROY:	//�I������OK�ł���
		PostQuitMessage(0);		//�����̃��b�Z�[�W�ɂO�𑗂�
		break;

	case WM_INPUT:
	case WM_MOUSEMOVE:
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MOUSEWHEEL:
	case WM_XBUTTONDOWN:
	case WM_XBUTTONUP:
	case WM_MOUSEHOVER:
		Mouse_ProcessMessage(uMsg, wParam, lParam);
		break;
	}

	

	//�K�p�̖������b�Z�[�W�͓K���ɏ��������ďI��
	return DefWindowProc(hWnd, uMsg, wParam, lParam);

}
