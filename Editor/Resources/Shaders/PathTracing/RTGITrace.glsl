// --------------------------------------------------
// -- Lux ray-traced GI: per-pixel indirect trace  --
// --------------------------------------------------
//
// One invocation per pixel. A primary ray finds the visible surface in the ray-traced scene (so no G-buffer
// decode is needed), then ONE cosine-weighted indirect path (direct light by next-event estimation at its
// hit, plus bounces) estimates the mean incident radiance (irradiance / pi). The estimate is blended into a
// per-pixel history (an exponential mean that restarts whenever the camera, lights or scene change, so it
// cannot ghost), then RTGIFilter.glsl denoises it and adds albedo * (1 - metalness) * it to scene colour
// as indirect diffuse. See docs/GI_PLAN.md, GI5.
//
#version 460 core
#pragma stage : comp
#extension GL_EXT_ray_query : require
#extension GL_EXT_nonuniform_qualifier : require

#include <RTGITraceCommon.glslh>
