#version 430 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;
layout (location = 5) in ivec4 aBoneIds[2]; 
layout (location = 7) in vec4 aWeights[2];

out vec3 FragPos;
out vec3 FragNormal;	
out vec2 FragTextureCoords;
out mat3 TBN;

#include "shader/dataDef/camerauboDef.comp"
#include "shader/Helper/animationHelper.comp"

uniform mat4 model;

void main()
{
	FragTextureCoords = aTexCoords;

    CalucateResult data = CalculateIfHasAnimationData(vec4(aPos, 1.0f), aNormal, aTangent, aBitangent);

    vec4 worldPos = model * data.pos;
	FragPos = worldPos.xyz;

	mat3 normalMatrix = transpose(inverse(mat3(model)));
	vec3 T = normalize(normalMatrix * data.tangent  );
	vec3 B = normalize(normalMatrix * data.bitangent);
	vec3 N = normalize(normalMatrix * data.normal	);

	TBN = mat3(T, B, N);
	FragNormal = N;

	gl_Position = camera.projection * camera.view * worldPos;
} 
