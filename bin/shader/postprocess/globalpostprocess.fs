#version 450 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D colorBuffer;

uniform float exposureValue;

uniform bool gammaEnable;
uniform float gammaValue;

uniform bool bloomEnable;
uniform sampler2D bloomMap;

uniform bool filpY = false;

vec3 debugshow();

vec3 aces(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main()
{
    vec3 scrColor = texture(colorBuffer, filpY ? vec2(TexCoords.x, 1.0 - TexCoords.y): TexCoords).rgb;

    vec3 result = scrColor;

    if(bloomEnable) 
    {
        vec3 bloomColor = texture(bloomMap, filpY ? vec2(TexCoords.x, 1.0 - TexCoords.y): TexCoords).rgb;
        result += bloomColor;  
    }

    result *= exposureValue;                    // 曝光控制
    // result = vec3(1.0) - exp(-result);       // 色调映射
    result = aces(result);                      // 色调映射

    if(gammaEnable) result = pow(result, vec3(1.0 / gammaValue));

    FragColor = vec4(result, 1.0);
}

vec3 debugshow()
{
    vec3 hdrColor = texture(colorBuffer, TexCoords).rgb;
    vec3 bloomColor = texture(bloomMap, TexCoords).rgb;

    vec2 uv = TexCoords;
    
    bool left = uv.x < 0.5;
    bool top = uv.y > 0.5;
    bool right = uv.x > 0.5;
    bool bottom = uv.y < 0.5;

    vec2 sampleUV = vec2(
        uv.x - (uv.x > 0.5 ? 0.5 : 0.0),
        uv.y - (uv.y < 0.5 ? -0.5 : 0.0)
    );
    sampleUV.x = sampleUV.x * 2;
    sampleUV.y = (sampleUV.y - 0.5) * 2;

    //result = texture(hdrBuffer, sampleUV).rgb;

    vec3 result = vec3(0.f);

    if (left && top) {
    }
    else if (right && top) {  
    }   
    else if (right && bottom) {
    }   
    else if (left && bottom) { 
    }
    return result;
}
