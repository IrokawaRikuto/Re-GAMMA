// Collision2D.cpp
// 2D�v���C���[ vs �V�[���Փ� + 2D�g���K�[ + �e���ǏՓ� + �e��ʐڐG + �ړ�����

#include <cfloat>
#include <cmath>
#include <algorithm>
#include "Collision.h"
#include "Player2D.h"

using namespace DirectX;
using namespace mu;

// �O���錾
extern XMFLOAT3 GetPlayer2DSolidCollider();
extern bool      Field_IsSolid(FIELD t);
extern bool      Field_IsTrigger(FIELD t);

//=========================================================================================================
// �g���K�[�����v�Z�i2D�j
//=========================================================================================================

static TRIGGER_SIDE CalcTriggerSide2D(const XMFLOAT3& playerPos, float playerRotZ, const XMFLOAT3& triggerPos)
{
    float dx = triggerPos.x - playerPos.x;
    float dy = triggerPos.y - playerPos.y;

    float radZ = XMConvertToRadians(playerRotZ);
    float cosZ = cosf(-radZ);
    float sinZ = sinf(-radZ);

    float localX = dx * cosZ - dy * sinZ;
    float localY = dx * sinZ + dy * cosZ;

    if (fabsf(localX) > fabsf(localY))
        return (localX > 0) ? TRIGGER_SIDE_RIGHT : TRIGGER_SIDE_LEFT;
    else
        return (localY > 0) ? TRIGGER_SIDE_TOP : TRIGGER_SIDE_BOTTOM;
}

//=========================================================================================================
// XZ���ʂ̓��ρi�e�Փ˗p�j
//=========================================================================================================

static float DotXZ(const XMFLOAT3& a, const XMFLOAT3& b)
{
    return a.x * b.x + a.z * b.z;
}

static float ProjectRadiusOnAxis_Yaw(const XMFLOAT3& axis, const XMFLOAT3& half, float yawDeg)
{
    const float yaw   = XMConvertToRadians(yawDeg);
    const XMFLOAT3 right = { cosf(yaw), 0.0f, -sinf(yaw) };
    const XMFLOAT3 fwd   = { sinf(yaw), 0.0f,  cosf(yaw) };
    const XMFLOAT3 up    = { 0.0f, 1.0f, 0.0f };

    return fabsf(Dot(axis, right)) * half.x
         + fabsf(Dot(axis, up))    * half.y
         + fabsf(Dot(axis, fwd))   * half.z;
}

//=========================================================================================================
// 2D�v���C���[ vs �V�[���iSolid�j�Փ�
//=========================================================================================================

int Player2DField_Collision()
{
    int hit = HIT_NONE;

    PLAYER* player = GetPlayer2D();
    std::vector<MAPDATA>& map = GetFieldMap();
    if (!player || map.empty()) return hit;

    player->isGround = false;
    Capsule2D capsule = Player2D_GetCapsule();
    const int MAX_ITERATIONS = 4;

    for (int iter = 0; iter < MAX_ITERATIONS; ++iter)
    {
        bool hitThisIter = false;

        for (size_t i = 0; i < map.size(); ++i)
        {
            if (!Field_IsSolid(map[i].no)) continue;

            XMFLOAT3 push, norm;
            if (!Resolve_Capsule2D_OBB(capsule, map[i].pos,
                Field_GetCollisionHalfSize(map[i]), map[i].rotate.y, &push, &norm))
                continue;

            // [FIX] Side wall: flatten the normal Y so a horizontal push into
            // a vertical face cannot leak into upward velocity (the 2D player
            // was slowly climbing walls). Floors/ceilings keep their Y normal.
            if (fabsf(norm.y) < 0.7f)
            {
                norm.y = 0.0f;
                float nl = sqrtf(norm.x * norm.x + norm.z * norm.z);
                if (nl > 1e-6f) { norm.x /= nl; norm.z /= nl; }
            }

            if (fabsf(push.x) > 1e-8f || fabsf(push.y) > 1e-8f || fabsf(push.z) > 1e-8f)
            {
                // [FIX] Side wall: do not let the MTV push the capsule UP (it
                // popped the 2D player up vertical walls). Keep push.y only for
                // floor/ceiling resolves (vertical normal).
                if (fabsf(norm.y) < 0.7f) push.y = 0.0f;
                capsule.center = capsule.center + push;
                hitThisIter = true;
            }

            float ax = fabsf(norm.x), ay = fabsf(norm.y), az = fabsf(norm.z);

            // [FIX] Normal-projected velocity clipping (was: hard zero per axis).
            // Removes vibration from "input -> penetrate -> push -> input" cycle
            // by removing only the into-wall component; tangent velocity survives.
            float vDotN = player->Velocity.x * norm.x
                        + player->Velocity.y * norm.y
                        + player->Velocity.z * norm.z;
            if (vDotN < 0.0f)
            {
                player->Velocity.x -= norm.x * vDotN;
                player->Velocity.y -= norm.y * vDotN;
                player->Velocity.z -= norm.z * vDotN;
            }

            if (ay >= ax && ay >= az)
            {
                if (norm.y > 0)
                { player->isGround = true; hit = HIT_GROUND; }
            }
            else if (ax >= az)
            {
                hit = (norm.x > 0) ? HIT_WALL_PlusX : HIT_WALL_NegX;
            }
            else
            {
                hit = (norm.z > 0) ? HIT_WALL_PlusZ : HIT_WALL_NegZ;
            }
        }

        if (!hitThisIter) break;
    }

    float totalHeight = PLAYER2D_CAPSULE_HEIGHT + PLAYER2D_CAPSULE_RADIUS * 2.0f;
    player->Position.x = capsule.center.x;
    player->Position.y = capsule.center.y - totalHeight * 0.5f;
    player->Position.z = capsule.center.z;

    return hit;
}

//=========================================================================================================
// 2D�g���K�[����
//=========================================================================================================

bool Collision_Player2DTrigger(TRIGGER_HIT* outHit, float extraRange)
{
    if (outHit) *outHit = TRIGGER_HIT{};

    PLAYER* p = GetPlayer2D();
    if (!p) return false;

    auto& map = GetFieldMap();
    if (map.empty()) return false;

    Capsule2D capsule = Player2D_GetCapsule();
    Capsule2D expandedCapsule = capsule;
    expandedCapsule.radius += extraRange;

    bool        found  = false;
    float       bestD2 = 1e30f;
    TRIGGER_HIT best;

    for (size_t i = 0; i < map.size(); ++i)
    {
        if (!Field_IsTrigger(map[i].no)) continue;

        XMFLOAT3 tHalf = Field_GetCollisionHalfSize(map[i]);
        if (!Capsule2D_Intersect_OBB(expandedCapsule, map[i].pos, tHalf, map[i].rotate.y))
            continue;

        float dx = map[i].pos.x - capsule.center.x;
        float dy = map[i].pos.y - capsule.center.y;
        float d2 = dx * dx + dy * dy;

        if (!found || d2 < bestD2)
        {
            found  = true;
            bestD2 = d2;
            best.hit      = true;
            best.mapIndex = (int)i;
            best.type     = map[i].no;
            best.side     = CalcTriggerSide2D(capsule.center, p->Rotation.z, map[i].pos);
        }
    }

    if (!found) return false;
    if (outHit) *outHit = best;
    return true;
}

//=========================================================================================================
// 2D�e���ǏՓˁi���x�N���b�v�j
//=========================================================================================================

bool Player2DShadow_Collision()
{
    // [REWRITE] AABB-based side push for shadow prisms. The previous
    // polygon-edge implementation had two paths: an outside swept test
    // (correct, kept in spirit) and an inside multi-pass clip that
    // iteratively pushed the player outward, producing the visible
    // shove-back when the player ended up overlapping a prism. The
    // new code uses the prism world AABB for both detection and
    // resolution: a slab-method sweep finds the first face the velocity
    // would penetrate, and only that face's axis component is zeroed.
    // The tangent component survives so the player slides along walls.
    PLAYER* player = GetPlayer2D();
    if (!player) return false;

    const auto& prisms = Collision_GetShadowPrisms();
    if (prisms.empty()) return false;

    ShadowContactState& sc = GetShadowContactState();
    Capsule2D capsule = Player2D_GetCapsule();

    const float skin = 0.02f;
    const float eps  = 1e-6f;

    XMFLOAT3 dXZ = { player->Velocity.x, 0.0f, player->Velocity.z };
    bool hitAny = false;

    sc.frameSlopeDeltaY = 0.0f;
    sc.dbgMoveInXZ  = dXZ;
    sc.dbgMoveOutXZ = dXZ;
    sc.dbgMoveSlope = { 0, 0, 0 };

    // Slope projection: while standing on a sloped segment, project
    // horizontal input onto the segment direction (kept from old impl).
    if (sc.hasStandDir && sc.lastStandingPrismIndex >= 0 && player->Velocity.y <= 0.02f)
    {
        float dl = sqrtf(dXZ.x * dXZ.x + dXZ.z * dXZ.z);
        if (dl > 1e-6f)
        {
            float d = dXZ.x * sc.standDirXZ.x + dXZ.z * sc.standDirXZ.z;
            dXZ.x = sc.standDirXZ.x * d;
            dXZ.z = sc.standDirXZ.z * d;
        }
    }

    const float px = capsule.center.x;
    const float pz = capsule.center.z;
    const float pr = capsule.radius;

    for (int prismIdx = 0; prismIdx < (int)prisms.size(); ++prismIdx)
    {
        const ShadowPrism* prism = prisms[prismIdx];
        if (!prism || !prism->isValid) continue;

        // Currently standing prism: TopContact will resolve it.
        if (sc.standSegValid && sc.standSegWalkable &&
            prismIdx == sc.lastStandingPrismIndex && player->Velocity.y <= 0.02f)
            continue;

        // Foot above the prism top: landing -> not a wall.
        const float footY = player->Position.y;
        const float topTol = pr + 0.10f;
        if (footY >= prism->groundMaxY - topTol) continue;

        // Skip near-horizontal prisms (those are ground for TopContact).
        if (prism->n.y > 0.70f) continue;

        // Vertical Y overlap: capsule has to share Y range with the prism
        // slab or this is not a relevant collider this frame.
        const float capTopY    = capsule.center.y + capsule.halfHeight + pr;
        const float capBottomY = capsule.center.y - capsule.halfHeight - pr;
        if (capTopY < prism->aabbMin.y || capBottomY > prism->aabbMax.y) continue;

        // Expanded AABB in XZ (Minkowski sum with capsule disc).
        const float aMinX = prism->aabbMin.x - pr - skin;
        const float aMaxX = prism->aabbMax.x + pr + skin;
        const float aMinZ = prism->aabbMin.z - pr - skin;
        const float aMaxZ = prism->aabbMax.z + pr + skin;

        // Already inside the expanded AABB? Let the player keep moving --
        // do not push outward. They will escape via gravity / input. This
        // avoids the old shove-back when the capsule incidentally overlaps
        // a prism edge (very common when landing on a wall-mounted shadow).
        if (px > aMinX && px < aMaxX && pz > aMinZ && pz < aMaxZ) continue;

        const float vx = dXZ.x;
        const float vz = dXZ.z;

        // X slab
        float tEnterX = -1e30f, tExitX = 1e30f;
        if (fabsf(vx) > eps)
        {
            float t1 = (aMinX - px) / vx;
            float t2 = (aMaxX - px) / vx;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            tEnterX = t1; tExitX = t2;
        }
        else
        {
            if (px <= aMinX || px >= aMaxX) continue;
        }

        // Z slab
        float tEnterZ = -1e30f, tExitZ = 1e30f;
        if (fabsf(vz) > eps)
        {
            float t1 = (aMinZ - pz) / vz;
            float t2 = (aMaxZ - pz) / vz;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            tEnterZ = t1; tExitZ = t2;
        }
        else
        {
            if (pz <= aMinZ || pz >= aMaxZ) continue;
        }

        const float tEnter = (tEnterX > tEnterZ) ? tEnterX : tEnterZ;
        const float tExit  = (tExitX  < tExitZ ) ? tExitX  : tExitZ;
        if (tEnter > tExit) continue;
        if (tEnter < 0.0f || tEnter > 1.0f) continue;

        // Hit. Clamp the entry-axis component to land just before the face;
        // leave the tangent component intact so the player can slide.
        const float tStop = (tEnter > 0.001f) ? (tEnter - 0.001f) : 0.0f;
        if (tEnterX >= tEnterZ)
        {
            dXZ.x = vx * tStop;
        }
        else
        {
            dXZ.z = vz * tStop;
        }
        hitAny = true;
    }

    sc.dbgMoveOutXZ    = dXZ;
    sc.frameSlopeDeltaY = 0.0f;

    if (sc.standSegValid && sc.standSegWalkable && sc.hasStandDir &&
        sc.lastStandingPrismIndex >= 0 && player->Velocity.y <= 0.02f && sc.standLenXZ > 1e-6f)
    {
        float amount = dXZ.x * sc.standDirXZ.x + dXZ.z * sc.standDirXZ.z;
        sc.frameSlopeDeltaY = sc.standSegAB.y * (amount / sc.standLenXZ);
    }
    sc.dbgMoveSlope = { dXZ.x, sc.frameSlopeDeltaY, dXZ.z };

    player->Velocity.x = dXZ.x;
    player->Velocity.z = dXZ.z;

    return hitAny;
}

//=========================================================================================================
// 2D shadow top contact (stand on prism top edges)
//=========================================================================================================

bool Player2DShadow_TopContact()
{
    PLAYER* player = GetPlayer2D();
    if (!player) return false;

    const auto& prisms = Collision_GetShadowPrisms();
    if (prisms.empty()) return false;

    ShadowContactState& sc = GetShadowContactState();
    Capsule2D capsule = Player2D_GetCapsule();

    const float kSkin          = 0.01f;
    const float kStandWidth    = capsule.radius + 0.12f;
    const float kContactUp     = 0.25f;
    const float kContactDown   = -(capsule.radius + 0.45f);
    const float kJumpEscapeVel = 0.02f;
    const bool  isRising       = (player->Velocity.y > kJumpEscapeVel);
    const float kMaxWalkSlopeTan = 57.0f;

    auto Dot3   = [](const XMFLOAT3& a, const XMFLOAT3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    auto LenSqXZ = [](const XMFLOAT3& v) { return v.x * v.x + v.z * v.z; };

    auto CapsuleProjection = [&](const XMFLOAT3& axisW) -> float {
        XMFLOAT3 capAxis = { -sinf(capsule.rotationZ), cosf(capsule.rotationZ), 0.0f };
        return capsule.radius + fabsf(Dot(axisW, capAxis)) * capsule.halfHeight;
    };

    // �J�v�Z����AABB�i�e����p�j
    XMFLOAT3 capHalf = capsule.GetBoundingHalfSize();
    const float aabbPad = 0.30f;
    XMFLOAT3 capMin = capsule.center - (capHalf + XMFLOAT3(aabbPad, aabbPad, aabbPad));
    XMFLOAT3 capMax = capsule.center + (capHalf + XMFLOAT3(aabbPad, aabbPad, aabbPad));

    auto AABBOverlap = [](const XMFLOAT3& aMin, const XMFLOAT3& aMax,
                          const XMFLOAT3& bMin, const XMFLOAT3& bMax) -> bool {
        return (aMin.x <= bMax.x && aMax.x >= bMin.x) &&
               (aMin.y <= bMax.y && aMax.y >= bMin.y) &&
               (aMin.z <= bMax.z && aMax.z >= bMin.z);
    };

    const float footY     = player->Position.y;
    const float footYPrev = footY - player->Velocity.y - sc.frameSlopeDeltaY;

    int      bestIdx     = -1;
    float    bestAbs     = 1e9f;
    float    bestGroundY = 0.0f;
    XMFLOAT3 bestDirXZ   = { 1, 0, 0 };
    bool     bestHasDir  = false;
    XMFLOAT3 bestSegA{}, bestSegB{}, bestSegQ{}, bestSegAB = { 1, 0, 0 };
    float    bestLenXZ   = 1.0f;
    bool     bestWalkable = false;

    for (int prismIdx = 0; prismIdx < (int)prisms.size(); ++prismIdx)
    {
        const ShadowPrism* prism = prisms[prismIdx];
        if (!prism || !prism->isValid) continue;
        if (prism->baseWorld.size() < 2) continue;

        XMFLOAT3 pMin = prism->aabbMin - XMFLOAT3(aabbPad, aabbPad, aabbPad);
        XMFLOAT3 pMax = prism->aabbMax + XMFLOAT3(aabbPad, aabbPad, aabbPad);
        if (!AABBOverlap(capMin, capMax, pMin, pMax)) continue;

        XMFLOAT3 rel = capsule.center - prism->origin;
        float n0 = Dot3(rel, prism->n);
        float rN = CapsuleProjection(prism->n);
        const float slabTol = 0.05f;
        if (n0 < -rN - slabTol || n0 > prism->thickness + rN + slabTol) continue;

        float sliceN = Clamp(n0, 0.0f, prism->thickness);

        auto ProcessSeg = [&](XMFLOAT3 a, XMFLOAT3 b)
        {
            a = a + prism->n * sliceN;
            b = b + prism->n * sliceN;

            XMFLOAT3 e = b - a;
            if (LenSqXZ(e) < 1e-5f) return;

            XMFLOAT3 p  = capsule.center;
            XMFLOAT3 ab = b - a;
            float abxz2  = ab.x * ab.x + ab.z * ab.z;
            if (abxz2 < 1e-6f) return;

            float lenXZ   = sqrtf(abxz2);
            float slopeTan = fabsf(ab.y) / (lenXZ + 1e-6f);
            bool  walkable = (slopeTan <= kMaxWalkSlopeTan);

            float t = ((p.x - a.x) * ab.x + (p.z - a.z) * ab.z) / abxz2;
            t = Clamp(t, 0.0f, 1.0f);

            XMFLOAT3 q   = a + ab * t;
            float dx      = p.x - q.x;
            float dz      = p.z - q.z;
            float distXZ  = sqrtf(dx * dx + dz * dz);
            if (distXZ > kStandWidth) return;

            float groundY      = q.y;
            float distToGround = footY - groundY;

            if (isRising && distToGround > 0.0f) return;

            bool crossed = (footYPrev >= groundY + kSkin) && (footY <= groundY + kSkin);
            if (!crossed)
            {
                if (distToGround < kContactDown || distToGround > kContactUp) return;
            }

            float absDist = fabsf(distToGround);
            if (absDist < bestAbs)
            {
                bestAbs     = absDist;
                bestIdx     = prismIdx;
                bestGroundY = groundY;
                bestSegA    = a;
                bestSegB    = b;
                bestSegQ    = q;
                bestSegAB   = ab;
                bestLenXZ   = lenXZ;
                bestWalkable = walkable;

                XMFLOAT3 dir = { ab.x, 0.0f, ab.z };
                float dl = sqrtf(dir.x * dir.x + dir.z * dir.z);
                if (dl > 1e-6f) { dir.x /= dl; dir.z /= dl; bestDirXZ = dir; bestHasDir = true; }
                else            { bestHasDir = false; }
            }
        };

        if (!prism->standSegments.empty())
        {
            for (const auto& s : prism->standSegments)
                ProcessSeg(s.a, s.b);
        }
        else
        {
            const int nV = (int)prism->baseWorld.size();
            for (int i = 0; i < nV; ++i)
                ProcessSeg(prism->baseWorld[i], prism->baseWorld[(i + 1) % nV]);
        }
    }

    bool hitAny = false;

    if (bestIdx >= 0)
    {
        float targetY       = bestGroundY + kSkin;
        player->Position.y  = targetY;
        if (player->Velocity.y < 0.0f) player->Velocity.y = 0.0f;
        player->isGround = true;
        hitAny = true;

        sc.lastStandingPrismIndex = bestIdx;
        sc.graceFrames            = 0;
        sc.lastShadowTopPos       = { player->Position.x, targetY, player->Position.z };

        sc.standSegValid    = true;
        sc.standSegWalkable = bestWalkable;
        sc.standSegA        = bestSegA;
        sc.standSegB        = bestSegB;
        sc.standSegAB       = bestSegAB;
        sc.standClosestQ    = bestSegQ;
        sc.standLenXZ       = (bestLenXZ > 1e-6f) ? bestLenXZ : 1.0f;

        sc.hasStandDir = bestHasDir;
        if (bestHasDir)
        {
            sc.standDirXZ     = bestDirXZ;
            sc.lastStandDirXZ = bestDirXZ;
        }
    }
    else
    {
        // Coyote time
        const int GRACE = 6;
        if (!isRising && sc.lastStandingPrismIndex >= 0)
        {
            sc.graceFrames++;
            if (sc.graceFrames <= GRACE)
            {
                player->isGround = true;
                hitAny = true;
                if (player->Velocity.y < 0.0f) player->Velocity.y = 0.0f;
            }
            else
            {
                sc.lastStandingPrismIndex = -1;
                sc.graceFrames    = 0;
                sc.standSegValid  = false;
                sc.standSegWalkable = false;
            }
        }
        else
        {
            sc.lastStandingPrismIndex = -1;
            sc.graceFrames    = 0;
            sc.standSegValid  = false;
            sc.standSegWalkable = false;
        }
    }

    return hitAny;
}

//=========================================================================================================
// 2D�ړ������i���x���ʒu���Փˁ���ʐڐG�j
//=========================================================================================================

// [NEW] Predictive velocity clamp against 3D solid field boxes.
// Resolves the user-reported vibration when the player presses into a 3D wall
// (or an invisible E box) while in shadow state: the reactive Field_Collision
// below pushes the capsule out only AFTER it has already penetrated, so each
// frame the player visibly bumps into the wall and bounces back. By predicting
// the next-frame capsule center and removing the into-wall velocity component
// here, the player stops cleanly at the wall surface (same behavior as the
// shadow-prism slab sweep above).
//
// Runs after Player2DShadow_Collision so it sees the velocity that survives
// shadow clipping. Iterates up to 4 times so chained boxes resolve correctly.
static void Player2DField_VelocityClamp()
{
    PLAYER* player = GetPlayer2D();
    std::vector<MAPDATA>& map = GetFieldMap();
    if (!player || map.empty()) return;

    const int MAX_ITER = 4;
    for (int iter = 0; iter < MAX_ITER; ++iter)
    {
        Capsule2D capsule = Player2D_GetCapsule();
        Capsule2D nextCap = capsule;
        nextCap.center = capsule.center + player->Velocity;

        bool clamped = false;
        for (size_t i = 0; i < map.size(); ++i)
        {
            if (!Field_IsSolid(map[i].no)) continue;

            XMFLOAT3 push, norm;
            if (!Resolve_Capsule2D_OBB(nextCap, map[i].pos,
                Field_GetCollisionHalfSize(map[i]), map[i].rotate.y, &push, &norm))
                continue;

            // [FIX] Side wall: flatten the normal Y so a horizontal push into
            // a vertical face cannot leak into upward velocity (the 2D player
            // was slowly climbing walls). Floors/ceilings keep their Y normal.
            if (fabsf(norm.y) < 0.7f)
            {
                norm.y = 0.0f;
                float nl = sqrtf(norm.x * norm.x + norm.z * norm.z);
                if (nl > 1e-6f) { norm.x /= nl; norm.z /= nl; }
            }

            // Predicted overlap. `norm` points OUT of the box. Remove the
            // into-wall component of velocity so we land just at the surface.
            float vDotN = player->Velocity.x * norm.x
                        + player->Velocity.y * norm.y
                        + player->Velocity.z * norm.z;
            if (vDotN < 0.0f)
            {
                player->Velocity.x -= norm.x * vDotN;
                player->Velocity.y -= norm.y * vDotN;
                player->Velocity.z -= norm.z * vDotN;
                clamped = true;
            }
        }
        if (!clamped) break;
    }
}

int Collision_Player2D_MoveAndCollision()
{
    PLAYER* player = GetPlayer2D();
    if (!player) return HIT_NONE;

    ShadowContactState& sc = GetShadowContactState();

    Player2DShadow_Collision();
    Player2DField_VelocityClamp();   // [NEW] predictive clamp vs 3D boxes

    player->Position.x += player->Velocity.x;
    player->Position.y += player->Velocity.y;
    player->Position.z += player->Velocity.z;
    player->Position.y += sc.frameSlopeDeltaY;

    int hit = Player2DField_Collision();

    Player2DShadow_TopContact();

    return hit;
}