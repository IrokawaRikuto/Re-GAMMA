//Pushing_Obj_Manager.cpp
#include "Pushing_Obj_Manager.h"

#include "collision.h"
#include "player3D.h"
#include "keyboard.h"
#include "newKeyBind.h"
#include "Input.h"
#include "Bill_Board.h"
#include "mouse.h"
#include "camera.h"
#include "keyboard.h"

#include <cfloat>
#include <algorithm>

#include "UtilMath.h"
using namespace mu;

// Constants
static const float kPushMaxDist = 5.0f;     // Maximum distance to detect pushable object
static const float kPushSpeed = 0.05f;      // Speed at which objects are pushed

// State
static PUSH_STATE g_PushState = PUSH_STATE_NONE;
static PUSH_TARGET g_CurrentTarget;
static float g_LockedYawDeg = 0.0f;  // player rotation locked at grab time

static bool g_ShowBillBoard = false;
static XMFLOAT3 g_BillBoardPosition = { 0.0f, 0.0f, 0.0f };

static ID3D11Device* g_pDevice = nullptr;
static ID3D11DeviceContext* g_pContext = nullptr;
static ID3D11ShaderResourceView* g_BillBoardTexture = nullptr;

// Get field half size
static XMFLOAT3 Field_GetHalfSize(const MAPDATA& m)
{
    return XMFLOAT3{
        BOX_RADIUS * m.scale.x,
        BOX_RADIUS * m.scale.y,
        BOX_RADIUS * m.scale.z
    };
}

// Check if field type is pushable
static bool Field_IsPushable(FIELD t)
{
    switch (t)
    {
    case FIELD_OBJ_BOX:
        return true;
    default:
        return false;
    }
}

// Ray vs AABB intersection
static bool RaycastAABB(
    const XMFLOAT3& rayO,
    const XMFLOAT3& rayD_in,
    const XMFLOAT3& boxC,
    const XMFLOAT3& boxHalf,
    float maxDist,
    XMFLOAT3* outNormal,
    float* outT)
{
    XMFLOAT3 rayD = Normalize(rayD_in);

    float tmin = 0.0f;
    float tmax = maxDist;
    XMFLOAT3 hitN = { 0, 0, 0 };

    auto slab = [&](float ro, float rd, float c, float h, XMFLOAT3 nNeg, XMFLOAT3 nPos) -> bool
        {
            if (fabsf(rd) < 1e-6f)
            {
                return (ro >= c - h && ro <= c + h);
            }

            float inv = 1.0f / rd;
            float t1 = (c - h - ro) * inv;
            float t2 = (c + h - ro) * inv;

            XMFLOAT3 n1 = nNeg;
            XMFLOAT3 n2 = nPos;

            if (t1 > t2) { std::swap(t1, t2); std::swap(n1, n2); }

            if (t1 > tmin) { tmin = t1; hitN = n1; }
            if (t2 < tmax) { tmax = t2; }

            return tmin <= tmax;
        };

    if (!slab(rayO.x, rayD.x, boxC.x, boxHalf.x, { -1, 0, 0 }, { 1, 0, 0 })) return false;
    if (!slab(rayO.y, rayD.y, boxC.y, boxHalf.y, { 0, -1, 0 }, { 0, 1, 0 })) return false;
    if (!slab(rayO.z, rayD.z, boxC.z, boxHalf.z, { 0, 0, -1 }, { 0, 0, 1 })) return false;

    if (tmin < 0.0f || tmin > maxDist) return false;

    if (outNormal) *outNormal = hitN;
    if (outT) *outT = tmin;
    return true;
}

// Ray vs OBB intersection
static bool RaycastOBB(
    const XMFLOAT3& rayO,
    const XMFLOAT3& rayD_in,
    const MAPDATA& box,
    float maxDist,
    XMFLOAT3* outNormalW,
    float* outT)
{
    const XMFLOAT3 half = Field_GetHalfSize(box);

    const XMMATRIX R = XMMatrixRotationRollPitchYaw(
        XMConvertToRadians(box.rotate.x),
        XMConvertToRadians(box.rotate.y),
        XMConvertToRadians(box.rotate.z));

    const XMMATRIX invR = XMMatrixTranspose(R);

    XMVECTOR O = XMLoadFloat3(&rayO);
    XMVECTOR D = XMLoadFloat3(&rayD_in);
    D = XMVector3Normalize(D);

    XMVECTOR C = XMLoadFloat3(&box.pos);

    XMVECTOR Orel = O - C;
    XMVECTOR OlocV = XMVector3TransformNormal(Orel, invR);
    XMVECTOR DlocV = XMVector3TransformNormal(D, invR);

    XMFLOAT3 Oloc, Dloc;
    XMStoreFloat3(&Oloc, OlocV);
    XMStoreFloat3(&Dloc, DlocV);

    XMFLOAT3 nL = { 0, 0, 0 };
    float t = 0.0f;

    if (!RaycastAABB(Oloc, Dloc, XMFLOAT3(0, 0, 0), half, maxDist, &nL, &t))
        return false;

    XMVECTOR nW = XMVector3TransformNormal(XMLoadFloat3(&nL), R);
    nW = XMVector3Normalize(nW);

    if (outNormalW) XMStoreFloat3(outNormalW, nW);
    if (outT) *outT = t;
    return true;
}

// Find pushable target from player's forward direction
static bool FindPushableTarget(PUSH_TARGET* outT)
{
    PLAYER* p3 = GetPlayer3D();
    if (!p3) return false;

    // Ray origin at player center
    XMFLOAT3 rayO = p3->Position;
    rayO.y += Player3D_GetSolidHalfSize().y; // Center height

    // Ray direction is player's forward
    XMFLOAT3 rayD = Player3D_GetForward();

    std::vector<MAPDATA>& map = GetFieldMap();
    if (map.size() == 0) return false;

    float bestT = FLT_MAX;
    PUSH_TARGET best{};
    best.fieldIndex = -1;

    float maxDistSq = kPushMaxDist * kPushMaxDist;

      for (size_t i = 0; i < map.size(); ++i)
    {
        const MAPDATA& m = map[i];
        if (!Field_IsPushable(m.no)) continue;

        float dx = m.pos.x - rayO.x;
        float dy = m.pos.y - rayO.y;
        float dz = m.pos.z - rayO.z;
        float distSq = dx * dx + dy * dy + dz * dz;

        XMFLOAT3 half = Field_GetHalfSize(m);
        float objRadius = sqrtf(half.x * half.x + half.y * half.y + half.z * half.z);
        float threshold = kPushMaxDist + objRadius;

        if (distSq > threshold * threshold)
            continue;

        XMFLOAT3 normal;
        float t;

        if (RaycastOBB(rayO, rayD, m, kPushMaxDist, &normal, &t))
        {
            if (t < bestT)
            {
                bestT = t;
                best.fieldIndex = (int)i;
                // Push direction is opposite of hit normal (push into the object)
                best.pushDirection = { -normal.x, 0.0f, -normal.z };
            }
        }
    }

    if (best.fieldIndex < 0) return false;
    if (outT) *outT = best;
    return true;
}

// Check if object can move in the given direction (collision check)
static bool CanObjectMove(int fieldIndex, const XMFLOAT3& moveDir, float moveAmount)
{
    std::vector<MAPDATA>& map = GetFieldMap();
    if (fieldIndex < 0 || fieldIndex >= (int)map.size()) return false;

    const MAPDATA& obj = map[fieldIndex];
    XMFLOAT3 objHalf = Field_GetHalfSize(obj);

    // Calculate new position
    XMFLOAT3 newPos = obj.pos;
    newPos.x += moveDir.x * moveAmount;
    newPos.z += moveDir.z * moveAmount;

    float objMaxRadius = sqrtf(objHalf.x * objHalf.x + objHalf.y * objHalf.y + objHalf.z * objHalf.z);

    // Check collision with other solid objects
    for (size_t i = 0; i < map.size(); ++i)
    {
        if ((int)i == fieldIndex) continue;

        const MAPDATA& other = map[i];

        if (other.no == FIELD_GOAL || other.no == FIELD_STAGE_1 ||
            other.no == FIELD_STAGE_2 || other.no == FIELD_STAGE_3 ||
            other.no == FIELD_PORTAL_K || other.no == FIELD_PORTAL_J ||
            other.no == FIELD_FOUNTAIN || other.no == FIELD_EMPTY_BOX)
            continue;


        XMFLOAT3 otherHalf = Field_GetHalfSize(other);
        float otherMaxRadius = sqrtf(otherHalf.x * otherHalf.x + otherHalf.y * otherHalf.y + otherHalf.z * otherHalf.z);

        float distSq = (newPos.x - other.pos.x) * (newPos.x - other.pos.x) +
            (newPos.y - other.pos.y) * (newPos.y - other.pos.y) +
            (newPos.z - other.pos.z) * (newPos.z - other.pos.z);
        float maxDist = objMaxRadius + otherMaxRadius;

        if (distSq > maxDist * maxDist)
            continue;

        // Simple AABB overlap check
        float dx = fabsf(newPos.x - other.pos.x);
        float dy = fabsf(newPos.y - other.pos.y);
        float dz = fabsf(newPos.z - other.pos.z);

        float overlapX = (objHalf.x + otherHalf.x) - dx;
        float overlapY = (objHalf.y + otherHalf.y) - dy;
        float overlapZ = (objHalf.z + otherHalf.z) - dz;

        if (overlapX > 0.0f && overlapY > 0.0f && overlapZ > 0.0f)
        {
            return false; // Collision detected
        }
    }

    return true;
}

// Apply push movement to the object
static void ApplyPushMovement(int fieldIndex, const XMFLOAT3& pushDir)
{
    std::vector<MAPDATA>& map = GetFieldMap();
    if (fieldIndex < 0 || fieldIndex >= (int)map.size()) return;

    // Check if object can move
    if (!CanObjectMove(fieldIndex, pushDir, kPushSpeed))
    {
        return; // Can't move, blocked by something
    }

    // Move the object
    map[fieldIndex].pos.x += pushDir.x * kPushSpeed;
    map[fieldIndex].pos.z += pushDir.z * kPushSpeed;
}

void PlayerPushManager_Init()
{
    g_pDevice = Direct3D_GetDevice();

    g_PushState = PUSH_STATE_NONE;
    g_CurrentTarget.fieldIndex = -1;
    g_CurrentTarget.pushDirection = { 0, 0, 0 };

    TexMetadata metadata;
    ScratchImage image;
    LoadFromWICFile(L"asset\\texture\\UI\\Keyboard\\KeyboardPush.png", WIC_FLAGS_NONE, &metadata, image);
    CreateShaderResourceView(g_pDevice, image.GetImages(), image.GetImageCount(), metadata, &g_BillBoardTexture);

}

void PlayerPushManager_Finalize()
{
    SAFE_RELEASE(g_BillBoardTexture);
    g_PushState = PUSH_STATE_NONE;
    g_CurrentTarget.fieldIndex = -1;
}

static bool IsTargetStillValid(int fieldIndex, float maxDist)
{
    PLAYER* p3 = GetPlayer3D();
    if (!p3) return false;

    std::vector<MAPDATA>& map = GetFieldMap();
    if (fieldIndex < 0 || fieldIndex >= (int)map.size()) return false;

    const MAPDATA& m = map[fieldIndex];
    if (!Field_IsPushable(m.no)) return false;

    XMFLOAT3 playerPos = p3->Position;
    playerPos.y += Player3D_GetSolidHalfSize().y;

    float dx = m.pos.x - playerPos.x;
    float dy = m.pos.y - playerPos.y;
    float dz = m.pos.z - playerPos.z;
    float distSq = dx * dx + dy * dy + dz * dz;

    float threshold = maxDist + 1.0f;
    return distSq <= threshold * threshold;
}

void PlayerPushManager_Update()
{
    PLAYER* p3 = GetPlayer3D();
    if (!p3)
    {
        g_PushState = PUSH_STATE_NONE;
        g_CurrentTarget.fieldIndex = -1;
        g_ShowBillBoard = false;
        return;
    }

    // [FIX] Push trigger: left mouse button (and gamepad B for controller).
    // Was bound to ActionKey (F) which collided with ChangeKey (mode switch).
    // [FIX] Use Mouse_PeekState (not Mouse_GetState) so we don't consume the
    // relative-mode delta. Player3DCamera_Update runs later in the same
    // frame and needs that delta for mouse-look. Previously this call
    // silently swallowed the delta and the camera felt completely dead.
    Mouse_State pushMs{};
    Mouse_PeekState(&pushMs);
    bool actionHeld = pushMs.leftButton;
    // Push pad button: was B in the old layout, but B is now Jump per the
    // new Switch-style mapping. Moved to RB (right shoulder) so push doesn't
    // fire on every jump.
    if (gPad.IsConnected() && gPad.IsButtonPressed(XINPUT_GAMEPAD_RIGHT_SHOULDER)) actionHeld = true;

    // Nearby pushable -> show prompt billboard above the object.
    PUSH_TARGET nearbyTarget;
    bool hasNearbyTarget = FindPushableTarget(&nearbyTarget);
    if (hasNearbyTarget && g_PushState == PUSH_STATE_NONE)
    {
        g_ShowBillBoard = true;
        std::vector<MAPDATA>& map = GetFieldMap();
        if (nearbyTarget.fieldIndex >= 0 && nearbyTarget.fieldIndex < (int)map.size())
        {
            const MAPDATA& obj = map[nearbyTarget.fieldIndex];
            XMFLOAT3 objHalf = Field_GetHalfSize(obj);
            g_BillBoardPosition = XMFLOAT3(
                obj.pos.x,
                obj.pos.y + objHalf.y + 1.0f,
                obj.pos.z);
        }
    }
    else
    {
        g_ShowBillBoard = false;
    }

    // Push speed: ~30% of walk equilibrium (moveSpeed*damping/(1-damping)
    // = 0.009*0.925/0.075 = 0.111 per frame -> 30% = 0.033). Slightly higher
    // here so the box still feels responsive while clearly slower than walk.
    const float kPushSpeedPerFrame = 0.04f;

    switch (g_PushState)
    {
    case PUSH_STATE_NONE:
    {
        if (actionHeld && !p3->isDash)
        {
            PUSH_TARGET target;
            if (FindPushableTarget(&target))
            {
                g_CurrentTarget = target;

                // [FIX] Snap push axis to nearest cardinal direction in XZ
                // so the box can only travel along world X or Z (no diagonals).
                std::vector<MAPDATA>& fm = GetFieldMap();
                XMFLOAT3 boxPos = fm[target.fieldIndex].pos;
                float dx = boxPos.x - p3->Position.x;
                float dz = boxPos.z - p3->Position.z;
                XMFLOAT3 axis;
                if (fabsf(dx) >= fabsf(dz))
                    axis = { (dx >= 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f };
                else
                    axis = { 0.0f, 0.0f, (dz >= 0.0f) ? 1.0f : -1.0f };
                g_CurrentTarget.pushDirection = axis;

                // Lock player rotation so they face the push axis. atan2(x,z)
                // matches Player3D_Move's yaw convention; add FirstRotation.y
                // offset so the world-aligned yaw lines up with the model.
                float yawDeg = XMConvertToDegrees(atan2f(axis.x, axis.z));
                g_LockedYawDeg = yawDeg + p3->FirstRotation.y;

                g_PushState = PUSH_STATE_PUSHING;
                g_ShowBillBoard = false;
                p3->isPushing = true;
                p3->CurrentAnimIndex = PLAYER_ANIM_PUSH;
            }
        }
    }
    break;

    case PUSH_STATE_PUSHING:
    {
        g_ShowBillBoard = false;
        if (!actionHeld || p3->isDash)
        {
            g_PushState = PUSH_STATE_NONE;
            g_CurrentTarget.fieldIndex = -1;
            p3->isPushing = false;
            break;
        }

        if (!IsTargetStillValid(g_CurrentTarget.fieldIndex, kPushMaxDist))
        {
            g_PushState = PUSH_STATE_NONE;
            g_CurrentTarget.fieldIndex = -1;
            p3->isPushing = false;
            p3->CurrentAnimIndex = PLAYER_ANIM_IDLE;
            break;
        }

        const XMFLOAT3& axis = g_CurrentTarget.pushDirection;

        // Camera-relative input: build a WSAD movement vector in camera
        // space and project it onto the snapped push axis. So when the
        // player approaches the box from the front W/S pushes, but if the
        // camera is to one side then A/D becomes the active control. Only
        // the projection onto the push axis is applied -- diagonal input
        // along the perpendicular is dropped.
        float wsAxis = 0.0f, adAxis = 0.0f;
        if (Keyboard_IsKeyDown(KK_W)) wsAxis += 1.0f;
        if (Keyboard_IsKeyDown(KK_S)) wsAxis -= 1.0f;
        if (Keyboard_IsKeyDown(KK_A)) adAxis -= 1.0f;
        if (Keyboard_IsKeyDown(KK_D)) adAxis += 1.0f;
        if (gPad.IsConnected())
        {
            float lx = gPad.GetLeftStickX();
            float ly = gPad.GetLeftStickY();
            if (fabsf(ly) >= 0.30f) wsAxis = ly;
            if (fabsf(lx) >= 0.30f) adAxis = lx;
        }

        XMFLOAT3 camPos = GetCameraPosition();
        XMFLOAT3 camAt  = GetCameraAtPosition();
        XMFLOAT3 camFwd = { camAt.x - camPos.x, 0.0f, camAt.z - camPos.z };
        float fl = sqrtf(camFwd.x * camFwd.x + camFwd.z * camFwd.z);
        if (fl > 1e-6f) { camFwd.x /= fl; camFwd.z /= fl; }
        // Camera right vector = cross(+Y, fwd) = (fwd.z, 0, -fwd.x).
        float rightX = camFwd.z;
        float rightZ = -camFwd.x;

        // Move vector in camera space, lifted to world.
        float worldX = camFwd.x * wsAxis + rightX * adAxis;
        float worldZ = camFwd.z * wsAxis + rightZ * adAxis;
        // Project onto the snapped push axis.
        float proj = worldX * axis.x + worldZ * axis.z;
        if (proj >  1.0f) proj =  1.0f;
        if (proj < -1.0f) proj = -1.0f;
        float amount = kPushSpeedPerFrame * proj;

        // [FIX] Move player position DIRECTLY (bypass velocity damping) so the
        // player and box translate by EXACTLY the same delta each frame. The
        // previous velocity-based approach left the player taking 0.925x of the
        // box's step (damping), so the two drifted apart over time. Zero the
        // velocity afterwards so leftover momentum doesn't carry past release.
        p3->Rotation.y = g_LockedYawDeg;

        if (fabsf(amount) > 1e-6f)
        {
            std::vector<MAPDATA>& fm = GetFieldMap();
            if (CanObjectMove(g_CurrentTarget.fieldIndex, axis, amount))
            {
                fm[g_CurrentTarget.fieldIndex].pos.x += axis.x * amount;
                fm[g_CurrentTarget.fieldIndex].pos.z += axis.z * amount;
                p3->Position.x += axis.x * amount;
                p3->Position.z += axis.z * amount;
            }
            // Box blocked: neither moves, player stays in contact.
        }
        p3->Velocity.x = 0.0f;
        p3->Velocity.z = 0.0f;

        p3->isPushing = true;
        p3->CurrentAnimIndex = PLAYER_ANIM_PUSH;
    }
    break;
    }
}

void PlayerPushManager_Draw()
{
    if (g_ShowBillBoard)
    {
        g_pContext = Direct3D_GetDeviceContext();

        Shader_Begin();
        // [FIX] Set deterministic draw state. Previously this billboard
        // inherited whatever blend/depth Player3D_Draw left, so when Option
        // (or any other path) tweaked state, the push prompt would flicker
        // because the actual GPU state varied frame-to-frame.
        SetBlendState(BLENDSTATE_ALFA);
        SetDepthTest(TRUE);

        g_pContext->PSSetShaderResources(0, 1, &g_BillBoardTexture);

        XMFLOAT2 size = { 2.0f, 2.0f };  // Billboard size
        XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, 1.0f };

        DrawBillBoard(g_BillBoardPosition, size, color, 0, 1, 1);
    }
}

// Add getter for billboard visibility (optional, for debugging)
bool PlayerPushManager_ShouldShowBillBoard()
{
    return g_ShowBillBoard;
}

XMFLOAT3 PlayerPushManager_GetBillBoardPosition()
{
    return g_BillBoardPosition;
}

PUSH_STATE PlayerPushManager_GetState()
{
    return g_PushState;
}

bool PlayerPushManager_IsPushing()
{
    return g_PushState == PUSH_STATE_PUSHING;
}

const PUSH_TARGET* PlayerPushManager_GetCurrentTarget()
{
    if (g_CurrentTarget.fieldIndex < 0) return nullptr;
    return &g_CurrentTarget;
}