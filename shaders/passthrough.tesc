#version 460

layout (vertices = 3) out;

layout(location = 0) in vec3 color[];

layout(location = 0) out vec3 outColor[3];

void main()
{
    gl_TessLevelOuter[0] = 1.0;
    gl_TessLevelOuter[1] = 64.0;
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    outColor[gl_InvocationID] = color[gl_InvocationID];
}
