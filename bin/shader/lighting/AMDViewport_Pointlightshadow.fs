#version 460 core

struct LightProp{
    vec3 lightPos;
    float farPlane;
};

layout(std430, binding = 6) buffer LightProps
{
	LightProp lightProp[];
};

flat in int Index;
in vec3 WorldPos;

void main()
{
    float lightDistance = length(WorldPos - lightProp[Index].lightPos);
    gl_FragDepth = lightDistance / lightProp[Index].farPlane;
}