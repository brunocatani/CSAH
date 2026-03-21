// Extended Materials — Parallax Occlusion Mapping
// Ported from Skyrim Community Shaders:
//   features/Extended Materials/Shaders/ExtendedMaterials/ExtendedMaterials.hlsli
//
// Key references:
//   https://github.com/tgjones/slimshader-cpp/blob/master/src/Shaders/Sdk/Direct3D11/DetailTessellation11/POM.hlsl
//   https://github.com/alandtse/SSEShaderTools/blob/main/shaders_vr/ParallaxEffect.h
//   https://advances.realtimerendering.com/s2006/Tatarchuk-POM.pdf
//
// FO4VR simplifications vs Skyrim CS source:
//   - No LANDSCAPE / terrain code paths (deferred to sub-project 2)
//   - No TRUE_PBR code paths
//   - No TERRAIN_VARIATION / StochasticOffsets
//   - CS_IsVR  is a runtime uint from SharedDataCB (b3), NOT a compile-time #define
//   - CS_IsInterior is a runtime uint from SharedDataCB (b3), NOT a compile-time #define
//   - EM_MaxSteps is declared in the ExtendedMaterials CB in Lighting.hlsl (available at include site)
//
// Prerequisites (must be included before this file in Lighting.hlsl):
//   #include "Common/SharedData.hlsli"   — provides CS_IsVR, CS_IsInterior
//   ExtendedMaterials CB declaring EM_MaxSteps

#ifndef EXTENDED_MATERIALS_HLSLI
#define EXTENDED_MATERIALS_HLSLI

// ---------------------------------------------------------------------------
// Displacement parameter block — one per material (not per terrain tile)
// ---------------------------------------------------------------------------
struct DisplacementParams
{
    float DisplacementScale;   // scales the raw [0,1] height into a signed offset
    float DisplacementOffset;  // constant bias added after scaling
    float HeightScale;         // physical height in world units (controls POM depth)
    float FlattenAmount;       // increases the effective view-angle denominator → less warp at grazing
};

namespace ExtendedMaterials
{

// ---------------------------------------------------------------------------
// AdjustDisplacementNormalized
// Maps a raw [0,1] height sample back to a normalised [0,1] range using the
// per-material scale/offset so artists can tune the zero-plane and contrast.
// ---------------------------------------------------------------------------
float AdjustDisplacementNormalized(float displacement, DisplacementParams params)
{
    return (displacement - 0.5) * params.DisplacementScale + 0.5 + params.DisplacementOffset;
}

float4 AdjustDisplacementNormalized(float4 displacement, DisplacementParams params)
{
    return float4(
        AdjustDisplacementNormalized(displacement.x, params),
        AdjustDisplacementNormalized(displacement.y, params),
        AdjustDisplacementNormalized(displacement.z, params),
        AdjustDisplacementNormalized(displacement.w, params));
}

// ---------------------------------------------------------------------------
// GetMipLevel
// Computes an explicit mip level for SampleLevel() calls inside the POM loop,
// preventing the hardware from computing derivatives over a non-uniform loop.
//
// VR adjustment: textureDims and mipLevel are both halved for VR to compensate
// for the doubled render target width (side-by-side stereo). Uses CS_IsVR as a
// runtime branch — NOT a compile-time #define — so a single shader binary works
// for both flat and VR.
//
// screenNoise: a per-pixel blue-noise value in [0,1) used to stochastically
// dither between adjacent integer mip levels, reducing visible mip banding.
// ---------------------------------------------------------------------------
float GetMipLevel(float2 coords, Texture2D<float4> tex, float screenNoise)
{
    // Query texture dimensions once (free on D3D11 SM5 hardware)
    float2 textureDims;
    tex.GetDimensions(textureDims.x, textureDims.y);

    // FO4 parallax textures are not the dedicated displacement-only textures that
    // Skyrim TRUE_PBR uses, so we apply the same ÷2 correction Skyrim CS uses for
    // its non-PARALLAX, non-TRUE_PBR path (height lives in alpha of a larger atlas).
    textureDims /= 2.0;

    // VR: render target is twice as wide (side-by-side), so UV derivatives are
    // half what they should be → divide dims again to recover correct mip bias.
    [branch] if (CS_IsVR)
        textureDims /= 2.0;

    float2 texCoordsPerSize = coords * textureDims;

    float2 dxSize = ddx(texCoordsPerSize);
    float2 dySize = ddy(texCoordsPerSize);

    // Use the *minimum* rate-of-change (Skyrim CS choice) — keeps mip conservative,
    // avoids over-blurring on the slower-changing axis.
    float minTexCoordDelta = min(dot(dxSize, dxSize), dot(dySize, dySize));

    // log2 of sqrt == 0.5 * log2
    float mipLevel = max(0.5 * log2(minTexCoordDelta), 0.0);

    // Non-dedicated displacement path: bump by 1 (matches Skyrim CS non-PARALLAX path)
    mipLevel += 1.0;

    // VR: an additional +1 to reduce over-sharpening / shimmering in stereo.
    [branch] if (CS_IsVR)
        mipLevel += 1.0;

    // Stochastic mip selection: dither between floor(mip) and floor(mip)+1
    // using per-pixel noise so the transition band is invisible at full res.
    mipLevel = floor(mipLevel) + (screenNoise < frac(mipLevel) ? 1.0 : 0.0);

    return mipLevel;
}

// ---------------------------------------------------------------------------
// GetParallaxCoords
// Main POM ray-march.  Marches a view ray through the height field, then
// performs a 4-step contact-refinement pass to tighten the intersection.
//
// Parameters:
//   distance    — fragment distance from camera (world units), used to fade
//                 POM out at EM_FadeDistance
//   coords      — original (non-displaced) UV coordinates
//   mipLevel    — result of GetMipLevel(); used for all SampleLevel calls
//   viewDir     — view direction in world space (camera→fragment, unnormalised
//                 is fine; we normalise into tangent space below)
//   tbn         — tangent-to-world matrix (rows = T, B, N in world space)
//   noise       — per-pixel screen noise in [0,1) for stochastic sampling
//   tex         — displacement/height texture (height in specified channel)
//   texSampler  — sampler for tex
//   channel     — colour channel index: 0=R, 1=G, 2=B, 3=A
//   params      — per-material displacement parameters
//   pixelOffset — [out] linear parallax depth for contact-shadow use
//
// Returns the parallax-displaced UV coordinate.
//
// Step count logic (runtime):
//   Interior  → maxSteps = 8   (tighter scenes, less aliasing headroom needed)
//   Exterior  → maxSteps = 16
//   VR        → halve whichever was chosen (GPU budget is tighter per-eye)
//   EM_MaxSteps caps the final count (artist/performance override from CB)
// ---------------------------------------------------------------------------
float2 GetParallaxCoords(
    float              distance,
    float2             coords,
    float              mipLevel,
    float3             viewDir,
    float3x3           tbn,
    float              noise,
    Texture2D<float4>  tex,
    SamplerState       texSampler,
    uint               channel,
    DisplacementParams params,
    out float          pixelOffset)
{
    // Transform view direction into tangent space and project onto the
    // height-field plane.  The denominator bias (0.7z + 0.3 + FlattenAmount)
    // prevents runaway offsets at near-grazing angles.
    float3 viewDirTS = normalize(mul(tbn, viewDir));
    viewDirTS.xy /= viewDirTS.z * 0.7 + 0.3 + params.FlattenAmount;

    // Distance fade: beyond 2048 units POM is indistinguishable from flat.
    float nearBlendToFar = saturate(distance / 2048.0);

    float scale     = params.HeightScale;
    float maxHeight = 0.1 * scale;
    float minHeight = maxHeight * 0.5;

    [branch] if (nearBlendToFar < 1.0)
    {
        // --- Step count ---
        // Base: exterior=16, interior=8; halve for VR; clamp by EM_MaxSteps.
        float maxSteps = CS_IsInterior ? 8.0 : 16.0;

        [branch] if (CS_IsVR)
            maxSteps = max(maxSteps * 0.5, 1.0);

        // User override: EM_MaxSteps > 0 caps the maximum
        if (EM_MaxSteps > 0u)
            maxSteps = min(maxSteps, (float)EM_MaxSteps);

        uint numSteps = uint((maxSteps * (1.0 - nearBlendToFar)) + 0.5);
        numSteps = clamp(numSteps, 1u, (uint)max(6.0, scale * maxSteps));

        float stepSize = rcp((float)numSteps);

        float2 offsetPerStep = viewDirTS.xy * float2(maxHeight, maxHeight) * stepSize.xx;
        float2 prevOffset    = viewDirTS.xy * float2(minHeight, minHeight) + coords.xy;

        float prevBound  = 1.0;
        float prevHeight = 1.0;

        float2 pt1 = 0;
        float2 pt2 = 0;

        uint numStepsTemp      = numSteps;
        bool contactRefinement = false;

        // --- Ray-march loop (4 samples per iteration = batched for GPU) ---
        [loop] while (numSteps > 0)
        {
            // Compute 4 candidate UV offsets in one batch
            float4 currentOffset[2];
            currentOffset[0] = prevOffset.xyxy - float4(1, 1, 2, 2) * offsetPerStep.xyxy;
            currentOffset[1] = prevOffset.xyxy - float4(3, 3, 4, 4) * offsetPerStep.xyxy;

            // Corresponding depth bounds
            float4 currentBound = prevBound.xxxx - float4(1, 2, 3, 4) * stepSize;

            // Sample the height field at all 4 positions
            float4 currHeight;
            currHeight.x = tex.SampleLevel(texSampler, currentOffset[0].xy, mipLevel)[channel];
            currHeight.y = tex.SampleLevel(texSampler, currentOffset[0].zw, mipLevel)[channel];
            currHeight.z = tex.SampleLevel(texSampler, currentOffset[1].xy, mipLevel)[channel];
            currHeight.w = tex.SampleLevel(texSampler, currentOffset[1].zw, mipLevel)[channel];

            // Apply per-material scale/offset so height field is in [0,1] range
            currHeight = AdjustDisplacementNormalized(currHeight, params);

            // Test which samples are above (or at) the marching plane
            bool4 testResult = currHeight >= currentBound;

            [branch] if (any(testResult))
            {
                // Select the *shallowest* intersection (priority order: x > y > z > w
                // because x is closest to the camera along the ray)
                float2 outOffset = 0;

                [flatten] if (testResult.w)
                {
                    outOffset = currentOffset[1].xy;
                    pt1 = float2(currentBound.w, currHeight.w);
                    pt2 = float2(currentBound.z, currHeight.z);
                }
                [flatten] if (testResult.z)
                {
                    outOffset = currentOffset[0].zw;
                    pt1 = float2(currentBound.z, currHeight.z);
                    pt2 = float2(currentBound.y, currHeight.y);
                }
                [flatten] if (testResult.y)
                {
                    outOffset = currentOffset[0].xy;
                    pt1 = float2(currentBound.y, currHeight.y);
                    pt2 = float2(currentBound.x, currHeight.x);
                }
                [flatten] if (testResult.x)
                {
                    outOffset = prevOffset;
                    pt1 = float2(currentBound.x, currHeight.x);
                    pt2 = float2(prevBound, prevHeight);
                }

                if (contactRefinement)
                {
                    // Second pass found tighter bracket → done
                    break;
                }
                else
                {
                    // First hit: restart the loop over a finer interval
                    // between the step before this hit (pt2.x) and this step.
                    contactRefinement = true;
                    prevOffset = outOffset;
                    prevBound  = pt2.x;
                    numSteps   = numStepsTemp;
                    stepSize  /= (float)numSteps;
                    offsetPerStep /= (float)numSteps;
                    continue;
                }
            }

            // No intersection yet — advance to the next batch of 4
            prevOffset = currentOffset[1].zw;
            prevBound  = currentBound.w;
            prevHeight = currHeight.w;
            numSteps  -= 4;
        }

        // --- Secant interpolation between the two bracketing samples ---
        float delta2 = pt2.x - pt2.y;
        float delta1 = pt1.x - pt1.y;
        float denominator = delta2 - delta1;

        float parallaxAmount = 0.0;
        [flatten] if (denominator == 0.0)
        {
            parallaxAmount = 0.0;
        }
        else
        {
            parallaxAmount = (pt1.x * delta2 - pt2.x * delta1) / denominator;
        }

        // Fade out the distance contribution (squared for smoother roll-off)
        nearBlendToFar *= nearBlendToFar;

        float offset = (1.0 - parallaxAmount) * -maxHeight + minHeight;
        pixelOffset = lerp(parallaxAmount * scale, 0.0, nearBlendToFar);
        return lerp(viewDirTS.xy * offset + coords.xy, coords, nearBlendToFar);
    }

    // Beyond fade distance: return original UVs, no displacement
    pixelOffset = 0.0;
    return coords;
}

// ---------------------------------------------------------------------------
// GetParallaxSoftShadowMultiplier
// Cheap self-shadow: march 4 samples toward the light source and test whether
// the height field blocks them.
//
// Parameters:
//   coords      — already-displaced UV coordinate (output of GetParallaxCoords)
//   mipLevel    — same mip level used in GetParallaxCoords
//   L           — light direction in tangent space
//   sh0         — height at the displaced coordinate (reference height)
//   tex/texSampler/channel — same texture resources as GetParallaxCoords
//   quality     — shadow quality in [0,1]; 0 = disabled, steps enabled at
//                 0.25 / 0.5 / 0.75 thresholds (1/2/3/4 samples)
//   noise       — per-pixel screen noise for stochastic sample spacing
//   params      — per-material displacement parameters
//
// Returns a [0,1] shadow multiplier (1 = fully lit, 0 = fully shadowed).
// ---------------------------------------------------------------------------
float GetParallaxSoftShadowMultiplier(
    float2             coords,
    float              mipLevel,
    float3             L,
    float              sh0,
    Texture2D<float4>  tex,
    SamplerState       texSampler,
    uint               channel,
    float              quality,
    float              noise,
    DisplacementParams params)
{
    [branch] if (quality > 0.0)
    {
        // Ray direction projected onto the UV plane, scaled by height influence
        float2 rayDir = L.xy * 0.1 * params.HeightScale;

        // Stochastic spacing: reciprocal of (step + noise) to jitter sample positions
        float4 multipliers = rcp(float4(1, 2, 3, 4) + noise);

        float4 sh = 0;

        // Always take at least 1 sample
        sh.x = AdjustDisplacementNormalized(
            tex.SampleLevel(texSampler, coords + rayDir * multipliers.x, mipLevel)[channel], params);

        if (quality > 0.25)
            sh.y = AdjustDisplacementNormalized(
                tex.SampleLevel(texSampler, coords + rayDir * multipliers.y, mipLevel)[channel], params);

        if (quality > 0.5)
            sh.z = AdjustDisplacementNormalized(
                tex.SampleLevel(texSampler, coords + rayDir * multipliers.z, mipLevel)[channel], params);

        if (quality > 0.75)
            sh.w = AdjustDisplacementNormalized(
                tex.SampleLevel(texSampler, coords + rayDir * multipliers.w, mipLevel)[channel], params);

        // Penumbra: any sample above the reference height blocks light.
        // The pow(…, 2.0) gives a smooth quadratic fall-off.
        return pow(1.0 - saturate(dot(max(0.0, sh - sh0), 1.0)) * quality, 2.0);
    }

    return 1.0;
}

}  // namespace ExtendedMaterials

#endif  // EXTENDED_MATERIALS_HLSLI
