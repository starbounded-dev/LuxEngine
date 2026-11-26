// Area lights over deferred lighting: a full-screen pass blended additively into scene color, only
// in the graph on frames with area lights (SceneRenderer::AreaLightPass).
#version 450 core
#pragma stage : vert

#include <DeferredAreaVertex.glslh>

#version 450 core
#pragma stage : frag

#include <DeferredAreaLighting.glslh>
