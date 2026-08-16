#version 450 core
out vec4 FragColor;

in vec3 FragTextureCoords;

uniform samplerCube skybox;

void main()
{
	FragColor = texture(skybox, FragTextureCoords);
}