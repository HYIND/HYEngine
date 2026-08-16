#version 450 core
out vec4 FragColor;

in vec2 TexCoords;

#include "shader/dataDef/camerauboDef.comp"

uniform sampler2D colorMap;
uniform sampler2D depthMap;


uniform vec3 fogColor;
uniform float fogHeight;
uniform float fogDistanceFalloff;   // 距离衰减
uniform float fogHeightFalloff;     // 高度衰减系数

float DepthBufferConvertToViewDepth(float depth)
{
    float z_ndc = depth * 2.0 - 1.0;
    return (2.0 * camera.nearPlane * camera.farPlane) / (camera.farPlane + camera.nearPlane - z_ndc * (camera.farPlane - camera.nearPlane));
}

vec3 ReconstructWorldPosition(float depth, vec2 texCoords)
{
    vec4 ndcPos = vec4(texCoords * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    
    vec4 viewPos = inverse(camera.projection) * ndcPos;
    viewPos /= viewPos.w; // 透视除法
    
    // 逆视图变换到世界空间
    vec4 worldPos = camera.invView * viewPos;
    return worldPos.xyz;
}

void main()
{
    // vec3 scrColor = texture(colorMap, TexCoords).rgb;
	// float samplerDepth = texture(depthMap, TexCoords).r;

    // float depth = DepthBufferConvertToViewDepth(samplerDepth);

    // float fogStart = camera.nearPlane + (camera.farPlane - camera.nearPlane) * 0.25f;
    // float fogEnd =  camera.nearPlane + (camera.farPlane - camera.nearPlane) * 0.85f;

    // float fogFactor = 0;

    // if (depth >= fogEnd)
    //     fogFactor = 1.0;
    // else if (depth >= fogStart)
    //     fogFactor = clamp(log(depth / fogStart) / log(fogEnd / fogStart), 0.f, 1.f);

    // vec3 result = mix(scrColor, fogColor, fogFactor);
    // FragColor = vec4(result, 1.0);



    vec3 scrColor = texture(colorMap, TexCoords).rgb;
    float samplerDepth = texture(depthMap, TexCoords).r;

    float viewDepth = DepthBufferConvertToViewDepth(samplerDepth);
    vec3 worldPos = ReconstructWorldPosition(samplerDepth, TexCoords);
    
    float fogHeight = max(camera.position.y - camera.farPlane * 0.3, fogHeight);
    float heightDiff = max(0.0, fogHeight - worldPos.y);
    float heightFactor = 1.0 - exp(-heightDiff * fogHeightFalloff);

    float distanceDiff = max(0.0, viewDepth - camera.farPlane * 0.3);
    float distanceFactor = 1.0 - exp(-distanceDiff * fogDistanceFalloff);

    float fogFactor = distanceFactor * 0.3 + heightFactor * 0.7;
    fogFactor = clamp(fogFactor, 0.0, 1.0);
    
    vec3 result = mix(scrColor, fogColor, fogFactor);
    FragColor = vec4(result, 1.0);
    // FragColor = vec4(vec3(fogFactor), 1.0);
}
