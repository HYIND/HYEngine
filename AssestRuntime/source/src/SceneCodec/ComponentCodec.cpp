#include "SceneCodec/ComponentCodec.h"
#include "CommonComponent.h"
#include "CommonData/Camera.h"
#include "Components/Physics.h"
#include "Components/Script.h"
#include "Render/Components/RenderModel.h"
#include "VulkanRenderEngine/Base/Model.h"
#include "GameRuntimeComponents.h"
#include "GamePlayComponents.h"
#include "Input/Components/PlayerInput.h"

#include <cmath>
#include <limits>
#include <stdexcept>

using namespace NComponentCodec;

Json EncodeVec3(const glm::vec3& v)
{
	return Json{ v.x, v.y, v.z };
}

Json EncodeVec2(const glm::vec2& v)
{
	return Json{ v.x, v.y };
}

Json EncodeFloat(const float& v)
{
	return Json{ v };
}

// 统一使用 w, x, y, z 顺序存储四元数。
Json EncodeQuat(const glm::quat& q)
{
	return Json{ q.w, q.x, q.y, q.z };
}

Json EncodeMat4(const glm::mat4& matrix)
{
	Json data = Json::array();
	for (int column = 0; column < 4; ++column)
		for (int row = 0; row < 4; ++row)
			data.push_back(matrix[column][row]);
	return data;
}

bool DecodeVec3(const Json& j, glm::vec3& v)
{
	if (!j.is_array() || j.size() != 3)
		return false;

	try
	{
		v = glm::vec3(
			j[0].get<float>(),
			j[1].get<float>(),
			j[2].get<float>());

		return true;
	}
	catch (const Json::exception&)
	{
		return false;
	}
}

bool DecodeVec2(const Json& j, glm::vec2& v)
{
	if (!j.is_array() || j.size() != 2)
		return false;

	try
	{
		v = glm::vec2(
			j[0].get<float>(),
			j[1].get<float>());

		return true;
	}
	catch (const Json::exception&)
	{
		return false;
	}
}

bool DecodeMat4(const Json& j, glm::mat4& matrix)
{
	if (!j.is_array() || j.size() != 16)
		return false;

	try
	{
		for (int column = 0; column < 4; ++column)
			for (int row = 0; row < 4; ++row)
				matrix[column][row] = j[column * 4 + row].get<float>();
		return true;
	}
	catch (const Json::exception&)
	{
		return false;
	}
}

bool DecodeFloat(const Json& j, float& v)
{
	if (!j.is_array() || j.size() != 1)
		return false;

	try
	{
		v = j[0].get<float>();
		return true;
	}
	catch (const Json::exception&)
	{
		return false;
	}
}

bool DecodeQuat(const Json& j, glm::quat& q)
{
	if (!j.is_array() || j.size() != 4)
		return false;

	try
	{
		q = glm::quat(
			j[0].get<float>(),
			j[1].get<float>(),
			j[2].get<float>(),
			j[3].get<float>());

		return true;
	}
	catch (const Json::exception&)
	{
		return false;
	}
}

const char* GetBodyTypeName(Physics::BodyType type)
{
	switch (type)
	{
	case Physics::BodyType::Static: return "Static";
	case Physics::BodyType::Dynamic: return "Dynamic";
	case Physics::BodyType::Kinematic: return "Kinematic";
	default: throw std::runtime_error("Invalid physics body type");
	}
}

bool DecodeBodyType(const Json& value, Physics::BodyType& type)
{
	if (!value.is_string())
		return false;
	const auto name = value.get<std::string>();
	if (name == "Static") type = Physics::BodyType::Static;
	else if (name == "Dynamic") type = Physics::BodyType::Dynamic;
	else if (name == "Kinematic") type = Physics::BodyType::Kinematic;
	else return false;
	return true;
}

const char* GetShapeTypeName(ShapeType type)
{
	switch (type)
	{
	case ShapeType::Box: return "Box";
	case ShapeType::Sphere: return "Sphere";
	case ShapeType::Capsule: return "Capsule";
	case ShapeType::Cylinder: return "Cylinder";
	case ShapeType::ConvexHull: return "ConvexHull";
	case ShapeType::TriangleMesh: return "TriangleMesh";
	default: throw std::runtime_error("Invalid collision shape type");
	}
}

bool DecodeShapeType(const Json& value, ShapeType& type)
{
	if (!value.is_string())
		return false;
	const auto name = value.get<std::string>();
	if (name == "Box") type = ShapeType::Box;
	else if (name == "Sphere") type = ShapeType::Sphere;
	else if (name == "Capsule") type = ShapeType::Capsule;
	else if (name == "Cylinder") type = ShapeType::Cylinder;
	else if (name == "ConvexHull") type = ShapeType::ConvexHull;
	else if (name == "TriangleMesh") type = ShapeType::TriangleMesh;
	else return false;
	return true;
}

bool DecodeFiniteFloat(const Json& value, float& result)
{
	if (!value.is_number())
		return false;
	result = value.get<float>();
	return std::isfinite(result);
}

bool DecodePhysicsVec3(const Json& value, glm::vec3& result)
{
	if (!value.is_array() || value.size() != 3)
		return false;
	return DecodeFiniteFloat(value[0], result.x)
		&& DecodeFiniteFloat(value[1], result.y)
		&& DecodeFiniteFloat(value[2], result.z);
}

bool IsFinite(const glm::quat& value)
{
	return std::isfinite(value.w) && std::isfinite(value.x)
		&& std::isfinite(value.y) && std::isfinite(value.z);
}

bool ValidateChildShape(const CollisionShape::ChildShape& shape, Physics::BodyType bodyType)
{
	if (!std::isfinite(shape.size.x) || !std::isfinite(shape.size.y) || !std::isfinite(shape.size.z)
		|| !std::isfinite(shape.position.x) || !std::isfinite(shape.position.y) || !std::isfinite(shape.position.z)
		|| !IsFinite(shape.rotation))
		return false;

	for (const auto& vertex : shape.vertices)
		if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z))
			return false;

	switch (shape.shapeType)
	{
	case ShapeType::Box:
		return shape.size.x >= 0.f && shape.size.y >= 0.f && shape.size.z >= 0.f;
	case ShapeType::Sphere:
		return shape.size.x >= 0.f;
	case ShapeType::Capsule:
		return shape.size.x >= 0.f && shape.size.y >= 0.f;
	case ShapeType::Cylinder:
		return shape.size.x >= 0.f && shape.size.y >= 0.f && shape.size.z >= 0.f;
	case ShapeType::ConvexHull:
		return true;
	case ShapeType::TriangleMesh:
		if (bodyType != Physics::BodyType::Static || shape.vertices.empty()
			|| shape.indices.empty() || shape.indices.size() % 3 != 0)
			return false;
		for (unsigned int index : shape.indices)
			if (index >= shape.vertices.size())
				return false;
		return true;
	default:
		return false;
	}
}

Json EncodeChildShape(const CollisionShape::ChildShape& shape)
{
	Json vertices = Json::array();
	for (const auto& vertex : shape.vertices)
		vertices.push_back(EncodeVec3(vertex));
	Json indices = shape.indices;
	return Json{
		{"type", GetShapeTypeName(shape.shapeType)},
		{"size", EncodeVec3(shape.size)},
		{"position", EncodeVec3(shape.position)},
		{"rotation", EncodeQuat(shape.rotation)},
		{"vertices", std::move(vertices)},
		{"indices", std::move(indices)}
	};
}

bool DecodeChildShape(const Json& data, CollisionShape::ChildShape& shape, Physics::BodyType bodyType)
{
	if (!data.is_object()
		|| !data.contains("type")
		|| !data.contains("size")
		|| !data.contains("position")
		|| !data.contains("rotation")
		|| !data.contains("vertices") || !data["vertices"].is_array()
		|| !data.contains("indices") || !data["indices"].is_array()
		|| !DecodeShapeType(data["type"], shape.shapeType)
		|| !DecodePhysicsVec3(data["size"], shape.size)
		|| !DecodePhysicsVec3(data["position"], shape.position)
		|| !DecodeQuat(data["rotation"], shape.rotation)
		|| !IsFinite(shape.rotation))
		return false;

	for (const auto& vertexData : data["vertices"])
	{
		glm::vec3 vertex;
		if (!DecodePhysicsVec3(vertexData, vertex))
			return false;
		shape.vertices.push_back(vertex);
	}

	for (const auto& indexData : data["indices"])
	{
		if (!indexData.is_number_integer() && !indexData.is_number_unsigned())
			return false;
		if (!indexData.is_number_unsigned() && indexData.get<int64_t>() < 0)
			return false;
		const uint64_t index = indexData.get<uint64_t>();
		if (index > std::numeric_limits<unsigned int>::max())
			return false;
		shape.indices.push_back(static_cast<unsigned int>(index));
	}

	return ValidateChildShape(shape, bodyType);
}

bool ValidatePhysicsSettings(const Physics& physics)
{
	return std::isfinite(physics.mass) && physics.mass >= 0.f
		&& std::isfinite(physics.friction) && physics.friction >= 0.f
		&& std::isfinite(physics.rollingFriction) && physics.rollingFriction >= 0.f
		&& std::isfinite(physics.restitution) && physics.restitution >= 0.f
		&& std::isfinite(physics.stepHeight)
		&& std::isfinite(physics.walkSpeed)
		&& std::isfinite(physics.jumpSpeed)
		&& std::isfinite(physics.maxSlope)
		&& std::isfinite(physics.maxPenetrationDepth);
}

std::vector<ComponentCodec>& NComponentCodec::GetComponentCodecs()
{
	static std::vector<ComponentCodec> codecs;
	return codecs;
}

void NComponentCodec::RegisterCodec(ComponentCodec codec)
{
	auto& codecs = GetComponentCodecs();
	auto existing = std::find_if(codecs.begin(), codecs.end(), [&](const ComponentCodec& item) {
		return item.type == codec.type || item.componentType == codec.componentType;
		});
	if (existing != codecs.end())
		*existing = std::move(codec);
	else
		codecs.push_back(std::move(codec));
}

static void RegisterGeneralCodec()
{
	RegisterComponentCodec<Controller>(
		"engine.controller",
		[](const Controller& controller) {
			return Json{
				{"yaw", controller.yaw},
				{"pitch", controller.pitch},
				{"mouseSensitivity", controller.mouseSensitivity}
			};
		},
		[](Controller& controller, const Json& data) {
			float yaw, pitch, mouseSensitivity;
			if (!data.is_object()
				|| !DecodeFiniteFloat(data.value("yaw", Json()), yaw)
				|| !DecodeFiniteFloat(data.value("pitch", Json()), pitch)
				|| !DecodeFiniteFloat(data.value("mouseSensitivity", Json()), mouseSensitivity)
				|| mouseSensitivity < 0.f)
				return false;
			controller.yaw = yaw;
			controller.pitch = pitch;
			controller.mouseSensitivity = mouseSensitivity;
			return true;
		});

	RegisterComponentCodec<CameraComponent>(
		"engine.camera",
		[](const CameraComponent& component) {
			if (!std::isfinite(component.fov) || !std::isfinite(component.nearPlane) || !std::isfinite(component.farPlane)
				|| !std::isfinite(component.worldUp.x) || !std::isfinite(component.worldUp.y) || !std::isfinite(component.worldUp.z)
				)
				throw std::runtime_error("Invalid camera state");
			return Json{
				{"worldUp", EncodeVec3(component.worldUp)},
				{"fov", EncodeFloat(component.fov)},
				{"nearPlane", EncodeFloat(component.nearPlane)},
				{"farPlane", EncodeFloat(component.farPlane)},
				{"isFirstPerson", component.isFirstPerson}
			};
		},
		[](CameraComponent& component, const Json& data) {
			glm::vec3 wroldUp;
			float fov, nearPlane, farPlane;
			if (!data.is_object()
				|| !data.contains("isFirstPerson") || !data["isFirstPerson"].is_boolean()
				|| !data.contains("worldUp") || !DecodePhysicsVec3(data["worldUp"], wroldUp)
				|| !data.contains("fov") || !DecodeFloat(data["fov"], fov)
				|| !data.contains("nearPlane") || !DecodeFloat(data["nearPlane"], nearPlane)
				|| !data.contains("farPlane") || !DecodeFloat(data["farPlane"], farPlane)
				)
				return false;
			component.worldUp = wroldUp;
			component.fov = fov;
			component.nearPlane = nearPlane;
			component.farPlane = farPlane;
			component.isFirstPerson = data["isFirstPerson"].get<bool>();
			return true;
		});

	RegisterComponentCodec<Movement>(
		"engine.movement",
		[](const Movement& movement) { return Json{ {"canJump", movement.canJump} }; },
		[](Movement& movement, const Json& data) {
			if (!data.is_object() || !data.contains("canJump") || !data["canJump"].is_boolean())
				return false;
			movement.canJump = data["canJump"].get<bool>();
			return true;
		});

	RegisterComponentCodec<PlayerInput>(
		"engine.player_input",
		[](const PlayerInput&) { return Json::object(); },
		[](PlayerInput&, const Json& data) { return data.is_object(); });

	RegisterComponentCodec<TagCurrentControl>(
		"engine.tag_current_control",
		[](const TagCurrentControl&) { return Json::object(); },
		[](TagCurrentControl&, const Json& data) { return data.is_object(); });

	RegisterComponentCodec<TagMainCamera>(
		"engine.tag_main_camera",
		[](const TagMainCamera&) { return Json::object(); },
		[](TagMainCamera&, const Json& data) { return data.is_object(); });

	RegisterComponentCodec<TagLight>(
		"engine.tag_light",
		[](const TagLight&) { return Json::object(); },
		[](TagLight&, const Json& data) { return data.is_object(); });

	RegisterComponentCodec<CameraFollow>(
		"gameplay.camera_follow",
		[](const CameraFollow& follow) {
			return Json{
				{"target", follow.target ? std::to_string(follow.target.getId()) : std::string{}},
				{"offset", EncodeVec3(follow.offset)},
				{"smoothSpeed", follow.smoothSpeed}
			};
		},
		[](CameraFollow& follow, const Json& data) {
			return data.is_object()
				&& data.contains("target") && data["target"].is_string()
				&& data.contains("offset") && DecodeVec3(data["offset"], follow.offset)
				&& data.contains("smoothSpeed") && DecodeFiniteFloat(data["smoothSpeed"], follow.smoothSpeed);
		},
		{}, {},
		[](CameraFollow& follow, const Json& data, const std::unordered_map<std::string, Entity>& entities) {
			const auto target = data.at("target").get<std::string>();
			if (target.empty())
				return true;
			auto found = entities.find(target);
			if (found == entities.end())
				return false;
			follow.target = found->second;
			return true;
		});

	RegisterComponentCodec<Transform>(
		"engine.transform",
		[](const Transform& transform) {
			return Json{
				{"position", EncodeVec3(transform.position)},
				{"rotation", EncodeQuat(transform.rotation)},
				{"scale", EncodeVec3(transform.scale)}
			};
		},
		[](Transform& transform, const Json& data) {
			if (!data.is_object() || !data.contains("position") || !data.contains("rotation") || !data.contains("scale"))
				return false;
			const auto& position = data.at("position");
			const auto& rotation = data.at("rotation");
			const auto& scale = data.at("scale");

			if (!DecodeVec3(position, transform.position)
				|| !DecodeQuat(rotation, transform.rotation)
				|| !DecodeVec3(scale, transform.scale)
				)
				return false;

			return true;
		});

	RegisterComponentCodec<Transform2D>(
		"engine.transform2D",
		[](const Transform2D& transform) {
			return Json{
				{"position", EncodeVec2(transform.position)},
				{"rotation", EncodeFloat(transform.rotation)},
				{"scale", EncodeVec2(transform.scale)}
			};
		},
		[](Transform2D& transform, const Json& data) {
			if (!data.is_object() || !data.contains("position") || !data.contains("rotation") || !data.contains("scale"))
				return false;
			const auto& position = data.at("position");
			const auto& rotation = data.at("rotation");
			const auto& scale = data.at("scale");

			if (!DecodeVec2(position, transform.position)
				|| !DecodeFloat(rotation, transform.rotation)
				|| !DecodeVec2(scale, transform.scale)
				)
				return false;

			return true;
		});

	RegisterComponentCodec<Script>(
		"engine.script",
		[](const Script& script) {
			Json data = nullptr;
			if (script.data)
			{
				data = {
					{"scriptGuid", script.data->scriptGuid}
				};
			}
			return Json{
				{"enabled", script.enabled},
				{"data", std::move(data)}
			};
		},
		[](Script& script, const Json& data) {
			if (!data.is_object()
				|| !data.contains("enabled") || !data["enabled"].is_boolean()
				|| !data.contains("data"))
				return false;

			const auto& sourceData = data["data"];
			if (sourceData.is_null())
				script.data.reset();
			else
			{
				if (!sourceData.is_object()
					|| !sourceData.contains("scriptGuid") || !sourceData["scriptGuid"].is_string())
					return false;

				script.data = std::make_shared<ScriptSourceData>();
				script.data->scriptGuid = sourceData["scriptGuid"].get<std::string>();
			}

			script.enabled = data["enabled"].get<bool>();
			script.needReload = true;
			return true;
		});

	RegisterComponentCodec<Physics>(
		"engine.physics",
		[](const Physics& physics) {
			if (!ValidatePhysicsSettings(physics))
				throw std::runtime_error("Invalid physics settings");

			Json children = Json::array();
			for (const auto& child : physics.collisionShape.children)
			{
				if (!child || !ValidateChildShape(*child, physics.bodyType))
					throw std::runtime_error("Invalid physics collision shape");
				children.push_back(EncodeChildShape(*child));
			}

			return Json{
				{"bodyType", GetBodyTypeName(physics.bodyType)},
				{"mass", physics.mass},
				{"friction", physics.friction},
				{"rollingFriction", physics.rollingFriction},
				{"restitution", physics.restitution},
				{"isSensor", physics.isSensor},
				{"fixedRotation", physics.fixedRotation},
				{"isBullet", physics.isBullet},
				{"allowSleep", physics.allowSleep},
				{"isCharacter", physics.isCharacter},
				{"stepHeight", physics.stepHeight},
				{"walkSpeed", physics.walkSpeed},
				{"jumpSpeed", physics.jumpSpeed},
				{"maxSlope", physics.maxSlope},
				{"maxPenetrationDepth", physics.maxPenetrationDepth},
				{"collisionShapes", std::move(children)}
			};
		},
		[](Physics& physics, const Json& data) {
			if (!data.is_object()
				|| !data.contains("bodyType")
				|| !data.contains("isSensor") || !data["isSensor"].is_boolean()
				|| !data.contains("fixedRotation") || !data["fixedRotation"].is_boolean()
				|| !data.contains("isBullet") || !data["isBullet"].is_boolean()
				|| !data.contains("allowSleep") || !data["allowSleep"].is_boolean()
				|| !data.contains("isCharacter") || !data["isCharacter"].is_boolean()
				|| !data.contains("collisionShapes") || !data["collisionShapes"].is_array())
				return false;

			Physics decoded;
			if (!DecodeBodyType(data["bodyType"], decoded.bodyType)
				|| !DecodeFiniteFloat(data.value("mass", Json()), decoded.mass)
				|| !DecodeFiniteFloat(data.value("friction", Json()), decoded.friction)
				|| !DecodeFiniteFloat(data.value("rollingFriction", Json()), decoded.rollingFriction)
				|| !DecodeFiniteFloat(data.value("restitution", Json()), decoded.restitution)
				|| !DecodeFiniteFloat(data.value("stepHeight", Json()), decoded.stepHeight)
				|| !DecodeFiniteFloat(data.value("walkSpeed", Json()), decoded.walkSpeed)
				|| !DecodeFiniteFloat(data.value("jumpSpeed", Json()), decoded.jumpSpeed)
				|| !DecodeFiniteFloat(data.value("maxSlope", Json()), decoded.maxSlope)
				|| !DecodeFiniteFloat(data.value("maxPenetrationDepth", Json()), decoded.maxPenetrationDepth)
				|| !ValidatePhysicsSettings(decoded))
				return false;

			decoded.isSensor = data["isSensor"].get<bool>();
			decoded.fixedRotation = data["fixedRotation"].get<bool>();
			decoded.isBullet = data["isBullet"].get<bool>();
			decoded.allowSleep = data["allowSleep"].get<bool>();
			decoded.isCharacter = data["isCharacter"].get<bool>();
			for (const auto& childData : data["collisionShapes"])
			{
				auto child = std::make_shared<CollisionShape::ChildShape>();
				if (!DecodeChildShape(childData, *child, decoded.bodyType))
					return false;
				decoded.collisionShape.children.push_back(std::move(child));
			}

			physics = std::move(decoded);
			return true;
		});

	RegisterComponentCodec<RenderModel>(
		"engine.render_model",
		[](const RenderModel& renderModel) {
			Json modelGuid = nullptr;
			if (renderModel.model)
			{
				if (renderModel.model->GetAssetGUID().empty())
					throw std::runtime_error("RenderModel has no asset GUID");
				modelGuid = renderModel.model->GetAssetGUID();
			}
			return Json{
				{"enabled", renderModel.visible},
				{"trans", EncodeMat4(renderModel.trans)},
				{"modelGuid", std::move(modelGuid)}
			};
		},
		[](RenderModel& renderModel, const Json& data) {
			if (!data.is_object()
				|| !data.contains("enabled") || !data["enabled"].is_boolean()
				|| !data.contains("trans")
				|| !data.contains("modelGuid"))
				return false;

			const auto& modelGuid = data["modelGuid"];
			if (!modelGuid.is_null() && !modelGuid.is_string())
				return false;
			if (!DecodeMat4(data["trans"], renderModel.trans))
				return false;

			renderModel.visible = data["enabled"].get<bool>();
			if (modelGuid.is_null())
				renderModel.model.reset();
			else
			{
				const auto guid = modelGuid.get<std::string>();
				if (guid.empty())
					return false;
				renderModel.model = std::make_shared<Model>();
				renderModel.model->SetAssetGUID(guid);
			}
			return true;
		});

	RegisterComponentCodec<RenderLight>(
		"engine.render_light",

		// Encode
		[](const RenderLight& light) -> Json {
			Json data;

			switch (light.type)
			{
			case LightType::Directional:
			{
				const auto* d = light.GetData<DirectionalLightData>();
				if (!d) return Json{};

				data = {
					{"color", EncodeVec3(d->color)},
					{"luxIntensity", d->luxIntensity},
					{"volumetricScatteringIntensity", d->volumetricScatteringIntensity},
					{"castShadow", d->castShadow},
					{"cascadeLevel", d->cascadeLevel},
					{"shadowMapSize", d->shadowMapSize}
				};
				break;
			}

			case LightType::Point:
			{
				const auto* d = light.GetData<PointLightData>();
				if (!d) return Json{};

				data = {
					{"color", EncodeVec3(d->color)},
					{"cdIntensity", d->cdIntensity},
					{"volumetricScatteringIntensity", d->volumetricScatteringIntensity},
					{"castShadow", d->castShadow},
					{"shadowMapSize", d->shadowMapSize}
				};
				break;
			}

			case LightType::Spot:
			{
				const auto* d = light.GetData<SpotLightData>();
				if (!d) return Json{};

				data = {
					{"color", EncodeVec3(d->color)},
					{"cdIntensity", d->cdIntensity},
					{"volumetricScatteringIntensity", d->volumetricScatteringIntensity},
					{"cutOffAngle", d->cutOffAngle},
					{"outercutOffAngle", d->outercutOffAngle},
					{"castShadow", d->castShadow},
					{"shadowMapSize", d->shadowMapSize}
				};
				break;
			}

			default:
				return Json{};
			}

			return {
				{"type", static_cast<uint32_t>(light.type)},
				{"renderCube", light.renderCube},
				{"data", std::move(data)}
			};
		},

		// Decode
		[](RenderLight& light, const Json& json) -> bool {
			if (!json.is_object()
				|| !json.contains("type")
				|| !json.contains("data")
				|| !json.contains("renderCube"))
			{
				return false;
			}

			if (!json["type"].is_number_unsigned())
			{
				return false;
			}

			const uint32_t type = json["type"].get<uint32_t>();
			const auto& data = json["data"];

			if (!data.is_object())
				return false;

			switch (static_cast<LightType>(type))
			{
			case LightType::Directional:
			{
				DirectionalLightData d{};

				if (!data.contains("color")
					|| !DecodeVec3(data["color"], d.color))
					return false;

				d.luxIntensity = data.at("luxIntensity").get<float>();
				d.volumetricScatteringIntensity =
					data.at("volumetricScatteringIntensity").get<float>();
				d.castShadow = data.at("castShadow").get<bool>();
				d.cascadeLevel = data.at("cascadeLevel").get<uint32_t>();
				d.shadowMapSize = data.at("shadowMapSize").get<uint32_t>();

				light = RenderLight(d);
				break;
			}

			case LightType::Point:
			{
				PointLightData d{};

				if (!data.contains("color")
					|| !DecodeVec3(data["color"], d.color))
					return false;

				d.color = glm::vec3(
					data["color"][0].get<float>(),
					data["color"][1].get<float>(),
					data["color"][2].get<float>());

				d.cdIntensity = data.at("cdIntensity").get<float>();
				d.volumetricScatteringIntensity =
					data.at("volumetricScatteringIntensity").get<float>();
				d.castShadow = data.at("castShadow").get<bool>();
				d.shadowMapSize = data.at("shadowMapSize").get<uint32_t>();

				light = RenderLight(d);
				break;
			}

			case LightType::Spot:
			{
				SpotLightData d{};

				if (!data.contains("color")
					|| !DecodeVec3(data["color"], d.color))
					return false;

				d.color = glm::vec3(
					data["color"][0].get<float>(),
					data["color"][1].get<float>(),
					data["color"][2].get<float>());

				d.cdIntensity = data.at("cdIntensity").get<float>();
				d.volumetricScatteringIntensity =
					data.at("volumetricScatteringIntensity").get<float>();
				d.cutOffAngle = data.at("cutOffAngle").get<float>();
				d.outercutOffAngle = data.at("outercutOffAngle").get<float>();
				d.castShadow = data.at("castShadow").get<bool>();
				d.shadowMapSize = data.at("shadowMapSize").get<uint32_t>();

				light = RenderLight(d);
				break;
			}

			default:
				return false;
			}

			light.renderCube = json.at("renderCube").get<bool>();
			return true;
		}
	);

}

void NComponentCodec::RegisterBuiltInCodecs()
{
	static const bool registered = [] {
		RegisterGeneralCodec();
		return true;
		}();

	(void)registered;
}