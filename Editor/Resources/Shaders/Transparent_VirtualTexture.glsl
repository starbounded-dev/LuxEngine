/*// -- Hazel Engine PBR shader --
// -----------------------------
// Note: this shader is still very much in progress. There are likely many bugs and future additions that will go in.
//       Currently heavily updated.
//
// References upon which this is based:
// - Unreal Engine 4 PBR notes (https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf)
// - Frostbite's SIGGRAPH 2014 paper (https://seblagarde.wordpress.com/2015/07/14/siggraph-2014-moving-frostbite-to-physically-based-rendering/)
// - Michał Siejak's PBR project (https://github.com/Nadrin)
// - My implementation from years ago in the Sparky engine (https://github.com/TheCherno/Sparky)
*/
#version 450 core
#pragma stage:vert

#include <PBRTransparentVertex.glslh>

#version 450 core

#extension GL_EXT_nonuniform_qualifier : enable

#pragma stage : frag

#define LUX_VIRTUAL_TEXTURE_FEEDBACK
#include <PBRTransparentFragment.glslh>
