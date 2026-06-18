/*==============================================================================

   Direct3D�̏������֘A [direct3d.cpp]
--------------------------------------------------------------------------------

==============================================================================*/
#include <d3d11.h>
#include "direct3d.h"
#include "debug_ostream.h"
#include <float.h>

#pragma comment(lib, "d3d11.lib")//DirectX�̃v���O������ǉ�����
// #pragma comment(lib, "dxgi.lib")

/* �e��C���^�[�t�F�[�X */
static ID3D11Device* g_pDevice = nullptr;
static ID3D11DeviceContext* g_pDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;

/* �o�b�N�o�b�t�@�֘A */
static ID3D11RenderTargetView* g_pRenderTargetView = nullptr;
static ID3D11Texture2D* g_pDepthStencilBuffer = nullptr;
static ID3D11DepthStencilView* g_pDepthStencilView = nullptr;
static D3D11_TEXTURE2D_DESC g_BackBufferDesc{};
static D3D11_VIEWPORT g_Viewport{};////////////////�ǉ�

static bool configureBackBuffer(); // �o�b�N�o�b�t�@�̐ݒ�E����
static void releaseBackBuffer(); // �o�b�N�o�b�t�@�̉��


static float	bFactor[4] = { 0.0f,0.0f,0.0f,0.0f };
static ID3D11BlendState* bState[BLENDSTATE_MAX];
static ID3D11DepthStencilState* g_DepthStateEnable;
static ID3D11DepthStencilState* g_DepthStateDisable;

ID3D11Texture2D* g_pShadowCubemapTex = nullptr;
ID3D11RenderTargetView* g_pShadowCubemapRTV[6] = {};
ID3D11ShaderResourceView* g_pShadowCubemapSRV = nullptr;
ID3D11SamplerState* g_pShadowSamplerState = nullptr;
ID3D11RasterizerState* g_pShadowRasterizer = nullptr;

static ID3D11Texture2D* g_pShadowDepthTex = nullptr;
static ID3D11DepthStencilView* g_pShadowDepthDSV = nullptr;

XMFLOAT3 g_ShadowLightPos = XMFLOAT3(0.0f, 1.0f, 10.0f);//���C�g�̍��W
float g_ShadowRadius = 50.0f;//���C�g�̔��a

bool Direct3D_Initialize(HWND hWnd)
{
	/* �f�o�C�X�A�X���b�v�`�F�[���A�R���e�L�X�g���� */
	DXGI_SWAP_CHAIN_DESC swap_chain_desc{};
	swap_chain_desc.Windowed = TRUE;
	swap_chain_desc.BufferCount = 2;
	// [FIX] Pin the back buffer to the design resolution (1920x1080).
	// Previously these were left auto (=window client size), which made the
	// back buffer shrink in the smaller debug window (1280x720). All 2D UI
	// projects to Direct3D_GetBackBufferWidth/Height, so the smaller buffer
	// caused UI elements authored at 1920x1080 (offsets like +450, +600) to
	// fall outside the visible area. Fixing the back buffer size makes UI
	// layout deterministic; DXGI stretches the buffer to fit any window size.
	swap_chain_desc.BufferDesc.Width  = 1920;
	swap_chain_desc.BufferDesc.Height = 1080;
	// �� �E�B���h�E�T�C�Y�ɍ��킹�Ď����I�ɐݒ肳���
	swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc.SampleDesc.Count = 1;
	swap_chain_desc.SampleDesc.Quality = 0;
	swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;//0�ɂ��Ă݂�
	swap_chain_desc.OutputWindow = hWnd;

	/*
	IDXGIFactory1* pFactory;
	CreateDXGIFactory1(IID_PPV_ARGS(&pFactory));
	IDXGIAdapter1* pAdapter;
	pFactory->EnumAdapters1(1, &pAdapter); // �Z�J���_���A�_�v�^���擾
	pFactory->Release();
	DXGI_ADAPTER_DESC1 desc;
	pAdapter->GetDesc1(&desc); // �A�_�v�^�̏����擾���Ċm�F�������ꍇ
	pAdapter->Release(); // D3D11CreateDeviceAndSwapChain()�̑�P�����ɓn���ė��p���I�������������
	*/

	UINT device_flags = 0;

#if defined(DEBUG) || defined(_DEBUG)
	//device_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL levels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0
	};

	D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;

	HRESULT hr = D3D11CreateDeviceAndSwapChain(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		device_flags,
		levels,
		ARRAYSIZE(levels),
		D3D11_SDK_VERSION,
		&swap_chain_desc,
		&g_pSwapChain,
		&g_pDevice,
		&feature_level,
		&g_pDeviceContext);

	if (FAILED(hr)) {
		MessageBox(hWnd, "Direct3D�̏������Ɏ��s���܂���", "�G���[", MB_OK);
		return false;
	}

	if (!configureBackBuffer()) {
		MessageBox(hWnd, "�o�b�N�o�b�t�@�̐ݒ�Ɏ��s���܂���", "�G���[", MB_OK);
		return false;
	}

	// �T���v���[�X�e�[�g�ݒ�
	D3D11_SAMPLER_DESC samplerDesc;
	ZeroMemory(&samplerDesc, sizeof(samplerDesc));
	samplerDesc.Filter = D3D11_FILTER_ANISOTROPIC;//������Ƃ����t�B���^�[�ɂ���
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;//���̍��W�͈͊O�͉摜�J��Ԃ�
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;//�c�̍��W�͈͊O�͉摜�J��Ԃ�
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;//���g�p
	samplerDesc.MipLODBias = 0;
	samplerDesc.MaxAnisotropy = 16;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	ID3D11SamplerState* samplerState = NULL;
	g_pDevice->CreateSamplerState(&samplerDesc, &samplerState);
	//�T���v���[���V�F�[�_�[�փZ�b�g
	g_pDeviceContext->PSSetSamplers(0, 1, &samplerState);



	// �u�����h�X�e�[�g�ݒ�
	D3D11_BLEND_DESC blendDesc;
	ZeroMemory(&blendDesc, sizeof(blendDesc));
	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	//�u�����h����
	blendDesc.RenderTarget[0].BlendEnable = FALSE;	//�u�����h����
	g_pDevice->CreateBlendState(&blendDesc, &bState[BLENDSTATE_NONE]);

	//���u�����h
	blendDesc.RenderTarget[0].BlendEnable = TRUE;	//�u�����h�L��
	g_pDevice->CreateBlendState(&blendDesc, &bState[BLENDSTATE_ALFA]);//<<ALPHA�I

	//���Z����
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	g_pDevice->CreateBlendState(&blendDesc, &bState[BLENDSTATE_ADD]);

	//���Z����
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_SUBTRACT;//<<<<�\���F = �w�i - �|���S��
	//	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;//<<<<�\���F = �w�i - �|���S��
	g_pDevice->CreateBlendState(&blendDesc, &bState[BLENDSTATE_SUB]);

	SetBlendState(BLENDSTATE_ALFA);//�f�t�H���g�ݒ�


	// �[�x�X�e���V���X�e�[�g�ݒ�
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc;
	ZeroMemory(&depthStencilDesc, sizeof(depthStencilDesc));
	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
	depthStencilDesc.StencilEnable = FALSE;
	g_pDevice->CreateDepthStencilState(&depthStencilDesc, &g_DepthStateEnable);//�[�x�L���X�e�[�g
	depthStencilDesc.DepthEnable = FALSE;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	g_pDevice->CreateDepthStencilState(&depthStencilDesc, &g_DepthStateDisable);//�[�x�����X�e�[�g

	g_pDeviceContext->OMSetDepthStencilState(g_DepthStateDisable, NULL); //�f�t�H���g�@�[�x����

	Direct3D_InitializeShadowMap();

	return true;
}

void	SetDepthTest(bool flg)
{
	if (flg == true)
	{
		g_pDeviceContext->OMSetDepthStencilState(g_DepthStateEnable, NULL); //�f�t�H���g�@�[�x����
	}
	else
	{
		g_pDeviceContext->OMSetDepthStencilState(g_DepthStateDisable, NULL); //�f�t�H���g�@�[�x����
	}


}

void Direct3D_Finalize()
{
	releaseBackBuffer();

	if (g_pSwapChain) {
		g_pSwapChain->Release();
		g_pSwapChain = nullptr;
	}

	if (g_pDeviceContext) {
		g_pDeviceContext->Release();
		g_pDeviceContext = nullptr;
	}

	if (g_pDevice) {
		g_pDevice->Release();
		g_pDevice = nullptr;
	}

	SAFE_RELEASE(g_pShadowCubemapTex);
	for (int i = 0; i < 6; i++) {
		SAFE_RELEASE(g_pShadowCubemapRTV[i]);
	}
	SAFE_RELEASE(g_pShadowCubemapSRV);
	SAFE_RELEASE(g_pShadowSamplerState);
	SAFE_RELEASE(g_pShadowRasterizer);

	SAFE_RELEASE(g_pShadowDepthTex);
	SAFE_RELEASE(g_pShadowDepthDSV);
}

void Direct3D_Clear()
{
	float clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	g_pDeviceContext->ClearRenderTargetView(g_pRenderTargetView, clear_color);
	g_pDeviceContext->ClearDepthStencilView(g_pDepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

	// �����_�[�^�[�Q�b�g�r���[�ƃf�v�X�X�e���V���r���[�̐ݒ�/////////////�ǉ�
	g_pDeviceContext->OMSetRenderTargets(1, &g_pRenderTargetView, g_pDepthStencilView);


}

void Direct3D_Present()
{
	// �X���b�v�`�F�[���̕\��
	g_pSwapChain->Present(1, 0);
}

ID3D11Device* Direct3D_GetDevice()
{
	return g_pDevice;
}

ID3D11DeviceContext* Direct3D_GetDeviceContext()
{
	return g_pDeviceContext;
}

unsigned int Direct3D_GetBackBufferWidth()
{
	return g_BackBufferDesc.Width;
}

unsigned int Direct3D_GetBackBufferHeight()
{
	return g_BackBufferDesc.Height;
}

bool configureBackBuffer()
{
	HRESULT hr;

	ID3D11Texture2D* back_buffer_pointer = nullptr;

	// �o�b�N�o�b�t�@�̎擾
	hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back_buffer_pointer);

	if (FAILED(hr)) {
		hal::dout << "�o�b�N�o�b�t�@�̎擾�Ɏ��s���܂���" << std::endl;
		return false;
	}

	// �o�b�N�o�b�t�@�̃����_�[�^�[�Q�b�g�r���[�̐���
	hr = g_pDevice->CreateRenderTargetView(back_buffer_pointer, nullptr, &g_pRenderTargetView);

	if (FAILED(hr)) {
		back_buffer_pointer->Release();
		hal::dout << "�o�b�N�o�b�t�@�̃����_�[�^�[�Q�b�g�r���[�̐����Ɏ��s���܂���" << std::endl;
		return false;
	}

	// �o�b�N�o�b�t�@�̏�ԁi���j���擾
	back_buffer_pointer->GetDesc(&g_BackBufferDesc);

	back_buffer_pointer->Release(); // �o�b�N�o�b�t�@�̃|�C���^�͕s�v�Ȃ̂ŉ��

	// �f�v�X�X�e���V���o�b�t�@�̐���
	D3D11_TEXTURE2D_DESC depth_stencil_desc{};
	depth_stencil_desc.Width = g_BackBufferDesc.Width;
	depth_stencil_desc.Height = g_BackBufferDesc.Height;
	depth_stencil_desc.MipLevels = 1;
	depth_stencil_desc.ArraySize = 1;
	depth_stencil_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depth_stencil_desc.SampleDesc.Count = 1;
	depth_stencil_desc.SampleDesc.Quality = 0;
	depth_stencil_desc.Usage = D3D11_USAGE_DEFAULT;
	depth_stencil_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	depth_stencil_desc.CPUAccessFlags = 0;
	depth_stencil_desc.MiscFlags = 0;
	hr = g_pDevice->CreateTexture2D(&depth_stencil_desc, nullptr, &g_pDepthStencilBuffer);

	if (FAILED(hr)) {
		hal::dout << "�f�v�X�X�e���V���o�b�t�@�̐����Ɏ��s���܂���" << std::endl;
		return false;
	}

	// �f�v�X�X�e���V���r���[�̐���
	D3D11_DEPTH_STENCIL_VIEW_DESC depth_stencil_view_desc{};
	depth_stencil_view_desc.Format = depth_stencil_desc.Format;
	depth_stencil_view_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	depth_stencil_view_desc.Texture2D.MipSlice = 0;
	depth_stencil_view_desc.Flags = 0;
	hr = g_pDevice->CreateDepthStencilView(g_pDepthStencilBuffer, &depth_stencil_view_desc, &g_pDepthStencilView);

	if (FAILED(hr)) {
		hal::dout << "�f�v�X�X�e���V���r���[�̐����Ɏ��s���܂���" << std::endl;
		return false;
	}


	// �r���[�|�[�g�̐ݒ�/////////////////////�ǉ�
	g_Viewport.TopLeftX = 0.0f;
	g_Viewport.TopLeftY = 0.0f;
	g_Viewport.Width = static_cast<FLOAT>(g_BackBufferDesc.Width);
	g_Viewport.Height = static_cast<FLOAT>(g_BackBufferDesc.Height);
	g_Viewport.MinDepth = 0.0f;
	g_Viewport.MaxDepth = 1.0f;
	g_pDeviceContext->RSSetViewports(1, &g_Viewport); // �r���[�|�[�g�̐ݒ�
	////////////////////////////////////////////�ǉ�


	return true;
}

void releaseBackBuffer()
{
	if (g_pRenderTargetView) {
		g_pRenderTargetView->Release();
		g_pRenderTargetView = nullptr;
	}

	if (g_pDepthStencilBuffer) {
		g_pDepthStencilBuffer->Release();
		g_pDepthStencilBuffer = nullptr;
	}

	if (g_pDepthStencilView) {
		g_pDepthStencilView->Release();
		g_pDepthStencilView = nullptr;
	}
}


//�ȉ��̊֐�����ԉ��֒ǉ�
void SetBlendState(BLENDSTATE blend)
{

	g_pDeviceContext->OMSetBlendState(bState[blend], bFactor, 0xffffffff);

}

void Direct3D_InitializeShadowMap()
{
	SAFE_RELEASE(g_pShadowCubemapTex);
	for (int i = 0; i < 6; i++) {
		SAFE_RELEASE(g_pShadowCubemapRTV[i]);
	}
	SAFE_RELEASE(g_pShadowCubemapSRV);
	SAFE_RELEASE(g_pShadowSamplerState);
	SAFE_RELEASE(g_pShadowRasterizer);
	SAFE_RELEASE(g_pShadowDepthTex);
	SAFE_RELEASE(g_pShadowDepthDSV);

	HRESULT hr;

	hal::dout << "Initializing shadow map (color cubemap)..." << std::endl;

	// ============ COLOR CUBEMAP (stores linear depth as color) ============
	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = SHADOW_MAP_SIZE;
	texDesc.Height = SHADOW_MAP_SIZE;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 6;
	texDesc.Format = DXGI_FORMAT_R32_FLOAT;  // Color format
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;  // RTV, not DSV! 
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

	hr = g_pDevice->CreateTexture2D(&texDesc, nullptr, &g_pShadowCubemapTex);
	if (FAILED(hr)) {
		hal::dout << "ERROR: Failed to create shadow cubemap texture!  HRESULT: " << hr << std::endl;
		return;
	}
	hal::dout << "Shadow cubemap texture created successfully" << std::endl;

	// ============ Create RTV for each cubemap face ============
	for (int i = 0; i < 6; i++)
	{
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
		rtvDesc.Texture2DArray.MipSlice = 0;
		rtvDesc.Texture2DArray.FirstArraySlice = i;
		rtvDesc.Texture2DArray.ArraySize = 1;

		hr = g_pDevice->CreateRenderTargetView(g_pShadowCubemapTex, &rtvDesc, &g_pShadowCubemapRTV[i]);
		if (FAILED(hr)) {
			hal::dout << "ERROR: Failed to create RTV for face " << i << "! HR: " << hr << std::endl;
		}
		else {
			hal::dout << "Created RTV for face " << i << std::endl;
		}
	}

	// ============ Separate depth buffer for shadow rendering ============
	D3D11_TEXTURE2D_DESC depthDesc = {};
	depthDesc.Width = SHADOW_MAP_SIZE;
	depthDesc.Height = SHADOW_MAP_SIZE;
	depthDesc.MipLevels = 1;
	depthDesc.ArraySize = 1;
	depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
	depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.SampleDesc.Quality = 0;
	depthDesc.Usage = D3D11_USAGE_DEFAULT;

	hr = g_pDevice->CreateTexture2D(&depthDesc, nullptr, &g_pShadowDepthTex);
	if (FAILED(hr)) {
		hal::dout << "ERROR: Failed to create shadow depth texture! HR: " << hr << std::endl;
		return;
	}

	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Texture2D.MipSlice = 0;

	hr = g_pDevice->CreateDepthStencilView(g_pShadowDepthTex, &dsvDesc, &g_pShadowDepthDSV);
	if (FAILED(hr)) {
		hal::dout << "ERROR: Failed to create shadow depth DSV! HR: " << hr << std::endl;
	}
	else {
		hal::dout << "Shadow depth DSV created successfully" << std::endl;
	}

	// ============ Cubemap SRV for sampling in pixel shader ============
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MipLevels = 1;
	srvDesc.TextureCube.MostDetailedMip = 0;

	hr = g_pDevice->CreateShaderResourceView(g_pShadowCubemapTex, &srvDesc, &g_pShadowCubemapSRV);
	if (FAILED(hr)) {
		hal::dout << "ERROR: Failed to create cubemap SRV!  HR: " << hr << std::endl;
	}
	else {
		hal::dout << "Cubemap SRV created successfully" << std::endl;
	}

	// ============ Regular sampler for manual depth comparison ============
	D3D11_SAMPLER_DESC sampDesc = {};
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.BorderColor[0] = 1.0f;
	sampDesc.BorderColor[1] = 1.0f;
	sampDesc.BorderColor[2] = 1.0f;
	sampDesc.BorderColor[3] = 1.0f;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampDesc.MaxAnisotropy = 1;
	sampDesc.MipLODBias = 0;
	sampDesc.MinLOD = 0;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

	hr = g_pDevice->CreateSamplerState(&sampDesc, &g_pShadowSamplerState);
	if (FAILED(hr)) {
		hal::dout << "ERROR: Failed to create shadow sampler! HR: " << hr << std::endl;
	}
	else {
		hal::dout << "Shadow sampler created successfully" << std::endl;
	}

	//D3D11_RASTERIZER_DESC rsDesc = {};
	//rsDesc.FillMode = D3D11_FILL_SOLID;
	//rsDesc.CullMode = D3D11_CULL_NONE;
	//rsDesc.DepthClipEnable = TRUE;

	//hr = g_pDevice->CreateRasterizerState(&rsDesc, &g_pShadowRasterizer);
	//if (FAILED(hr))
	//{
	//	hal::dout << "ERROR: Failed to create shadow rasterizer state!" << std::endl;
	//}

	hal::dout << "Shadow map initialization complete!" << std::endl;
}

void Direct3D_BeginShadowPass(int faceIndex)
{
	if (faceIndex < 0 || faceIndex >= 6) return;

	// Set viewport to shadow map size
	D3D11_VIEWPORT vp = {};
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	vp.Width = (FLOAT)SHADOW_MAP_SIZE;
	vp.Height = (FLOAT)SHADOW_MAP_SIZE;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	g_pDeviceContext->RSSetViewports(1, &vp);
	//g_pDeviceContext->RSSetState(g_pShadowRasterizer);

	// Render to color cubemap face + depth buffer
	g_pDeviceContext->OMSetRenderTargets(1, &g_pShadowCubemapRTV[faceIndex], g_pShadowDepthDSV);


	// Clear color to 1.0 (far) and depth
	float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	g_pDeviceContext->ClearRenderTargetView(g_pShadowCubemapRTV[faceIndex], clearColor);
	g_pDeviceContext->ClearDepthStencilView(g_pShadowDepthDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void Direct3D_EndShadowPass()
{
	g_pDeviceContext->OMSetRenderTargets(1, &g_pRenderTargetView, g_pDepthStencilView);
	g_pDeviceContext->RSSetViewports(1, &g_Viewport);
	g_pDeviceContext->RSSetState(nullptr);
}

XMMATRIX Direct3D_GetCubemapFaceViewProj(int faceIndex, const XMFLOAT3& lightPos, float radius)
{
	XMVECTOR eye = XMLoadFloat3(&lightPos);

	XMVECTOR directions[6] = {
		XMVectorSet(1,  0,  0, 0), // +X
		XMVectorSet(-1,  0,  0, 0), // -X
		XMVectorSet(0,  1,  0, 0), // +Y
		XMVectorSet(0, -1,  0, 0), // -Y
		XMVectorSet(0,  0,  1, 0), // +Z
		XMVectorSet(0,  0, -1, 0)  // -Z
	};

	XMVECTOR ups[6] = {
		XMVectorSet(0, 1, 0, 0),  // +X
		XMVectorSet(0, 1, 0, 0),  // -X
		XMVectorSet(0, 0,-1, 0),  // +Y
		XMVectorSet(0, 0, 1, 0),  // -Y
		XMVectorSet(0, 1, 0, 0),  // +Z
		XMVectorSet(0, 1, 0, 0)   // -Z
	};

	XMVECTOR target = XMVectorAdd(eye, directions[faceIndex]);
	XMVECTOR up = ups[faceIndex];

	XMMATRIX view = XMMatrixLookAtLH(eye, target, up);

	float nearPlane = 0.1f;
	float farPlane = radius;
	XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.0f, nearPlane, farPlane);

	return view * proj;
}