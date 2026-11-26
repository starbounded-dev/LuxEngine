// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#version 460 core
#pragma stage : rgen
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_query : require
#extension GL_EXT_shader_invocation_reorder : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require

#define LUX_PATH_TRACE_SER
#include <PathTracePixel.glslh>

void main()
{
	PathTracePixel(gl_LaunchIDEXT.xy);
}

#version 460 core
#pragma stage : rmiss
#extension GL_EXT_ray_tracing : require
#include <PathTraceHitPayload.glslh>

layout(location = 0) rayPayloadInEXT PathTraceHitPayload g_Hit;

void main()
{
	g_Hit.Instance = 0xffffffffu;
}

#version 460 core
#pragma stage : rchit
#extension GL_EXT_ray_tracing : require
#include <PathTraceHitPayload.glslh>

layout(location = 0) rayPayloadInEXT PathTraceHitPayload g_Hit;
hitAttributeEXT vec2 g_Barycentric;

void main()
{
	g_Hit.Barycentric = g_Barycentric;
	g_Hit.Distance = gl_HitTEXT;
	g_Hit.Primitive = uint(gl_PrimitiveID);
	g_Hit.Instance = uint(gl_InstanceCustomIndexEXT);
}
