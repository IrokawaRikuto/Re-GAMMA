//Audio.cpp
#include <d3d11.h>
#include <DirectXMath.h>
#include "direct3d.h"
#include "shader.h"
#include "sprite.h"
#include "keyboard.h"
#include "audio.h"
using namespace DirectX;


//=========================================================================================================
// �\���̒�`
//=========================================================================================================
struct AUDIO
{
	IXAudio2SourceVoice* SourceVoice{};
	BYTE* SoundData{};

	int						Length{};
	int						PlayLength{};

	// �t�F�[�h�p�p�����[�^
	bool					IsFading{};
	bool					IsFadeIn{};
	float					CurrentVolume{};
	float					TargetVolume{};
	float					FadeSpeed{};
};

//=========================================================================================================
// �}�N����`
//=========================================================================================================
#define AUDIO_MAX (100)

//=========================================================================================================
// �O���[�o���ϐ�
//=========================================================================================================
static IXAudio2* g_Xaudio{};
static IXAudio2MasteringVoice* g_MasteringVoice{};
static AUDIO g_Audio[AUDIO_MAX]{};

// Option-controlled volumes (0..1). g_BgmVolume routes through the master
// mixer for now; g_SeVolume is stored for the future SE system.
static float g_BgmVolume = 0.5f;
static float g_SeVolume  = 0.5f;

//=========================================================================================================
// ����������
//=========================================================================================================
// System click SE: loaded once at InitAudio and replayed from the Title /
// Pause / Option button activations. Cached at file scope so we don't have
// to thread an ID through every UI module.
static int g_SystemClickSE = -1;

void InitAudio()
{
	// XAudio����
	XAudio2Create(&g_Xaudio, 0);

	// �}�X�^�����O�{�C�X����
	g_Xaudio->CreateMasteringVoice(&g_MasteringVoice);

	// Seed master to the configured BGM volume so audio starts at a sane
	// level (was 1.0 by default which blasted audio on first play).
	g_MasteringVoice->SetVolume(g_BgmVolume);

	// Preload the click SE. -1 if the file isn't present so callers can
	// safely no-op.
	g_SystemClickSE = LoadAudio("asset\\Audio\\button_click.wav");
}

void SystemSE_PlayClick()
{
	if (g_SystemClickSE < 0) return;
	SetAudioVolume(g_SystemClickSE, GetSeVolume());
	PlayAudio(g_SystemClickSE, false);
}

//=========================================================================================================
// �I������
//=========================================================================================================
void UninitAudio()
{
	g_MasteringVoice->DestroyVoice();
	g_Xaudio->Release();
}

//=========================================================================================================
// �X�V����
//=========================================================================================================
void UpdateAudio()
{
	for (int i = 0; i < AUDIO_MAX; i++)
	{
		if (g_Audio[i].IsFading && g_Audio[i].SourceVoice)
		{
			if (g_Audio[i].IsFadeIn)
			{
				// �t�F�[�h�C������
				g_Audio[i].CurrentVolume += g_Audio[i].FadeSpeed;

				if (g_Audio[i].CurrentVolume >= g_Audio[i].TargetVolume)
				{
					g_Audio[i].CurrentVolume = g_Audio[i].TargetVolume;
					g_Audio[i].IsFading = false;
				}
			}
			else
			{
				// �t�F�[�h�A�E�g����
				g_Audio[i].CurrentVolume -= g_Audio[i].FadeSpeed;

				// ��~�t���O�iTargetVolume < 0�j�̏ꍇ��0�܂ŉ����Ē�~
				bool shouldStop = (g_Audio[i].TargetVolume < 0.0f);
				float actualTarget = shouldStop ? 0.0f : g_Audio[i].TargetVolume;

				if (g_Audio[i].CurrentVolume <= actualTarget)
				{
					g_Audio[i].CurrentVolume = actualTarget;
					g_Audio[i].IsFading = false;

					// ��~�t���O�������Ă���ꍇ�̂ݒ�~
					if (shouldStop)
					{
						g_Audio[i].SourceVoice->Stop();
					}
				}
			}

			// �{�����[���K�p
			g_Audio[i].SourceVoice->SetVolume(g_Audio[i].CurrentVolume);
		}
	}
}

//=========================================================================================================
// ���y�ǂݍ���
//=========================================================================================================
int LoadAudio(const char* FileName)
{
	int index = -1;

	for (int i = 0; i < AUDIO_MAX; i++)
	{
		if (g_Audio[i].SourceVoice == nullptr)
		{
			index = i;
			break;
		}
	}

	if (index == -1)
		return -1;




	// �T�E���h�f�[�^�Ǎ�
	WAVEFORMATEX wfx = { 0 };

	{
		HMMIO hmmio = NULL;
		MMIOINFO mmioinfo = { 0 };
		MMCKINFO riffchunkinfo = { 0 };
		MMCKINFO datachunkinfo = { 0 };
		MMCKINFO mmckinfo = { 0 };
		UINT32 buflen;
		LONG readlen;


		hmmio = mmioOpen((LPSTR)FileName, &mmioinfo, MMIO_READ);
		assert(hmmio);

		riffchunkinfo.fccType = mmioFOURCC('W', 'A', 'V', 'E');
		mmioDescend(hmmio, &riffchunkinfo, NULL, MMIO_FINDRIFF);

		mmckinfo.ckid = mmioFOURCC('f', 'm', 't', ' ');
		mmioDescend(hmmio, &mmckinfo, &riffchunkinfo, MMIO_FINDCHUNK);

		if (mmckinfo.cksize >= sizeof(WAVEFORMATEX))
		{
			mmioRead(hmmio, (HPSTR)&wfx, sizeof(wfx));
		}
		else
		{
			PCMWAVEFORMAT pcmwf = { 0 };
			mmioRead(hmmio, (HPSTR)&pcmwf, sizeof(pcmwf));
			memset(&wfx, 0x00, sizeof(wfx));
			memcpy(&wfx, &pcmwf, sizeof(pcmwf));
			wfx.cbSize = 0;
		}
		mmioAscend(hmmio, &mmckinfo, 0);

		datachunkinfo.ckid = mmioFOURCC('d', 'a', 't', 'a');
		mmioDescend(hmmio, &datachunkinfo, &riffchunkinfo, MMIO_FINDCHUNK);



		buflen = datachunkinfo.cksize;
		g_Audio[index].SoundData = new unsigned char[buflen];
		readlen = mmioRead(hmmio, (HPSTR)g_Audio[index].SoundData, buflen);


		g_Audio[index].Length = readlen;
		g_Audio[index].PlayLength = readlen / wfx.nBlockAlign;


		mmioClose(hmmio, 0);
	}


	// �T�E���h�\�[�X����
	g_Xaudio->CreateSourceVoice(&g_Audio[index].SourceVoice, &wfx);
	assert(g_Audio[index].SourceVoice);

	// �t�F�[�h�p�����[�^������
	g_Audio[index].IsFading = false;
	g_Audio[index].IsFadeIn = false;
	g_Audio[index].CurrentVolume = 1.0f;
	g_Audio[index].TargetVolume = 1.0f;
	g_Audio[index].FadeSpeed = 0.0f;

	return index;
}

//=========================================================================================================
// ���y��~����
//=========================================================================================================
void UnloadAudio(int Index)
{
	g_Audio[Index].SourceVoice->Stop();
	g_Audio[Index].SourceVoice->DestroyVoice();

	delete[] g_Audio[Index].SoundData;
	g_Audio[Index].SoundData = nullptr;
	g_Audio[Index].SourceVoice = nullptr;
}




//=========================================================================================================
// ���y�Đ�����
//=========================================================================================================
void PlayAudio(int Index, bool Loop)
{
	g_Audio[Index].SourceVoice->Stop();
	g_Audio[Index].SourceVoice->FlushSourceBuffers();


	// �o�b�t�@�ݒ�
	XAUDIO2_BUFFER bufinfo;

	memset(&bufinfo, 0x00, sizeof(bufinfo));
	bufinfo.AudioBytes = g_Audio[Index].Length;
	bufinfo.pAudioData = g_Audio[Index].SoundData;
	bufinfo.PlayBegin = 0;
	bufinfo.PlayLength = g_Audio[Index].PlayLength;

	// ���[�v�ݒ�
	if (Loop)
	{
		bufinfo.LoopBegin = 0;
		bufinfo.LoopLength = g_Audio[Index].PlayLength;
		bufinfo.LoopCount = XAUDIO2_LOOP_INFINITE;
	}

	g_Audio[Index].SourceVoice->SubmitSourceBuffer(&bufinfo, NULL);


	// �Đ�
	g_Audio[Index].SourceVoice->Start();

}

void SetMasterVolume(float Volume)
{
	if (g_MasteringVoice)
	{
		g_MasteringVoice->SetVolume(Volume);
	}
}

void SetAudioVolume(int Index, float Volume)
{
	if (g_Audio[Index].SourceVoice)
	{
		g_Audio[Index].SourceVoice->SetVolume(Volume);
		g_Audio[Index].CurrentVolume = Volume;
	}
}

//=========================================================================================================
// �t�F�[�h�C���J�n
//=========================================================================================================
void FadeInAudio(int Index, float Duration, float TargetVolume)
{
	if (g_Audio[Index].SourceVoice)
	{
		// [FIX] Do NOT reset CurrentVolume or SetVolume(0). When this fires while a
		// previous fade-out is still in progress (e.g., rapid 3D<->2D mode switching
		// mid-crossfade), resetting would snap the current level to silence and ramp
		// back up -> audible volume jolt. Preserving CurrentVolume continues the ramp
		// smoothly from wherever it is.
		g_Audio[Index].IsFading = true;
		g_Audio[Index].IsFadeIn = true;
		g_Audio[Index].TargetVolume = TargetVolume;
		g_Audio[Index].FadeSpeed = TargetVolume / (Duration * 60.0f); // slope per frame @60FPS
	}
}

//=========================================================================================================
// �t�F�[�h�A�E�g�J�n
//=========================================================================================================
void FadeOutAudio(int Index, float Duration)
{
	if (g_Audio[Index].SourceVoice)
	{
		g_Audio[Index].IsFading = true;
		g_Audio[Index].IsFadeIn = false;
		g_Audio[Index].TargetVolume = 0.0f;
		// [FIX] Constant slope (was: CurrentVolume / (Duration * 60), which varied with
		// where the fade started, breaking symmetry with FadeInAudio whose slope is
		// TargetVolume / (Duration * 60) = 1.0 / (Duration * 60). With matching slopes,
		// a crossfade pair's volumes move at the same rate, and mid-fade re-triggers
		// behave predictably (each side reaches its target in time-from-current-level).
		g_Audio[Index].FadeSpeed = 1.0f / (Duration * 60.0f);
	}
}

//=========================================================================================================
// �t�F�[�h�A�E�g����~�i�t�F�[�h�A�E�g������ɍĐ����~����j
//=========================================================================================================
void FadeOutAndStopAudio(int Index, float Duration)
{
	if (g_Audio[Index].SourceVoice)
	{
		g_Audio[Index].IsFading = true;
		g_Audio[Index].IsFadeIn = false;
		g_Audio[Index].TargetVolume = -1.0f; // negative = stop after fade sentinel
		// [FIX] Constant slope, same reasoning as FadeOutAudio.
		g_Audio[Index].FadeSpeed = 1.0f / (Duration * 60.0f);
	}
}

//=========================================================================================================
// �t�F�[�h�C���Đ��i�Đ��ƃt�F�[�h�C���𓯎����s�j
//=========================================================================================================
void PlayAudioWithFadeIn(int Index, bool Loop, float Duration, float TargetVolume)
{
	// FadeInAudio no longer resets CurrentVolume (preserves rapid re-crossfade).
	// Force silence here so this helper still gives the "start from 0, ramp up"
	// semantics callers expect.
	SetAudioVolume(Index, 0.0f);
	PlayAudio(Index, Loop);
	FadeInAudio(Index, Duration, TargetVolume);
}

// ---------------------------------------------------------------------
// Option-controlled BGM / SE volume
// ---------------------------------------------------------------------
static inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void SetBgmVolume(float v)
{
	g_BgmVolume = Clamp01(v);
	// While SE is not yet wired up, BGM uses the master mixer directly.
	if (g_MasteringVoice) g_MasteringVoice->SetVolume(g_BgmVolume);
}

float GetBgmVolume()
{
	return g_BgmVolume;
}

void SetSeVolume(float v)
{
	g_SeVolume = Clamp01(v);
	// SE playback path is not yet implemented; value is stored for later.
}

float GetSeVolume()
{
	return g_SeVolume;
}
