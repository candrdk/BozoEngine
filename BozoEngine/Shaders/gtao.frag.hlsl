#pragma pack_matrix(column_major)

// TODO: Just use push constants instead here
[[vk::binding(0, 0)]]
cbuffer GTAO_UBO
{
    float4x4 proj;
    float4x4 invProj;
    float4 params; // DirectionSampleCount SliceCount WorldRadius Power
    float2 pixelSize; // float2(1/width, 1/height)
};

[[vk::combinedImageSampler]] [[vk::binding(0, 1)]] Texture2D<float4> texDepth;
[[vk::combinedImageSampler]] [[vk::binding(0, 1)]] SamplerState      texDepthState;
[[vk::combinedImageSampler]] [[vk::binding(1, 1)]] Texture2D<float4> texNormal;
[[vk::combinedImageSampler]] [[vk::binding(1, 1)]] SamplerState      texNormalState;

static const float PI      = 3.14159265359f;
static const float HALF_PI = 1.57079632679f;

float FastSqrt(float x)
{
    return (float) (asfloat(0x1fbd1df5 + (asint(x) >> 1)));
}

float FastAcos(float x)
{
    float ax = abs(x);
    float res = -0.156583 * ax + HALF_PI;
    res *= FastSqrt(1.0 - ax);
    return (x >= 0) ? res : PI - res;
}

float3 ViewspacePosFromDepthBuffer(const float2 uv)
{
    float z = texDepth.Sample(texDepthState, uv).r;
    float4 clip = float4(uv * 2.0 - 1.0, z, 1.0);
    float4 view = mul(invProj, clip);
    return view.xyz / view.w;
}

float4 main([[vk::location(0)]] float2 inUV : TEXCOORD0, float4 svPos : SV_Position) : SV_Target0
{
    float2 h = 0.0;
    
    const float DirectionSampleCount = params.x;
    const float SliceCount = params.y;
    const float WorldRadius = params.z;
    const float Power = params.w;
    
    const float3 cPosV = ViewspacePosFromDepthBuffer(inUV);
    const float3 viewV = (float3)normalize(-cPosV);
    const float3 normalV = normalize(texNormal.Sample(texNormalState, inUV).xyz * 2.0 - 1.0);
    
    // proj[0][0] = 1/(aspect*tan(fovY/2)), proj[1][1] = 1/tan(fovY/2).
    // Dividing 2 by each gives the NDC-to-viewspace scale per axis.
    const float2 NDCToViewMul = float2(2.0 / proj[0][0], 2.0 / proj[1][1]);
    
    // How much viewspace distance one pixel covers at the center pixel's depth.
    const float2 pixelSizeView = NDCToViewMul * pixelSize * abs(cPosV.z);
    
    // World-space radius expressed as a pixel count on screen.
    const float screenspaceRadius = WorldRadius / pixelSizeView.x;
    
    // Interleaved gradient noise — spatially varying per pixel so neighboring
    // pixels sample different slice directions, eliminating the grid artifacts.
    // TODO: Check if this actually works properly?
    float noise = frac(52.9829189f * frac(0.06711056f * svPos.x + 0.00583715f * svPos.y));
    
    float visibility = 0.0;
    for (float slice = 0.0; slice < SliceCount; slice++)
    {
        float phi = (PI / SliceCount) * (slice + noise);
        float2 omega = float2(cos(phi), sin(phi));
        
        float3 directionV = float3(omega.x, omega.y, 0.0);
        float3 orthoDirectionV = directionV - viewV * dot(directionV, viewV);
        float3 axisV = normalize(cross(directionV, viewV));
        float3 projNormalV = normalV - axisV * dot(normalV, axisV);
        
        float sgnN = sign(dot(orthoDirectionV, projNormalV));
        float cosN = saturate(dot(projNormalV, viewV) / length(projNormalV));
        float n = sgnN * FastAcos(cosN);
        
        [unroll]
        for (int side = 0; side <= 1; side++)
        {
            float cHorizonCos = -1.0;
            
            for (float sample = 0.0; sample < DirectionSampleCount; sample++)
            {
                float s = (sample + 1.0) / DirectionSampleCount;
                float2 sTexCoord = inUV + (-1 + 2 * side) * s * screenspaceRadius * pixelSize * float2(omega.x, -omega.y);
                // float s = sample / DirectionSampleCount;
                // float2 sTexCoord = inUV + (-1 + 2 * side) * s * WorldRadius * float2(omega.x, -omega.y);
                float3 sPosV = ViewspacePosFromDepthBuffer(sTexCoord);
                //float3 sHorizonV = normalize(sPosV - cPosV);
                //cHorizonCos = max(cHorizonCos, dot(sHorizonV, viewV));
                
                float3 sDelta = sPosV - cPosV;
                float3 sHorizonV = normalize(sDelta);
                float weight = saturate(1.0 - length(sDelta) / WorldRadius);
                cHorizonCos = max(cHorizonCos, lerp(-1.0, dot(sHorizonV, viewV), weight * weight));
            }
        
            h[side] = n + clamp((-1 + 2 * side) * FastAcos(cHorizonCos) - n, -HALF_PI, HALF_PI);
            visibility += length(projNormalV) * (cosN + 2 * h[side] * sin(n) - cos(2 * h[side] - n)) / 4;
        }
    }

    visibility /= SliceCount;
    visibility = pow(visibility, Power);
    visibility = saturate(visibility);
    
    return float4(visibility, visibility, visibility, 1.0);
}