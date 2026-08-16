#version 330 core

struct Material {
	sampler2D texture_diffuse;
    sampler2D texture_specular;
	sampler2D texture_normal;
	sampler2D texture_height1;
    sampler2D texture_emission1;

    float shininess;

	int texture_diffuse_count;
	int texture_specular_count;
	int texture_normal_count;
	int texture_height_count;
	int texture_emission_count;
}; 

struct DirLight {
	vec3 direction;

	vec3 color;
	float ambientStrength;
	float diffuseStrength;
	float specularStrength;

	mat4 lightSpaceMatrix;
	sampler2D shadowMap;
};

struct PointLight {
    vec3 position;

	vec3 color;
	float ambientStrength;
	float diffuseStrength;
	float specularStrength;

	float constant;	//常数衰减项
    float linear;	//线性衰减项
    float quadratic;//平方衰减项

	float shadowFarPlane;
	samplerCube shadowMap;
};

struct SpotLight {
    vec3 position;

	vec3 color;
	float ambientStrength;
	float diffuseStrength;
	float specularStrength;

	float constant;	//常数衰减项
    float linear;	//线性衰减项
    float quadratic;//平方衰减项

	vec3 direction;		//聚光方向
    float cutOff;		//聚光内切光角
    float outerCutOff;	//聚光外切光角

	mat4 lightSpaceMatrix;
	sampler2D shadowMap;
};

in vec2 FragTextureCoords;
in vec3 FragNormal;
in vec3 FragPos;
in mat3 TBN;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec4 BrightColor;

#define MAX_LIGHTS 16

uniform vec3 CameraPos;
uniform Material material;

uniform int dirLightCount;
uniform int pointLightCount;
uniform int spotLightCount;
uniform DirLight dirLights[MAX_LIGHTS];
uniform PointLight pointLights[MAX_LIGHTS];
uniform SpotLight spotLights[MAX_LIGHTS];

uniform samplerCube skybox;

vec3 NormalBias(vec3 fragPos, vec3 lightDir, vec3 normal, float biasFactor);
float DirLightShadowCalculation(vec4 fragPosLightSpace, sampler2D shadowMap);
float PointLightShadowCalculation(vec3 fragPos, vec3 lightPos, samplerCube shadowMap, float shadowFarPlane);
vec4 CalcEmissionLight();
vec4 CalcSkyBoxReflect();
vec4 CalcSkyBoxReFract();
vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir);
vec3 CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir);
vec3 CalcSpotLight(SpotLight light, vec3 normal, vec3 fragPos, vec3 viewDir);

void main()
{
	vec3 norm = vec3(0.f);
	if(material.texture_normal_count > 0)
	{
		vec3 tex_normal = texture(material.texture_normal, FragTextureCoords).rgb;
		tex_normal = normalize(tex_normal * 2.0 - 1.0);   
		tex_normal = normalize(TBN * tex_normal);
		norm = tex_normal;
	}
	else 
	{
		norm = normalize(FragNormal);
	}

    vec4 texColor = texture(material.texture_diffuse, FragTextureCoords);
    if(texColor.a < 0.02)
        discard;

	vec3 viewDir = normalize(CameraPos - FragPos);

	vec3 lightColor = vec3(0.f,0.f,0.f);

	int dir_count = clamp(dirLightCount, 0, MAX_LIGHTS);
	for(int i = 0;i < dir_count; i++)
		lightColor += CalcDirLight(dirLights[i], norm, viewDir);
	int point_count = clamp(pointLightCount, 0, MAX_LIGHTS);
	for(int i = 0;i < point_count; i++)
		lightColor += CalcPointLight(pointLights[i], norm, FragPos, viewDir);
	int spot_count = clamp(spotLightCount, 0, MAX_LIGHTS);
	for(int i = 0;i < spot_count; i++)
		lightColor += CalcSpotLight(spotLights[i], norm, FragPos, viewDir);

	// result += CalcEmissionLight();
	// result += CalcSkyBoxReFract();

	FragColor = vec4(lightColor, texColor.a);

	float brightness = dot(FragColor.rgb, vec3(0.2126, 0.7152, 0.0722));
    if(brightness > 1.0)
        BrightColor = vec4(FragColor.rgb, 1.0);
	else
		BrightColor = vec4(0.0, 0.0, 0.0, 1.0);
}

vec3 NormalBias(vec3 fragPos, vec3 lightDir, vec3 normal, float biasFactor)
{
	float bias = biasFactor * (1.0 - dot(normalize(normal), lightDir));
	vec3 shiftedPos = fragPos + normal * bias;
	return shiftedPos;
}

float DirLightShadowCalculation(vec4 fragPosLightSpace, sampler2D shadowMap)
{
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    float currentDepth = projCoords.z;

	float shadow = 0.0;
	vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
	for(int x = -1; x <= 1; ++x)
	{
		for(int y = -1; y <= 1; ++y)
		{
			float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r; 
			shadow += currentDepth > pcfDepth ? 1.0 : 0.0;        
		}    
	}
	shadow /= 9.0;

	if(projCoords.z >= 1.0)
        shadow = 0.0;
	
    return shadow;
}

float PointLightShadowCalculation(vec3 fragPos, vec3 lightPos, samplerCube shadowMap, float shadowFarPlane)
{
    vec3 fragToLight = fragPos - lightPos;
    float currentDepth = length(fragToLight);

	//float closestDepth = texture(shadowMap, fragToLight).r * shadowFarPlane; 
	//return currentDepth > closestDepth ? 1.0f : 0.0f;

	float shadow = 0.0;
	float offset = 0.02;
	int samplenumber = 1;	//向四周采样的数量
	float stride = offset / float(samplenumber);
	int sumsamplecount = (1 + samplenumber * 2) * (1 + samplenumber * 2) * (1 + samplenumber * 2);
	for(float x = -offset; x <= offset; x += stride)
	{
		for(float y = -offset; y <= offset; y += stride)
		{
			for(float z = -offset; z <= offset; z += stride)
			{
				float closestDepth = texture(shadowMap, fragToLight + vec3(x, y, z)).r * shadowFarPlane;
				if(currentDepth > closestDepth)
					shadow += 1.0;
			}
		}
	}
	shadow /= float(sumsamplecount);

    return shadow;
}

vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir)
{
	vec3 samplerDiffuseColor = vec3(0.f,0.f,0.f);
	vec3 samplerSpecularColor = vec3(0.f,0.f,0.f);

	if(material.texture_diffuse_count > 0)
		samplerDiffuseColor = texture(material.texture_diffuse, FragTextureCoords).rgb; 		
	if(material.texture_specular_count > 0)
		samplerSpecularColor = texture(material.texture_specular, FragTextureCoords).rgb;

	vec3 lightDir = normalize(light.direction);

	// 环境光照
	vec3 ambient = light.color * light.ambientStrength * samplerDiffuseColor;

	// 漫反射光照
	float diff = max(dot(normal, -lightDir), 0.0f);
	vec3 diffuse = light.color * light.diffuseStrength * diff * samplerDiffuseColor;

	// 镜面反射光照
	vec3 ReflectLightDir = reflect(lightDir, normal);
	float spec = pow(max(dot(viewDir, ReflectLightDir), 0.0), material.shininess);	
	vec3 specular = light.color * light.specularStrength * spec * samplerSpecularColor;

	// vec3 result = ambient + diffuse + specular;

	// 阴影处理
	vec3 shiftedPos = NormalBias(FragPos, lightDir, normal, 0.05);
	vec4 fragPosLightSpace = light.lightSpaceMatrix * vec4(shiftedPos, 1.0);
	float shadow = DirLightShadowCalculation(fragPosLightSpace, light.shadowMap);                      

	vec3 result = ambient.rgb + (1.0 - shadow) * (diffuse.rgb + specular.rgb);

	return result;
}

vec3 CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir)
{
	vec3 samplerDiffuseColor = vec3(0.f,0.f,0.f);
	vec3 samplerSpecularColor = vec3(0.f,0.f,0.f);

	if(material.texture_diffuse_count > 0)
		samplerDiffuseColor = texture(material.texture_diffuse, FragTextureCoords).rgb; 		
	if(material.texture_specular_count > 0)
		samplerSpecularColor = texture(material.texture_specular, FragTextureCoords).rgb;

	vec3 lightDir = normalize(fragPos - light.position);

	float distance = length(light.position - fragPos);
	float attenuation = 1.0f / ((light.constant) + (light.linear * distance) + (light.quadratic * distance * distance));

	// 环境光照
	vec3 ambient = light.color * light.ambientStrength * samplerDiffuseColor;

	// 漫反射光照
	float diff = max(dot(normal, -lightDir), 0.0f);
	vec3 diffuse = light.color * light.diffuseStrength * diff * samplerDiffuseColor;

	// 镜面反射光照
	vec3 ReflectLightDir = reflect(lightDir, normal);
	float spec = pow(max(dot(viewDir, ReflectLightDir), 0.0), material.shininess);	
	vec3 specular = light.color * light.specularStrength * spec * samplerSpecularColor;

	ambient *= attenuation;
	diffuse *= attenuation;
	specular *= attenuation;

	//vec3 result = ambient + diffuse + specular;

	// 阴影处理
	vec3 shiftedPos = NormalBias(fragPos, lightDir, normal, 0.05);
	float shadow = PointLightShadowCalculation(shiftedPos, light.position, light.shadowMap, light.shadowFarPlane);           

	vec3 result = ambient.rgb + (1.0 - shadow) * (diffuse.rgb + specular.rgb);

	return result;
}

vec3 CalcSpotLight(SpotLight light, vec3 normal, vec3 fragPos, vec3 viewDir)
{

	vec3 samplerDiffuseColor = vec3(0.f,0.f,0.f);
	vec3 samplerSpecularColor = vec3(0.f,0.f,0.f);

	if(material.texture_diffuse_count > 0)
		samplerDiffuseColor = texture(material.texture_diffuse, FragTextureCoords).rgb; 		
	if(material.texture_specular_count > 0)
		samplerSpecularColor = texture(material.texture_specular, FragTextureCoords).rgb;

	bool needintensity = false;
	float intensity = 1.0f;

	float outerCutOff = min(light.cutOff ,light.outerCutOff);

	vec3 lightDir = normalize(fragPos - light.position);

	float distance = length(light.position - fragPos);
	float attenuation = 1.0f / ((light.constant) + (light.linear * distance) + (light.quadratic * distance * distance));

	float theta = dot(lightDir, normalize(light.direction));
	if(theta < outerCutOff)	//在聚光切光角范围外，不执行光照计算，保留基础的环境光
	{
		vec3 ambient = light.color * light.ambientStrength * attenuation * samplerDiffuseColor;
		return ambient;
	}
	else if (theta < light.cutOff)	//内外切光角之间，做边缘软化
	{
		needintensity = true;
		float epsilon  = light.cutOff - outerCutOff;
		intensity = clamp((theta - outerCutOff) / epsilon, 0.0f, 1.0f); 
	}

	// 环境光照
	vec3 ambient = light.color * light.ambientStrength * samplerDiffuseColor;

	// 漫反射光照
	float diff = max(dot(normal, -lightDir), 0.0f);
	vec3 diffuse = light.color * light.diffuseStrength * diff * samplerDiffuseColor;

	// 镜面反射光照
	vec3 ReflectLightDir = reflect(lightDir, normal);
	float spec = pow(max(dot(viewDir, ReflectLightDir), 0.0), material.shininess);	
	vec3 specular = light.color * light.specularStrength * spec * samplerSpecularColor;

	ambient *= attenuation;
	diffuse *= attenuation;
	specular *= attenuation;

	vec3 diffuse_specular_light = diffuse.rgb + specular.rgb;
	if(needintensity)
		diffuse_specular_light *= intensity;

	//vec3 result = ambient.rgb + diffuse_specular_light;

	// 阴影处理
	vec3 shiftedPos = NormalBias(fragPos, lightDir, normal, 0.05);
	vec4 fragPosLightSpace = light.lightSpaceMatrix * vec4(shiftedPos, 1.0);
	float shadow = DirLightShadowCalculation(fragPosLightSpace, light.shadowMap);                      
	
	vec3 shadowlight = (1.0 - shadow) * diffuse_specular_light;
	
	vec3 result = ambient.rgb + shadowlight;

	return result;
}

vec4 CalcEmissionLight()
{
	vec4 samplerEmissionColor = vec4(0.f,0.f,0.f,0.f);

	if(material.texture_emission_count > 0)
		samplerEmissionColor = texture(material.texture_emission1, FragTextureCoords).rgba; 		

	float emissionStrength = 1.0f;
	vec4 emission = vec4(vec3(1.0f * emissionStrength), 1.0f) * samplerEmissionColor;
	return emission;
}

vec4 CalcSkyBoxReflect()
{
    vec3 I = normalize(FragPos - CameraPos);
    vec3 R = reflect(I, normalize(FragNormal));
	return vec4(texture(skybox, R).rgb, 1.0);
}

vec4 CalcSkyBoxReFract()
{
    float ratio = 1.00 / 1.52;
    vec3 I = normalize(FragPos - CameraPos);
    vec3 R = refract(I, normalize(FragNormal), ratio);
	return vec4(texture(skybox, R).rgb, 1.0);
}