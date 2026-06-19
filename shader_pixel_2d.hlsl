/*==============================================================================

   2D描画用ピクセルシェーダー [shader_pixel_2d.hlsl]
--------------------------------------------------------------------------------
==============================================================================*/

Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

// SHADOW MAP
TextureCube g_ShadowCubemap : register(t1);
SamplerState g_ShadowSampler : register(s1);

struct LIGHT
{
    bool Enable;
    bool3 dummy;
    float4 Dir;
    float4 Diffuse;
    float4 Ambient;
    float Radius;
    float3 pad1;
};

cbuffer Buffer2 : register(b2)
{
    LIGHT Light;
}


cbuffer Buffer4 : register(b4)
{
    float3 ShadowLightPos;
    float ShadowPassMode;
    float ShadowRadius;
    float3 pad2;
    float ShadowIntensity;
};

// Iris-out transition: when IrisActive > 0.5 the pixel shader paints
// solid black outside a circle centered on screen-space IrisCenter with
// radius IrisRadius (in back-buffer pixels). Inside the circle pixels
// pass through normally. As IrisRadius shrinks to 0 the whole screen
// goes black -- used for the F-to-enter-stage transition.
cbuffer Buffer6 : register(b6)
{
    float2 IrisCenter;
    float  IrisRadius;
    float  IrisActive;
};

struct PS_INPUT
{
    float4 posH : SV_POSITION; //ピクセルの座標
    float4 color : COLOR0; //ピクセルの色
    float2 texcoord : TEXCOORD0;
    float4 posLight : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float3 posWorld : TEXCOORD3;
};
float SampleShadowPCF(
    float3 dir,
    float myDepth,
    float dist,
    float3 normal);

float4 main(PS_INPUT ps_in) : SV_TARGET
{
    // ========== ALPHA DISCARD ==========
    // Sample texture early so we can discard transparent pixels
    float4 texCol = g_Texture.Sample(g_SamplerState, ps_in.texcoord);
    texCol *= ps_in.color;
    if (texCol.a < 0.1f)
    {
        discard;
    }

    // ========== SHADOW PASS MODE ==========
    // When rendering to shadow cubemap, output linear depth as color
    if (ShadowPassMode > 0.5f)
    {
        float3 toPixel = ps_in.posWorld - ShadowLightPos;
        float distToLight = length(toPixel);
        
        float nearPlane = 0.1f;
        float farPlane = ShadowRadius;
        float depth = saturate((distToLight - nearPlane) / (farPlane - nearPlane));
        
        return float4(depth, depth, depth, 1.0f);
    }

    // ========== IRIS CUTOUT (stage entry transition) ==========
    // Apply only to the normal pass; the shadow pass above has already
    // returned. Outside the circle -> solid black opaque. Inside ->
    // fall through to regular lighting.
    if (IrisActive > 0.5f)
    {
        float2 d = ps_in.posH.xy - IrisCenter;
        float dist = length(d);
        if (dist >= IrisRadius)
        {
            return float4(0.0f, 0.0f, 0.0f, 1.0f);
        }
    }

    // ========== NORMAL RENDERING PASS ==========
    
    // Base color already sampled above
    float4 col = texCol;

    if (!Light.Enable)
    {
        return col.rgba;
    }
    
    // Light & ambient
    // ライト色と環境光
    float lightIntensity = 2.0f; // master light intensity / ライト強度スケール
    float3 lightColor = lightIntensity * Light.Diffuse.rgb; // slightly warm light / やや暖色系
    float3 ambientColor = Light.Ambient.rgb; // very dark ambient / 非常に暗い環境光

    // ---------------- Distance attenuation (physically-inspired) ---------------
    // 距離による減衰（物理ベース風の逆二乗減衰）
    float3 toPixel = ps_in.posWorld - ShadowLightPos; // vector from light to pixel / ライトからピクセルへのベクトル
    float distToLight = length(toPixel); // distance from light / ライトからの距離

    float att = 1.0f; // attenuation factor / 減衰係数
    if (ShadowRadius > 0.0f)
    {
        // Avoid divide-by-zero / 0 除算を回避
        float d = max(distToLight, 0.1f);

        // Inverse-square style attenuation:
        // factor controls reach: larger = faster falloff (shorter range)
        // 逆二乗風減衰:
        //   factor を大きくすると減衰が速くなり、ライトの有効範囲が短くなる
        const float factor = 0.02f;
        att = 1.0f / (1.0f + factor * d * d);

        // Soft cutoff near radius so nothing pops
        // ライト半径付近でソフトにフェードアウトさせる
        float edge = saturate(1.0f - distToLight / ShadowRadius);
        //edge = pow(edge, 3.0f);
        //edge = edge * edge; // smoother edge / エッジをなめらかに
        att *= edge;
    }

    // ---------------- Spotlight cone -----------------------------------------
    // Encoded:
    //   Light.Dir.w >= 0.5  -> spotlight mode
    //   Light.Dir.xyz       -> cone direction (world space, will be normalized)
    // Pixels outside the cone get spotFactor = 0 so they only see ambient.
    float spotFactor = 1.0f;
    if (Light.Dir.w >= 0.5f)
    {
        float3 spotDir = normalize(Light.Dir.xyz);
        float3 dirToPx = normalize(toPixel); // light -> pixel
        float cosA = dot(dirToPx, spotDir);
        // Crisp cone edge so the lit/shadow boundary is clearly visible.
        // ~34 deg outer half-angle, ~28 deg inner -> narrow soft band.
        // cosOuter must match kSpotCosOuter in PlayerModeSwitchManager.cpp
        // (the CPU lit-wall transform gate).
        float cosOuter = 0.83f;
        float cosInner = 0.88f;
        spotFactor = smoothstep(cosOuter, cosInner, cosA);
    }
    att *= spotFactor;

    // ---------------- Shadow factor (PCF with cube map) -----------------------
    // シャドウファクタ（キューブマップ＋PCF）
    float shadowFactor = 1.0f;
    
    float minShadowDist = 0.5f;

    if (ShadowRadius > 0.0f &&
        ShadowIntensity > 0.0f &&
        distToLight <= ShadowRadius &&
        distToLight > minShadowDist)
    {
        float3 direction = normalize(toPixel);

        float nearPlane = 0.1f;
        float farPlane = ShadowRadius;
        float myDepth = (distToLight - nearPlane) / (farPlane - nearPlane);

        shadowFactor = SampleShadowPCF(
                        direction,
                        myDepth,
                        distToLight,
                        normalize(ps_in.normal));
        shadowFactor = lerp(1.0f, shadowFactor, saturate(ShadowIntensity));
    }

    // ========== FINAL OUTPUT ==========
    // [FIX] Decouple shadow darkening from distance attenuation.
    // Previously: finalLight = ambient + lightColor * (att * shadowFactor)
    // -> when light is far, att is tiny, so the lit/shadow contrast vanishes.
    // Now: apply distance-based lighting first, then multiply by a shadow term
    // that always darkens to a fixed fraction regardless of distance.
    float3 litColor = ambientColor + lightColor * att;
    float shadowDarken = lerp(0.25f, 1.0f, shadowFactor);
    float3 finalLight = litColor * shadowDarken;
    float3 finalRGB = col.rgb * finalLight;

    return float4(finalRGB, col.a);
}

float SampleShadowPCF(
    float3 dir,
    float myDepth,
    float dist,
    float3 normal)
{
    const int SAMPLE_COUNT = 6;

    float3 offsets[SAMPLE_COUNT] =
    {
        float3(1, 0, 0),
        float3(-1, 0, 0),
        float3(0, 1, 0),
        float3(0, -1, 0),
        float3(0, 0, 1),
        float3(0, 0, -1)
    };

    // ================= ADAPTIVE RADIUS =================
    float minRadius = 0.008f;
    float maxRadius = 0.04f;

    float dist01 = saturate(dist / ShadowRadius);
    float sampleRadius = lerp(minRadius, maxRadius, dist01 * dist01);

    // ================= ADAPTIVE BIAS =================
    float3 L = normalize(-Light.Dir.xyz);
    float NdotL = saturate(dot(normal, L));

    float bias = max(0.004f * (1.0f - NdotL), 0.001f);
    bias *= lerp(1.0f, 2.0f, dist01);

    // ================= PCF =================
    float shadow = 0.0f;

    for (int i = 0; i < SAMPLE_COUNT; i++)
    {
        float3 sampleDir =
            normalize(dir + offsets[i] * sampleRadius);

        float stored =
            g_ShadowCubemap.Sample(g_ShadowSampler, sampleDir).r;

        shadow += (myDepth > stored + bias) ? 1.0f : 0.0f;
    }

    return 1.0f - shadow / SAMPLE_COUNT;
}


/*
==============  English ================
More visible range:
  - Increase ShadowLightRadius (in C++)
  - Decrease factor (0.01 = very long, 0.1 = short)
  - Increase lightIntensity

Brighter scene:
  - Increase lightIntensity
  - Increase ambientColor

Darker shadows:
  - Decrease shadowFactor minimum (0.1f → 0.05f)
  - Decrease ambientColor

==========　Jpanese（日本語)  ============

// ===== 明るさ調整 =====
float lightIntensity = 2.0f;      // 大きく → 明るい、小さく → 暗い

// ===== 環境光（影の最低明るさ）=====
float3 ambientColor = float3(0. 05f, 0. 05f, 0.08f);  // 大きく → 影が明るい

// ===== 光の届く距離 =====
const float factor = 0.02f;       // 小さく → 遠くまで届く

// ===== 影の暗さ =====
shadowFactor = 0.1f;              // 小さく → 影が暗い（0.05f = とても暗い）

// C++側（direct3d.cpp）
float g_ShadowLightRadius = 15.0f;  // 大きく → ライトの範囲が広い
*/



