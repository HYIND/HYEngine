#include "SceneCodec/BuiltInCodecs.h"
#include "SceneCodec/ComponentCodec.h"
#include "CommonComponent.h"
#include "PlatformRuntimeComponents.h"
#include "VulkanRenderEngine/Base/StaticModel.h"
#include "VulkanRenderEngine/Base/Texture2D.h"
#include <nlohmann/json.hpp>

using Json = nlohmann::json;

using namespace NComponentCodec;

namespace
{

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
}

namespace
{
    Json KindJson(const char* kind, Json fields = Json::object())
    {
        fields["kind"] = kind;
        return fields;
    }

    std::string ReadKind(const Json& j)
    {
        if (!j.is_object() || !j.contains("kind") || !j["kind"].is_string())
            return {};
        return j["kind"].get<std::string>();
    }

    bool ReadFloat(const Json& j, const char* key, float& out)
    {
        if (!j.is_object() || !j.contains(key) || !j[key].is_number())
            return false;
        out = j[key].get<float>();
        return std::isfinite(out);
    }

    bool ReadInt(const Json& j, const char* key, int& out)
    {
        if (!j.is_object() || !j.contains(key) || !j[key].is_number_integer())
            return false;
        out = j[key].get<int>();
        return true;
    }

    bool ReadVec3(const Json& j, const char* key, glm::vec3& out)
    {
        return j.is_object() && j.contains(key) && DecodeVec3(j[key], out);
    }

    bool ReadQuat(const Json& j, const char* key, glm::quat& out)
    {
        return j.is_object() && j.contains(key) && DecodeQuat(j[key], out);
    }

    // ---------------- 生成器 ----------------

    Json EncodeScaleGenerator(const std::shared_ptr<IScaleGenerator>& generator)
    {
        if (auto* p = dynamic_cast<const RandomScaleGenerator*>(generator.get()))
            return KindJson("Random", { {"min", EncodeVec3(p->min)}, {"max", EncodeVec3(p->max)} });
        if (auto* p = dynamic_cast<const FixedScaleGenerator*>(generator.get()))
            return KindJson("Fixed", { {"scale", EncodeVec3(p->scale)} });
        return nullptr;
    }

    std::shared_ptr<IScaleGenerator> DecodeScaleGenerator(const Json& j)
    {
        const auto kind = ReadKind(j);
        if (kind == "Random")
        {
            glm::vec3 min(1.0f), max(1.0f);
            ReadVec3(j, "min", min);
            ReadVec3(j, "max", max);
            return std::make_shared<RandomScaleGenerator>(min, max);
        }
        if (kind == "Fixed")
        {
            glm::vec3 scale(1.0f);
            ReadVec3(j, "scale", scale);
            return std::make_shared<FixedScaleGenerator>(scale);
        }
        return nullptr;
    }

    Json EncodeColorGenerator(const std::shared_ptr<IColorGenerator>& generator)
    {
        if (auto* p = dynamic_cast<const RandomColorGenerator*>(generator.get()))
            return KindJson("Random", { {"min", EncodeVec3(p->min)}, {"max", EncodeVec3(p->max)} });
        if (auto* p = dynamic_cast<const FixedColorGenerator*>(generator.get()))
            return KindJson("Fixed", { {"color", EncodeVec3(p->color)} });
        return nullptr;
    }

    std::shared_ptr<IColorGenerator> DecodeColorGenerator(const Json& j)
    {
        const auto kind = ReadKind(j);
        if (kind == "Random")
        {
            glm::vec3 min(1.0f), max(1.0f);
            ReadVec3(j, "min", min);
            ReadVec3(j, "max", max);
            return std::make_shared<RandomColorGenerator>(min, max);
        }
        if (kind == "Fixed")
        {
            glm::vec3 color(1.0f);
            ReadVec3(j, "color", color);
            return std::make_shared<FixedColorGenerator>(color);
        }
        return nullptr;
    }

    Json EncodeLifeTimeGenerator(const std::shared_ptr<ILifeTimeGenerator>& generator)
    {
        if (auto* p = dynamic_cast<const RandomLifeTimeGenerator*>(generator.get()))
            return KindJson("Random", { {"min", p->min}, {"max", p->max} });
        if (auto* p = dynamic_cast<const FixedLifeTimeGenerator*>(generator.get()))
            return KindJson("Fixed", { {"lifetime", p->lifetime} });
        return nullptr;
    }

    std::shared_ptr<ILifeTimeGenerator> DecodeLifeTimeGenerator(const Json& j)
    {
        const auto kind = ReadKind(j);
        if (kind == "Random")
        {
            float min = 1.0f, max = 1.0f;
            ReadFloat(j, "min", min);
            ReadFloat(j, "max", max);
            return std::make_shared<RandomLifeTimeGenerator>(min, max);
        }
        if (kind == "Fixed")
        {
            float lifetime = 1.0f;
            ReadFloat(j, "lifetime", lifetime);
            return std::make_shared<FixedLifeTimeGenerator>(lifetime);
        }
        return nullptr;
    }

    Json EncodeOpacityGenerator(const std::shared_ptr<IOpacityGenerator>& generator)
    {
        if (auto* p = dynamic_cast<const RandomOpacityGenerator*>(generator.get()))
            return KindJson("Random", { {"min", p->min}, {"max", p->max} });
        if (auto* p = dynamic_cast<const FixedOpactiyGenerator*>(generator.get()))
            return KindJson("Fixed", { {"opacity", p->opacity} });
        return nullptr;
    }

    std::shared_ptr<IOpacityGenerator> DecodeOpacityGenerator(const Json& j)
    {
        const auto kind = ReadKind(j);
        if (kind == "Random")
        {
            float min = 1.0f, max = 1.0f;
            ReadFloat(j, "min", min);
            ReadFloat(j, "max", max);
            return std::make_shared<RandomOpacityGenerator>(min, max);
        }
        if (kind == "Fixed")
        {
            float opacity = 1.0f;
            ReadFloat(j, "opacity", opacity);
            return std::make_shared<FixedOpactiyGenerator>(opacity);
        }
        return nullptr;
    }

    Json EncodePositionGenerator(const std::shared_ptr<IPositionGenerator>& generator)
    {
        if (dynamic_cast<const PointGenerator*>(generator.get()))
            return KindJson("Point");
        if (auto* p = dynamic_cast<const SphericalGenerator*>(generator.get()))
            return KindJson("Spherical", { {"radius", p->radius} });
        if (auto* p = dynamic_cast<const BallGenerator*>(generator.get()))
            return KindJson("Ball", { {"radius", p->radius} });
        if (auto* p = dynamic_cast<const BoxGenerator*>(generator.get()))
            return KindJson("Box", { {"halfSize", EncodeVec3(p->halfSize)} });
        return nullptr;
    }

    std::shared_ptr<IPositionGenerator> DecodePositionGenerator(const Json& j)
    {
        const auto kind = ReadKind(j);
        if (kind == "Point")
            return std::make_shared<PointGenerator>();
        if (kind == "Spherical")
        {
            float radius = 1.0f;
            ReadFloat(j, "radius", radius);
            return std::make_shared<SphericalGenerator>(radius);
        }
        if (kind == "Ball")
        {
            float radius = 1.0f;
            ReadFloat(j, "radius", radius);
            return std::make_shared<BallGenerator>(radius);
        }
        if (kind == "Box")
        {
            glm::vec3 halfSize(1.0f);
            ReadVec3(j, "halfSize", halfSize);
            return std::make_shared<BoxGenerator>(halfSize);
        }
        return nullptr;
    }

    Json EncodeDirectionGenerator(const std::shared_ptr<IDirectionGenerator>& generator)
    {
        if (auto* p = dynamic_cast<const FixedDirectionConeGenerator*>(generator.get()))
            return KindJson("FixedCone", { {"direction", EncodeVec3(p->direction)}, {"spreadAngle", p->spreadAngle} });
        if (auto* p = dynamic_cast<const FollowEmitterDirectionConeGenerator*>(generator.get()))
            return KindJson("FollowEmitterCone", { {"spreadAngle", p->spreadAngle} });
        if (auto* p = dynamic_cast<const RadialDirectionGenerator*>(generator.get()))
            return KindJson("Radial", { {"spreadAngle", p->spreadAngle} });
        return nullptr;
    }

    std::shared_ptr<IDirectionGenerator> DecodeDirectionGenerator(const Json& j)
    {
        const auto kind = ReadKind(j);
        if (kind == "FixedCone")
        {
            glm::vec3 direction(0.0f, 1.0f, 0.0f);
            float spreadAngle = 0.0f;
            ReadVec3(j, "direction", direction);
            ReadFloat(j, "spreadAngle", spreadAngle);
            if (glm::length(direction) < 1e-6f)
                direction = glm::vec3(0.0f, 1.0f, 0.0f);
            return std::make_shared<FixedDirectionConeGenerator>(direction, spreadAngle);
        }
        if (kind == "FollowEmitterCone")
        {
            float spreadAngle = 0.0f;
            ReadFloat(j, "spreadAngle", spreadAngle);
            return std::make_shared<FollowEmitterDirectionConeGenerator>(spreadAngle);
        }
        if (kind == "Radial")
        {
            float spreadAngle = 0.0f;
            ReadFloat(j, "spreadAngle", spreadAngle);
            return std::make_shared<RadialDirectionGenerator>(spreadAngle);
        }
        return nullptr;
    }

    Json EncodeRotationGenerator(const std::shared_ptr<IRotationGenerator>& generator)
    {
        if (auto* p = dynamic_cast<const FixedRotationGenerator*>(generator.get()))
            return KindJson("Fixed", { {"rotation", EncodeQuat(p->rotation)} });
        if (dynamic_cast<const FollowEmitterRotationGenerator*>(generator.get()))
            return KindJson("FollowEmitter");
        if (dynamic_cast<const FollowVelocityRotationGenerator*>(generator.get()))
            return KindJson("FollowVelocity");
        return nullptr;
    }

    std::shared_ptr<IRotationGenerator> DecodeRotationGenerator(const Json& j)
    {
        const auto kind = ReadKind(j);
        if (kind == "Fixed")
        {
            glm::quat rotation = glm::identity<glm::quat>();
            ReadQuat(j, "rotation", rotation);
            return std::make_shared<FixedRotationGenerator>(rotation);
        }
        if (kind == "FollowEmitter")
            return std::make_shared<FollowEmitterRotationGenerator>();
        if (kind == "FollowVelocity")
            return std::make_shared<FollowVelocityRotationGenerator>();
        return nullptr;
    }

    Json EncodeSpeedGenerator(const std::shared_ptr<ISpeedGenerator>& generator)
    {
        if (auto* p = dynamic_cast<const RandomSpeedGenerator*>(generator.get()))
            return KindJson("Random", { {"min", p->min}, {"max", p->max} });
        if (auto* p = dynamic_cast<const FixedSpeedGenerator*>(generator.get()))
            return KindJson("Fixed", { {"speed", p->speed} });
        return nullptr;
    }

    std::shared_ptr<ISpeedGenerator> DecodeSpeedGenerator(const Json& j)
    {
        const auto kind = ReadKind(j);
        if (kind == "Random")
        {
            float min = 1.0f, max = 1.0f;
            ReadFloat(j, "min", min);
            ReadFloat(j, "max", max);
            return std::make_shared<RandomSpeedGenerator>(min, max);
        }
        if (kind == "Fixed")
        {
            float speed = 1.0f;
            ReadFloat(j, "speed", speed);
            return std::make_shared<FixedSpeedGenerator>(speed);
        }
        return nullptr;
    }

    Json EncodePositionOffsetGenerator(const std::shared_ptr<IPositionOffsetGenerator>& generator)
    {
        if (auto* p = dynamic_cast<const FixedPositionOffsetGenerator*>(generator.get()))
            return KindJson("Fixed", { {"offset", EncodeVec3(p->offset)} });
        if (dynamic_cast<const RadialPositionOffsetGenerator*>(generator.get()))
            return KindJson("Radial");
        return nullptr;
    }

    std::shared_ptr<IPositionOffsetGenerator> DecodePositionOffsetGenerator(const Json& j)
    {
        const auto kind = ReadKind(j);
        if (kind == "Fixed")
        {
            glm::vec3 offset(0.0f);
            ReadVec3(j, "offset", offset);
            return std::make_shared<FixedPositionOffsetGenerator>(offset);
        }
        if (kind == "Radial")
            return std::make_shared<RadialPositionOffsetGenerator>();
        return nullptr;
    }

    // ---------------- 更新器 ----------------

    Json EncodeOpacityUpdater(const std::shared_ptr<IOpacityUpdater>& updater)
    {
        if (auto* p = dynamic_cast<const LinearOpacityUpdater*>(updater.get()))
            return KindJson("Linear", { {"goalOpacity", p->goalOpacity} });
        return nullptr;
    }

    std::shared_ptr<IOpacityUpdater> DecodeOpacityUpdater(const Json& j)
    {
        const auto kind = ReadKind(j);
        if (kind == "Linear")
        {
            float goalOpacity = 0.0f;
            ReadFloat(j, "goalOpacity", goalOpacity);
            return std::make_shared<LinearOpacityUpdater>(goalOpacity);
        }
        return nullptr;
    }

    Json EncodeBaseColorUpdater(const std::shared_ptr<IBaseColorUpdater>& updater)
    {
        if (auto* p = dynamic_cast<const LinearBaseColorUpdater*>(updater.get()))
            return KindJson("Linear", { {"goalColor", EncodeVec3(p->goalColor)} });
        if (auto* p = dynamic_cast<const FireColorUpdater*>(updater.get()))
            return KindJson("Fire", {
                {"innerColor", EncodeVec3(p->innerColor)},
                {"midColor", EncodeVec3(p->midColor)},
                {"outerColor", EncodeVec3(p->outerColor)} });
        return nullptr;
    }

    std::shared_ptr<IBaseColorUpdater> DecodeBaseColorUpdater(const Json& j)
    {
        const auto kind = ReadKind(j);
        if (kind == "Linear")
        {
            glm::vec3 goalColor(1.0f);
            ReadVec3(j, "goalColor", goalColor);
            return std::make_shared<LinearBaseColorUpdater>(goalColor);
        }
        if (kind == "Fire")
        {
            FireColorUpdater defaults;
            glm::vec3 inner = defaults.innerColor;
            glm::vec3 mid = defaults.midColor;
            glm::vec3 outer = defaults.outerColor;
            ReadVec3(j, "innerColor", inner);
            ReadVec3(j, "midColor", mid);
            ReadVec3(j, "outerColor", outer);
            return std::make_shared<FireColorUpdater>(inner, mid, outer);
        }
        return nullptr;
    }

    Json EncodeScaleUpdater(const std::shared_ptr<IScaleUpdater>& updater)
    {
        if (auto* p = dynamic_cast<const LinearScaleUpdater*>(updater.get()))
            return KindJson("Linear", { {"goalScale", EncodeVec3(p->goalScale)} });
        return nullptr;
    }

    std::shared_ptr<IScaleUpdater> DecodeScaleUpdater(const Json& j)
    {
        const auto kind = ReadKind(j);
        if (kind == "Linear")
        {
            glm::vec3 goalScale(1.0f);
            ReadVec3(j, "goalScale", goalScale);
            return std::make_shared<LinearScaleUpdater>(goalScale);
        }
        return nullptr;
    }

    Json EncodeRotationUpdater(const std::shared_ptr<IRotationUpdater>& updater)
    {
        if (dynamic_cast<const FollowVelocityRotationUpdater*>(updater.get()))
            return KindJson("FollowVelocity");
        return nullptr;
    }

    std::shared_ptr<IRotationUpdater> DecodeRotationUpdater(const Json& j)
    {
        const auto kind = ReadKind(j);
        if (kind == "FollowVelocity")
            return std::make_shared<FollowVelocityRotationUpdater>();
        return nullptr;
    }

    Json EncodeAccelerationUpdater(const std::shared_ptr<IAccelerationUpdater>& updater)
    {
        if (dynamic_cast<const NoneAccelerationUpdater*>(updater.get()))
            return KindJson("None");
        if (dynamic_cast<const GravityAccelerationUpdater*>(updater.get()))
            return KindJson("Gravity");
        if (auto* p = dynamic_cast<const LinearAccelerationUpdater*>(updater.get()))
            return KindJson("Linear", {
                {"accelerationDir", EncodeVec3(p->accelerationDir)},
                {"initAccelerationValue", p->initAccelerationValue},
                {"goalAccelerationValue", p->goalAccelerationValue} });
        if (auto* p = dynamic_cast<const PerlinTurbulenceUpdater*>(updater.get()))
            return KindJson("PerlinTurbulence", { {"strength", p->strength}, {"frequency", p->frequency} });
        if (auto* p = dynamic_cast<const FiexedAccelerationUpdater*>(updater.get()))
            return KindJson("Fixed", { {"acceleration", EncodeVec3(p->acceleration)} });
        return nullptr;
    }

    std::shared_ptr<IAccelerationUpdater> DecodeAccelerationUpdater(const Json& j)
    {
        const auto kind = ReadKind(j);
        if (kind == "None")
            return nullptr;		// 空指针就是 None
        if (kind == "Gravity")
            return std::make_shared<GravityAccelerationUpdater>();
        if (kind == "Linear")
        {
            glm::vec3 accelerationDir(0.0f, 1.0f, 0.0f);
            float initAccelerationValue = 0.0f;
            float goalAccelerationValue = 0.0f;
            ReadVec3(j, "accelerationDir", accelerationDir);
            ReadFloat(j, "initAccelerationValue", initAccelerationValue);
            ReadFloat(j, "goalAccelerationValue", goalAccelerationValue);
            if (glm::length(accelerationDir) < 1e-6f)
                accelerationDir = glm::vec3(0.0f, 1.0f, 0.0f);
            return std::make_shared<LinearAccelerationUpdater>(accelerationDir, initAccelerationValue, goalAccelerationValue);
        }
        if (kind == "PerlinTurbulence")
        {
            float strength = 0.5f;
            float frequency = 1.5f;
            ReadFloat(j, "strength", strength);
            ReadFloat(j, "frequency", frequency);
            return std::make_shared<PerlinTurbulenceUpdater>(strength, frequency);
        }
        if (kind == "Fixed")
        {
            glm::vec3 acceleration(0.0f);
            ReadVec3(j, "acceleration", acceleration);
            return std::make_shared<FiexedAccelerationUpdater>(acceleration);
        }
        return nullptr;
    }

    // ---------------- 粒子渲染器 ----------------
    const char* GetParticleFacingName(ParticleFacing facing)
    {
        switch (facing)
        {
        case ParticleFacing::CameraPlane: return "CameraPlane";
        case ParticleFacing::CameraPosition: return "CameraPosition";
        case ParticleFacing::VelocityAligned: return "VelocityAligned";
        case ParticleFacing::AxisLocked: return "AxisLocked";
        default: return "CameraPlane";
        }
    }

    bool DecodeParticleFacing(const Json& value, ParticleFacing& facing)
    {
        if (!value.is_string())
            return false;
        const auto name = value.get<std::string>();
        if (name == "CameraPlane") facing = ParticleFacing::CameraPlane;
        else if (name == "CameraPosition") facing = ParticleFacing::CameraPosition;
        else if (name == "VelocityAligned") facing = ParticleFacing::VelocityAligned;
        else if (name == "AxisLocked") facing = ParticleFacing::AxisLocked;
        else return false;
        return true;
    }

    const char* GetParticleMaskName(ParticleMask mask)
    {
        switch (mask)
        {
        case ParticleMask::None: return "None";
        case ParticleMask::Disc: return "Disc";
        case ParticleMask::SoftDisc: return "SoftDisc";
        default: return "Disc";
        }
    }

    bool DecodeParticleMask(const Json& value, ParticleMask& mask)
    {
        if (!value.is_string())
            return false;
        const auto name = value.get<std::string>();
        if (name == "None") mask = ParticleMask::None;
        else if (name == "Disc") mask = ParticleMask::Disc;
        else if (name == "SoftDisc") mask = ParticleMask::SoftDisc;
        else return false;
        return true;
    }

    const char* GetParticleMeshTypeName(ParticleMeshType meshType)
    {
        switch (meshType)
        {
        case ParticleMeshType::Cube: return "Cube";
        case ParticleMeshType::Sphere: return "Sphere";
        case ParticleMeshType::Custom: return "Custom";
        default: return "Sphere";
        }
    }

    bool DecodeParticleMeshType(const Json& value, ParticleMeshType& meshType)
    {
        if (!value.is_string())
            return false;
        const auto name = value.get<std::string>();
        if (name == "Cube") meshType = ParticleMeshType::Cube;
        else if (name == "Sphere") meshType = ParticleMeshType::Sphere;
        else if (name == "Custom") meshType = ParticleMeshType::Custom;
        else return false;
        return true;
    }

    // 场景里只存 GUID：材质由 SceneCodec 解析成 material，模型由 SceneCodec 解析成 model。
    Json EncodeAssetGuid(const std::shared_ptr<Material>& material, const AssetGUID& cachedGuid)
    {
        if (material)
        {
            const auto& guid = !cachedGuid.empty() ? cachedGuid : material->GetAssetGUID();
            if (!guid.empty())
                return guid;
        }

        if (!cachedGuid.empty())
            return cachedGuid;

        return nullptr;
    }

    Json EncodeAssetGuid(const std::shared_ptr<StaticModel>& model, const AssetGUID& cachedGuid)
    {
        if (model)
        {
            const auto& guid = !cachedGuid.empty() ? cachedGuid : model->GetAssetGUID();
            if (!guid.empty())
                return guid;
        }

        if (!cachedGuid.empty())
            return cachedGuid;

        return nullptr;
    }

    // 渲染器：billboard / 内置 cube / 内置 sphere / 自定义模型
    Json EncodeParticleRenderer(const ParticleRenderer& renderer)
    {
        if (const auto* meshRenderer = std::get_if<ParticleMeshRenderer>(&renderer))
        {
            const bool custom = meshRenderer->meshType == ParticleMeshType::Custom;

            return Json{
                { "type", "Mesh" },
                { "meshType", GetParticleMeshTypeName(meshRenderer->meshType) },
                { "modelGuid", custom ? EncodeAssetGuid(meshRenderer->model, meshRenderer->modelGuid) : Json(nullptr) },
                { "materialGuid", custom ? Json(nullptr) : EncodeAssetGuid(meshRenderer->material, meshRenderer->materialGuid) }
            };
        }

        const auto& billboardRenderer = std::get<ParticleBillboardRenderer>(renderer);

        return Json{
            { "type", "Billboard" },
            { "facing", GetParticleFacingName(billboardRenderer.facing) },
            { "mask", GetParticleMaskName(billboardRenderer.mask) },
            { "materialGuid", EncodeAssetGuid(billboardRenderer.material, billboardRenderer.materialGuid) }
        };
    }

    ParticleRenderer DecodeParticleRenderer(const Json& value)
    {
        if (!value.is_object())
            return ParticleBillboardRenderer{};

        const auto type = value.value("type", std::string("Billboard"));

        if (type == "Mesh")
        {
            ParticleMeshRenderer renderer;

            if (auto it = value.find("meshType"); it != value.end())
                DecodeParticleMeshType(*it, renderer.meshType);

            if (auto it = value.find("modelGuid"); it != value.end() && it->is_string())
                renderer.modelGuid = it->get<std::string>();

            if (auto it = value.find("materialGuid"); it != value.end() && it->is_string())
                renderer.materialGuid = it->get<std::string>();

            return renderer;
        }

        ParticleBillboardRenderer renderer;
        if (auto it = value.find("facing"); it != value.end())
            DecodeParticleFacing(*it, renderer.facing);
        if (auto it = value.find("mask"); it != value.end())
            DecodeParticleMask(*it, renderer.mask);
        if (auto it = value.find("materialGuid"); it != value.end() && it->is_string())
            renderer.materialGuid = it->get<std::string>();
        return renderer;
    }

    const char* GetParticleBlendModeName(ParticleBlendMode blendMode)
    {
        switch (blendMode)
        {
        case ParticleBlendMode::Additive: return "Additive";
        case ParticleBlendMode::AlphaBlend: return "AlphaBlend";
        default: return "Additive";
        }
    }

    bool DecodeParticleBlendMode(const Json& value, ParticleBlendMode& blendMode)
    {
        if (!value.is_string())
            return false;
        const auto name = value.get<std::string>();
        if (name == "Additive") blendMode = ParticleBlendMode::Additive;
        else if (name == "AlphaBlend") blendMode = ParticleBlendMode::AlphaBlend;
        else return false;
        return true;
    }
}

std::vector<NComponentCodec::ComponentCodec> NBuiltInCodecs::GenerateBuiltInCodecs()
{
    std::vector<ComponentCodec> result;

    result.push_back(GenerateComponentCodec<Controller>(
        "engine.controller",
        "Controller",
        [](const Controller& controller) {
            return Json{
                { "yaw", controller.yaw },
                { "pitch", controller.pitch },
                { "mouseSensitivity", controller.mouseSensitivity }
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
        }));

    result.push_back(GenerateComponentCodec<CameraComponent>(
        "engine.camera",
        "Camera",
        [](const CameraComponent& component) {
            if (!std::isfinite(component.fov) || !std::isfinite(component.nearPlane) || !std::isfinite(component.farPlane)
                || !std::isfinite(component.worldUp.x) || !std::isfinite(component.worldUp.y) || !std::isfinite(component.worldUp.z)
                )
                throw std::runtime_error("Invalid camera state");
            return Json{
                { "worldUp", EncodeVec3(component.worldUp) },
                { "fov", EncodeFloat(component.fov) },
                { "nearPlane", EncodeFloat(component.nearPlane) },
                { "farPlane", EncodeFloat(component.farPlane) },
                { "isFirstPerson", component.isFirstPerson }
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
        }));

    result.push_back(GenerateComponentCodec<Movement>(
        "engine.movement",
        "Movement",
        [](const Movement& movement) { return Json{ { "canJump", movement.canJump } }; },
        [](Movement& movement, const Json& data) {
            if (!data.is_object() || !data.contains("canJump") || !data["canJump"].is_boolean())
                return false;
            movement.canJump = data["canJump"].get<bool>();
            return true;
        }));

    result.push_back(GenerateComponentCodec<PlayerInput>(
        "engine.player_input",
        "PlayerInput",
        [](const PlayerInput&) { return Json::object(); },
        [](PlayerInput&, const Json& data) { return data.is_object(); }));

    result.push_back(GenerateComponentCodec<TagCurrentControl>(
        "engine.tag_current_control",
        "TagCurrentControl",
        [](const TagCurrentControl&) { return Json::object(); },
        [](TagCurrentControl&, const Json& data) { return data.is_object(); }));

    result.push_back(GenerateComponentCodec<TagFreeCamera>(
        "engine.tag_free_camera",
        "FreeCamera",
        [](const TagFreeCamera& component) { return Json{ { "velocity", component.velocity } }; },
        [](TagFreeCamera& component, const Json& data) {
            float velocity = 0.f;
            if (!data.is_object() || !data.contains("velocity") || !DecodeFiniteFloat(data["velocity"], velocity) || velocity < 0.f)
                return false;
            component.velocity = velocity;
            return true;
        }));

    result.push_back(GenerateComponentCodec<TagMainCamera>(
        "engine.tag_main_camera",
        "TagMainCamera",
        [](const TagMainCamera&) { return Json::object(); },
        [](TagMainCamera&, const Json& data) { return data.is_object(); }));

    result.push_back(GenerateComponentCodec<CameraFollow>(
        "gameplay.camera_follow",
        "CameraFollow",
        [](const CameraFollow& follow) {
            return Json{ { "target", follow.target ? std::to_string(follow.target.getId()) : std::string{} },
        { "offset", EncodeVec3(follow.offset) },{ "smoothSpeed", follow.smoothSpeed } };
        },
        [](CameraFollow& follow, const Json& data) {
            return data.is_object() && data.contains("target") && data["target"].is_string()
                && data.contains("offset") && DecodeVec3(data["offset"], follow.offset)
                && data.contains("smoothSpeed") && DecodeFiniteFloat(data["smoothSpeed"], follow.smoothSpeed);
        },
        {}, {},
        [](CameraFollow& follow, const Json& data, const std::unordered_map<std::string, Entity>& entities) {
            const auto key = data.at("target").get<std::string>();
            if (key.empty()) { follow.target = Entity{}; return true; }
            auto found = entities.find(key);
            if (found == entities.end()) return false;
            follow.target = found->second;
            return true;
        }));

    ComponentEditorBehavior transformEditorBehavior;
    transformEditorBehavior.removable = false;
    result.push_back(GenerateComponentCodec<Transform>(
        "engine.transform",
        "Transform",
        [](const Transform& transform) {
            return Json{
                { "position", EncodeVec3(transform.position) },
                { "rotation", EncodeQuat(transform.rotation) },
                { "scale", EncodeVec3(transform.scale) }
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
        }, {}, {}, {}, 1, {}, transformEditorBehavior));

    result.push_back(GenerateComponentCodec<Transform2D>(
        "engine.transform2D",
        "Transform2D",
        [](const Transform2D& transform) {
            return Json{
                { "position", EncodeVec2(transform.position) },
                { "rotation", EncodeFloat(transform.rotation) },
                { "scale", EncodeVec2(transform.scale) }
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
        }));

    result.push_back(GenerateComponentCodec<Script>(
        "engine.script",
        "Script",
        [](const Script& script) {
            Json data = nullptr;
            if (script.data)
            {
                data = {
                    { "scriptGuid", script.data->scriptGuid }
                };
            }
            return Json{
                { "enabled", script.enabled },
                { "data", std::move(data) }
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
        }
        , {}, {}, {}, 1, {},
        ComponentEditorBehavior{
            .remove =
            [](Entity entity) -> bool
            {
                auto* world = entity.getWorld();
                if (!world)
                    return false;

                bool result = world && world->removeComponentByTypeID(entity, ComponentType<Script>::getId());
                if (entity.hasComponent<ScriptRuntime>())
                    result &= world->removeComponentByTypeID(entity, ComponentType<ScriptRuntime>::getId());
                  return result;
            }
        }
    ));

    result.push_back(GenerateComponentCodec<UIView>(
        "engine.uiview",
        "UI View",
        [](const UIView& view) {
            return Json{
                { "documentGuid", view.documentGuid },
                { "viewName", view.viewName },
                { "layer", view.layer },
                { "capturesInput", view.capturesInput },
                { "enabled", view.enabled }
            };
        },
        [](UIView& view, const Json& data) {
            if (!data.is_object())
                return false;

            view.documentGuid = data.value("documentGuid", AssetGUID());
            // 老场景没有 viewName：存成空串（无名视图），不猜名字，免得串到别的视图上。
            view.viewName = data.value("viewName", std::string());
            view.layer = data.value("layer", 0);
            view.capturesInput = data.value("capturesInput", true);
            view.enabled = data.value("enabled", true);
            view.needReload = true;
            return true;
        },
        // 属性面板里 UIView 有自己的绘制器（Editor/source/include/PropertiesHelper.inl 的
        // DrawComponent<UIView>），viewName 就在那里编辑，所以这里不再列 editorProperties ——
        // 列了也不会被调用（自定义绘制器优先），只会变成两份真话。
        {}, {}, {}, 1,
        std::vector<ComponentProperty>{},
        ComponentEditorBehavior{
            .add =
            [](Entity entity)
            {
                if (entity && !entity.hasComponent<UIView>())
                    entity.addComponent<UIView>();
            },
            .remove =
            [](Entity entity) -> bool
            {
                auto* world = entity.getWorld();
                if (!world)
                    return false;

                bool result = world->removeComponentByTypeID(entity, ComponentType<UIView>::getId());
                if (entity.hasComponent<UIViewRuntime>())
                    result &= world->removeComponentByTypeID(entity, ComponentType<UIViewRuntime>::getId());
                return result;
            }
        }
    ));

    result.push_back(GenerateComponentCodec<Physics>(
        "engine.physics",
        "Physics",
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
                { "bodyType", GetBodyTypeName(physics.bodyType) },
                { "mass", physics.mass },
                { "friction", physics.friction },
                { "rollingFriction", physics.rollingFriction },
                { "restitution", physics.restitution },
                { "isSensor", physics.isSensor },
                { "fixedRotation", physics.fixedRotation },
                { "isBullet", physics.isBullet },
                { "allowSleep", physics.allowSleep },
                { "isCharacter", physics.isCharacter },
                { "stepHeight", physics.stepHeight },
                { "walkSpeed", physics.walkSpeed },
                { "jumpSpeed", physics.jumpSpeed },
                { "maxSlope", physics.maxSlope },
                { "maxPenetrationDepth", physics.maxPenetrationDepth },
                { "collisionShapes", std::move(children) }
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
        }
        , {}, {}, {}, 1, {},
        ComponentEditorBehavior{
            .remove =
            [](Entity entity) -> bool
            {
                auto* world = entity.getWorld();
                if (!world)
                    return false;

                bool result = world && world->removeComponentByTypeID(entity, ComponentType<Physics>::getId());
                if (entity.hasComponent<PhysicsRuntime>())
                    result &= world->removeComponentByTypeID(entity, ComponentType<PhysicsRuntime>::getId());
                return result;
            }
        }
    ));

    result.push_back(GenerateComponentCodec<RenderStaticModel>(
        "engine.render_static_model",
        "Render StaticModel",
        [](const RenderStaticModel& renderModel) {
            Json modelGuid = nullptr;
            if (renderModel.model)
            {
                if (renderModel.model->GetAssetGUID().empty())
                    throw std::runtime_error("RenderStaticModel has no asset GUID");
                modelGuid = renderModel.model->GetAssetGUID();
            }
            Json materials = Json::array();
            if (renderModel.model)
            {
                const auto& meshInfos = renderModel.model->GetMeshInfos();
                for (uint32_t index = 0; index < meshInfos.size(); ++index)
                {
                    const auto& material = meshInfos[index].material;
                    if (!material)
                        continue;

                    const auto& guid = material->GetAssetGUID();
                    Json record = { { "index", index },{ "guid", guid } };
                    if (guid.empty())
                    {
                        const auto properties = material->GetProperties();
                        record["properties"] = {
                            { "albedo", EncodeVec3(properties.albedo) },
                            { "metallic", properties.metallic },
                            { "roughness", properties.roughness },
                            { "ambientOcclusion", properties.ambientOcclusion },
                            { "opacity", properties.opacity },
                            { "ior", properties.IOR },
                            { "emissionColor", EncodeVec3(properties.emissionColor) },
                            { "emissionStrength", properties.emissionStrength },
                            { "alphaMode", static_cast<uint32_t>(properties.alphamode) },
                            { "maskThreshold", properties.maskthreshold },
                            { "twoSided", properties.twosided }
                        };
                    }
                    materials.push_back(std::move(record));
                }
            }

            return Json{
                { "enabled", renderModel.visible },
                { "trans", EncodeMat4(renderModel.trans) },
                { "modelGuid", std::move(modelGuid) },
                { "materials", std::move(materials) }
            };
        },
        [](RenderStaticModel& renderModel, const Json& data) {
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
            renderModel.materialOverrideGUIDs.clear();
            renderModel.materialOverrideProperties.clear();
            if (data.contains("materials"))
            {
                if (!data["materials"].is_array())
                    return false;
                for (const auto& record : data["materials"])
                {
                    if (!record.is_object()
                        || !record.contains("index") || !record["index"].is_number_unsigned()
                        || !record.contains("guid") || !record["guid"].is_string())
                        return false;
                    const uint32_t index = record["index"].get<uint32_t>();
                    const auto guid = record["guid"].get<std::string>();
                    if (!guid.empty())
                    {
                        renderModel.materialOverrideGUIDs[index] = guid;
                        continue;
                    }
                    if (!record.contains("properties") || !record["properties"].is_object())
                        return false;

                    const auto& p = record["properties"];
                    MaterialProperties properties;
                    uint32_t alphaMode = 0;
                    if (!p.contains("albedo") || !DecodeVec3(p["albedo"], properties.albedo)
                        || !p.contains("emissionColor") || !DecodeVec3(p["emissionColor"], properties.emissionColor)
                        || !p.contains("metallic") || !DecodeFiniteFloat(p["metallic"], properties.metallic)
                        || !p.contains("roughness") || !DecodeFiniteFloat(p["roughness"], properties.roughness)
                        || !p.contains("ambientOcclusion") || !DecodeFiniteFloat(p["ambientOcclusion"], properties.ambientOcclusion)
                        || !p.contains("opacity") || !DecodeFiniteFloat(p["opacity"], properties.opacity)
                        || !p.contains("ior") || !DecodeFiniteFloat(p["ior"], properties.IOR)
                        || !p.contains("emissionStrength") || !DecodeFiniteFloat(p["emissionStrength"], properties.emissionStrength)
                        || !p.contains("alphaMode") || !p["alphaMode"].is_number_unsigned()
                        || !p.contains("maskThreshold") || !DecodeFiniteFloat(p["maskThreshold"], properties.maskthreshold)
                        || !p.contains("twoSided") || !p["twoSided"].is_boolean())
                        return false;
                    alphaMode = p["alphaMode"].get<uint32_t>();
                    if (alphaMode > static_cast<uint32_t>(AlphaMode::Blend))
                        return false;
                    properties.alphamode = static_cast<AlphaMode>(alphaMode);
                    properties.twosided = p["twoSided"].get<bool>();
                    renderModel.materialOverrideProperties[index] = properties;
                }
            }
            if (modelGuid.is_null())
                renderModel.model.reset();
            else
            {
                const auto guid = modelGuid.get<std::string>();
                if (guid.empty())
                    return false;
                renderModel.model = std::make_shared<StaticModel>();
                renderModel.model->SetAssetGUID(guid);
            }
            return true;
        }));

    result.push_back(GenerateComponentCodec<RenderSkinnedModel>(
        "engine.render_skinned_model",
        "Render SkinnedModel",
        [](const RenderSkinnedModel& renderModel) {
            Json modelGuid = nullptr;
            if (renderModel.model)
            {
                if (renderModel.model->GetAssetGUID().empty())
                    throw std::runtime_error("RenderSkinnedModel has no asset GUID");
                modelGuid = renderModel.model->GetAssetGUID();
            }

            Json materials = Json::array();
            if (renderModel.model)
            {
                const auto& meshInfos = renderModel.model->GetMeshInfos();
                for (uint32_t index = 0; index < meshInfos.size(); ++index)
                {
                    const auto& material = meshInfos[index].material;
                    if (!material)
                        continue;

                    const auto& guid = material->GetAssetGUID();
                    Json record = { { "index", index },{ "guid", guid } };
                    if (guid.empty())
                    {
                        const auto properties = material->GetProperties();
                        record["properties"] = {
                            { "albedo", EncodeVec3(properties.albedo) },
                            { "metallic", properties.metallic },
                            { "roughness", properties.roughness },
                            { "ambientOcclusion", properties.ambientOcclusion },
                            { "opacity", properties.opacity },
                            { "ior", properties.IOR },
                            { "emissionColor", EncodeVec3(properties.emissionColor) },
                            { "emissionStrength", properties.emissionStrength },
                            { "alphaMode", static_cast<uint32_t>(properties.alphamode) },
                            { "maskThreshold", properties.maskthreshold },
                            { "twoSided", properties.twosided }
                        };
                    }
                    materials.push_back(std::move(record));
                }
            }

            return Json{
                { "enabled", renderModel.visible },
                { "trans", EncodeMat4(renderModel.trans) },
                { "modelGuid", std::move(modelGuid) },
                { "materials", std::move(materials) }
            };
        },
        [](RenderSkinnedModel& renderModel, const Json& data) {
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

            renderModel.materialOverrideGUIDs.clear();
            renderModel.materialOverrideProperties.clear();
            if (data.contains("materials"))
            {
                if (!data["materials"].is_array())
                    return false;
                for (const auto& record : data["materials"])
                {
                    if (!record.is_object()
                        || !record.contains("index") || !record["index"].is_number_unsigned()
                        || !record.contains("guid") || !record["guid"].is_string())
                        return false;
                    const uint32_t index = record["index"].get<uint32_t>();
                    const auto guid = record["guid"].get<std::string>();
                    if (!guid.empty())
                    {
                        renderModel.materialOverrideGUIDs[index] = guid;
                        continue;
                    }
                    if (!record.contains("properties") || !record["properties"].is_object())
                        return false;

                    const auto& p = record["properties"];
                    MaterialProperties properties;
                    uint32_t alphaMode = 0;
                    if (!p.contains("albedo") || !DecodeVec3(p["albedo"], properties.albedo)
                        || !p.contains("emissionColor") || !DecodeVec3(p["emissionColor"], properties.emissionColor)
                        || !p.contains("metallic") || !DecodeFiniteFloat(p["metallic"], properties.metallic)
                        || !p.contains("roughness") || !DecodeFiniteFloat(p["roughness"], properties.roughness)
                        || !p.contains("ambientOcclusion") || !DecodeFiniteFloat(p["ambientOcclusion"], properties.ambientOcclusion)
                        || !p.contains("opacity") || !DecodeFiniteFloat(p["opacity"], properties.opacity)
                        || !p.contains("ior") || !DecodeFiniteFloat(p["ior"], properties.IOR)
                        || !p.contains("emissionStrength") || !DecodeFiniteFloat(p["emissionStrength"], properties.emissionStrength)
                        || !p.contains("alphaMode") || !p["alphaMode"].is_number_unsigned()
                        || !p.contains("maskThreshold") || !DecodeFiniteFloat(p["maskThreshold"], properties.maskthreshold)
                        || !p.contains("twoSided") || !p["twoSided"].is_boolean())
                        return false;
                    alphaMode = p["alphaMode"].get<uint32_t>();
                    if (alphaMode > static_cast<uint32_t>(AlphaMode::Blend))
                        return false;
                    properties.alphamode = static_cast<AlphaMode>(alphaMode);
                    properties.twosided = p["twoSided"].get<bool>();
                    renderModel.materialOverrideProperties[index] = properties;
                }
            }

            if (modelGuid.is_null())
                renderModel.model.reset();
            else
            {
                const auto guid = modelGuid.get<std::string>();
                if (guid.empty())
                    return false;
                renderModel.model = std::make_shared<SkinnedModel>();
                renderModel.model->SetAssetGUID(guid);
            }
            return true;
        }));

    result.push_back(GenerateComponentCodec<SkeletonAnimatorGroup>(
        "engine.skeleton_animator_group",
        "Skeleton Animator Group",
        [](const SkeletonAnimatorGroup& group) {
            Json animators = Json::array();
            for (const auto& [name, data] : group.animatorDatas)
            {
                if (!data)
                    continue;
                animators.push_back(Json{
                    { "name", name },
                    { "clip", data->clipGuid },
                    { "loop", data->maxLoopCount }
                });
            }

            // default 存名字不存下标：animator 是按名字寻址的，改名后这里能看出是悬空引用。
            return Json{
                { "skeleton", group.skeletonGuid },
                { "default", group.defaultAnimator },
                { "idle", group.idleAnimator },
                { "animators", std::move(animators) }
            };
        },
        [](SkeletonAnimatorGroup& group, const Json& data) {
            if (!data.is_object()
                || !data.contains("skeleton") || !data["skeleton"].is_string()
                || !data.contains("default") || !data["default"].is_string()
                || !data.contains("animators") || !data["animators"].is_array())
                return false;

            group.animatorDatas.clear();
            group.skeleton.reset();
            group.skeletonGuid = data["skeleton"].get<std::string>();
            group.defaultAnimator = data["default"].get<std::string>();
            group.idleAnimator = data.value("idle", std::string());

            for (const auto& record : data["animators"])
            {
                if (!record.is_object()
                    || !record.contains("name") || !record["name"].is_string()
                    || !record.contains("clip") || !record["clip"].is_string()
                    || !record.contains("loop") || !record["loop"].is_number_integer())
                    return false;

                const std::string name = record["name"].get<std::string>();
                if (name.empty() || group.animatorDatas.find(name) != group.animatorDatas.end())
                    return false;

                auto slot = std::make_shared<SkeletonAnimatorGroup::SkeletonAnimatorData>(record["clip"].get<std::string>());
                slot->maxLoopCount = record["loop"].get<int>();
                group.animatorDatas[name] = std::move(slot);
            }

            if (!group.defaultAnimator.empty() && group.animatorDatas.find(group.defaultAnimator) == group.animatorDatas.end())
                group.defaultAnimator.clear();
            if (!group.idleAnimator.empty() && group.animatorDatas.find(group.idleAnimator) == group.animatorDatas.end())
                group.idleAnimator.clear();
            return true;
        }));

    result.push_back(GenerateComponentCodec<RenderLight>(
        "engine.render_light",
        "RenderLight",
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
                    { "color", EncodeVec3(d->color) },
                    { "luxIntensity", d->luxIntensity },
                    { "volumetricScatteringIntensity", d->volumetricScatteringIntensity },
                    { "castShadow", d->castShadow },
                    { "cascadeLevel", d->cascadeLevel },
                    { "shadowMapSize", d->shadowMapSize }
                };
                break;
            }

            case LightType::Point:
            {
                const auto* d = light.GetData<PointLightData>();
                if (!d) return Json{};

                data = {
                    { "color", EncodeVec3(d->color) },
                    { "cdIntensity", d->cdIntensity },
                    { "volumetricScatteringIntensity", d->volumetricScatteringIntensity },
                    { "castShadow", d->castShadow },
                    { "shadowMapSize", d->shadowMapSize }
                };
                break;
            }

            case LightType::Spot:
            {
                const auto* d = light.GetData<SpotLightData>();
                if (!d) return Json{};

                data = {
                    { "color", EncodeVec3(d->color) },
                    { "cdIntensity", d->cdIntensity },
                    { "volumetricScatteringIntensity", d->volumetricScatteringIntensity },
                    { "cutOffAngle", d->cutOffAngle },
                    { "outercutOffAngle", d->outercutOffAngle },
                    { "castShadow", d->castShadow },
                    { "shadowMapSize", d->shadowMapSize }
                };
                break;
            }

            default:
                return Json{};
            }

            return {
                { "type", static_cast<uint32_t>(light.type) },
                { "renderCube", light.renderCube },
                { "data", std::move(data) }
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
        }));

    result.push_back(GenerateComponentCodec<ParticleEmitter>(
        "engine.particle_emitter",
        "Particle Emitter",
        // Encode
        [](const ParticleEmitter& emitter) {
            const auto& p = emitter.emitterParams;

            return Json{
                { "enable", emitter.enable },
                { "renderer", EncodeParticleRenderer(p.renderer) },
                { "blendMode", GetParticleBlendModeName(p.blendMode) },
                { "emitRate", p.emitRate },
                { "emitRateJitter", p.emitRateJitter },
                { "burstAmount", p.burstAmount },
                { "burstCount", p.burstCount },
                { "burstInterval", p.burstInterval },
                { "warmupTime", p.warmupTime },
                { "scaleGenerator", EncodeScaleGenerator(p.scaleGenerator) },
                { "colorGenerator", EncodeColorGenerator(p.colorGenerator) },
                { "lifeTimeGenerator", EncodeLifeTimeGenerator(p.lifeTimeGenerator) },
                { "opacityGenerator", EncodeOpacityGenerator(p.opacityGenerator) },
                { "positionGenerator", EncodePositionGenerator(p.posGenerator) },
                { "directionGenerator", EncodeDirectionGenerator(p.dirGenerator) },
                { "speedGenerator", EncodeSpeedGenerator(p.speedGenerator) },
                { "rotationGenerator", EncodeRotationGenerator(p.rotGenerator) },
                { "positionOffsetGenerator", EncodePositionOffsetGenerator(p.posOffsetGenerator) },
                { "opacityUpdater", EncodeOpacityUpdater(p.opacityUpdater) },
                { "baseColorUpdater", EncodeBaseColorUpdater(p.baseColorUpdater) },
                { "scaleUpdater", EncodeScaleUpdater(p.scaleUpdater) },
                { "rotationUpdater", EncodeRotationUpdater(p.rotationUpdater) },
                { "localAccelerationUpdater", EncodeAccelerationUpdater(p.localAccelerationUpdater) },
                { "worldAccelerationUpdater", EncodeAccelerationUpdater(p.worldAccelerationUpdater) }
            };
        },
        // Decode
        [](ParticleEmitter& emitter, const Json& data) -> bool {
            if (!data.is_object())
                return false;
            if (data.contains("enable") && !data["enable"].is_boolean())
                return false;

            ParticleEmitter decoded;
            decoded.enable = data.value("enable", true);

            auto& p = decoded.emitterParams;
            p.renderer = DecodeParticleRenderer(data.value("renderer", Json()));
            if (data.contains("blendMode") && !DecodeParticleBlendMode(data["blendMode"], p.blendMode))
                return false;

            ReadFloat(data, "emitRate", p.emitRate);
            ReadFloat(data, "emitRateJitter", p.emitRateJitter);
            ReadInt(data, "burstAmount", p.burstAmount);
            ReadInt(data, "burstCount", p.burstCount);
            ReadFloat(data, "burstInterval", p.burstInterval);
            ReadFloat(data, "warmupTime", p.warmupTime);

            p.scaleGenerator = DecodeScaleGenerator(data.value("scaleGenerator", Json()));
            p.colorGenerator = DecodeColorGenerator(data.value("colorGenerator", Json()));
            p.lifeTimeGenerator = DecodeLifeTimeGenerator(data.value("lifeTimeGenerator", Json()));
            p.opacityGenerator = DecodeOpacityGenerator(data.value("opacityGenerator", Json()));
            p.posGenerator = DecodePositionGenerator(data.value("positionGenerator", Json()));
            p.dirGenerator = DecodeDirectionGenerator(data.value("directionGenerator", Json()));
            p.speedGenerator = DecodeSpeedGenerator(data.value("speedGenerator", Json()));
            p.rotGenerator = DecodeRotationGenerator(data.value("rotationGenerator", Json()));
            p.posOffsetGenerator = DecodePositionOffsetGenerator(data.value("positionOffsetGenerator", Json()));
            p.opacityUpdater = DecodeOpacityUpdater(data.value("opacityUpdater", Json()));
            p.baseColorUpdater = DecodeBaseColorUpdater(data.value("baseColorUpdater", Json()));
            p.scaleUpdater = DecodeScaleUpdater(data.value("scaleUpdater", Json()));
            p.rotationUpdater = DecodeRotationUpdater(data.value("rotationUpdater", Json()));
            p.localAccelerationUpdater = DecodeAccelerationUpdater(data.value("localAccelerationUpdater", Json()));
            p.worldAccelerationUpdater = DecodeAccelerationUpdater(data.value("worldAccelerationUpdater", Json()));

            emitter = std::move(decoded);
            return true;
        },
        {}, {}, {}, 1,
        std::vector<ComponentProperty>{
            MakeComponentProperty<ParticleEmitter, bool>("enable", "Enable", &ParticleEmitter::enable, EditorPropertyKind::Boolean)
        },
        // 编辑器里 Add Component 时给一套能立刻出画面的默认参数（否则 IsVaild() 为 false 什么都看不到）。
        ComponentEditorBehavior{
            .add = [](Entity entity) {
                ParticleEmitter emitter;
                FillDefaultParticleEmitterParams(emitter.emitterParams);
                entity.addComponent<ParticleEmitter>(std::move(emitter));
            }
        }
    ));

    return result;
}
