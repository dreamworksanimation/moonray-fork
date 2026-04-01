// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "LightAccelerator.isph"
#include <moonray/rendering/pbr/core/PbrTLState.isph>
#include <moonray/rendering/pbr/core/RayState.isph>

export uniform uint32_t
LightAccelerator_hudValidation(uniform bool verbose)
{
    LIGHT_ACCELERATOR_VALIDATION;
}



// The main intersection function for the Embree light acceleration structure.
// The intention is to avoid a brute-force search through the array of lights to find the closest hit.
// Only the bounded lights (cylinder, disk, rect, sphere, spot) are put in the acceleration structure.
// Unbounded lights by definition do not have a bounding volume so they must still be iterated through
// in search of a hit.

varying int
LightAccelerator_intersect(uniform PbrTLState * uniform pbrTls,
                           const uniform LightAccelerator * uniform acc,
                           const varying Vec3f &P,
                           const varying Vec3f &cullingNormal,
                           const varying Vec3f &wi,
                           varying float time, 
                           varying float maxDistance,
                           varying bool includeRayTerminationLights,
                           const varying int visibilityMask,
                           varying IntegratorSample1D &samples, 
                           varying int depth,
                           varying LightIntersection &isect, 
                           varying int &numHits,
                           const uniform int * uniform lightIdMap,
                           varying intptr_t bsdfLobeLightSet,
                           const varying Rdl2LightSetList * uniform parentLobeLightSets)
{
    MNRY_ASSERT(acc->mBoundedLightCount >= VECTOR_THRESHOLD_COUNT);
    MNRY_ASSERT(acc->mRtcScene != nullptr);
    if (acc->mBoundedLightCount < VECTOR_THRESHOLD_COUNT  ||  acc->mRtcScene == nullptr) {
        return -1;
    }

    LightIntersection boundedIsect;
    varying int boundedLightIdx = LightAccelerator_intersectBounded(acc,
                                                                    P, cullingNormal, wi, time,
                                                                    maxDistance,
                                                                    includeRayTerminationLights,
                                                                    visibilityMask,
                                                                    samples,
                                                                    depth,
                                                                    boundedIsect,
                                                                    numHits,
                                                                    lightIdMap,
                                                                    bsdfLobeLightSet,
                                                                    parentLobeLightSets);

    LightIntersection unboundedIsect;
    varying int unboundedLightIdx = LightAccelerator_intersectUnbounded(pbrTls,
                                                                        acc,
                                                                        P, wi, time,
                                                                        maxDistance,
                                                                        includeRayTerminationLights,
                                                                        visibilityMask,
                                                                        samples,
                                                                        depth,
                                                                        unboundedIsect,
                                                                        numHits,
                                                                        lightIdMap,
                                                                        bsdfLobeLightSet,
                                                                        parentLobeLightSets);

    if (unboundedLightIdx >= 0) {
        isect = unboundedIsect;
        return unboundedLightIdx;
    }

    isect = boundedIsect;
    return boundedLightIdx;
}


// A simple wrapper for the Embree intersection test against the light acceleration structure.
// Its main role is to convert between the Moonray-style parameters and the Embree Ray struct.
// Note that we don't call the Embree intersection function via Embree's ISPC API. Instead
// we call into a CPP function which itself calls the appropriate vector version.

varying int
LightAccelerator_intersectBounded(const uniform LightAccelerator * uniform acc,
                                  const varying Vec3f &P,
                                  const varying Vec3f &cullingNormal,
                                  const varying Vec3f &wi,
                                  varying float time, 
                                  varying float maxDistance,
                                  varying bool includeRayTerminationLights,
                                  const varying int visibilityMask,
                                  varying IntegratorSample1D &samples, 
                                  varying int depth,
                                  varying LightIntersection &isect, 
                                  varying int &numHits,
                                  const uniform int * uniform lightIdMap,
                                  varying intptr_t bsdfLobeLightSet,
                                  const varying Rdl2LightSetList * uniform parentLobeLightSets)
{
    RTCRayHit rayHit;
    rayHit.ray.org_x = P.x;
    rayHit.ray.org_y = P.y;
    rayHit.ray.org_z = P.z;
    rayHit.ray.tnear = 0.0f;
    rayHit.ray.dir_x = wi.x;
    rayHit.ray.dir_y = wi.y;
    rayHit.ray.dir_z = wi.z;
    rayHit.ray.time  = time;
    rayHit.ray.tfar  = maxDistance;
    rayHit.ray.mask  = visibilityMask;
    rayHit.ray.id = programIndex;
    rayHit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
    rayHit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;

    varying int iIncludeRayTerminationLights = includeRayTerminationLights;

    // ----------------------------------------------
    // Extract the parentLobeLightSets list for each lane into uniform arrays so we can 
    // pass it to the CPP intersection function. We must approach it this way, instead of 
    // passing it directly as varying like the other parameters, because the Rdl2LightSetList
    // is split into lo and hi parts. Due to SoA structure, the lo and hi parts will not be 
    // adjacent in memory for each lane, so the CPP function would not be able to reconstruct
    // the correct Rdl2LightSetList for each lane without us doing this manual extraction and repacking.

    uniform Rdl2LightSetList parentLobeLightSetsBuf[programCount];
    // Initialize all lanes to 0 in case some are inactive
    memset(parentLobeLightSetsBuf, 0, sizeof(parentLobeLightSetsBuf));
    if (parentLobeLightSets != nullptr) {
        foreach_active (lane) {
            parentLobeLightSetsBuf[lane].mNumLightSets = extract(parentLobeLightSets->mNumLightSets, lane);
            for (uniform int j = 0; j < parentLobeLightSetsBuf[lane].mNumLightSets; j++) {
                parentLobeLightSetsBuf[lane].mLightSetsPtrLo[j] = extract(parentLobeLightSets->mLightSetsPtrLo[j], lane);
                parentLobeLightSetsBuf[lane].mLightSetsPtrHi[j] = extract(parentLobeLightSets->mLightSetsPtrHi[j], lane);
            }
        }
    }
    // ----------------------------------------------

    CPP_lightIntersect(acc->mRtcScene,
                       rayHit,
                       &iIncludeRayTerminationLights,
                       &isect.data[0],
                       &isect.data[1],
                       &samples.mSequenceID,
                       &samples.mTotalSamples,
                       &samples.mSampleNumber,
                       &depth,
                       &isect.distance,
                       &numHits,
                       &isect.pdf,
                       &isect.geomID,
                       &isect.primID,
                       &cullingNormal,
                       lightIdMap,
                       &bsdfLobeLightSet,
                       parentLobeLightSetsBuf);

    if (rayHit.hit.geomID == RTC_INVALID_GEOMETRY_ID) {
        isect.distance = maxDistance;
        return -1;
    }

    isect.N.x      = rayHit.hit.Ng_x;
    isect.N.y      = rayHit.hit.Ng_y;
    isect.N.z      = rayHit.hit.Ng_z;
    isect.uv.x     = rayHit.hit.u;
    isect.uv.y     = rayHit.hit.v;
    return rayHit.hit.primID;
}



// Randomly intersect a ray against the LightAccelerator's list of unbounded lights.

varying int
LightAccelerator_intersectUnbounded(uniform PbrTLState * uniform pbrTls,
                                    const uniform LightAccelerator * uniform acc,
                                    const varying Vec3f &P,
                                    const varying Vec3f &wi,
                                    varying float time,
                                    varying float maxDistance,
                                    varying bool includeRayTerminationLights,
                                    const varying int visibilityMask,
                                    varying IntegratorSample1D &samples,
                                    varying int depth,
                                    varying LightIntersection &isect,
                                    varying int &numHits,
                                    const uniform int * uniform lightIdMap,
                                    varying intptr_t bsdfLobeLightSet,
                                    const varying Rdl2LightSetList * uniform parentLobeLightSets)
{
    isect.distance = maxDistance;

    varying int chosenLightIdx = -1;
    for (uniform int idx = 0; idx < acc->mUnboundedLightCount; idx++) {
        // skip lights that do not exist in LightSet
        if (lightIdMap[idx + acc->mBoundedLightCount] == -1) {
            continue;
        }
        const uniform Light * uniform light = acc->mUnboundedLights[idx];
        LightIntersection currentIsect;
        
        if (!isLightActive(light, visibilityMask, includeRayTerminationLights, bsdfLobeLightSet, parentLobeLightSets)) {
            continue;
        }

        Vec3f cullingNormal;
        // Unbounded lights do not need a culling normal.
        setInvalidCullingNormal(&cullingNormal);
        if (Light_intersect(light, P, cullingNormal, wi, time, maxDistance, currentIsect)) {

            numHits++;

            if (chooseThisLight(pbrTls, samples, depth, numHits)) {
                chosenLightIdx = idx + acc->mBoundedLightCount;
                isect = currentIsect;
            }
        }
    }

    return chosenLightIdx;
}

