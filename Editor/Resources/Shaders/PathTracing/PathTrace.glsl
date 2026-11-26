// -------------------------------------------------
// -- Lux path tracer (editor reference renderer) --
// -------------------------------------------------
//
// Unidirectional path tracer over a TLAS using ray queries (GL_EXT_ray_query), one pixel per
// invocation, accumulating a running mean in an RGBA32F image and writing the HDR result to an
// RGBA16F image the renderer copies into scene colour. Opt-in (SceneRendererOptions::EnablePathTracer);
// editor-only; see docs/GI_PLAN.md.
//
// Lighting: next-event estimation to one light per bounce (directional, point, spot, rect area),
// BSDF-sampled continuation (cosine diffuse + GGX specular mixture), constant sky/ground environment on
// escape. BRDF terms follow PBR.glslh (GGX + correlated Smith + Frostbite diffuse) so a path-traced
// frame and the raster frame agree on direct lighting. Textures are not sampled: albedo is the
// material's constant colour (a documented v1 limit).
//
#version 460 core
#pragma stage : comp
#extension GL_EXT_ray_query : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include <PathTracePixel.glslh>

void main()
{
	PathTracePixel(gl_GlobalInvocationID.xy);
}
