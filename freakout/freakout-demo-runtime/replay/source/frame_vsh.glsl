#version 460
layout(location = 0) in vec4 position;
layout(location = 1) in vec2 texcoord;
layout(location = 2) in vec4 diffuse;
layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 tint;

void main()
{
    gl_Position = position;
    uv = texcoord;
    tint = diffuse;
}
