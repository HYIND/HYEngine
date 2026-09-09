#version 460 core

struct LightProp{
    vec3 lightPos;
    float farPlane;
};

layout(set = 0, binding = 6) buffer LightProps
{
	LightProp lightProp[];
};

layout (location = 0) flat in int Index;
layout (location = 1) in vec3 WorldPos;

void main()
{
    float lightDistance = length(WorldPos - lightProp[Index].lightPos);
    gl_FragDepth = lightDistance / lightProp[Index].farPlane;
}