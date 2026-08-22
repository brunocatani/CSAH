TextureCubeArray<float4> VanillaEnvironment : register(t8);
Texture2D<float3> DFLightAlbedo : register(t29);
TextureCube<float3> PublishedEnvironment : register(t30);
TextureCube<float> PublishedValidity : register(t31);
TextureCube<float3> PreviousPublishedEnvironment : register(t32);
TextureCube<float> PreviousPublishedValidity : register(t33);
TextureCube<float4> PublishedPosition : register(t34);
TextureCube<float4> PreviousPublishedPosition : register(t35);
Texture2D<float> SurfaceClass : register(t47);
Texture2D<float> SceneDepth : register(t7);
SamplerState EnvironmentSampler : register(s8);
SamplerState MaterialSampler : register(s3);

cbuffer IblMaterialConstants : register(b5)
{
    float IblWeight : packoffset(c0.x);
    float ComplexMaterialWeight : packoffset(c0.y);
    float EnvironmentTransitionWeight : packoffset(c0.z);
    float PreviousEnvironmentAvailable : packoffset(c0.w);
    float3 PublishedProbeOrigin : packoffset(c1.x);
    float PublishedProbeOriginValid : packoffset(c1.w);
    float3 PreviousPublishedProbeOrigin : packoffset(c2.x);
    float PreviousPublishedProbeOriginValid : packoffset(c2.w);
};

// Every exact FO4VR DFComposite material contract owns t7 and b12. Regular
// permutations declare 51 rows in stock bytecode and are extended to 61 by
// the generator; conditional permutations already declare 77. Rows 32..39
// are the two inverse projections, rows 59/60 are the live world-space eye
// origins, rows 0..2 transform world offsets to view space, and rows 20..22
// transform view vectors back to the environment's world-space convention.
cbuffer Fo4VrSceneConstants : register(b12)
{
    float4 Scene[77];
};

cbuffer BasicWetnessSettings : register(b9)
{
    // x=enabled, y=wetness, z=diffuse darkening, w=specular multiplier.
    float4 BasicWetnessParams;
    // x=IBL roughness/LOD scale. Remaining values are reserved.
    float4 BasicWetnessMaterialParams;
};

struct PixelInput
{
    float4 DirectionAndArray : TEXCOORD0;
    float Lod : TEXCOORD1;
    float EncodedMaterialTag : TEXCOORD2;
    float2 ScreenUv : TEXCOORD3;
};

bool ReconstructReceiverWorldPosition(
    float2 packedUv,
    out float3 receiverWorldPosition)
{
    receiverWorldPosition = 0.0f;
    const float depth = SceneDepth.SampleLevel(
        MaterialSampler,
        packedUv,
        0.0f);
    if (!(depth == depth && depth >= 0.0f && depth <= 1.0f))
    {
        return false;
    }

    const uint eye = packedUv.x >= 0.5f ? 1u : 0u;
    const float localX = frac(packedUv.x * 2.0f);
    const float mappedDepth = depth <= 0.01f ?
        depth * 100.0f : depth * 1.01f - 0.01f;
    const float4 clipPosition = float4(
        localX * 2.0f - 1.0f,
        1.0f - packedUv.y * 2.0f,
        mappedDepth,
        1.0f);
    const uint inverseProjectionBase = 32u + eye * 4u;
    float4 homogeneousView;
    homogeneousView.x = dot(
        Scene[inverseProjectionBase], clipPosition);
    homogeneousView.y = dot(
        Scene[inverseProjectionBase + 1u], clipPosition);
    homogeneousView.z = dot(
        Scene[inverseProjectionBase + 2u], clipPosition);
    homogeneousView.w = dot(
        Scene[inverseProjectionBase + 3u], clipPosition);
    if (!(abs(homogeneousView.w) > 1.0e-5f))
    {
        return false;
    }

    const float3 leftOrigin = Scene[59].xyz;
    const float3 rightOrigin = Scene[60].xyz;
    const float3 midpointOrigin = (leftOrigin + rightOrigin) * 0.5f;
    const float3 eyeOrigin = eye == 0u ? leftOrigin : rightOrigin;
    const float3 eyeToMidpointWorld = eyeOrigin - midpointOrigin;
    const float3 eyeToMidpointView = float3(
        dot(Scene[0].xyz, eyeToMidpointWorld),
        dot(Scene[1].xyz, eyeToMidpointWorld),
        dot(Scene[2].xyz, eyeToMidpointWorld));
    const float3 midpointRelativeView =
        homogeneousView.xyz / homogeneousView.w + eyeToMidpointView;
    const float3 midpointRelativeWorld = float3(
        dot(Scene[20].xyz, midpointRelativeView),
        dot(Scene[21].xyz, midpointRelativeView),
        dot(Scene[22].xyz, midpointRelativeView));
    receiverWorldPosition = midpointOrigin + midpointRelativeWorld;
    return all(receiverWorldPosition == receiverWorldPosition) &&
        all(abs(receiverWorldPosition) < 8000000.0f);
}

float3 CorrectProbeDirection(
    TextureCube<float4> positionTexture,
    float3 direction,
    float3 receiverWorldPosition,
    float3 probeOrigin,
    float roughnessLod,
    float originValid)
{
    if (!(originValid > 0.5f))
    {
        return direction;
    }

    const float4 hit = positionTexture.SampleLevel(
        EnvironmentSampler,
        direction,
        0.0f);
    const float3 probeToHit = hit.xyz - probeOrigin;
    const float radius = length(probeToHit);
    const float3 probeToReceiver = receiverWorldPosition - probeOrigin;
    const float receiverDistanceSquared = dot(
        probeToReceiver,
        probeToReceiver);
    const float projection = dot(probeToReceiver, direction);
    const float discriminant = projection * projection -
        (receiverDistanceSquared - radius * radius);
    if (!(hit.w > 1.0e-4f && radius > 16.5f &&
          receiverDistanceSquared < radius * radius &&
          discriminant > 1.0e-4f))
    {
        return direction;
    }

    const float travel = -projection + sqrt(discriminant);
    const float3 corrected = normalize(
        probeToReceiver + direction * travel);
    const float correctionStrength = hit.w *
        (1.0f - saturate(roughnessLod / 7.0f));
    return normalize(lerp(direction, corrected, correctionStrength));
}

float4 PSMain(PixelInput input) : SV_Target0
{
    const float surfaceClass = SurfaceClass.SampleLevel(
        MaterialSampler,
        input.ScreenUv,
        0.0);
    const float surfaceCode = surfaceClass * 255.0;
    const float ordinaryOrGrass = 1.0 - step(1.5, surfaceCode);
    const float terrain = 1.0 - step(0.5, abs(surfaceCode - 4.0));
    const float wetness = saturate(
        BasicWetnessParams.x * BasicWetnessParams.y *
        saturate(ordinaryOrGrass + terrain));
    const float wetLod = input.Lod * lerp(
        1.0,
        saturate(BasicWetnessMaterialParams.x),
        wetness);
    float4 vanilla = VanillaEnvironment.SampleLevel(
        EnvironmentSampler,
        input.DirectionAndArray,
        wetLod);
    [branch]
    if (IblWeight > 1.0 / 255.0)
    {
        // FO4VR's vanilla cube coordinate is an incident lookup direction.
        // The view-derived environment stores outgoing world directions, so
        // dynamic specular consumption must use the opposite direction. This
        // sign is deliberately local to specular materials; Diffuse IBL keeps
        // the provider's proven world-direction convention.
        const float3 dynamicDirection = -input.DirectionAndArray.xyz;
        float3 receiverWorldPosition;
        const bool receiverPositionValid =
            ReconstructReceiverWorldPosition(
                input.ScreenUv,
                receiverWorldPosition);
        const float3 publishedDirection = receiverPositionValid ?
            CorrectProbeDirection(
                PublishedPosition,
                dynamicDirection,
                receiverWorldPosition,
                PublishedProbeOrigin,
                input.Lod,
                PublishedProbeOriginValid) : dynamicDirection;
        float3 published = PublishedEnvironment.SampleLevel(
            EnvironmentSampler,
            publishedDirection,
            input.Lod);
        float validity = PublishedValidity.SampleLevel(
            EnvironmentSampler,
            publishedDirection,
            input.Lod);
        [branch]
        if (PreviousEnvironmentAvailable > 0.5 &&
            EnvironmentTransitionWeight < 1.0)
        {
            const float3 previousDirection = receiverPositionValid ?
                CorrectProbeDirection(
                    PreviousPublishedPosition,
                    dynamicDirection,
                    receiverWorldPosition,
                    PreviousPublishedProbeOrigin,
                    input.Lod,
                    PreviousPublishedProbeOriginValid) : dynamicDirection;
            const float3 previousPublished =
                PreviousPublishedEnvironment.SampleLevel(
                    EnvironmentSampler,
                    previousDirection,
                    input.Lod);
            const float previousValidity =
                PreviousPublishedValidity.SampleLevel(
                    EnvironmentSampler,
                    previousDirection,
                    input.Lod);
            published = lerp(
                previousPublished,
                published,
                saturate(EnvironmentTransitionWeight));
            validity = lerp(
                previousValidity,
                validity,
                saturate(EnvironmentTransitionWeight));
        }
        float weight = saturate(validity * IblWeight);
        vanilla.xyz = lerp(vanilla.xyz, published, weight);
    }

    float ordinaryMaterialTag = step(0.5, input.EncodedMaterialTag);
    float metalness = saturate(
        (1.0 - input.EncodedMaterialTag) * 2.0) *
        ComplexMaterialWeight * ordinaryMaterialTag;
    [branch]
    if (metalness > 1.0 / 255.0)
    {
        float3 retainedDiffuse = DFLightAlbedo.SampleLevel(
            MaterialSampler,
            input.ScreenUv,
            0.0);
        float3 baseColour =
            retainedDiffuse / max(1.0 - metalness, 1.0 / 255.0);
        vanilla.xyz *= lerp(1.0.xxx, baseColour, metalness);
    }
    vanilla.xyz *= lerp(
        1.0,
        max(BasicWetnessParams.w, 0.0),
        wetness);
    return vanilla;
}
