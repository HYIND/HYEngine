#include "SceneCodec/ComponentCodec.h"
#include "CommonComponent.h"
#include "GameRuntimeComponents.h"
#include "GamePlayComponents.h"

using namespace NComponentCodec;

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
				{"position", {transform.position.x, transform.position.y, transform.position.z}},
				{"rotation", {transform.rotation.w, transform.rotation.x, transform.rotation.y, transform.rotation.z}},
				{"scale", {transform.scale.x, transform.scale.y, transform.scale.z}}
			};
		},
		[](Transform& transform, const Json& data) {
			if (!data.is_object() || !data.contains("position") || !data.contains("rotation") || !data.contains("scale"))
				return false;
			const auto& position = data.at("position");
			const auto& rotation = data.at("rotation");
			const auto& scale = data.at("scale");
			if (!position.is_array() || position.size() != 3
				|| !rotation.is_array() || rotation.size() != 4
				|| !scale.is_array() || scale.size() != 3)
				return false;
			transform.position = glm::vec3(
				position[0].get<float>(), position[1].get<float>(), position[2].get<float>());
			transform.rotation = glm::quat(
				rotation[0].get<float>(), rotation[1].get<float>(),
				rotation[2].get<float>(), rotation[3].get<float>());
			transform.scale = glm::vec3(
				scale[0].get<float>(), scale[1].get<float>(), scale[2].get<float>());
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
					{"color", {d->color.x, d->color.y, d->color.z}},
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
					{"color", {d->color.x, d->color.y, d->color.z}},
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
					{"color", {d->color.x, d->color.y, d->color.z}},
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

				if (!data.contains("color") || !data["color"].is_array()
					|| data["color"].size() != 3)
					return false;

				d.color = glm::vec3(
					data["color"][0].get<float>(),
					data["color"][1].get<float>(),
					data["color"][2].get<float>());

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

				if (!data.contains("color") || !data["color"].is_array()
					|| data["color"].size() != 3)
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

				if (!data.contains("color") || !data["color"].is_array()
					|| data["color"].size() != 3)
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

	//RegisterComponentCodec<RenderModel>(
	//	"engine.transform",
	//	[](const RenderModel& transform) {
	//		return Json{
	//			{"position", {transform.position.x, transform.position.y, transform.position.z}},
	//			{"rotation", {transform.rotation.w, transform.rotation.x, transform.rotation.y, transform.rotation.z}},
	//			{"scale", {transform.scale.x, transform.scale.y, transform.scale.z}}
	//		};
	//	},
	//	[](RenderModel& transform, const Json& data) {
	//		if (!data.is_object() || !data.contains("position") || !data.contains("rotation") || !data.contains("scale"))
	//			return false;
	//		const auto& position = data.at("position");
	//		const auto& rotation = data.at("rotation");
	//		const auto& scale = data.at("scale");
	//		if (!position.is_array() || position.size() != 3
	//			|| !rotation.is_array() || rotation.size() != 4
	//			|| !scale.is_array() || scale.size() != 3)
	//			return false;
	//		transform.position = glm::vec3(
	//			position[0].get<float>(), position[1].get<float>(), position[2].get<float>());
	//		transform.rotation = glm::quat(
	//			rotation[0].get<float>(), rotation[1].get<float>(),
	//			rotation[2].get<float>(), rotation[3].get<float>());
	//		transform.scale = glm::vec3(
	//			scale[0].get<float>(), scale[1].get<float>(), scale[2].get<float>());
	//		return true;
	//	});
}

void NComponentCodec::RegisterBuiltInCodecs()
{
	static const bool registered = [] {
		RegisterGeneralCodec();
		return true;
		}();

	(void)registered;
}