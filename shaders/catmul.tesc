#version 460
#define OUT_COUNT 2

layout (vertices = OUT_COUNT) out;

layout(location = 0) in vec3 color[];
layout(location = 0) out vec3 outColor[OUT_COUNT];

layout(location = 1) patch out vec4 p_1;
layout(location = 2) patch out vec4 p2;

void main()
{
        if(gl_InvocationID == 0) 
        {
            gl_TessLevelOuter[0] = float(1);
            gl_TessLevelOuter[1] = float(64);

            p_1 = gl_in[0].gl_Position;
            p2 = gl_in[3].gl_Position;
        }

        if(gl_InvocationID == 0) 
        {
                gl_out[gl_InvocationID].gl_Position = gl_in[1].gl_Position;
        }

        if(gl_InvocationID == 1) 
        {
                gl_out[gl_InvocationID].gl_Position = gl_in[2].gl_Position;
        }
}
