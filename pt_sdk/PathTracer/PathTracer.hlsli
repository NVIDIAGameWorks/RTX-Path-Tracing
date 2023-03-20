/*
* Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef __PATH_TRACER_HLSLI__ // using instead of "#pragma once" due to https://github.com/microsoft/DirectXShaderCompiler/issues/3943
#define __PATH_TRACER_HLSLI__

#include "PathTracerTypes.hlsli"

#include "Scene/ShadingData.hlsli"
#include "Scene/Lights/EnvMapSampler.hlsli"
// #include "Rendering/Materials/InteriorListHelpers.hlsli" not yet ported

#include "StablePlanes.hlsli"

// figure out where to move this so it's not in th emain path tracer code
#if STABLE_PLANES_MODE==STABLE_PLANES_BUILD_PASS && ENABLE_DEBUG_DELTA_TREE_VIZUALISATION
void        DeltaTreeVizHandleMiss(inout PathState path, const float3 rayOrigin, const float3 rayDir, const float rayTCurrent, const PathTracerParams params, const AUXContext auxContext);
void        DeltaTreeVizHandleHit(inout PathState path, const float3 rayOrigin, const float3 rayDir, const float rayTCurrent, const SurfaceData bridgedData, bool rejectedFalseHit, bool hasFinishedSurfaceBounces, float volumeAbsorption, const PathTracerParams params, const AUXContext auxContext);
#endif

// currently only static functions but could hold path tracer setup
struct PathTracer
{
    // compile-time constants (see Falcor\Source\RenderPasses\PathTracer\PathTracer.cpp)
    static const bool           kDisableCaustics            = false;    // consider setting to 'true' for real-time
    static const bool           kUseNEE                     = true;
    static const bool           kUseMIS                     = true;
    static const bool           kUseEnvLight                = true;
    static const bool           kUseEmissiveLights          = true;
    static const bool           kUseAnalyticLights          = true;
    static const bool           kUseRussianRoulette         = true;
    static const bool           kUseBSDFSampling            = true;
    static const bool           kUseLightsInDielectricVolumes = true;       // TODO: remove this switch
    static const uint           kMaxDiffuseBounces          = 4;

    static const MISHeuristic   kMISHeuristic               = MISHeuristic::Balance;
    //static const float          kMISPowerExponent           = 1.5f;

    static PathState        emptyPathInitialize(uint2 pixelPos, uint sampleIndex, float pixelConeSpreadAngle)
    {
        PathState path;
        path.id                     = PathIDFromPixel(pixelPos);
        path.flagsAndVertexIndex    = 0;
        path.sceneLength            = 0;
        path.fireflyFilterK         = 1.0;
        path.packedCounters         = 0;

        // Original (from Falcor) is a bit more complex, allowing for multiple samples per pixel; that is not implemented here for simplicity
#ifndef USING_STATELESS_SAMPLE_GENERATOR
        path.sg                     = SampleGenerator::make(pixelPos, sampleIndex);
#else
        path.sg                     = SampleGenerator::make(pixelPos, 0, sampleIndex);
#endif

        for( uint i = 0; i < INTERIOR_LIST_SLOT_COUNT; i++ )
            path.interiorList.slots[i] = 0;

        path.origin                 = float3(0,0,0);
        path.dir                    = float3(0,0,0);
        path.pdf                    = 0;
        // path.normal                 = float3(0,0,0);

        path.thp                    = float3(1,1,1);
#if STABLE_PLANES_MODE!=STABLE_PLANES_NOISY_PASS
        path.L                      = float3(0,0,0);
#else
        path.denoiserSampleHitTFromPlane = 0.0;
        path.denoiserDiffRadianceHitDist = float4(0, 0, 0, 0);
        path.denoiserSpecRadianceHitDist = float4(0, 0, 0, 0);
        path.secondaryL             = 0.0;
#endif

        path.setHitPacked( HitInfo::make().getData() );
        path.setActive();
        path.setDeltaOnlyPath(true);

        path.rayCone                = RayCone::make(0, pixelConeSpreadAngle);

#if STABLE_PLANES_MODE==STABLE_PLANES_BUILD_PASS
        path.imageXform             = float3x3( 1.f, 0.f, 0.f,
                                                0.f, 1.f, 0.f,
                                                0.f, 0.f, 1.f);
        path.setFlag(PathFlags::stablePlaneOnDominantBranch, true); // stable plane 0 starts being dominant but this can change; in the _NOISY_PASS this is predetermined and can't change
#endif
        path.setStablePlaneIndex(0);
        path.stableBranchID         = 1; // camera has 1; makes IDs unique

        return path;
    }

    static void             pathSetupPrimaryRay(inout PathState path, const Ray ray)
    {
        path.origin = ray.origin;
        path.dir    = ray.dir;
    }

    /** Check if the path has finished all surface bounces and needs to be terminated.
        Note: This is expected to be called after generateScatterRay(), which increments the bounce counters.
        \param[in] path Path state.
        \return Returns true if path has processed all bounces.
    */
    static bool hasFinishedSurfaceBounces(const PathState path)
    {
        if (Bridge::getMaxBounceLimit()<path.getVertexIndex())
            return true;
        const uint diffuseBounces = path.getCounter(PackedCounters::DiffuseBounces);
        return diffuseBounces > kMaxDiffuseBounces;
    }

    /** Compute index of refraction for medium on the outside of the current dielectric volume.
        \param[in] interiorList Interior list.
        \param[in] materialID Material ID of intersected material.
        \param[in] entering True if material is entered, false if material is left.
        \return Index of refraction.
    */
    static float computeOutsideIoR(const InteriorList interiorList, const uint materialID, const bool entering)
    {
        // The top element holds the material ID of currently highest priority material.
        // This is the material on the outside when entering a new medium.
        uint outsideMaterialID = interiorList.getTopMaterialID();

        if (!entering)
        {
            // If exiting the currently highest priority material, look at the next element
            // on the stack to find out what is on the outside.
            if (outsideMaterialID == materialID) outsideMaterialID = interiorList.getNextMaterialID();
        }

        // If no material, assume the default IoR for vacuum.
        if (outsideMaterialID == InteriorList::kNoMaterial) return 1.f;

        // this is implemented in \Falcor\Scene\Material\MaterialSystem.hlsli 
        // and probably need to get ported to Bridge::XXX - yet to decide
        return Bridge::loadIoR(outsideMaterialID);
    }

    /** Handle hits on dielectrics.
    \return True if this is an valid intersection, false if it is rejected.
    */
    static bool handleNestedDielectrics(inout SurfaceData surfaceData, inout PathState path, const AUXContext auxContext)
    {
        // Check for false intersections.
        uint nestedPriority = surfaceData.sd.mtl.getNestedPriority();
        if (!path.interiorList.isTrueIntersection(nestedPriority))
        {
            // If it is a false intersection, we reject the hit and continue the path
            // on the other side of the interface.
            // If the offset position is not quite large enough, due to self-intersections
            // it is possible we repeatedly hit the same surface and try to reject it.
            // This has happened in a few occasions with large transmissive triangles.
            // As a workaround, count number of rejected hits and terminate the path if too many.
            if (path.getCounter(PackedCounters::RejectedHits) < kMaxRejectedHits)
            {
#if 0 && ENABLE_DEBUG_VIZUALISATION // do debugging for rejected pixels too!
                if (auxContext.debug.IsDebugPixel())
                {
                    // IoR debugging - .x - "outside", .y - "interior", .z - frontFacing, .w - "eta" (eta is isFrontFace?outsideIoR/insideIoR:insideIoR/outsideIoR)
                    // auxContext.debug.Print(path.getVertexIndex()-1, float4(-42,-42,-42,-42) ); //float4(surfaceData.sd.IoR, surfaceData.interiorIoR, surfaceData.sd.frontFacing, surfaceData.bsdf.data.eta) );
                    // path segment
                    auxContext.debug.DrawLine(path.origin, surfaceData.sd.posW, 0.4.xxx, 0.1.xxx);
                    auxContext.debug.DrawLine(surfaceData.sd.posW, surfaceData.sd.posW + surfaceData.sd.T * auxContext.debug.LineScale()*0.2, float3(0.1, 0, 0), float3(0.5, 0, 0));
                    auxContext.debug.DrawLine(surfaceData.sd.posW, surfaceData.sd.posW + surfaceData.sd.B * auxContext.debug.LineScale()*0.2, float3(0, 0.1, 0), float3(0, 0.5, 0));
                    auxContext.debug.DrawLine(surfaceData.sd.posW, surfaceData.sd.posW + surfaceData.sd.N * auxContext.debug.LineScale()*0.2, float3(0, 0, 0.1), float3(0, 0, 0.5));
                }
#endif

                path.incrementCounter(PackedCounters::RejectedHits);
                path.interiorList.handleIntersection(surfaceData.sd.materialID, nestedPriority, surfaceData.sd.frontFacing);
                path.origin = surfaceData.sd.computeNewRayOrigin(false);
                path.decrementVertexIndex();
            }
            else
            {
                path.terminate();
            }
            return false;
        }

        // Compute index of refraction for medium on the outside.
        Bridge::updateOutsideIoR( surfaceData, computeOutsideIoR(path.interiorList, surfaceData.sd.materialID, surfaceData.sd.frontFacing) );

        return true;
    }

    /** Update the path throughouput.
        \param[in,out] path Path state.
        \param[in] weight Vertex throughput.
    */
    static void updatePathThroughput(inout PathState path, const float3 weight)
    {
        path.thp *= weight;
    }

    /** Apply russian roulette to terminate paths early.
        \param[in,out] path Path.
        \param[in] u Uniform random number in [0,1).
        \return Returns true if path was terminated.
    */
    static bool terminatePathByRussianRoulette(inout PathState path, float u)
    {
        const float rrVal = luminance(path.thp);
        float prob = max(0.f, 1.f - rrVal);

        prob = saturate( prob - 0.2 ); prob = prob*prob*prob*prob; // make it a very mild version of Russian Roulette (different from Falcor!)

        if (u < prob)
        {
            path.terminate();
            return true;
        }
        updatePathThroughput(path, 1.0 / (1.0 - prob));
        return false;
    }

    /** Add radiance to the path contribution.
        \param[in,out] path Path state.
        \param[in] radiance Vertex radiance.
    */
    static void addToPathContribution(inout PathState path, float3 radiance, const AUXContext auxContext)
    {
#if STABLE_PLANES_MODE != STABLE_PLANES_NOISY_PASS // noisy mode should either output everything to denoising buffers, with stable stuff handled in MODE 1; there is no 'residual'
        path.L += max( 0.xxx, path.thp*radiance );
#endif
    }

    /** Generates a new scatter ray using BSDF importance sampling.
        \param[in] sd Shading data.
        \param[in] bsdf BSDF at the shading point.
        \param[in,out] path The path state.
        \return True if a ray was generated, false otherwise.
    */
    static bool generateScatterRay(const ShadingData sd, const ActiveBSDF bsdf, inout PathState path, const PathTracerParams params, const AUXContext auxContext)
    {
        BSDFSample result;
        bool valid = bsdf.sample(sd, path.sg, result, kUseBSDFSampling);

        if (valid) valid = generateScatterRay(result, sd, bsdf, path, params, auxContext);

        return valid;
    }

    /** Generates a new scatter ray given a valid BSDF sample.
        \param[in] bs BSDF sample (assumed to be valid).
        \param[in] sd Shading data.
        \param[in] bsdf BSDF at the shading point.
        \param[in,out] path The path state.
        \return True if a ray was generated, false otherwise.
    */
    static bool generateScatterRay(const BSDFSample bs, const ShadingData sd, const ActiveBSDF bsdf, inout PathState path, const PathTracerParams params, const AUXContext auxContext)
    {
        bool isCurveHit = false; // path.hit.getType() == HitType::Curve; <- not supported

        if (path.hasFlag(PathFlags::stablePlaneOnPlane) && bs.pdf == 0)
        {
            // Set the flag to remember that this secondary path started with a delta branch,
            // so that its secondary radiance would not be directed into ReSTIR GI later.
            path.setFlag(PathFlags::stablePlaneOnDeltaBranch);
        }

        path.dir = bs.wo;
        if (auxContext.ptConsts.useReSTIRGI && path.hasFlag(PathFlags::stablePlaneOnPlane) && bs.pdf != 0 && path.hasFlag(PathFlags::stablePlaneOnDominantBranch))
        {
            // ReSTIR GI decomposes the throughput of the primary scatter ray into the BRDF and PDF components.
            // The PDF component is applied here, and the BRDF component is applied in the ReSTIR GI final shading pass.
            updatePathThroughput(path, 1.0 / bs.pdf);
        }
        else
        {
            // No ReSTIR GI, or not SP 0, or a secondary vertex, or a delta event - use full BRDF/PDF weight
            updatePathThroughput(path, bs.weight);
        }
        path.pdf = bs.pdf;

        path.clearEventFlags(); // removes PathFlags::transmission, PathFlags::specular, PathFlags::delta flags

        // Handle reflection events.
        if (bs.isLobe(LobeType::Reflection))
        {
            // We classify specular events as diffuse if the roughness is above some threshold.
            float roughness = bsdf.getProperties(sd).roughness;
            bool isDiffuse = bs.isLobe(LobeType::DiffuseReflection) || roughness > params.specularRoughnessThreshold;

            if (isDiffuse)
            {
                path.incrementCounter(PackedCounters::DiffuseBounces);
            }
            else
            {
                // path.incrementBounces(BounceType::Specular);
                path.setSpecular();
            }
        }
        // Handle transmission events.
        if (bs.isLobe(LobeType::Transmission))
        {
            // path.incrementBounces(BounceType::Transmission);
            path.setTransmission();

            // if( auxContext.debug.IsDebugPixel() )
            //     auxContext.debug.Print( path.getVertexIndex()*2+0, sd.posW );

            // Compute ray origin for next ray segment.
            path.origin = sd.computeNewRayOrigin(false);

            // if( auxContext.debug.IsDebugPixel() )
            //     auxContext.debug.Print( path.getVertexIndex()*2+1, path.origin );

            // Update interior list and inside volume flag.
            if (!sd.mtl.isThinSurface())
            {
                uint nestedPriority = sd.mtl.getNestedPriority();
                path.interiorList.handleIntersection(sd.materialID, nestedPriority, sd.frontFacing);
                path.setInsideDielectricVolume(!path.interiorList.isEmpty());
            }
        }

        // Handle delta events.
        if (bs.isLobe(LobeType::Delta))
            path.setDelta();
        else
            path.setDeltaOnlyPath(false);

        float angleBefore = path.rayCone.getSpreadAngle();
        if (!path.isDelta())
            path.rayCone = RayCone::make(path.rayCone.getWidth(), min( path.rayCone.getSpreadAngle() + ComputeRayConeSpreadAngleExpansionByScatterPDF( bs.pdf ), 2.0 * M_PI ) );


        // if bouncePDF then it's a delta event - expansion angle is 0
        path.fireflyFilterK = ComputeNewScatterFireflyFilterK(path.fireflyFilterK, auxContext.ptConsts.camera.pixelConeSpreadAngle, bs.pdf, bs.lobeP);

//#if STABLE_PLANES_MODE!=STABLE_PLANES_NOISY_PASS
//        if( auxContext.debug.IsDebugPixel() )
//            //auxContext.debug.Print( path.getVertexIndex(), path.fireflyFilterK, luminance(path.L), bs.pdf, bs.lobeP);
//            auxContext.debug.Print( path.getVertexIndex(), path.isDelta(), luminance(path.thp), bs.pdf, bs.lobeP);
//#endif
        
        // // Save the shading normal. This is needed for MIS. <- edit; not currently needed :D
        // path.normal = sd.N;

        // Mark the path as valid only if it has a non-zero throughput.
        bool valid = any(path.thp > 0.f);

#if STABLE_PLANES_MODE==STABLE_PLANES_NOISY_PASS
        if (valid)
            StablePlanesOnScatter(path, bs, auxContext);
#endif

        return valid;
    }

    /** Evaluates the currently configured heuristic for multiple importance sampling (MIS).
        \param[in] n0 Number of samples taken from the first sampling strategy.
        \param[in] p0 Pdf for the first sampling strategy.
        \param[in] n1 Number of samples taken from the second sampling strategy.
        \param[in] p1 Pdf for the second sampling strategy.
        \return Weight for the contribution from the first strategy (p0).
    */
    static float evalMIS(float n0, float p0, float n1, float p1)
    {
        switch (kMISHeuristic)
        {
        case MISHeuristic::Balance:
        {
            // Balance heuristic
            float q0 = n0 * p0;
            float q1 = n1 * p1;
            return q0 / (q0 + q1);
        }
        case MISHeuristic::PowerTwo:
        {
            // Power two heuristic
            float q0 = (n0 * p0) * (n0 * p0);
            float q1 = (n1 * p1) * (n1 * p1);
            return q0 / (q0 + q1);
        }
        case MISHeuristic::PowerExp:
        {
            const float kMISPowerExponent = 1.5;    // <- TODO: get it from PathTracerParams
            // Power exp heuristic
            float q0 = pow(n0 * p0, kMISPowerExponent);
            float q1 = pow(n1 * p1, kMISPowerExponent);
            return q0 / (q0 + q1);
        }
        default:
            return 0.f;
        }
    }

    /** Generates a light sample on the environment map.
        \param[in] vertex Path vertex.
        \param[in,out] sg Sample generator.
        \param[out] ls Struct describing valid samples.
        \return True if the sample is valid and has nonzero contribution, false otherwise.
    */
    static bool generateEnvMapSample(const PathVertex vertex, inout SampleGenerator sg, out PathLightSample ls)
    {
        ls = PathLightSample::make();   // Default initialization to avoid divergence at returns. TODO: might be unnecessary; check for perf and side-effects

        if (!kUseEnvLight || !Bridge::EnvMap::HasEnvMap())
            return false;
        
        // Sample environment map.
        EnvMapSample lightSample;
        if (!Bridge::EnvMap::Sample(sampleNext2D(sg), lightSample)) 
            return false;

#if 0 // pdf correctness test
    float rpdf = envMapSampler.evalPdf(lightSample.dir);
    float relDiff = abs((lightSample.pdf - rpdf) / (1e-6 + abs(lightSample.pdf) + abs(rpdf)));
    if( relDiff > 5e-7 )
    {
        lightSample.pdf = 1e10;
        lightSample.Le.r += 1e20;
    }
#endif
       
        // Setup returned sample.
        ls.Li = lightSample.pdf > 0.f ? lightSample.Le / lightSample.pdf : float3(0,0,0);
        ls.pdf = lightSample.pdf;
        ls.origin = vertex.getRayOrigin(lightSample.dir);
        ls.distance = kMaxRayTravel;
        ls.dir = lightSample.dir;

        return any(ls.Li > 0.f);
    }

    /** Generates a light sample on the emissive geometry.
        \param[in] vertex Path vertex.
        \param[in] upperHemisphere True if only upper hemisphere (w.r.t. shading normal) should be considered.
        \param[in,out] sg Sample generator.
        \param[out] ls Struct describing valid samples.
        \return True if the sample is valid and has nonzero contribution, false otherwise.
    */
    static bool generateEmissiveSample(const PathVertex vertex, const bool upperHemisphere, inout SampleGenerator sg, out PathLightSample ls)
    {
        ls = PathLightSample::make();   // Default initialization to avoid divergence at returns. TODO: might be unnecessary; check for perf and side-effects
        return false;
#if 0 // to be implemented yet
        if (!kUseEmissiveLights) return false;

        TriangleLightSample tls;
        if (!emissiveSampler.sampleLight(vertex.pos, vertex.normal, upperHemisphere, sg, tls)) return false;

        // Setup returned sample.
        ls.Li = tls.pdf > 0.f ? tls.Le / tls.pdf : float3(0);
        ls.pdf = tls.pdf;
        // Offset shading and light position to avoid self-intersection.
        float3 lightPos = computeRayOrigin(tls.posW, tls.normalW);
        ls.origin = vertex.getRayOrigin(lightPos - vertex.pos);
        float3 toLight = lightPos - ls.origin;
        ls.distance = length(toLight);
        ls.dir = normalize(toLight);

        return any(ls.Li > 0.f);
#endif
    }

    /** Generates a light sample on the analytic lights.
        \param[in] vertex Path vertex.
        \param[in,out] sg Sample generator.
        \param[out] ls Struct describing valid samples.
        \return True if the sample is valid and has nonzero contribution, false otherwise.
    */
    static bool generateAnalyticLightSample(const PathVertex vertex, inout SampleGenerator sg, out PathLightSample ls)
    {
        ls = PathLightSample::make();   // Default initialization to avoid divergence at returns. TODO: might be unnecessary; check for perf and side-effects
        
        uint lightCount = Bridge::getAnalyticLightCount(); // gScene.getLightCount();
        if (!kUseAnalyticLights || lightCount == 0) return false;
        
        // Sample analytic light source selected uniformly from the light list.
        // TODO: Sample based on estimated contributions as pdf.
        uint lightIndex = min(uint(sampleNext1D(sg) * lightCount), lightCount - 1);
        
        // Sample local light source.
        AnalyticLightSample lightSample;
        if (!Bridge::sampleAnalyticLight(vertex.pos, /*gScene.getLight*/(lightIndex), sg, lightSample)) return false;
        
        // Setup returned sample.
        ls.pdf = lightSample.pdf / lightCount;
        ls.Li = lightSample.Li * lightCount;
        // Offset shading position to avoid self-intersection.
        ls.origin = vertex.getRayOrigin(lightSample.dir);
        // Analytic lights do not currently have a geometric representation in the scene.
        // Do not worry about adjusting the ray length to avoid self-intersections at the light.
        ls.distance = lightSample.distance;
        ls.dir = lightSample.dir;
        
        return any(ls.Li > 0.f);
    }

    /** Return the probabilities for selecting different light types.
        \param[out] p Probabilities.
    */
    static void getLightTypeSelectionProbabilities(out float p[3])
    {
        // Set relative probabilities of the different sampling techniques.
        // TODO: These should use estimated irradiance from each light type. Using equal probabilities for now.
        p[0] = (kUseEnvLight && Bridge::EnvMap::HasEnvMap()) ? 1.f : 0.f;
        p[1] = 0; // kUseEmissiveLights ? 1.f : 0.f;
        p[2] = kUseAnalyticLights ? 1.f : 0.f;

        // Normalize probabilities. Early out if zero.
        float sum = p[0] + p[1] + p[2];
        if (sum == 0.f) return;

        float invSum = 1.f / sum;
        p[0] *= invSum;
        p[1] *= invSum;
        p[2] *= invSum;
    }

    static float getEnvMapSelectionProbability()   { float p[3]; getLightTypeSelectionProbabilities(p); return p[0]; }
    static float getEmissiveSelectionProbability() { float p[3]; getLightTypeSelectionProbabilities(p); return p[1]; }
    static float getAnalyicSelectionProbability()  { float p[3]; getLightTypeSelectionProbabilities(p); return p[2]; }

    /** Select a light type for sampling.
        \param[out] lightType Selected light type.
        \param[out] pdf Probability for selected type.
        \param[in,out] sg Sample generator.
        \return Return true if selection is valid.
    */
    static bool selectLightType(out uint lightType, out float pdf, inout SampleGenerator sg)
    {
        float p[3];
        getLightTypeSelectionProbabilities(p);

        float u = sampleNext1D(sg);

        [unroll]
        for (lightType = 0; lightType < 3; ++lightType)
        {
            if (u < p[lightType])
            {
                pdf = p[lightType];
                return true;
            }
            u -= p[lightType];
        }

        return false;
    }

    /** Samples a light source in the scene.
        This function first stochastically selects a type of light source to sample,
        and then calls that the sampling function for the chosen light type.
        The upper/lower hemisphere is defined as the union of the hemispheres w.r.t. to the shading and face normals.
        \param[in] vertex Path vertex.
        \param[in] sampleUpperHemisphere True if the upper hemisphere should be sampled.
        \param[in] sampleLowerHemisphere True if the lower hemisphere should be sampled.
        \param[in,out] sg Sample generator.
        \param[out] ls Struct describing valid samples.
        \return True if the sample is valid and has nonzero contribution, false otherwise.
    */
    static bool generateLightSample(const PathVertex vertex, const bool sampleUpperHemisphere, const bool sampleLowerHemisphere, inout SampleGenerator sg, out PathLightSample ls)
    {
        ls = PathLightSample::make();   // Default initialization to avoid divergence at returns. TODO: might be unnecessary; check for perf and side-effects

        uint lightType;
        float selectionPdf;
        if (!selectLightType(lightType, selectionPdf, sg)) return false;

        bool valid = false;
        if ((kUseEnvLight&& Bridge::EnvMap::HasEnvMap()) && lightType == (uint)PathLightType::EnvMap) valid = generateEnvMapSample(vertex, sg, ls);
        if (kUseEmissiveLights && lightType == (uint)PathLightType::Emissive)
        {
            // Emissive light samplers have an option to exclusively sample the upper hemisphere.
            bool upperHemisphere = sampleUpperHemisphere && !sampleLowerHemisphere;
            valid = generateEmissiveSample(vertex, upperHemisphere, sg, ls);
        }
        if (kUseAnalyticLights && lightType == (uint)PathLightType::Analytic)
        {
            valid = generateAnalyticLightSample(vertex, sg, ls);
        }
        if (!valid) return false;

        // Reject samples in non-requested hemispheres.
        float cosTheta = dot(vertex.normal, ls.dir);
        // Flip the face normal to point in the same hemisphere as the shading normal.
        float3 faceNormal = sign(dot(vertex.normal, vertex.faceNormal)) * vertex.faceNormal;
        float cosThetaFace = dot(faceNormal, ls.dir);
        if (!sampleUpperHemisphere && (max(cosTheta, cosThetaFace) >= -kMinCosTheta)) return false;
        if (!sampleLowerHemisphere && (min(cosTheta, cosThetaFace) <= kMinCosTheta)) return false;

        // Account for light type selection.
        ls.lightType = lightType;
        ls.pdf *= selectionPdf;
        ls.Li /= selectionPdf;

        return true;
    }
    
    // Called after ray tracing just before handleMiss or handleHit, to advance internal states related to travel
    static void updatePathTravelled(inout PathState path, const float3 rayOrigin, const float3 rayDir, const float rayTCurrent, const AUXContext auxContext, uniform bool incrementVertexIndex = true, uniform bool updateOriginDir = true)
    {
        if (updateOriginDir)    // make sure these two are up to date; they are only intended as "output" from ray tracer but could be used as input by subsystems
        {
            path.origin = rayOrigin;    
            path.dir = rayDir;
        }
        if (incrementVertexIndex)
            path.incrementVertexIndex();                                        // Advance to next path vertex (PathState::vertexIndex). (0 - camera, 1 - first bounce, ...)
        path.rayCone = path.rayCone.propagateDistance(rayTCurrent);             // Grow the cone footprint based on angle; angle itself can change on scatter
        path.sceneLength = min(path.sceneLength+rayTCurrent, kMaxRayTravel);    // Advance total travel length

        // good place for debug viz
#if ENABLE_DEBUG_VIZUALISATION && !NON_PATH_TRACING_PASS// && STABLE_PLANES_MODE!=STABLE_PLANES_BUILD_PASS <- let's actually show the build rays - maybe even add them some separate effect in the future
        if( auxContext.debug.IsDebugPixel() )
            auxContext.debug.DrawLine(rayOrigin, rayOrigin+rayDir*rayTCurrent, float4(0.6.xxx, 0.2), float4(1.0.xxx, 1.0));
#endif
    }

    // Miss shader
    static void handleMiss(inout PathState path, const float3 rayOrigin, const float3 rayDir, const float rayTCurrent, const AUXContext auxContext)
    {
        updatePathTravelled(path, rayOrigin, rayDir, rayTCurrent, auxContext);

        const PathTracerParams params = Bridge::getPathTracerParams();

#if STABLE_PLANES_MODE==STABLE_PLANES_BUILD_PASS && ENABLE_DEBUG_DELTA_TREE_VIZUALISATION
        if (path.hasFlag(PathFlags::deltaTreeExplorer))
        {
            DeltaTreeVizHandleMiss(path, rayOrigin, rayDir, rayTCurrent, params, auxContext);
            return;
        }
#endif

        // Check if the scatter event is samplable by the light sampling technique.
        const bool isLightSamplable = path.isLightSamplable();
        const bool isLightSampled = path.isLightSampled();

        // Add env radiance. See counterpart logic for 'computeEmissive'
        bool computeEnv = (kUseEnvLight&& Bridge::EnvMap::HasEnvMap()) && (!kUseNEE || kUseMIS || !isLightSampled || !isLightSamplable);

        // We skip sampling environment when ReSTIR handled sampled in NEE, in the previous vertex. Delta and Transmission parts are never sampled by ReSTIR, so we have to handle those.
        if (path.hasFlag(PathFlags::lightSampledReSTIR) && !(path.isDelta() || path.isTransmission()) )
            computeEnv = false;

        float3 environmentEmission = 0.f;
        if (computeEnv)
        {
            float misWeight = 1.f;
            if (kUseNEE && kUseMIS && isLightSampled && isLightSamplable)
            {
                // If NEE and MIS are enabled, and we've already sampled the env map,
                // then we need to evaluate the MIS weight here to account for the remaining contribution.

                // Evaluate PDF, had it been generated with light sampling.
                float lightPdf = getEnvMapSelectionProbability() * Bridge::EnvMap::EvalPdf(path.dir);
              
                // Compute MIS weight by combining this with BSDF sampling.
                // Note we can assume path.pdf > 0.f since we shouldn't have got here otherwise.
                misWeight = evalMIS(1, path.pdf, 1, lightPdf);

                //if( auxContext.debug.IsDebugPixel() )
                //    auxContext.debug.Print( path.getVertexIndex(), path.pdf, lightPdf, misWeight );
            }
            float3 Le = Bridge::EnvMap::Eval(path.dir);
            environmentEmission = misWeight * Le;
    
        }

        environmentEmission = FireflyFilter( environmentEmission, auxContext.ptConsts.fireflyFilterThreshold, path.fireflyFilterK );

        path.clearHit();
        path.terminate();

#if STABLE_PLANES_MODE!=STABLE_PLANES_DISABLED
        if( !StablePlanesHandleMiss(path, environmentEmission, rayOrigin, rayDir, rayTCurrent, 0, params, auxContext) )
            return;
#endif

        if (any(environmentEmission>0))
            addToPathContribution(path, environmentEmission, auxContext);
    }

#if STABLE_PLANES_MODE==STABLE_PLANES_BUILD_PASS
    // splits out delta component, traces ray to next surface, saves the hit without any further processing
    // if verifyDominantFlag is true, will remove PathFlags::stablePlaneOnDominantBranch from result if not on dominant lobe (otherwise it stay as it was); if we're splitting more than 1 lobe then we can only follow one dominant so we must update - otherwise we can let the flag persist
    static PathState splitDeltaPath(const PathState oldPath, const float3 rayDir, const SurfaceData surfaceData, const DeltaLobe lobe, const uint deltaLobeIndex, const bool verifyDominantFlag, const AUXContext auxContext)
    {
        const ShadingData sd = surfaceData.sd;

        // 1. first generate new virtual path - this is just for consistency with the rest of the code, most of it is unused and compiled out
        PathState newPath       = oldPath;
        // newPath.incrementVertexIndex(); <- not needed, happens in nextHit
        newPath.dir             = lobe.Wo;
        newPath.thp             *= lobe.thp;
        newPath.pdf             = 0;
        newPath.origin          = sd.computeNewRayOrigin(lobe.transmission==0);  // bool param is viewside
        newPath.stableBranchID  = StablePlanesAdvanceBranchID( oldPath.stableBranchID, deltaLobeIndex );

        newPath.setDelta();

        // Handle reflection events.
        if (!lobe.transmission)
        {
            // newPath.incrementBounces(BounceType::Specular);
            newPath.setSpecular();
        }
        else // transmission
        {
            // newPath.incrementBounces(BounceType::Transmission);
            newPath.setTransmission();

            // Update interior list and inside volume flag.
            if (!sd.mtl.isThinSurface())
            {
                uint nestedPriority = sd.mtl.getNestedPriority();
                newPath.interiorList.handleIntersection(sd.materialID, nestedPriority, sd.frontFacing);
                newPath.setInsideDielectricVolume(!newPath.interiorList.isEmpty());
            }
        }

        // Compute local transform (rotation component only) and apply to path transform (path.imageXForm). This intentionally ignores curvature/skewing to avoid complexity and need for full 4x4 matrix.
        float3x3 localT;
        if (lobe.transmission)
        {
            localT = MatrixRotateFromTo(lobe.Wo, rayDir);   // no need to refract again, we already have in and out vectors
        }
        else
        {
            const float3x3 toTangent  = float3x3(surfaceData.sd.T,surfaceData.sd.B,surfaceData.sd.N);
            const float3x3 mirror     = float3x3(1,0,0,0,1,0,0,0,-1); // reflect around z axis
            localT = mul(mirror,toTangent); 
            localT = mul(transpose(toTangent),localT);
        }
        newPath.imageXform = mul((float3x3)newPath.imageXform, localT);

        // Testing the xforms - virt should always transform to rayDir here
        // float3 virt = mul( (float3x3)newPath.imageXform, lobe.Wo );
        // if (auxContext.debug.IsDebugPixel() && oldPath.getVertexIndex()==1)
        // {
        //     auxContext.debug.Print(oldPath.getVertexIndex()+0, rayDir );
        //     auxContext.debug.Print(oldPath.getVertexIndex()+1, lobe.Wo );
        //     auxContext.debug.Print(oldPath.getVertexIndex()+2, virt );
        // }

        // clear dominant flag if it this lobe isn't dominant but we were on a dominant path
        if (verifyDominantFlag && newPath.hasFlag(PathFlags::stablePlaneOnDominantBranch))
        {
            int psdDominantDeltaLobeIndex = int(sd.mtl.getPSDDominantDeltaLobeP1())-1;
            if ( deltaLobeIndex!=psdDominantDeltaLobeIndex )
                newPath.setFlag(PathFlags::stablePlaneOnDominantBranch, false);
        }

        return newPath;
    }
#endif // #if STABLE_PLANES_MODE==STABLE_PLANES_BUILD_PASS

    // supports only TriangleHit for now; more to be added when needed
    static void handleHit(const uniform OptimizationHints optimizationHints, inout PathState path, const float3 rayOrigin, const float3 rayDir, const float rayTCurrent, const AUXContext auxContext)
    {
        updatePathTravelled(path, rayOrigin, rayDir, rayTCurrent, auxContext);

        const PathTracerParams params = Bridge::getPathTracerParams();

        const uint2 pixelPos = PathIDToPixel(path.id);
#if ENABLE_DEBUG_VIZUALISATION
        const bool debugPath = auxContext.debug.IsDebugPixel();
#else
        const bool debugPath = false;
#endif

        // Upon hit:
        // - Load vertex/material data
        // - Compute MIS weight if path.getVertexIndex() > 1 and emissive hit
        // - Add emitted radiance
        // - Sample light(s) using shadow rays
        // - Sample scatter ray or terminate

        const bool isPrimaryHit     = path.getVertexIndex() == 1;
        const HitType hitType       = HitInfo::make(path.hitPacked).getType();
        const bool isTriangleHit    = hitType == HitType::Triangle;
        const bool isCurveHit       = hitType == HitType::Curve;

        const TriangleHit triangleHit = TriangleHit::make(path.hitPacked);

        const uint vertexIndex = path.getVertexIndex();

        const SurfaceData bridgedData = Bridge::loadSurface(optimizationHints, triangleHit, rayDir, path.rayCone, params, path.getVertexIndex(), auxContext.debug);

#if STABLE_PLANES_MODE==STABLE_PLANES_NOISY_PASS
        // if( auxContext.debug.IsDebugPixel() && path.getVertexIndex()==1 )
        //     auxContext.debug.Print( 4, path.rayCone.getSpreadAngle(), path.rayCone.getWidth(), rayTCurrent, path.sceneLength);
#endif

        
        // Account for volume absorption.
        float volumeAbsorption = 0;   // used for stats
        if (!path.interiorList.isEmpty())
        {
            const uint materialID = path.interiorList.getTopMaterialID();
            const HomogeneousVolumeData hvd = Bridge::loadHomogeneousVolumeData(materialID); // gScene.materials.getHomogeneousVolumeData(materialID);
            const float3 transmittance = HomogeneousVolumeSampler::evalTransmittance(hvd, rayTCurrent);
            volumeAbsorption = 1 - luminance(transmittance);
            updatePathThroughput(path, transmittance);
        }

        // Reject false hits in nested dielectrics but also updates 'outside index of refraction' and dependent data
        bool rejectedFalseHit = !handleNestedDielectrics(bridgedData, path, auxContext);

#if STABLE_PLANES_MODE==STABLE_PLANES_BUILD_PASS && ENABLE_DEBUG_DELTA_TREE_VIZUALISATION
        if (path.hasFlag(PathFlags::deltaTreeExplorer))
        {
            DeltaTreeVizHandleHit(path, rayOrigin, rayDir, rayTCurrent, bridgedData, rejectedFalseHit, hasFinishedSurfaceBounces(path), volumeAbsorption, params, auxContext);
            return;
        }
#endif
        if (rejectedFalseHit)
            return;

        // These will not change anymore, so make const shortcuts
        const ShadingData sd    = bridgedData.sd;
        const ActiveBSDF bsdf   = bridgedData.bsdf;

#if ENABLE_DEBUG_VIZUALISATION && STABLE_PLANES_MODE!=STABLE_PLANES_BUILD_PASS
        if (debugPath)
        {
            // IoR debugging - .x - "outside", .y - "interior", .z - frontFacing, .w - "eta" (eta is isFrontFace?outsideIoR/insideIoR:insideIoR/outsideIoR)
            // auxContext.debug.Print(path.getVertexIndex(), float4(sd.IoR, bridgedData.interiorIoR, sd.frontFacing, bsdf.data.eta) );

            // draw tangent space
            auxContext.debug.DrawLine(sd.posW, sd.posW + sd.T * auxContext.debug.LineScale(), float4(0.7, 0, 0, 0.5), float4(1.0, 0, 0, 0.5));
            auxContext.debug.DrawLine(sd.posW, sd.posW + sd.B * auxContext.debug.LineScale(), float4(0, 0.7, 0, 0.5), float4(0, 1.0, 0, 0.5));
            auxContext.debug.DrawLine(sd.posW, sd.posW + sd.N * auxContext.debug.LineScale(), float4(0, 0, 0.7, 0.5), float4(0, 0, 1.0, 0.5));

            // draw ray cone footprint
            float coneWidth = path.rayCone.getWidth();
            auxContext.debug.DrawLine(sd.posW + (-sd.T+sd.B) * coneWidth, sd.posW + (+sd.T+sd.B) * coneWidth, float4(0.5, 0.0, 1.0, 0.5), float4(0.5, 1.0, 0.0, 0.5) );
            auxContext.debug.DrawLine(sd.posW + (+sd.T+sd.B) * coneWidth, sd.posW + (+sd.T-sd.B) * coneWidth, float4(0.5, 0.0, 1.0, 0.5), float4(0.5, 1.0, 0.0, 0.5) );
            auxContext.debug.DrawLine(sd.posW + (+sd.T-sd.B) * coneWidth, sd.posW + (-sd.T-sd.B) * coneWidth, float4(0.5, 0.0, 1.0, 0.5), float4(0.5, 1.0, 0.0, 0.5) );
            auxContext.debug.DrawLine(sd.posW + (-sd.T-sd.B) * coneWidth, sd.posW + (-sd.T+sd.B) * coneWidth, float4(0.5, 0.0, 1.0, 0.5), float4(0.5, 1.0, 0.0, 0.5) );
        }
#endif

        BSDFProperties bsdfProperties = bsdf.getProperties(sd);

        // TODO: disable kDisableCaustics for simplicity
        // Disable specular lobes if caustics are disabled and path already contains a diffuse vertex.
        bool isSpecular = bsdfProperties.roughness <= params.specularRoughnessThreshold;
        if (kDisableCaustics && path.getCounter(PackedCounters::DiffuseBounces) > 0 && isSpecular)
        {
            sd.mtl.setActiveLobes((uint)LobeType::Diffuse);
        }

        // Optionally disable emission inside volumes.
        if (!kUseLightsInDielectricVolumes && path.isInsideDielectricVolume())
        {
            bsdfProperties.emission = float3(0,0,0);
        }

        // Check if the scatter event is samplable by the light sampling technique.
        const bool isLightSamplable = path.isLightSamplable(); // same as !path.isDelta()

        // Add emitted radiance.
        // The primary hit is always included, secondary hits only if emissive lights are enabled and the full light contribution hasn't been sampled elsewhere.
        bool computeEmissive = isPrimaryHit || (kUseEmissiveLights && (!(kUseNEE && !optimizationHints.OnlyDeltaLobes) || kUseMIS || !path.isLightSampled() || !isLightSamplable));

        // We skip sampling emissive when ReSTIR handled sampled in NEE, in the previous vertex. Delta and Transmission parts are never sampled by ReSTIR, so we have to handle those.
        if (path.hasFlag(PathFlags::lightSampledReSTIR) && !(path.isDelta() || path.isTransmission()) )
            computeEmissive = false;

        float3 surfaceEmission = 0.f;

        // This is where Multiple Importance Sampling (MIS) would be implemented if we were using emissive triangle light importance sampling, but we're not, so this is simple
        if (computeEmissive) // && any(bsdfProperties.emission > 0.f))
            surfaceEmission = bsdfProperties.emission; // * misWeight;

        surfaceEmission = FireflyFilter( surfaceEmission, auxContext.ptConsts.fireflyFilterThreshold, path.fireflyFilterK );

		// it's less confusing if this happens before stable planes hit handling
        if (any(surfaceEmission>0))
            addToPathContribution(path, surfaceEmission, auxContext);

#if STABLE_PLANES_MODE!=STABLE_PLANES_DISABLED
        StablePlanesHandleHit(path, rayOrigin, rayDir, rayTCurrent, optimizationHints.SortKey, params, auxContext, bridgedData, volumeAbsorption, surfaceEmission);
#endif

#if STABLE_PLANES_MODE!=STABLE_PLANES_BUILD_PASS // in build mode we've consumed emission and either updated or terminated path ourselves

        // Terminate after scatter ray on last vertex has been processed. Also terminates here if StablePlanesHandleHit terminated path.
        if (hasFinishedSurfaceBounces(path))
        {
            path.terminate();
            return;
        }

        // Compute origin for rays traced from this path vertex.
        path.origin = sd.computeNewRayOrigin();

        // Determine if BSDF has non-delta lobes.
        const uint lobes = bsdf.getLobes(sd);
        const bool hasNonDeltaLobes = ((lobes & (uint)LobeType::NonDelta) != 0) && (!optimizationHints.OnlyDeltaLobes);

        // Check if we should apply NEE.
        const bool applyNEE = (kUseNEE && !optimizationHints.OnlyDeltaLobes) && hasNonDeltaLobes;

        // Check if sample from RTXDI should be applied instead of NEE.
#if STABLE_PLANES_MODE==STABLE_PLANES_NOISY_PASS
        const bool applyReSTIR = auxContext.ptConsts.useReSTIR && hasNonDeltaLobes && path.hasFlag(PathFlags::stablePlaneOnDominantBranch) && path.hasFlag(PathFlags::stablePlaneOnPlane);
#else
        const bool applyReSTIR = false;
#endif

        // Debugging tool to remove direct lighting from primary surfaces
        const bool suppressNEE = path.hasFlag(PathFlags::stablePlaneOnDominantBranch) && path.hasFlag(PathFlags::stablePlaneOnPlane) && auxContext.ptConsts.suppressPrimaryNEE;

        // TODO: Support multiple shadow rays.
        path.setLightSampled(false, false);
        path.setFlag(PathFlags::lightSampledReSTIR, false);

        if (applyNEE || applyReSTIR)
        {
            PathLightSample ls = PathLightSample::make();
            bool validSample = false;

            if (applyReSTIR)
            {
                // ReSTIR is applied as a separate fullscreen pass, but we must ensure no other NEE is enabled here (so leave validSample as false), and flag so next emissive or envmap is correctly excluded/sampled
                path.setFlag(PathFlags::lightSampledReSTIR, true);
            }
            else
            {
                // Setup path vertex.
                PathVertex vertex = PathVertex::make(path.getVertexIndex(), sd.posW, sd.N, sd.faceN);

                // Determine if upper/lower hemispheres need to be sampled.
                bool sampleUpperHemisphere = isCurveHit || ((lobes & (uint)LobeType::NonDeltaReflection) != 0);
                if (!kUseLightsInDielectricVolumes && path.isInsideDielectricVolume()) sampleUpperHemisphere = false;
                bool sampleLowerHemisphere = isCurveHit || ((lobes & (uint)LobeType::NonDeltaTransmission) != 0);

                // Sample a light.
                validSample = generateLightSample(vertex, sampleUpperHemisphere, sampleLowerHemisphere, path.sg, ls);
                path.setLightSampled(sampleUpperHemisphere, sampleLowerHemisphere);
            }

#if 0 // there's cost to enabling this
            if( debugPath && isPrimaryHit )
            {
                DebugLogger::DebugPrint( 0, validSample?(1):(0) );
                DebugLogger::DebugPrint( 1, float4(ls.origin, ls.lightType) );      // origin, type
                DebugLogger::DebugPrint( 2, float4(ls.dir, ls.distance) );          // dir, distance
                DebugLogger::DebugPrint( 3, float4(ls.Li, ls.pdf) );                // radiance, pdf
            }
#endif

            if (validSample)
            {
                if (suppressNEE)    // keep it a valid sample so we don't add in normal path
                    ls.Li = 0;

                // Apply MIS weight.
                if (kUseMIS && ls.lightType != (uint)PathLightType::Analytic)
                {
                    float scatterPdf = bsdf.evalPdf(sd, ls.dir, kUseBSDFSampling);
                    ls.Li *= evalMIS(1, ls.pdf, 1, scatterPdf);
                }

#if PTSDK_DIFFUSE_SPECULAR_SPLIT
                float3 bsdfThpDiff, bsdfThpSpec; 
                bsdf.eval(sd, ls.dir, path.sg, bsdfThpDiff, bsdfThpSpec);
                float3 bsdfThp = bsdfThpDiff + bsdfThpSpec;
#else
                float3 bsdfThp = bsdf.eval(sd, ls.dir, path.sg);
#endif

                float3 neeContribution = /*path.thp **/ bsdfThp * ls.Li;

                if (any(neeContribution > 0))
                {
                    const RayDesc ray = ls.getVisibilityRay().toRayDesc();
                    
                    // Trace visibility ray
                    // If RTXDI is enabled, a visibility ray has already been fired so we can skip it 
                    // here. ( Non-visible lights result in validSample=false, so we won't get this far)
                    //logTraceRay(PixelStatsRayType::Visibility);
                    bool visible = Bridge::traceVisibilityRay(ray, path.rayCone, path.getVertexIndex(), auxContext.debug);
                    
                    if (visible) 
                    {
                        float neeFireflyFilterK = ComputeNewScatterFireflyFilterK(path.fireflyFilterK, auxContext.ptConsts.camera.pixelConeSpreadAngle, ls.pdf, 1.0);

#if PTSDK_DIFFUSE_SPECULAR_SPLIT
                        float3 diffuseRadiance  = FireflyFilter( bsdfThpDiff * ls.Li, auxContext.ptConsts.fireflyFilterThreshold, neeFireflyFilterK );
                        float3 specularRadiance = FireflyFilter( bsdfThpSpec * ls.Li, auxContext.ptConsts.fireflyFilterThreshold, neeFireflyFilterK );
#else
#error denoiser requires PTSDK_DIFFUSE_SPECULAR_SPLIT
                        float3 diffuseRadiance, specularRadiance;
                        diffuseRadiance=0;specularRadiance=0;
#endif
                        neeContribution = diffuseRadiance + specularRadiance; // will be 0 when denoiser captures it all

#if STABLE_PLANES_MODE==STABLE_PLANES_NOISY_PASS // fill
                        StablePlanesHandleNEE(path, diffuseRadiance, specularRadiance, ls.distance, auxContext);
#else
                        addToPathContribution(path, neeContribution, auxContext);
#endif
                    }
                }
            }
        }

        // Russian roulette to terminate paths early.
        if (kUseRussianRoulette && terminatePathByRussianRoulette(path, sampleNext1D(path.sg))) 
            return;

        // Generate the next path segment or terminate.
        bool valid = generateScatterRay(sd, bsdf, path, params, auxContext);

        // Check if this is the last path vertex.
        const bool isLastVertex = hasFinishedSurfaceBounces(path);

        // Terminate if this is the last path vertex and light sampling already completely sampled incident radiance.
        if ((kUseNEE && !optimizationHints.OnlyDeltaLobes) && !kUseMIS && isLastVertex && path.isLightSamplable()) valid = false;

        // Terminate caustics paths. // TODO: remove kDisableCaustics for simplicity
        if (kDisableCaustics && path.getCounter(PackedCounters::DiffuseBounces) > 0 && path.isSpecular()) valid = false;

        if (!valid)
        {
            path.terminate();
        }

#endif // #if STABLE_PLANES_MODE!=STABLE_PLANES_BUILD_PASS
    }

#if STABLE_PLANES_MODE!=STABLE_PLANES_DISABLED
    static void StablePlanesHandleHit(inout PathState path, const float3 rayOrigin, const float3 rayDir, const float rayTCurrent, uint SERSortKey, const PathTracerParams params, const AUXContext auxContext, const SurfaceData bridgedData, float volumeAbsorption, float3 surfaceEmission)
    {
#if STABLE_PLANES_MODE==STABLE_PLANES_BUILD_PASS // build
        const uint vertexIndex = path.getVertexIndex();
        const uint currentSPIndex = path.getStablePlaneIndex();

        if (vertexIndex == 1)
            auxContext.stablePlanes.StoreFirstHitRayLengthAndClearDominantToZero(path.sceneLength);

        bool setAsBase = true;    // if path no longer stable, stop and set as a base
        float passthroughOverride = 0.0;
        if (vertexIndex < min( cStablePlaneMaxVertexIndex, auxContext.ptConsts.maxStablePlaneVertexDepth))
        {
            DeltaLobe deltaLobes[cMaxDeltaLobes]; uint deltaLobeCount; float nonDeltaPart;
            bridgedData.bsdf.evalDeltaLobes(bridgedData.sd, deltaLobes, deltaLobeCount, nonDeltaPart);
            bool potentiallyVolumeTransmission = false;

            float pathThp = luminance(path.thp);    // perhaps give it another 10% just to avoid blocking at the end of a dark volume since all other pixels will be dark

            const float nonDeltaIgnoreThreshold = (1e-5);
            const float deltaIgnoreThreshold    = (0.001f);   
            const float neverIgnoreThreshold = auxContext.ptConsts.stablePlanesSplitStopThreshold / float(vertexIndex); // TODO: add screen space dither to threshold
            bool hasNonDeltaLobes = nonDeltaPart > nonDeltaIgnoreThreshold;
            passthroughOverride = saturate(1.0-nonDeltaPart*10.0); // if setting as base and no (or low) non-delta lobe, override denoising settings for better passthrough

            // prune non-noticeable delta lobes
            float nonZeroDeltaLobesThp[cMaxDeltaLobes];
            int nonZeroDeltaLobes[cMaxDeltaLobes];
            int nonZeroDeltaLobeCount = 0;
            for (int k = 0; k < deltaLobeCount; k++)
            {
                DeltaLobe lobe = deltaLobes[k];
                const float thp = pathThp*luminance(lobe.thp);
                if (thp>deltaIgnoreThreshold)
                {
                    nonZeroDeltaLobes[nonZeroDeltaLobeCount] = k;
                    nonZeroDeltaLobesThp[nonZeroDeltaLobeCount] = thp;
                    nonZeroDeltaLobeCount++;
                    potentiallyVolumeTransmission |= lobe.transmission; // we don't have a more clever way to do this at the moment
                }
            }
            if( nonZeroDeltaLobeCount > 0)
            {
                // sorting is a bad idea because it causes edges where data goes to one or the other side which prevents denoiser from sharing data alongside the edge and shows up as a seam
                // // bubble-sort ascending (biggest thp at the end, so we can just pop from back when forking later)
                // for (int i = 0; i < nonZeroDeltaLobeCount; i++)
                //     for (int j = i+1; j < nonZeroDeltaLobeCount; j++)
                //         if (nonZeroDeltaLobesThp[i] > nonZeroDeltaLobesThp[j])
                //         {
                //             swap( nonZeroDeltaLobesThp[i], nonZeroDeltaLobesThp[j] );
                //             swap( nonZeroDeltaLobes[i], nonZeroDeltaLobes[j] );
                //         }

                // in case plane index 0, we must stop at first non-direct junction; we can only continue if there's only one delta lobe and no non-delta at all (this then becomes just primary surface replacement)
                bool allowPSR = auxContext.ptConsts.allowPrimarySurfaceReplacement && (nonZeroDeltaLobeCount == 1) && (currentSPIndex == 0) && !potentiallyVolumeTransmission;
                bool canReuseExisting = (currentSPIndex != 0) && (nonZeroDeltaLobeCount>0);
                canReuseExisting |= allowPSR;
                canReuseExisting &= !hasNonDeltaLobes;       // stop on any diffuse lobe

                int availablePlaneCount = 0; int availablePlanes[cStablePlaneCount];
            
                auxContext.stablePlanes.GetAvailableEmptyPlanes(availablePlaneCount, availablePlanes);
            
                // if (auxContext.debug.IsDebugPixel() && vertexIndex == 2)
                //     auxContext.debug.Print( 0, currentSPIndex, availablePlaneCount, canReuseExisting, nonZeroDeltaLobeCount );

                float prunedThpTotal = 0.0;
                // prune lowest importance lobes that we can't handle
                while ((availablePlaneCount+canReuseExisting) < nonZeroDeltaLobeCount)
                {
                    int lowestThpIndex = 0;
                    float lowestThpValue = nonZeroDeltaLobesThp[0];
                    for (int i = 1; i < nonZeroDeltaLobeCount; i++)
                        if (nonZeroDeltaLobesThp[i] < lowestThpValue)
                        {
                            lowestThpIndex = i;
                            lowestThpValue = nonZeroDeltaLobesThp[i];
                        }
                    for (int j = lowestThpIndex; j < nonZeroDeltaLobeCount-1; j++)
                    {
                        nonZeroDeltaLobesThp[j] = nonZeroDeltaLobesThp[j+1];
                        nonZeroDeltaLobes[j]    = nonZeroDeltaLobes[j+1];
                    }
                    nonZeroDeltaLobeCount--;
                    prunedThpTotal += lowestThpValue;

                    // do not ignore the junction if we'll be completely missing a significant contribution
                    if (prunedThpTotal>neverIgnoreThreshold)
                        canReuseExisting = false;
                }

                // remove one lobe from the list for later reuse
                int lobeForReuse = -1;                                                  // could be one-liner with ?
                if (canReuseExisting)                                                       // could be one-liner with ?
                {                                                                           // could be one-liner with ?
                    lobeForReuse = nonZeroDeltaLobes[nonZeroDeltaLobeCount-1];          // could be one-liner with ?
                    nonZeroDeltaLobeCount--;                                                // could be one-liner with ?
                }                                                                           // could be one-liner with ?

                for( int i = 0; i < nonZeroDeltaLobeCount; i++ )
                {
                    const int lobeToExplore = nonZeroDeltaLobes[i];
                    //if (vertexIndex == 1 && auxContext.debug.IsDebugPixel())
                    //    auxContext.debug.Print(7, bestOption);
                    // split and then trace ray & enqueue hit for further traversal after this path is completed
                    PathState splitPath = PathTracer::splitDeltaPath(path, rayDir, bridgedData, deltaLobes[lobeToExplore], lobeToExplore, true, auxContext);
                    splitPath.setStablePlaneIndex(availablePlanes[i]);
                    auxContext.stablePlanes.StoreExplorationStart(availablePlanes[i], PathPayload::pack(splitPath).packed);
                }
                //if (auxContext.debug.IsDebugPixel())
                //    auxContext.debug.Print(5+vertexIndex, auxContext.stablePlanes.GetBranchID(0), auxContext.stablePlanes.GetBranchID(1), auxContext.stablePlanes.GetBranchID(2) );

                // and use current path to reuse lobe
                if ( lobeForReuse != -1 )
                {
                    setAsBase = false;
                    // split and use current path to continue on the best lobe
                    path = PathTracer::splitDeltaPath(path, rayDir, bridgedData, deltaLobes[lobeForReuse], lobeForReuse, nonZeroDeltaLobeCount>0, auxContext);
                }
            }
        }

        // we've reached the end of stable delta path exploration on this plane; figure out surface properties including motion vectors and store
        if (setAsBase)
        {
            // move surface world pos to first transform starting point reference frame; we then rotate it with the rotation-only imageXform, and place it back into worldspace (we used to have the whole transform but this takes too much space in payload...)
            const Ray cameraRay = Bridge::computeCameraRay( auxContext.pixelPos );
            float3 worldXFormFrom   = cameraRay.origin + cameraRay.dir * auxContext.stablePlanes.LoadFirstHitRayLength(auxContext.pixelPos);
            float3 imagePosW        = mul((float3x3)path.imageXform, bridgedData.sd.posW-worldXFormFrom)+worldXFormFrom;
            float3 prevImagePosW    = mul((float3x3)path.imageXform, bridgedData.prevPosW-worldXFormFrom)+worldXFormFrom;

            // figure out motion vectors 
            float3 motionVectors = Bridge::computeMotionVector(imagePosW.xyz, prevImagePosW.xyz); // <- simplify/improve this (for debugging to ignore imageXform, just feed in 'bridgedData.sd.posW, bridgedData.prevPosW')
            
            // denoising guide helpers
            //const float cIndirectRoughnessK = 0.12;
            const float roughness     = saturate(bridgedData.bsdf.data.roughness/*+passthroughOverride*cIndirectRoughnessK*/);
            float3 worldNormal  = bridgedData.sd.N;

            worldNormal = normalize(mul((float3x3)path.imageXform, worldNormal));

            float3 diffBSDFEstimate, specBSDFEstimate;
            BSDFProperties bsdfProperties = bridgedData.bsdf.getProperties(bridgedData.sd);
            bsdfProperties.estimateSpecDiffBSDF(diffBSDFEstimate, specBSDFEstimate, bridgedData.sd.N, bridgedData.sd.V);
            
            // diffBSDFEstimate = lerp( diffBSDFEstimate, 0.5.xxx, passthroughOverride * 0.5 );
            // specBSDFEstimate = lerp( specBSDFEstimate, 0.5.xxx, passthroughOverride * 0.5 );

            auxContext.stablePlanes.StoreStablePlane(currentSPIndex, vertexIndex, path.hitPacked, SERSortKey, path.stableBranchID, rayDir, path.sceneLength, path.thp, 
                motionVectors, roughness, worldNormal, diffBSDFEstimate, specBSDFEstimate, path.hasFlag(PathFlags::stablePlaneOnDominantBranch));
            
            // since we're building the planes and we've found a base plane, terminate here and the nextHit contains logic for continuing from other split paths if any (enqueued with .StoreExplorationStart)
            path.terminate();
        }
#elif STABLE_PLANES_MODE==STABLE_PLANES_NOISY_PASS // fill
        
        const int bouncesFromStablePlane = path.getCounter(PackedCounters::BouncesFromStablePlane);
        if (auxContext.ptConsts.useReSTIRGI && bouncesFromStablePlane == 1 && path.hasFlag(PathFlags::stablePlaneOnDominantBranch))
        {
            // Store the position and orientation of the first secondary vertex for ReSTIR GI
            Bridge::StoreSecondarySurfacePositionAndNormal(PathIDToPixel(path.id), bridgedData.sd.posW, bridgedData.sd.N);

            // // useful to show where secondary surface pos & normal were stored
            // if (auxContext.debug.IsDebugPixel())
            //     auxContext.debug.Print(path.getVertexIndex(), bridgedData.sd.posW);
        }

        path.denoiserSampleHitTFromPlane = StablePlaneAccumulateSampleHitT( path.denoiserSampleHitTFromPlane, rayTCurrent, bouncesFromStablePlane, path.isDeltaOnlyPath() );

        //if (auxContext.debug.IsDebugPixel())
        //    auxContext.debug.Print(path.getVertexIndex(), path.getStablePlaneIndex(), bouncesFromStablePlane, path.denoiserSampleHitTFromPlane, path.stableBranchID);

        if (!path.hasFlag(PathFlags::stablePlaneOnBranch)) // not on stable branch, we need to capture emission
        {
            path.secondaryL += surfaceEmission * path.thp;
        }
#endif
    }

#if STABLE_PLANES_MODE==STABLE_PLANES_NOISY_PASS // fill only
    static void StablePlanesOnScatter(inout PathState path, const BSDFSample bs, const AUXContext auxContext)
    {
        const bool wasOnStablePlane = path.hasFlag(PathFlags::stablePlaneOnPlane);
        if( wasOnStablePlane ) // if we previously were on plane, remember the first scatter type
        {
            path.setFlag(PathFlags::stablePlaneBaseScatterDiff, (bs.lobe & (uint)LobeType::Diffuse)!=0);
            //path.setFlag(PathFlags::stablePlaneBaseTransmission, (bs.isLobe(LobeType::Transmission))!=0);
        }
        path.setFlag(PathFlags::stablePlaneOnPlane, false);     // assume we're no longer going to be on stable plane

        const uint nextVertexIndex = path.getVertexIndex()+1;   // since below we're updating states for the next surface hit, we're using the next one

        // update stableBranchID if we're still on delta paths, and make sure we're still on a path (this effectively predicts the future hit based on pre-generated branches)
        if (path.hasFlag(PathFlags::stablePlaneOnBranch) && nextVertexIndex <= cStablePlaneMaxVertexIndex)
        {
            path.stableBranchID = StablePlanesAdvanceBranchID( path.stableBranchID, bs.getDeltaLobeIndex() );
            bool onStablePath = false;
            for (uint spi = 0; spi < cStablePlaneCount; spi++)
            {
                const uint planeBranchID = auxContext.stablePlanes.GetBranchID(spi);
                if (planeBranchID == cStablePlaneInvalidBranchID)
                    continue;

                // changing the stable plane for the future
                if (StablePlaneIsOnPlane(planeBranchID, path.stableBranchID))
                {
                    auxContext.stablePlanes.CommitDenoiserRadiance(path.getStablePlaneIndex(), path.denoiserSampleHitTFromPlane,
                        path.denoiserDiffRadianceHitDist, path.denoiserSpecRadianceHitDist,
                        path.secondaryL, path.hasFlag(PathFlags::stablePlaneBaseScatterDiff),
                        path.hasFlag(PathFlags::stablePlaneOnDeltaBranch),
                        path.hasFlag(PathFlags::stablePlaneOnDominantBranch));
                    path.setFlag(PathFlags::stablePlaneOnPlane, true);
                    path.setFlag(PathFlags::stablePlaneOnDeltaBranch, false);
                    path.setStablePlaneIndex(spi);
                    path.setFlag(PathFlags::stablePlaneOnDominantBranch, spi == auxContext.stablePlanes.LoadDominantIndex());
                    path.setCounter(PackedCounters::BouncesFromStablePlane, 0);
                    path.denoiserSampleHitTFromPlane    = 0.0;
                    path.denoiserDiffRadianceHitDist    = float4(0,0,0,0);
                    path.denoiserSpecRadianceHitDist    = float4(0,0,0,0);
                    path.secondaryL                     = 0;
                    onStablePath = true;
                    break;
                }

                const uint planeVertexIndex = StablePlanesVertexIndexFromBranchID(planeBranchID);

                onStablePath |= StablePlaneIsOnStablePath(planeBranchID, StablePlanesVertexIndexFromBranchID(planeBranchID), path.stableBranchID, nextVertexIndex);
            }
            path.setFlag(PathFlags::stablePlaneOnBranch, onStablePath);
        }
        else
        {
            // if we fell off the path, we stay on the last stable plane index and just keep depositing radiance there
            path.stableBranchID = cStablePlaneInvalidBranchID;
            path.setFlag(PathFlags::stablePlaneOnBranch, false);
            path.incrementCounter(PackedCounters::BouncesFromStablePlane); 
        }
        if (!path.hasFlag(PathFlags::stablePlaneOnPlane))
            path.incrementCounter(PackedCounters::BouncesFromStablePlane);
        // if (auxContext.debug.IsDebugPixel())
        //     auxContext.debug.Print(nextVertexIndex, path.getCounter(PackedCounters::BouncesFromStablePlane), path.hasFlag(PathFlags::stablePlaneOnPlane));
    }

    static void StablePlanesHandleNEE(inout PathState path, float3 diffuseRadiance, float3 specularRadiance, float sampleDistance, const AUXContext auxContext)
    {
        uint stablePlaneIndex = path.getStablePlaneIndex();

        const int bouncesFromStablePlane = 1+path.getCounter(PackedCounters::BouncesFromStablePlane); // since it's NEE, it's +1!
        float accSampleDistance = StablePlaneAccumulateSampleHitT( path.denoiserSampleHitTFromPlane, sampleDistance, bouncesFromStablePlane, false );

        //if (auxContext.debug.IsDebugPixel())
        //    auxContext.debug.Print(path.getVertexIndex(), bouncesFromStablePlane, path.denoiserSampleHitTFromPlane, accSampleDistance );

        if (path.hasFlag(PathFlags::stablePlaneOnPlane)) // we cound use bouncesFromStablePlane==0 for perf here, but needs testing
            //(path.getVertexIndex() == captureVertexIndex)
        {
            diffuseRadiance *= path.thp; // account for the path throughput: have to do it here since we're adding samples at different path vertices
            specularRadiance *= path.thp; // account for the path throughput: have to do it here since we're adding samples at different path vertices

            // this is the NEE at the denoising base surface - we've got separate inputs for diffuse and specular lobes, so capture them separately
            // there is no need to do StablePlaneCombineWithHitTCompensation - this always happens first
            path.denoiserDiffRadianceHitDist = float4( diffuseRadiance, accSampleDistance );
            path.denoiserSpecRadianceHitDist = float4( specularRadiance, accSampleDistance );
        }
        else //if( !(auxContext.ptConsts.stablePlanesSkipIndirectNoiseP0 && path.getStablePlaneIndex() == 0 && (bouncesFromStablePlane)>2) )
        {
            // this is one of the subsequent bounces past the denoising surface - it got scattered through a diffuse or specular lobe so we use that to determine where to capture
            float3 neeSum = diffuseRadiance+specularRadiance;
            path.secondaryL += neeSum * path.thp;
        }
    }
#endif

    static bool StablePlanesHandleMiss(inout PathState path, float3 emission, const float3 rayOrigin, const float3 rayDir, const float rayTCurrent, uint SERSortKey, const PathTracerParams params, const AUXContext auxContext)
    {
#if STABLE_PLANES_MODE==STABLE_PLANES_BUILD_PASS // build
        const uint vertexIndex = path.getVertexIndex();
        if (vertexIndex == 1)
            auxContext.stablePlanes.StoreFirstHitRayLengthAndClearDominantToZero(kMaxRayTravel);
        float3 motionVectors = Bridge::computeSkyMotionVector(auxContext.pixelPos);
        auxContext.stablePlanes.StoreStablePlane(path.getStablePlaneIndex(), vertexIndex, path.hitPacked, SERSortKey, path.stableBranchID, rayDir, path.sceneLength, path.thp, 
            motionVectors, 1, -rayDir, 1.xxx, 1.xxx, path.hasFlag(PathFlags::stablePlaneOnDominantBranch));
        return true; // collect the emission!
#elif STABLE_PLANES_MODE==STABLE_PLANES_NOISY_PASS // fill

        const int bouncesFromStablePlane = path.getCounter(PackedCounters::BouncesFromStablePlane);
        if (auxContext.ptConsts.useReSTIRGI && bouncesFromStablePlane == 1 && path.hasFlag(PathFlags::stablePlaneOnDominantBranch))
        {
            // Imagine a position and orientation for the environment and store it for ReSTIR GI
            const float3 worldPos = rayOrigin + rayDir * kMaxSceneDistance;
            const float3 normal = -rayDir;
            Bridge::StoreSecondarySurfacePositionAndNormal(PathIDToPixel(path.id), worldPos, normal);

            // // useful to show where secondary surface pos & normal were stored
            //  if (auxContext.debug.IsDebugPixel())                        
            //      auxContext.debug.Print(path.getVertexIndex(), worldPos);
        }

        path.denoiserSampleHitTFromPlane = StablePlaneAccumulateSampleHitT( path.denoiserSampleHitTFromPlane, kMaxSceneDistance, bouncesFromStablePlane, path.isDeltaOnlyPath() );

        //if (auxContext.debug.IsDebugPixel())
        //    auxContext.debug.Print(path.getVertexIndex(), path.getStablePlaneIndex(), bouncesFromStablePlane, path.denoiserSampleHitTFromPlane, 42);

        if (!path.hasFlag(PathFlags::stablePlaneOnBranch)) // not on stable branch, we need to capture emission
        {
            path.secondaryL += emission * path.thp;
        }
        return false; // tell path tracer that we're handling this radiance (or it was handled in MODE==1)
#endif
    }
#endif


};

// used only for debug visualization
#if STABLE_PLANES_MODE==STABLE_PLANES_BUILD_PASS && ENABLE_DEBUG_DELTA_TREE_VIZUALISATION
void DeltaTreeVizHandleMiss(inout PathState path, const float3 rayOrigin, const float3 rayDir, const float rayTCurrent, const PathTracerParams params, const AUXContext auxContext)
{
    if (path.hasFlag(PathFlags::deltaTreeExplorer))
    {
        DeltaTreeVizPathVertex info = DeltaTreeVizPathVertex::make(path.getVertexIndex(), path.stableBranchID, 0xFFFFFFFF, path.thp, 0.0, rayOrigin + rayDir * rayTCurrent, path.hasFlag(PathFlags::stablePlaneOnDominantBranch)); // empty info for sky
        auxContext.debug.DeltaTreeVertexAdd( info );
        //auxContext.debug.Print( 0, float4( path.thp, path.sceneLength ) );
        return;
    }
}
void DeltaTreeVizHandleHit(inout PathState path, const float3 rayOrigin, const float3 rayDir, const float rayTCurrent, const SurfaceData bridgedData, bool rejectedFalseHit, bool hasFinishedSurfaceBounces, float volumeAbsorption, const PathTracerParams params, const AUXContext auxContext)
{
    uint vertexIndex = path.getVertexIndex();
    if (rejectedFalseHit)
    {
        // just continue - it has already been updated with an offset
        PathPayload packedPayload = PathPayload::pack(path);
        auxContext.debug.DeltaSearchStackPush(packedPayload);
    }
    else
    {
        DeltaTreeVizPathVertex info = DeltaTreeVizPathVertex::make(vertexIndex, path.stableBranchID, bridgedData.sd.materialID, path.thp, volumeAbsorption, bridgedData.sd.posW, path.hasFlag(PathFlags::stablePlaneOnDominantBranch));

        bridgedData.bsdf.evalDeltaLobes(bridgedData.sd, info.deltaLobes, info.deltaLobeCount, info.nonDeltaPart);

        // use deltaTreeContinueRecursion to give up on searching after buffers are filled in; can easily happen in complex meshes with clean reflection/transmission materials
        bool deltaTreeContinueRecursion = auxContext.debug.DeltaTreeVertexAdd( info );
        deltaTreeContinueRecursion &= vertexIndex <= cStablePlaneMaxVertexIndex;

        if (!hasFinishedSurfaceBounces)
        {
            for (int i = info.deltaLobeCount-1; (i >= 0) && deltaTreeContinueRecursion; i--) // reverse-iterate to un-reverse outputs
            {
                DeltaLobe lobe = info.deltaLobes[i];

                if (luminance(path.thp*lobe.thp)>cDeltaTreeVizThpIgnoreThreshold)
                {
                    PathState deltaPath = PathTracer::splitDeltaPath(path, rayDir, bridgedData, lobe, i, false, auxContext);
                    deltaPath.incrementCounter(PackedCounters::BouncesFromStablePlane); 

                    // update stable plane index state
                    deltaPath.setFlag(PathFlags::stablePlaneOnPlane, false); // assume we're no longer on stable plane
                    if (deltaPath.getVertexIndex() <= cStablePlaneMaxVertexIndex)
                        for (uint spi = 0; spi < cStablePlaneCount; spi++)
                        {
                            const uint planeBranchID = auxContext.stablePlanes.GetBranchID(spi);
                            if (planeBranchID != cStablePlaneInvalidBranchID && StablePlaneIsOnPlane(planeBranchID, deltaPath.stableBranchID))
                            {
                                deltaPath.setFlag(PathFlags::stablePlaneOnPlane, true);
                                deltaPath.setStablePlaneIndex(spi);
                                deltaPath.setCounter(PackedCounters::BouncesFromStablePlane, 0);

                                // picking dominant flag from the actual build pass stable planes to be faithful debug for the StablePlanes system, which executed before this
                                const uint stablePlaneIndex = deltaPath.getStablePlaneIndex();
                                const uint dominantSPIndex = auxContext.stablePlanes.LoadDominantIndex();
                                deltaPath.setFlag(PathFlags::stablePlaneOnDominantBranch, stablePlaneIndex == dominantSPIndex && deltaPath.hasFlag(PathFlags::stablePlaneOnPlane) );
                            }
                        }
                    
                    deltaTreeContinueRecursion &= auxContext.debug.DeltaSearchStackPush(PathPayload::pack(deltaPath));
                    //    auxContext.debug.Print(vertexIndex * 2 + i, deltaTreeContinueRecursion);
                }
            }
        }
    }
}
#endif

#endif // __PATH_TRACER_HLSLI__
