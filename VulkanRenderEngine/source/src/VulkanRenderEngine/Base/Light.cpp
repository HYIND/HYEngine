#include "vkstdafx.h"
#include "VulkanRenderEngine/Base/Light.h"
#include "VulkanRenderEngine/Base/StaticMesh.h"
#include "VulkanRenderEngine/General/RenderHelper.h"

constexpr float PI = 3.14159265359;

constexpr float radius_threshold = 0.005f;

static std::vector<glm::vec3> GetFrustumCornersWorldSpace(const glm::mat4& projView)
{
	// 视锥体在 NDC 空间的 8 个顶点转换到世界空间
	const auto inv = glm::inverse(projView);

	std::vector<glm::vec3> frustumCorners;
	for (unsigned int x = 0; x < 2; ++x)
	{
		for (unsigned int y = 0; y < 2; ++y)
		{
			for (unsigned int z = 0; z < 2; ++z)
			{
				const glm::vec4 pt =
					inv * glm::vec4(
						2.0f * x - 1.0f,
						2.0f * y - 1.0f,
						(float)z,
						1.0f);
				frustumCorners.push_back(glm::vec3(pt / pt.w));
			}
		}
	}
	return frustumCorners;
}

Light::Light()
{
}

Light::Light(const glm::vec3& color)
	:_color(glm::max(glm::vec3(0.f), color))
{
}

void Light::SetColor(const glm::vec3& c) {
	_color = c;
}

void Light::SetColor(float r, float g, float b) {
	_color = glm::vec3(r, g, b);
}

void Light::SetColorTemperature(float temp)
{
	_color = Tool::ColorTemperatureToRGB(temp);
}

void Light::SetCastShadow(bool value)
{
	_castShadow = value;
}

void Light::SetShadowMapSize(uint32_t s)
{
	_shadowMapSize = s;
}

void Light::SetVolumeLightEnable(bool value)
{
	volumeLightEnable = value;
}

void Light::SetVolumeLightEffectByAtmospher(bool value)
{
	_volumeLightEffectByAtmospher = value;
}

void Light::SetVolumetricScatteringIntensity(float value)
{
	_volumetricScatteringIntensity = std::max(value, 0.0f);
}

glm::vec3 Light::GetColor() const
{
	return _color;
}

bool Light::GetCastShadow() const
{
	return _castShadow;
}

uint32_t Light::GetShadowMapSize() const
{
	return _shadowMapSize;
}

bool Light::GetVolumeLightEnable() const
{
	return volumeLightEnable;
}

bool Light::GetVolumeLightEffectByAtmospher() const
{
	return _volumeLightEffectByAtmospher;
}

float Light::GetVolumetricScatteringIntensity() const
{
	return _volumetricScatteringIntensity;
}

DirLight::~DirLight()				// 析构函数
{
}

void DirLight::SetDirection(const glm::vec3& dir) {
	_direction = glm::normalize(dir);
}

void DirLight::SetDirection(float x, float y, float z) {
	_direction = glm::normalize(glm::vec3(x, y, z));
}

void DirLight::SetIntensity(float lux)
{
	_luxIntensity = lux;
}

void DirLight::SetCascadeLevel(uint32_t level)
{
	_cascadeLevel = std::max(1u, level);
}

void DirLight::SetShadowDistance(float distance)
{
	_shadowDistance = std::max(distance, 1.0f);
}

void DirLight::SetCascadeSplitLambda(float lambda)
{
	_cascadeSplitLambda = std::clamp(lambda, 0.0f, 1.0f);
}

glm::vec3 DirLight::GetDirection() const {
	return _direction;
}

float DirLight::GetIntensity() const
{
	return _luxIntensity;
}


glm::mat4 DirLight::GetLightSpaceMatrix() const
{
	float virtual_distance = 800.f;
	glm::vec3 center = glm::vec3(0.f);
	glm::vec3 lightPos = center - _direction * virtual_distance;	//平行光虚拟位置
	float near_plane = 0.1f, far_plane = virtual_distance * 2;

	glm::vec3 up = glm::vec3(0.0, 1.0, 0.0);
	if (glm::abs(glm::dot(_direction, up)) > 0.9999f)
		up = glm::vec3(0.0, 0.0, 1.0);	// 视线方向与 Y 轴平行，改用 Z 轴作为 Up

	glm::mat4 lightView = glm::lookAt(lightPos, center, up);
	glm::mat4 lightProjection = vkOrtho(-virtual_distance, virtual_distance, -virtual_distance, virtual_distance, near_plane, far_plane);
	glm::mat4 lightSpaceMatrix = lightProjection * lightView;

	return lightSpaceMatrix;
}

glm::mat4 DirLight::GetLightSpaceMatrixWithFrustumCorners(const glm::mat4& cameraProjection, const glm::mat4& cameraView, glm::vec3* center, float* radius) const
{
	std::vector<glm::vec3> frustumCorners = GetFrustumCornersWorldSpace(cameraProjection * cameraView);
	glm::vec3 frustumCenter = glm::vec3(0.0f);
	for (const auto& corner : frustumCorners) {
		frustumCenter += corner;
	}
	frustumCenter /= frustumCorners.size();

	glm::vec3 up = glm::vec3(0.0, 1.0, 0.0);
	if (glm::abs(glm::dot(_direction, up)) > 0.9999f)
		up = glm::vec3(0.0, 0.0, 1.0);	// 视线方向与 Y 轴平行，改用 Z 轴作为 Up
	glm::mat4 lightView = glm::lookAt(frustumCenter, frustumCenter + _direction, up);

	AABB aabb;

	for (const auto& corner : frustumCorners) {
		glm::vec4 lightSpacePos = lightView * glm::vec4(corner, 1.0f);
		aabb.extend(lightSpacePos);
	}

	// 稍微扩大包围盒，防止边缘裁切（添加一个膨胀系数）
	constexpr float expand = 1.1f;
	glm::vec3 aabbCenter = (aabb.min + aabb.max) * 0.5f;
	glm::vec3 aabbHalfSize = (aabb.max - aabb.min) * 0.5f;
	aabbHalfSize *= expand;
	aabb.min = aabbCenter - aabbHalfSize;
	aabb.max = aabbCenter + aabbHalfSize;

	// 深度范围（光源视空间，lookAt 朝 _direction，所以 +z 指向光源）：
	//   * 背光一侧只要盖住切片本身（接收者都在切片里），留一点余量给法线偏移；
	//   * 朝光一侧要盖住「切片外、但能把影子投进切片」的遮挡物。它和切片大小无关——
	//     原来按切片厚度的倍数算，近处级联切得越细，朝光一侧越短，太阳低时高墙 / 树会被近平面裁掉、
	//     也会被 GPU 视锥剔除掉，影子凭空消失，而且随相机角度变化（切片在光方向上的厚度随角度变）。
	//   正交投影 + D32F 的深度是线性的，范围拉到几百米精度也只有亚毫米级，不需要省。
	constexpr float kMinCasterDistance = 500.0f;	// 朝光一侧至少覆盖的距离（米）
	const float delta = aabb.max.z - aabb.min.z;
	const float casterExtend = std::max({ delta * 5.0f, _shadowDistance * 2.0f, kMinCasterDistance });
	const float receiverPad = std::max(delta * 0.5f, 1.0f);
	const float nearZ = aabb.max.z + casterExtend;	// 朝光一侧（深度 0）
	const float farZ = aabb.min.z - receiverPad;		// 背光一侧（深度 1）

	// 构建正交投影矩阵
	// glm::ortho（右手、深度 0~1）把视空间 z = -zNear 映射到 0、z = -zFar 映射到 1，所以传入取负的距离
	glm::mat4 lightProjection = vkOrtho(
		aabb.min.x, aabb.max.x,
		aabb.min.y, aabb.max.y,
		-nearZ, -farZ
	);


	glm::mat4 lightSpaceMatrix = lightProjection * lightView;

	if (center)
		*center = frustumCenter;
	if (radius)
		*radius = glm::length(aabb.max - aabb.min) / 2.f;
	return lightSpaceMatrix;
}

uint32_t DirLight::GetCascadeLevel() const
{
	return _cascadeLevel;
}

float DirLight::GetShadowDistance() const
{
	return _shadowDistance;
}

float DirLight::GetCascadeSplitLambda() const
{
	return _cascadeSplitLambda;
}

PointLight::~PointLight()				// 析构函数
{
}

void PointLight::SetPosition(const glm::vec3& pos) {
	_position = pos;
}

void PointLight::SetPosition(float x, float y, float z) {
	_position = glm::vec3(x, y, z);
}

void PointLight::SetIntensity(float cd)
{
	_cdIntensity = cd;
}

glm::vec3 PointLight::GetPosition() const {
	return _position;
}

float PointLight::GetIntensity() const
{
	return _cdIntensity;
}

float PointLight::GetRadius() const
{
	return sqrt(_cdIntensity / (PI * radius_threshold));
}

void SpotLight::Draw(std::shared_ptr<VKWrapper::VKCommandBuffer>& cmd)
{
	//RenderHelp::renderLightCube();
}

SpotLight::~SpotLight()
{
}

void SpotLight::SetPosition(const glm::vec3& pos) {
	_position = pos;
}

void SpotLight::SetPosition(float x, float y, float z) {
	_position = glm::vec3(x, y, z);
}

void SpotLight::SetIntensity(float cd)
{
	_cdIntensity = cd;
}

void SpotLight::SetDirection(const glm::vec3& dir) {
	_direction = glm::normalize(dir);
}

void SpotLight::SetDirection(float x, float y, float z) {
	_direction = glm::normalize(glm::vec3(x, y, z));
}

void SpotLight::SetCutOffAngle(float cut) {
	_cutOffAngle = cut;
}

void SpotLight::SetOuterCutOffAngle(float outerCut) {
	_outercutOffAngle = outerCut;
}

glm::vec3 SpotLight::GetPosition() const {
	return _position;
}

glm::vec3 SpotLight::GetColor() const
{
	return _color;
}

float SpotLight::GetIntensity() const
{
	return _cdIntensity;
}

glm::vec3 SpotLight::GetDirection() const {
	return _direction;
}

float SpotLight::GetCutOffAngle() const {
	return _cutOffAngle;
}

float SpotLight::GetOuterCutOffAngle() const {
	return _outercutOffAngle;
}

glm::mat4 SpotLight::GetLightSpaceMatrix() const
{
	float near_plane = 0.1f, far_plane = GetRadius();
	glm::mat4 lightProjection = vkPerspective(glm::radians(_outercutOffAngle * 2), 1.0, 0.1f, far_plane);
	glm::vec3 up = glm::vec3(0.0, 1.0, 0.0);
	if (glm::abs(glm::dot(_direction, up)) > 0.9999f)
		up = glm::vec3(0.0, 0.0, 1.0);	// 视线方向与 Y 轴平行，改用 Z 轴作为 Up
	glm::mat4 lightView = glm::lookAt(_position, _position + _direction, up);
	glm::mat4 lightSpaceMatrix = lightProjection * lightView;

	return lightSpaceMatrix;
}

float SpotLight::GetRadius() const
{
	return sqrt(_cdIntensity / (PI * radius_threshold));
}

DirLight::DirLight(const glm::vec3& direction, const glm::vec3& color, float luxIntensity)
	: Light(color), _direction(glm::normalize(direction)), _luxIntensity(luxIntensity), _cascadeLevel(4)
{
}

DirLight::DirLight(const glm::vec3& direction, float colorTemperature, float luxIntensity)
	: DirLight(direction, Tool::ColorTemperatureToRGB(colorTemperature), luxIntensity)
{
}

PointLight::PointLight(const glm::vec3& position, const glm::vec3& color, float cdIntensity)
	: Light(color), _position(position), _cdIntensity(cdIntensity)
{
}

PointLight::PointLight(const glm::vec3& position, float colorTemperature, float cdIntensity)
	:PointLight(position, Tool::ColorTemperatureToRGB(colorTemperature), cdIntensity)
{
}

SpotLight::SpotLight(const glm::vec3& position, const glm::vec3& direction, float cutOffAngle, float outercutOffAngle, const glm::vec3& color, float cdIntensity)
	: Light(color), _position(position), _cdIntensity(cdIntensity), _direction(glm::normalize(direction)), _cutOffAngle(cutOffAngle), _outercutOffAngle(outercutOffAngle)
{
}

SpotLight::SpotLight(const glm::vec3& position, const glm::vec3& direction, float cutOffAngle, float outercutOffAngle, float colorTemperature, float cdIntensity)
	:SpotLight(position, direction, cutOffAngle, outercutOffAngle, Tool::ColorTemperatureToRGB(colorTemperature), cdIntensity)
{
}
