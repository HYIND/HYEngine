#version 460 core

#include "shader/dataDef/camerauboDef.comp"
#include "shader/effect/particle_common.comp"

layout(std430, set = 0, binding = 0) readonly buffer ParticleInstances { ParticleInstance particles[]; };

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoords;

layout(location = 0) out vec2 FragUV;
layout(location = 1) out vec3 FragWorldPos;
layout(location = 2) flat out vec4 FragColor;
layout(location = 3) flat out uint FragMask;
layout(location = 4) flat out uint FragMaterialIndex;
layout(location = 5) flat out uint FragFlags;

void main()
{
	ParticleInstance inst = particles[gl_InstanceIndex];

	vec3 worldPos;
	if ((inst.flags & PARTICLE_FLAG_MESH) != 0u)
	{
		// mesh 几何
		worldPos = inst.position + RotateByQuat(inst.rotation, aPos * inst.scale);
	}
	else
	{
		// billboard：用相机的右/上向量铺开，再叠加绕视轴的 roll
		float c = cos(inst.roll);
		float s = sin(inst.roll);
		vec2 local = vec2(aPos.x * c - aPos.y * s, aPos.x * s + aPos.y * c) * 2.0;	// [-0.5, 0.5] -> [-1, 1]
		worldPos = inst.position
			+ camera.directionRight * (local.x * inst.extentX * 0.5)
			+ camera.directionUp * (local.y * inst.extentY * 0.5);
	}

	FragUV = aTexCoords;
	FragWorldPos = worldPos;
	FragColor = inst.color;
	FragMask = inst.mask;
	FragMaterialIndex = inst.materialIndex;
	FragFlags = inst.flags;

	gl_Position = camera.projView * vec4(worldPos, 1.0);
}
