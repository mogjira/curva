#version 460

layout (isolines) in;

layout(location = 0) in vec3 color[];
layout(location = 0) out vec3 outColor;

layout(location = 1) patch in vec4 p_1;
layout(location = 2) patch in vec4 p2;

void main()
{    
    const float u = gl_TessCoord.x;

    const vec4 p0 = gl_in[0].gl_Position;
    const vec4 p1 = gl_in[1].gl_Position;

    const float b0 = (-1.f * u) + (2.f * u * u) + (-1.f * u * u * u);
    const float b1 = (2.f) + (-5.f * u * u) + (3.f * u * u * u);
    const float b2 = (u) + (4.f * u * u) + (-3.f * u * u * u);
    const float b3 = (-1.f * u * u) + (u * u * u);

    const vec4 o = 0.5 * (b0 * p_1 + b1 * p0 + b2 * p1 + b3 * p2);
    gl_Position = o;
    outColor = vec3(1, 0, 0);
}
