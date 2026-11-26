#version 460 core

// 材质预览（MaterialPreviewPass）：前向 PBR
//   材质采样直接复用 MaterialTextureDef.comp（set 2：materials[] + materialTextures[]），materials 里只有一项，下标固定 0；
//   光照是"摄影棚"：主光 + 轮廓光两盏方向光（方向由 push constant 给，不做阴影）+ 解析式环境光（天空/地面渐变 +
//   跟着主光方向走的柔光箱高光，镜面部分用 Karis 的 EnvBRDFApprox 近似分离求和），
//   不依赖场景的光照 SSBO、IBL 贴图和阴影图集；
//   最后在这里做曝光 + ACES + gamma，直接输出可以显示的 LDR（RGBA8 UNORM，和编辑器缩略图约定一致）。
//
// 定义 PREVIEW_IBL 时（HDRIEnvironment）：环境光换成 IBL，公式和场景 lightingHelper.comp 的 CalculateIBLLightingSplit 一致：
//   漫反射 = texture(辐照度, N) × albedo / π × (1 - metallic)；镜面 = textureLod(预过滤, R, roughness × (级数 - 1)) × (F0 × A + B)
//   主光 / 轮廓光按开关叠加（HDRI 本身已经带了灯光）；输出不透明，Blend 材质在这里直接和背后的 HDRI 混合。
//   push constant 的 params.y = 环境旋转（弧度），params.z = 环境亮度，params.w > 0.5 = 叠加直接光，keyLightDir.w = 背景 mip

#include "shader/dataDef/MaterialTextureDef.comp"
#include "shader/Helper/bdrfHelper.comp"

layout (location = 0) in vec3 FragPos;
layout (location = 1) in vec3 FragNormal;
layout (location = 2) in vec2 FragTextureCoords;
layout (location = 3) in mat3 TBN;

layout (push_constant) uniform PreviewParams
{
	mat4 viewProj;
	vec4 cameraPos;
	vec4 params;
	vec4 keyLightDir;
	vec4 rimLightDir;
} pc;

layout (location = 0) out vec4 FragColor;

#ifdef PREVIEW_IBL
layout (set = 0, binding = 0) uniform samplerCube iblDiffuse;
layout (set = 0, binding = 1) uniform samplerCube iblPrefilter;
layout (set = 0, binding = 2) uniform sampler2D iblBrdfLUT;
layout (set = 0, binding = 3) uniform samplerCube iblBackground;

// 世界方向 → 环境贴图里的方向：环境绕 Y 轴转了 yaw，查表时把方向反着转回去
vec3 ToEnvironment(vec3 dir)
{
	float s = sin(pc.params.y);
	float c = cos(pc.params.y);
	return vec3(c * dir.x - s * dir.z, dir.y, s * dir.x + c * dir.z);
}
#endif

// ---------------- 摄影棚光照 ----------------
const vec3 KEY_LIGHT_COLOR = vec3(1.00, 0.97, 0.92) * 3.2;
const vec3 RIM_LIGHT_COLOR = vec3(0.75, 0.85, 1.00) * 1.6;

const vec3 SKY_COLOR = vec3(0.55, 0.60, 0.68);
const vec3 HORIZON_COLOR = vec3(0.32, 0.32, 0.33);
const vec3 GROUND_COLOR = vec3(0.12, 0.11, 0.10);

vec3 EnvironmentColor(vec3 dir, float roughness, vec3 softboxDir)
{
	float y = clamp(dir.y, -1.0, 1.0);
	vec3 color = y >= 0.0
		? mix(HORIZON_COLOR, SKY_COLOR, smoothstep(0.0, 0.8, y))
		: mix(HORIZON_COLOR, GROUND_COLOR, smoothstep(0.0, 0.5, -y));

	// 柔光箱（放在主光方向）：光滑表面上看得到的一块亮斑，越粗糙越散、越暗
	float spread = mix(256.0, 2.0, clamp(roughness, 0.0, 1.0));
	float box = pow(max(dot(dir, softboxDir), 0.0), spread);
	color += box * vec3(2.2) * mix(1.0, 0.15, roughness);
	return color;
}

// Karis, "Physically Based Shading on Mobile"
vec3 EnvBRDFApprox(vec3 F0, float roughness, float NdotV)
{
	const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
	const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
	vec4 r = roughness * c0 + c1;
	float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
	vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
	return F0 * AB.x + AB.y;
}

vec3 DirectLight(vec3 N, vec3 V, vec3 L, vec3 radiance, PBRProperties prop)
{
	float NdotL = 0.0;
	vec3 diffuse = vec3(0.0);
	vec3 specular = vec3(0.0);
	CalcBDRF(N, V, L, prop.albedo, prop.metallic, prop.roughness, NdotL, diffuse, specular);
	return (diffuse + specular) * radiance * NdotL;
}

vec3 ACESFilm(vec3 x)
{
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
	MaterialData material = materials[0];

	// 透明度和场景渲染保持同一套规则：
	//   Opaque / Mask 走 GBuffer（geometrypass_StaticMesh.fs）：opacity < 0.01 就 discard，不管哪种模式，
	//     所以 Opaque 材质的透明贴图（或 albedo alpha）为 0 的地方在场景里也是镂空的；没被裁掉的部分按不透明画。
	//     Mask 的 calculateOpacity 已经按阈值变成 0 / 1，同一个判断就是 alpha test。
	//   Blend 走透明 Pass（transparent_static.fs / transparent_oit.fs）：opacity < 0.005 discard，其余按透明度混合。
	float opacity = clamp(calculateOpacity(material, FragTextureCoords), 0.0, 1.0);
	if (material.alphamode == 2u)
	{
		if (opacity < 0.005)
			discard;
	}
	else
	{
		if (opacity < 0.01)
			discard;
		opacity = 1.0;
	}

	vec3 V = normalize(pc.cameraPos.xyz - FragPos);

	// 背面：预览网格都是凸的（平面正面朝 +Z），插值的几何法线背对相机就是背面，不依赖三角形绕序。
	// 场景里非双面材质剔除背面：镂空处看进去是空的，平面从背后看不见；双面材质背面翻转法线照常着色
	const bool backFace = dot(FragNormal, V) < 0.0;
	if (backFace && material.twosided == 0)
		discard;

	vec3 N = calculateNormal(material, FragNormal, TBN, FragTextureCoords);
	if (dot(N, N) < 1e-6)
		N = normalize(FragNormal);
	if (backFace)
		N = -N;

	PBRProperties prop;
	getPBRProperties(material, FragTextureCoords, prop.albedo, prop.metallic, prop.roughness, prop.ambientOcclusion);
	prop.roughness = clamp(prop.roughness, 0.04, 1.0);
	prop.metallic = clamp(prop.metallic, 0.0, 1.0);

	float NdotV = max(dot(N, V), 1e-4);

	vec3 keyLightDir = normalize(pc.keyLightDir.xyz);
	vec3 rimLightDir = normalize(pc.rimLightDir.xyz);

	vec3 color = vec3(0.0);
	vec3 F0 = mix(vec3(0.02), prop.albedo, prop.metallic);
	vec3 R = reflect(-V, N);

#ifdef PREVIEW_IBL
	// 直接光（可选）
	if (pc.params.w > 0.5)
	{
		color += DirectLight(N, V, keyLightDir, KEY_LIGHT_COLOR, prop);
		color += DirectLight(N, V, rimLightDir, RIM_LIGHT_COLOR, prop);
	}

	// IBL
	float environmentIntensity = max(pc.params.z, 0.0);
	vec3 kdAlbedo;
	CalcBDRFDiffuse(prop.albedo, prop.metallic, kdAlbedo);
	vec3 iblDiffuseColor = texture(iblDiffuse, ToEnvironment(N)).rgb * kdAlbedo;
	float prefilterLevel = prop.roughness * float(textureQueryLevels(iblPrefilter) - 1);
	vec3 prefiltered = textureLod(iblPrefilter, ToEnvironment(R), prefilterLevel).rgb;
	vec2 AB = texture(iblBrdfLUT, vec2(NdotV, prop.roughness)).xy;
	vec3 iblSpecularColor = prefiltered * (F0 * AB.x + AB.y);
	color += (iblDiffuseColor + iblSpecularColor) * environmentIntensity * prop.ambientOcclusion;
#else
	// 直接光
	color += DirectLight(N, V, keyLightDir, KEY_LIGHT_COLOR, prop);
	color += DirectLight(N, V, rimLightDir, RIM_LIGHT_COLOR, prop);

	// 环境光（漫反射 + 镜面，F0 约定和 bdrfHelper 一致）
	vec3 ambientDiffuse = EnvironmentColor(N, 1.0, keyLightDir) * prop.albedo * (1.0 - prop.metallic);
	vec3 ambientSpecular = EnvironmentColor(R, prop.roughness, keyLightDir) * EnvBRDFApprox(F0, prop.roughness, NdotV);
	color += (ambientDiffuse + ambientSpecular) * prop.ambientOcclusion;
#endif

	// 自发光
	color += calculateEmission(material, FragTextureCoords);

#ifdef PREVIEW_IBL
	// 和背后的 HDRI 混合（在线性空间、色调映射之前，和背景 Pass 的结果一致），输出不透明
	if (opacity < 1.0)
	{
		vec3 behind = textureLod(iblBackground, ToEnvironment(-V), pc.keyLightDir.w).rgb * environmentIntensity;
		color = mix(behind, color, opacity);
	}
	opacity = 1.0;
#endif

	// 曝光 + 色调映射 + gamma
	float exposure = pc.params.x > 0.0 ? pc.params.x : 1.0;
	color = ACESFilm(color * exposure);
	color = pow(color, vec3(1.0 / 2.2));

	FragColor = vec4(color, opacity);
}
