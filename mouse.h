//--------------------------------------------------------------------------------------
// File: mouse.h
//
// ???????????
//
//--------------------------------------------------------------------------------------
// 2020/02/11
//     DirectXTK?????????C?????????????
//
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkId=248929
// http://go.microsoft.com/fwlink/?LinkID=615561
//--------------------------------------------------------------------------------------
#ifndef HAL_YOUHEI_MOUSE_H
#define HAL_YOUHEI_MOUSE_H
#pragma once


#include <windows.h>
#include <memory>


// ??????
typedef enum Mouse_PositionMode_tag
{
    MOUSE_POSITION_MODE_ABSOLUTE, // ???????
    MOUSE_POSITION_MODE_RELATIVE, // ???????
} Mouse_PositionMode;



// ????????
typedef struct MouseState_tag
{
    bool leftButton;
    bool middleButton;
    bool rightButton;
    bool xButton1;
    bool xButton2;
    int x;
    int y;
    int scrollWheelValue;
    Mouse_PositionMode positionMode;
} Mouse_State;


// ????????????
void Mouse_Initialize(HWND window);

// ?????????????
void Mouse_Finalize(void);

// ???????????
void Mouse_GetState(Mouse_State* pState);

// Read the current mouse state WITHOUT consuming the relative-mode delta.
// Mouse_GetState advances the gRelativeRead protocol so the very next caller
// (in the same frame) sees x = y = 0. Camera mouse-look is the canonical
// delta consumer; any other code in the same frame that only needs the
// button or scroll wheel state should use Mouse_PeekState instead so the
// camera still receives a real delta. Use cases: pause-menu click detection,
// box push/grab trigger, title-screen click trigger.
void Mouse_PeekState(Mouse_State* pState);

// ????????????????????????
void Mouse_ResetScrollWheelValue(void);

// ????????????????????????????????
void Mouse_SetMode(Mouse_PositionMode mode);

// ???????????
bool Mouse_IsConnected(void);

// ????????????????????
bool Mouse_IsVisible(void);

// ??????????????
void Mouse_SetVisible(bool visible);

// ??????????????????????????????
void Mouse_ProcessMessage(UINT message, WPARAM wParam, LPARAM lParam);

// Window handle the mouse module is bound to. Used by UI code that needs
// to convert between client-area pixels (which is what mouse coords are in)
// and back-buffer / orthographic-projection coords (which is what sprite
// draw calls use). Returns NULL before Mouse_Initialize has run.
HWND Mouse_GetWindow(void);

// Helper: fetch the bound window's client-area size as floats. Returns
// (0,0) if the window is invalid. Convenience wrapper around GetClientRect.
void Mouse_GetClientSize(float* outW, float* outH);


// ????
//
// ??????????????????????????????????????
//
// Mouse_Initialize(hwnd);
//
// ??????????????????????????????????
//
// LResult CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
// {
//     switch (message)
//     {
//     case WM_ACTIVATEAPP:
//     case WM_INPUT:
//     case WM_MOUSEMOVE:
//     case WM_LBUTTONDOWN:
//     case WM_LBUTTONUP:
//     case WM_RBUTTONDOWN:
//     case WM_RBUTTONUP:
//     case WM_MBUTTONDOWN:
//     case WM_MBUTTONUP:
//     case WM_MOUSEWHEEL:
//     case WM_XBUTTONDOWN:
//     case WM_XBUTTONUP:
//     case WM_MOUSEHOVER:
//         Mouse_ProcessMessage(message, wParam, lParam);
//         break;
//
//     }
// }
//

#endif // HAL_YOUHEI_MOUSE_H
