#include "SceneCodec/ComponentCodec.h"
#include "CommonComponent.h"
#include "GameRuntimeComponents.h"
#include "GamePlayComponents.h"

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