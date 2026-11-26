#version 450 core
#pragma stage:vert

#include <GBufferStaticVertex.glslh>

#version 450 core

#extension GL_EXT_nonuniform_qualifier : enable

#pragma stage : frag

#define LUX_VIRTUAL_TEXTURE_FEEDBACK
#include <GBufferStaticFragment.glslh>
