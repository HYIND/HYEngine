#version 450 core

out vec4 FragColor;

uniform vec3 lightColor;
uniform float Intensity;


void main()
{
	FragColor = vec4(lightColor, 1.0f);		// ��ɫ��
}