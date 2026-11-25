#version 460
layout(location = 0) out vec2 uv;

void main()
{
    vec2 corner = vec2(float((gl_VertexID & 1) * 2), float((gl_VertexID & 2)));
    uv = corner;
    gl_Position = vec4(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);
}
