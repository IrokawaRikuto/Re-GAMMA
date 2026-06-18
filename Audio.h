//Audio.h
#pragma once
#include<xaudio2.h>

// �������E�I������
void InitAudio();
void UninitAudio();
void UpdateAudio();

// ���y�t�@�C������
int LoadAudio(const char* FileName);
void UnloadAudio(int Index);

// �Đ�����
void PlayAudio(int Index, bool Loop = false);

// �{�����[������
void SetMasterVolume(float Volume);
void SetAudioVolume(int Index, float Volume);

// �t�F�[�h����
void FadeInAudio(int Index, float Duration, float TargetVolume = 1.0f);
void FadeOutAudio(int Index, float Duration);
void FadeOutAndStopAudio(int Index, float Duration);  // �t�F�[�h�A�E�g��ɒ�~
void PlayAudioWithFadeIn(int Index, bool Loop, float Duration, float TargetVolume = 1.0f);

// BGM/SE volume (driven by Option screen). Defaults 0.5 / 0.5.
// SetBgmVolume currently routes through the master mixer since SE is not
// yet wired up; when SE is added, BGM and SE will use separate submix
// voices so each category scales independently.
void  SetBgmVolume(float v);  // 0..1
float GetBgmVolume();
void  SetSeVolume(float v);   // 0..1
float GetSeVolume();

// Play the system click SE (button activation in Title / Pause / Option).
// The click sound is loaded once in InitAudio and reused via PlayAudio.
// Volume is scaled by GetSeVolume() each call so changes in the Option
// slider take effect immediately.
void SystemSE_PlayClick();
