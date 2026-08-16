#version 450 core
out float FragColor;

in vec2 TexCoords;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D texNoise;

uniform vec3 samples[64];
 
uniform int kernelSize;
uniform float radius;
uniform float bias;
uniform vec2 noiseScale;

#include "shader/dataDef/camerauboDef.comp"

//此函数使用的fragPos和normal为视空间下
float getOcclusion(vec3 fragPos, vec3 normal, vec3 randomVec)
{
    // create TBN change-of-basis matrix: from tangent-space to view-space
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);

    mat3 TBN = mat3(tangent, bitangent, normal);

    // iterate over the sample kernel and calculate occlusion factor
    float occlusion = 0.0;
    for(int i = 0; i < kernelSize; ++i)
    {
        // get sample position
        vec3 samplePos = TBN * samples[i]; // from tangent to view-space
        samplePos = fragPos + samplePos * radius; 

        // project sample position (to sample texture) (to get position on screen/texture)
        vec4 offset = vec4(samplePos, 1.0);
        offset = camera.projection * offset; // from view to clip-space
        offset.xyz /= offset.w; // perspective divide
        offset.xyz = offset.xyz * 0.5 + 0.5; // transform to range 0.0 - 1.0
        
        // get sample depth
        float sampleDepth = (camera.view * vec4(texture(gPosition, offset.xy).xyz,1.0f)).z; // get depth value of kernel sample
        
        // range check & accumulate
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;           
    }
    occlusion = 1.0 - (occlusion / kernelSize);
    
    return occlusion;
}

void main()
{
    vec3 worldFragPos = texture(gPosition, TexCoords).xyz;
    vec3 worldNormal = normalize(texture(gNormal, TexCoords).rgb);

    vec3 viewFragPos = (camera.view * vec4(worldFragPos,1.0f)).xyz;
    vec3 viewNormal = normalize(mat3(transpose(inverse(camera.view))) * normalize(worldNormal));

    vec3 randomVec = normalize(texture(texNoise, TexCoords * noiseScale).xyz);

    FragColor = getOcclusion(viewFragPos, viewNormal, randomVec);
}