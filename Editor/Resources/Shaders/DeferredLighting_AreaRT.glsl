// Area lights over deferred lighting: a full-screen pass blended additively into scene color, only
// in the graph on frames with area lights (SceneRenderer::AreaLightPass).
#version 460 core
#pragma stage : vert

#include <DeferredAreaVertex.glslh>

#version 460 core
#pragma stage : frag

#extension GL_EXT_ray_query : require
#define LUX_RT_AREA_SHADOWS 1
#include <DeferredAreaLighting.glslh>
