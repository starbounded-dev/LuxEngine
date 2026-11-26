#version 460
#extension GL_EXT_ray_query : require
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform accelerationStructureEXT scene;
layout(std430, set = 0, binding = 1) writeonly buffer Visibility { uint hits[]; };

float alphaAt(vec2 uv)
{
    vec2 texel = clamp(uv * 64.0 - 0.5, 0.0, 63.0);
    vec2 lower = floor(texel);
    vec2 lowerAlpha = step(8.0, lower) * (1.0 - step(56.0, lower));
    vec2 upperAlpha = step(8.0, lower + 1.0) * (1.0 - step(56.0, lower + 1.0));
    vec2 alpha = mix(lowerAlpha, upperAlpha, fract(texel));
    return alpha.x * alpha.y;
}

void main()
{
    uvec2 p = gl_GlobalInvocationID.xy;
    vec2 uv = (vec2(p) + vec2(0.2, 0.3)) / 1024.0;
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene, gl_RayFlagsTerminateOnFirstHitEXT, 0xff,
        vec3(uv, 1.0), 0.001, vec3(0.0, 0.0, -1.0), 2.0);
    while (rayQueryProceedEXT(query))
        if (alphaAt(uv) >= 0.5)
            rayQueryConfirmIntersectionEXT(query);
    hits[p.y * 1024u + p.x] = uint(rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT);
}
