#version 430 core

uniform vec3 lightPos[16];
uniform float farPlane[16];

in vec4 FragPos;
flat in int lightIndex;

void main()
{
    float lightDistance = length(FragPos.xyz - lightPos[lightIndex]);
    gl_FragDepth = lightDistance / farPlane[lightIndex];
}