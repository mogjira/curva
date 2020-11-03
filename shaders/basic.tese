#version 460

layout (isolines, equal_spacing, cw) in;

layout(set = 0, binding = 0) uniform Matrices {
    mat4 model;
    mat4 view;
    mat4 proj;
} matrices;

vec4 plane_curve(float u)
{
	vec4 p = (2*u*u - 3*u + 1) * gl_in[0].gl_Position;
		p += (-4*u*u + 4*u) * gl_in[1].gl_Position;
		p += (2*u*u - u) * gl_in[2].gl_Position;

	return p;
}

void main()
{    
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

	vec4 pos = plane_curve( u );
    gl_Position = matrices.proj * matrices.view * matrices.model * pos;
}
