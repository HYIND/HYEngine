#include "OpenGLRenderEngine/Base/Light.h"
#include "OpenGLRenderEngine/Base/Mesh.h"
#include "OpenGLRenderEngine/General/RenderHelp.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include <cmath>
#include <algorithm>

constexpr float PI = 3.14159265359;

constexpr float radius_threshold = 0.005f;


// 将色温（开尔文）转换为线性 RGB 值
// 输入: temperature (K), 有效范围 1000K ~ 40000K
// 输出: R, G, B (线性空间, 范围 0.0 ~ 1.0)
glm::vec3 ColorTemperatureToRGB(float temperature)
{
	// 钳制输入范围
	temperature = std::clamp(temperature, 1000.0f, 40000.0f);

	// 对温度进行归一化，用于后续计算
	float temp = temperature / 100.0f;  // 缩小100倍，便于计算

	float R, G, B;

	// ---- 红色通道 ----
	if (temp <= 66.0f)
		R = 1.0f;
	else
		R = 1.292936186f * std::pow(temp - 60.0f, -0.1332047592f);

	// ---- 绿色通道 ----
	if (temp <= 66.0f)
		G = 0.390081579f * std::log(temp) - 0.631841444f;
	else
		G = 1.129890861f * std::pow(temp - 60.0f, -0.0755148492f);

	// ---- 蓝色通道 ----
	if (temp <= 19.0f)  // 对应 1900K 以下
		B = 0.0f;
	else if (temp <= 66.0f)
		B = 0.543206789f * std::log(temp - 10.0f) - 1.196254089f;
	else
		B = 1.0f;

	// ---- 钳制到 [0, 1] ----
	R = std::clamp(R, 0.0f, 1.0f);
	G = std::clamp(G, 0.0f, 1.0f);
	B = std::clamp(B, 0.0f, 1.0f);

	return glm::vec3(R, G, B);
}

std::vector<glm::vec3> GetFrustumCornersWorldSpace(const glm::mat4& projView)
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
						2.0f * z - 1.0f,
						1.0f);
				frustumCorners.push_back(glm::vec3(pt / pt.w));
			}
		}
	}
	return frustumCorners;
}

void DirLight::Draw(Shader& shader)	// 绘制函数
{
	RenderHelp::renderLightSphere();
}

DirLight::~DirLight()				// 析构函数
{
}

void DirLight::setDirection(const glm::vec3& dir) {
	_direction = glm::normalize(dir);
}

void DirLight::setDirection(float x, float y, float z) {
	_direction = glm::normalize(glm::vec3(x, y, z));
}

void DirLight::setColor(const glm::vec3& c) {
	_color = c;
}

void DirLight::setColor(float r, float g, float b) {
	_color = glm::vec3(r, g, b);
}

void DirLight::setColorTemperature(float temp)
{
	_color = ColorTemperatureToRGB(temp);
}

void DirLight::setIntensity(float lux)
{
	_luxIntensity = lux;
}

void DirLight::setShadowMapWidth(float w)
{
	shadowMapWidth = w;
}

void DirLight::setShadowMapHeight(float h)
{
	shadowMapHeight = h;
}

void DirLight::setCascadeLevel(int level)
{
	_cascadeLevel = std::max(int(1), level);
}

glm::vec3 DirLight::getDirection() const {
	return _direction;
}

glm::vec3 DirLight::getColor() const
{
	return _color;
}

float DirLight::getIntensity() const
{
	return _luxIntensity;
}

float DirLight::getShadowMapWidth() const
{
	return shadowMapWidth;
}

float DirLight::getShadowMapHeight() const
{
	return shadowMapHeight;
}

glm::mat4 DirLight::getLightSpaceMatrix() const
{
	float virtual_distance = 800.f;
	glm::vec3 center = glm::vec3(0.f);
	glm::vec3 lightPos = center - _direction * virtual_distance;	//平行光虚拟位置
	float near_plane = 0.1f, far_plane = virtual_distance * 2;

	glm::vec3 up = glm::vec3(0.0, 1.0, 0.0);
	if (glm::abs(glm::dot(_direction, up)) > 0.9999f)
		up = glm::vec3(0.0, 0.0, 1.0);	// 视线方向与 Y 轴平行，改用 Z 轴作为 Up

	glm::mat4 lightView = glm::lookAt(lightPos, center, up);
	glm::mat4 lightProjection = glm::ortho(-virtual_distance, virtual_distance, -virtual_distance, virtual_distance, near_plane, far_plane);
	glm::mat4 lightSpaceMatrix = lightProjection * lightView;

	return lightSpaceMatrix;
}

glm::mat4 DirLight::getLightSpaceMatrixWithFrustumCorners(const glm::mat4& cameraProjection, const glm::mat4& cameraView, glm::vec3* center, float* radius) const
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
	aabb.min *= expand;
	aabb.max *= expand;

	constexpr float zScale = 10.0f;
	float delta = aabb.max.z - aabb.min.z;
	float middle = aabb.min.z + delta / 2.0f;
	aabb.min.z = middle - delta * zScale / 2.0f;
	aabb.max.z = middle + delta * zScale / 2.0f;

	// 构建正交投影矩阵
	glm::mat4 lightProjection = glm::ortho(
		aabb.min.x, aabb.max.x,
		aabb.min.y, aabb.max.y,
		aabb.min.z, aabb.max.z
	);

	glm::mat4 lightSpaceMatrix = lightProjection * lightView;

	if (center)
		*center = frustumCenter;
	if (radius)
		*radius = glm::length(aabb.max - aabb.min) / 2.f;
	return lightSpaceMatrix;
}

int DirLight::getCascadeLevel()
{
	return _cascadeLevel;
}

void PointLight::Draw(Shader& shader)	// 绘制函数
{
	RenderHelp::renderLightCube();
}

PointLight::~PointLight()				// 析构函数
{
}

void PointLight::setPosition(const glm::vec3& pos) {
	_position = pos;
}

void PointLight::setPosition(float x, float y, float z) {
	_position = glm::vec3(x, y, z);
}

void PointLight::setColor(const glm::vec3& c) {
	_color = c;
}

void PointLight::setColor(float r, float g, float b) {
	_color = glm::vec3(r, g, b);
}

void PointLight::setColorTemperature(float temp)
{
	_color = ColorTemperatureToRGB(temp);
}

void PointLight::setIntensity(float cd)
{
	_cdIntensity = cd;
}

void PointLight::setShadowMapWidth(float w)
{
	shadowMapWidth = w;
}

void PointLight::setShadowMapHeight(float h)
{
	shadowMapHeight = h;
}

glm::vec3 PointLight::getPosition() const {
	return _position;
}

glm::vec3 PointLight::getColor() const
{
	return _color;
}

float PointLight::getIntensity() const
{
	return _cdIntensity;
}

float PointLight::getShadowMapWidth() const
{
	return shadowMapWidth;
}

float PointLight::getShadowMapHeight() const
{
	return shadowMapHeight;
}

float PointLight::getRadius() const
{
	return sqrt(_cdIntensity / (PI * radius_threshold));
}

void SpotLight::Draw(Shader& shader)
{
	RenderHelp::renderLightCube();
}

SpotLight::~SpotLight()
{
}

void SpotLight::setPosition(const glm::vec3& pos) {
	_position = pos;
}

void SpotLight::setPosition(float x, float y, float z) {
	_position = glm::vec3(x, y, z);
}

void SpotLight::setColor(const glm::vec3& c) {
	_color = c;
}

void SpotLight::setColor(float r, float g, float b) {
	_color = glm::vec3(r, g, b);
}

void SpotLight::setColorTemperature(float temp)
{
	_color = ColorTemperatureToRGB(temp);
}

void SpotLight::setIntensity(float cd)
{
	_cdIntensity = cd;
}

void SpotLight::setDirection(const glm::vec3& dir) {
	_direction = glm::normalize(dir);
}

void SpotLight::setDirection(float x, float y, float z) {
	_direction = glm::normalize(glm::vec3(x, y, z));
}

void SpotLight::setCutOffAngle(float cut) {
	_cutOffAngle = cut;
}

void SpotLight::setOuterCutOffAngle(float outerCut) {
	_outercutOffAngle = outerCut;
}

glm::vec3 SpotLight::getPosition() const {
	return _position;
}

glm::vec3 SpotLight::getColor() const
{
	return _color;
}

float SpotLight::getIntensity() const
{
	return _cdIntensity;
}

glm::vec3 SpotLight::getDirection() const {
	return _direction;
}

float SpotLight::getCutOffAngle() const {
	return _cutOffAngle;
}

float SpotLight::getOuterCutOffAngle() const {
	return _outercutOffAngle;
}

float SpotLight::getShadowMapWidth() const
{
	return shadowMapWidth;
}

float SpotLight::getShadowMapHeight() const
{
	return shadowMapHeight;
}

glm::mat4 SpotLight::getLightSpaceMatrix() const
{
	float near_plane = 0.1f, far_plane = getRadius();
	glm::mat4 lightProjection = glm::perspective(glm::radians(_outercutOffAngle * 2), shadowMapWidth / shadowMapHeight, 0.1f, far_plane);
	glm::vec3 up = glm::vec3(0.0, 1.0, 0.0);
	if (glm::abs(glm::dot(_direction, up)) > 0.9999f)
		up = glm::vec3(0.0, 0.0, 1.0);	// 视线方向与 Y 轴平行，改用 Z 轴作为 Up
	glm::mat4 lightView = glm::lookAt(_position, _position + _direction, up);
	glm::mat4 lightSpaceMatrix = lightProjection * lightView;

	return lightSpaceMatrix;
}

float SpotLight::getRadius() const
{
	return sqrt(_cdIntensity / (PI * radius_threshold));
}

DirLight::DirLight(const glm::vec3& direction, const glm::vec3& color, float luxIntensity)
	: _direction(glm::normalize(direction)), _color(color), _luxIntensity(luxIntensity), _cascadeLevel(4)
{
}

DirLight::DirLight(const glm::vec3& direction, float colorTemperature, float luxIntensity)
	: DirLight(direction, ColorTemperatureToRGB(colorTemperature), luxIntensity)
{
}

PointLight::PointLight(const glm::vec3& position, const glm::vec3& color, float cdIntensity)
	: _position(position), _color(color), _cdIntensity(cdIntensity)
{
}

PointLight::PointLight(const glm::vec3& position, float colorTemperature, float cdIntensity)
	:PointLight(position, ColorTemperatureToRGB(colorTemperature), cdIntensity)
{
}

SpotLight::SpotLight(const glm::vec3& position, const glm::vec3& direction, float cutOffAngle, float outercutOffAngle, const glm::vec3& color, float cdIntensity)
	: _position(position), _color(color), _cdIntensity(cdIntensity), _direction(glm::normalize(direction)), _cutOffAngle(cutOffAngle), _outercutOffAngle(outercutOffAngle)
{
}

SpotLight::SpotLight(const glm::vec3& position, const glm::vec3& direction, float cutOffAngle, float outercutOffAngle, float colorTemperature, float cdIntensity)
	:SpotLight(position, direction, cutOffAngle, outercutOffAngle, ColorTemperatureToRGB(colorTemperature), cdIntensity)
{
}
