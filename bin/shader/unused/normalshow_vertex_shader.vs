#version 330 core
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;

out VS_OUT {
    vec3 v_normal;
} vs_out;

uniform mat4 model;

#include "shader/dataDef/camerauboDef.comp"

void main()
{
	gl_Position = view * model * vec4(position, 1.0); 
    mat3 normalMatrix = mat3(transpose(inverse(camera.view * model)));
    vs_out.v_normal = normalize(vec3(vec4(normalMatrix * normal, 0.0)));
}
