#version 330 core							// 3.30版本
layout (location = 0) in vec3 position;		
layout (location = 1) in vec3 normal;
layout (location = 2) in vec2 textureCoords;
layout (location = 3) in vec3 tangent;
layout (location = 4) in vec3 bitangent;

out vec2 FragTextureCoords;		// 将纹理坐标传到片元着色器
out vec3 FragNormal;			// 法向量输出通道, 将传给片元着色器
out vec3 FragPos;
out mat3 TBN;

uniform mat4 model;
uniform mat4 projection;
uniform mat4 view;

void main()
{
	FragPos = vec3(model*vec4(position, 1.0f));
	FragNormal = mat3(transpose(inverse(model))) * normalize(normal);
	FragTextureCoords = vec2(textureCoords.x, textureCoords.y);

	vec3 T = normalize(vec3(model * vec4(tangent,   0.0)));
	vec3 B = normalize(vec3(model * vec4(bitangent, 0.0)));
	vec3 N = normalize(vec3(model * vec4(normal,    0.0)));
	TBN = mat3(T, B, N);

	gl_Position = projection * view * model * vec4(position, 1.0f);
} 
