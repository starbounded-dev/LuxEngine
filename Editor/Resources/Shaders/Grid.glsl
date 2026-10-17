// Editor floor grid
//
// An anti-aliased grid on the y = 0 plane, drawn as a square around the camera that ends in a
// circular fade. Lines keep a constant on-screen width and fade to their average coverage once they
// get finer than a pixel (the "pristine grid" formulation, Ben Golus 2023), so the grid never shimmers
// or turns to moire. The cell size steps by powers of ten with the camera's distance, crossfading
// between neighbouring scales, and the world X and Z axes are drawn in their own colours.

#version 450 core
#pragma stage : vert
#include <Buffers.glslh>

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TexCoord;

layout(location = 0) out vec2 v_WorldXZ;

layout(push_constant) uniform Settings
{
	vec4 XAxisColor;
	vec4 ZAxisColor;
	vec4 MinorColor;
	vec4 MajorColor;
	float BaseCellSize;      // world size of the finest cell (metres)
	float FadeDistance;      // the grid fades out by this distance from the camera
	float AxisLineWidthPx;
	float MinorLineWidthPx;
	float MajorLineWidthPx;
} u_Settings;

void main()
{
	// The unit quad becomes a square on the floor centred under the camera.
	vec3 cameraPosition = u_Camera.InverseViewMatrix[3].xyz;
	vec2 worldXZ = cameraPosition.xz + a_Position.xy * u_Settings.FadeDistance;

	v_WorldXZ = worldXZ;
	gl_Position = u_Camera.ViewProjectionMatrix * vec4(worldXZ.x, 0.0, worldXZ.y, 1.0);
}

#version 450 core
#pragma stage : frag
#include <Buffers.glslh>

layout(location = 0) in vec2 v_WorldXZ;
layout(location = 0) out vec4 o_Color;

layout(push_constant) uniform Settings
{
	vec4 XAxisColor;
	vec4 ZAxisColor;
	vec4 MinorColor;
	vec4 MajorColor;
	float BaseCellSize;
	float FadeDistance;
	float AxisLineWidthPx;
	float MinorLineWidthPx;
	float MajorLineWidthPx;
} u_Settings;

// Minor cells are kept at least this many pixels wide; below that the next scale takes over.
const float k_MinMinorCellPixels = 60.0;

// Coverage of grid lines at integer values of uv, widthPx pixels wide. uvPerPixel is the screen-space
// rate of change of uv, passed in so derivatives are only taken in uniform control flow.
float GridLines(vec2 uv, vec2 uvPerPixel, float widthPx)
{
	vec2 targetWidth = uvPerPixel * widthPx;
	vec2 drawWidth = clamp(targetWidth, uvPerPixel, vec2(0.5));
	vec2 antialias = uvPerPixel * 1.5;
	vec2 lineDistance = 1.0 - abs(fract(uv) * 2.0 - 1.0);

	vec2 lines = 1.0 - smoothstep(drawWidth - antialias, drawWidth + antialias, lineDistance);
	lines *= clamp(targetWidth / drawWidth, 0.0, 1.0);
	// Below a pixel per cell, fade to the lines' average coverage instead of aliasing.
	lines = mix(lines, targetWidth, clamp(uvPerPixel * 2.0 - 1.0, 0.0, 1.0));
	return max(lines.x, lines.y);
}

// Coverage of one line along coordinate == 0.
float AxisLine(float coordinate, float coordinatePerPixel, float widthPx)
{
	float pixels = abs(coordinate) / max(coordinatePerPixel, 1e-8);
	return 1.0 - smoothstep(widthPx * 0.5 - 0.5, widthPx * 0.5 + 0.5, pixels);
}

vec4 GridScale(vec2 position, vec2 positionPerPixel, float cellSize)
{
	float minor = GridLines(position / cellSize, positionPerPixel / cellSize, u_Settings.MinorLineWidthPx);
	float major = GridLines(position / (cellSize * 10.0), positionPerPixel / (cellSize * 10.0), u_Settings.MajorLineWidthPx);

	vec4 color = u_Settings.MinorColor;
	color.a *= minor;
	vec4 majorColor = u_Settings.MajorColor;
	majorColor.a *= major;
	return mix(color, majorColor, majorColor.a);
}

void main()
{
	vec2 position = v_WorldXZ;
	vec2 positionPerPixel = vec2(length(vec2(dFdx(position.x), dFdy(position.x))),
	                             length(vec2(dFdx(position.y), dFdy(position.y))));

	vec3 cameraPosition = u_Camera.InverseViewMatrix[3].xyz;
	float radialDistance = length(position - cameraPosition.xz);
	if (radialDistance >= u_Settings.FadeDistance)
		discard;

	// Pick the power-of-ten cell size that keeps minor cells k_MinMinorCellPixels wide, and crossfade
	// to the next one up.
	float metresPerPixel = max(positionPerPixel.x, positionPerPixel.y);
	float scaleLevel = log(max(metresPerPixel * k_MinMinorCellPixels / u_Settings.BaseCellSize, 1e-6)) / log(10.0);
	float level = floor(scaleLevel);
	float cellSize = u_Settings.BaseCellSize * pow(10.0, level);
	vec4 color = mix(GridScale(position, positionPerPixel, cellSize),
	                 GridScale(position, positionPerPixel, cellSize * 10.0),
	                 smoothstep(0.0, 1.0, scaleLevel - level));

	// World X axis runs along z = 0, world Z axis along x = 0.
	float xAxis = AxisLine(position.y, positionPerPixel.y, u_Settings.AxisLineWidthPx) * u_Settings.XAxisColor.a;
	float zAxis = AxisLine(position.x, positionPerPixel.x, u_Settings.AxisLineWidthPx) * u_Settings.ZAxisColor.a;
	color.rgb = mix(color.rgb, u_Settings.XAxisColor.rgb, xAxis);
	color.a = xAxis + color.a * (1.0 - xAxis);
	color.rgb = mix(color.rgb, u_Settings.ZAxisColor.rgb, zAxis);
	color.a = zAxis + color.a * (1.0 - zAxis);

	// Fade with distance, and as the view grazes the floor (the plane collapses to a strip).
	float distanceFade = 1.0 - smoothstep(u_Settings.FadeDistance * 0.5, u_Settings.FadeDistance, radialDistance);
	vec3 toFragment = vec3(position.x, 0.0, position.y) - cameraPosition;
	float grazingFade = (u_Camera.IsOrthographic != 0)
		? smoothstep(0.02, 0.25, abs(u_Camera.InverseViewMatrix[2].y))
		: smoothstep(0.02, 0.25, abs(cameraPosition.y) / max(length(toFragment), 1e-5));
	color.a *= distanceFade * grazingFade;

	if (color.a < 1e-3)
		discard;

	o_Color = color;
}
