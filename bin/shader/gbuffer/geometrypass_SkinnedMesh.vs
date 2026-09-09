#version 430 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;
layout (location = 5) in ivec4 aBoneIds[2]; 
layout (location = 7) in vec4 aWeights[2];

layout (location = 0) out vec3 FragPos;
layout (location = 1) out vec3 FragNormal;	
layout (location = 2) out vec2 FragTextureCoords;
layout (location = 3) out mat3 TBN;
layout (location = 6) out vec2 MotionVector;
layout (location = 7) flat out uint materialIndex;

#include "shader/dataDef/camerauboDef.comp"
#include "shader/Helper/animationHelper.comp"

struct RenderData
{
	mat4 model;
	mat4 prevModel;
	uint materialIndex;
};

layout(push_constant) uniform PushConsts {
	RenderData renderdata;
};


void main()
{

	mat4 model = renderdata.model;
	mat4 prevModel = renderdata.prevModel;
	materialIndex = renderdata.materialIndex;

    CalucateResult data = CalculateIfHasAnimationData(vec4(aPos, 1.0f), aNormal, aTangent, aBitangent);

    vec4 worldPos = model * data.pos;
	FragPos = worldPos.xyz;

	mat3 normalMatrix = transpose(inverse(mat3(model)));
	vec3 T = normalize(normalMatrix * data.tangent  );
	vec3 B = normalize(normalMatrix * data.bitangent);
	vec3 N = normalize(normalMatrix * data.normal	);

	vec4 clipPos = camera.projView * worldPos;

    vec4 prevPos = CalculatePrevIfHasAnimationData(vec4(aPos, 1.0f));
	vec4 prevWorldPos = prevModel * prevPos;
	vec4 prevClipPos = prevcamera.projView * prevWorldPos;

	// 转 NDC 算 UV 差值
    vec2 uv_curr = clipPos.xy / clipPos.w * 0.5 + 0.5;
    vec2 uv_prev = prevClipPos.xy / prevClipPos.w * 0.5 + 0.5;


	TBN = mat3(T, B, N);
	FragNormal = N;
	gl_Position = clipPos;
	FragTextureCoords = aTexCoords;
    MotionVector = uv_curr - uv_prev;
}
