#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;
layout (location = 0) out vec3 FragPos;
layout (location = 1) out vec3 FragNormal;	
layout (location = 2) out vec2 FragTextureCoords;
layout (location = 3) out mat3 TBN;
// 运动矢量在片元着色器里算：透视除法之后的屏幕坐标对三角形不是线性的，
// 在顶点里先除再插值，靠近相机的大三角形（地面）会偏。这里只传裁剪坐标，插值后再除。
layout (location = 6) out vec4 CurClipPos;
layout (location = 8) out vec4 PrevClipPos;
layout (location = 7) flat out uint materialIndex;

#include "shader/dataDef/camerauboDef.comp"

struct RenderData
{
	mat4 model;
	mat4 prevModel;
	uint materialIndex;
};

layout(set = 0, binding = 2) buffer RenderDatas
{
	RenderData renderdata[];
};

void main()
{

	mat4 model = renderdata[gl_BaseInstance].model;
	mat4 prevModel = renderdata[gl_BaseInstance].prevModel;
	materialIndex = renderdata[gl_BaseInstance].materialIndex;

    vec4 worldPos = model * vec4(aPos, 1.0f);
	FragPos = worldPos.xyz;

	mat3 normalMatrix = transpose(inverse(mat3(model)));
	vec3 T = normalize(normalMatrix * aTangent  	);
	vec3 B = normalize(normalMatrix * aBitangent	);
	vec3 N = normalize(normalMatrix * aNormal		);

	vec4 clipPos = camera.projView * worldPos;

	vec4 prevWorldPos = prevModel * vec4(aPos, 1.0f);
	CurClipPos = clipPos;
	PrevClipPos = prevcamera.projView * prevWorldPos;


	TBN = mat3(T, B, N);
	FragNormal = N;
	gl_Position = clipPos;
	FragTextureCoords = aTexCoords;
}
