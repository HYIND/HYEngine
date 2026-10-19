#include "Script/BaseScriptModule.h"
#include "ECSCore/World.h"

bool TransformScriptModule::SetPosition(Entity entity, float x, float y, float z)
{
	if (!IsValid(_world, entity))
		return false;

	Transform* transform = entity.tryGetComponent<Transform>();
	if (!transform)
		return false;

	transform->position = { x, y, z };

	auto* physics = entity.tryGetComponent<Physics>();
	auto* runtime = entity.tryGetComponent<PhysicsRuntime>();

	if (physics && runtime)
	{
		physics->forceSyncTransform = true;
		if (runtime->body && !runtime->body->isActive())
			runtime->body->activate(true);
		if (physics->isCharacter && runtime->character && runtime->ghostObject && !runtime->ghostObject->isActive())
			runtime->ghostObject->activate(true);
	}

	return true;
}

bool TransformScriptModule::Translate(Entity entity, float x, float y, float z)
{
	if (!IsValid(_world, entity))
		return false;

	Transform* transform = entity.tryGetComponent<Transform>();
	if (!transform)
		return false;

	transform->position += glm::vec3(x, y, z);

	auto* physics = entity.tryGetComponent<Physics>();
	auto* runtime = entity.tryGetComponent<PhysicsRuntime>();

	if (physics && runtime)
	{
		physics->forceSyncTransform = true;
		if (runtime->body && !runtime->body->isActive())
			runtime->body->activate(true);
		if (physics->isCharacter && runtime->character && runtime->ghostObject && !runtime->ghostObject->isActive())
			runtime->ghostObject->activate(true);
	}
	return true;
}

TransformScriptModule::TransformScriptModule(World* world) :_world(world) {}

void TransformScriptModule::Register(sol::state_view lua, sol::usertype<Entity>& entityType)
{
	auto getPosition = [this](Entity entity) {
		return GetPosition(entity);
		};
	auto getDirection = [this](Entity entity) {
		return GetDirection(entity);
		};
	auto setPosition_Float = [this](Entity entity, float x, float y, float z) {
		return SetPosition(entity, x, y, z);
		};
	auto setPosition_Vector = [this](Entity entity, glm::vec3 vec) {
		return SetPosition(entity, vec.x, vec.y, vec.z);
		};
	auto translate_Float = [this](Entity entity, float x, float y, float z) {
		return Translate(entity, x, y, z);
		};
	auto translate_Vector = [this](Entity entity, glm::vec3 vec) {
		return Translate(entity, vec.x, vec.y, vec.z);
		};

	sol::table transform = lua.create_table();
	transform.set_function("get_position", getPosition);
	transform.set_function("get_direction", getDirection);
	lua.set_function("set_position", sol::overload(
		setPosition_Float,
		setPosition_Vector
	));

	lua.set_function("translate", sol::overload(
		translate_Float,
		translate_Vector
	));
	lua["Transform"] = transform;

	entityType.set("get_position", getPosition);
	entityType.set("get_direction", getDirection);
	entityType.set_function("set_position", sol::overload(
		setPosition_Float,
		setPosition_Vector
	));
	entityType.set_function("translate", sol::overload(
		translate_Float,
		translate_Vector
	));
}

std::unordered_map<std::string, IScriptModule::ComponentRequestFunc> TransformScriptModule::ComponentRegister()
{
	std::unordered_map<std::string, IScriptModule::ComponentRequestFunc> result;
	result["Transform"] =
		[](World& world, Entity entity) {
		if (!entity.hasComponent<Transform>())
			entity.addComponent<Transform>();
		};
	return result;
}

void GLMMathModule::Register(sol::state_view lua, sol::usertype<Entity>& entityType)
{
	RegisterVector(lua);
	RegisterQuaternion(lua);
	RegisterMatrix4(lua);
}

void GLMMathModule::RegisterVector(sol::state_view lua)
{
	lua.new_usertype<glm::vec3>(
		"vec3",

		// 构造函数：Vector3.new(x, y, z)
		sol::constructors<
		glm::vec3(),
		glm::vec3(glm::vec3),
		glm::vec3(float, float, float)
		>(),

		"x", sol::property(
			[](const glm::vec3& v) { return v.x; },
			[](glm::vec3& v, float x) { v.x = x; }
		),

		"y", sol::property(
			[](const glm::vec3& v) { return v.y; },
			[](glm::vec3& v, float y) { v.y = y; }
		),

		"z", sol::property(
			[](const glm::vec3& v) { return v.z; },
			[](glm::vec3& v, float z) { v.z = z; }
		),

		"length", [](const glm::vec3& v) {
			return glm::length(v);
		},
		"length_squared", [](const glm::vec3& v) {
			return glm::dot(v, v);
		},
		"normalize", [](glm::vec3& v) {
			return glm::normalize(v);
		},
		"dot", [](const glm::vec3& v, const glm::vec3& other) {
			return glm::dot(v, other);
		},
		"cross", [](const glm::vec3& v, const glm::vec3& other) {
			return glm::cross(v, other);
		},
		"distance", [](const glm::vec3& v, const glm::vec3& other) {
			return glm::distance(v, other);
		},

		sol::meta_function::addition,
		[](const glm::vec3& a, const glm::vec3& b) {
			return a + b;
		},
		sol::meta_function::subtraction,
		[](const glm::vec3& a, const glm::vec3& b) {
			return a - b;
		},
		sol::meta_function::multiplication,
		sol::overload(
			[](const glm::vec3& v, float s) {
				return v * s;
			},
			[](float s, const glm::vec3& v) {
				return s * v;
			},
			[](const glm::vec3& a, const glm::vec3& b) {
				return a * b;
			}
		),
		sol::meta_function::division,
		[](const glm::vec3& v, float s) {
			if (s == 0.0f)
				throw std::runtime_error("Vector3 division by zero");
			return v / s;
		},
		sol::meta_function::unary_minus,
		[](const glm::vec3& v) {
			return -v;
		},
		sol::meta_function::equal_to,
		[](const glm::vec3& a, const glm::vec3& b) {
			return a == b;
		}
	);
}

void GLMMathModule::RegisterQuaternion(sol::state_view lua)
{
	lua.new_usertype<glm::quat>(
		"quat",

		sol::constructors<
		glm::quat(),
		glm::quat(float, float, float, float)
		>(),

		// 分量
		"x", sol::property(
			[](const glm::quat& q) { return q.x; },
			[](glm::quat& q, float v) { q.x = v; }
		),
		"y", sol::property(
			[](const glm::quat& q) { return q.y; },
			[](glm::quat& q, float v) { q.y = v; }
		),
		"z", sol::property(
			[](const glm::quat& q) { return q.z; },
			[](glm::quat& q, float v) { q.z = v; }
		),
		"w", sol::property(
			[](const glm::quat& q) { return q.w; },
			[](glm::quat& q, float v) { q.w = v; }
		),

		"length", [](const glm::quat& q) {
			return glm::length(q);
		},
		"length_squared", [](const glm::quat& q) {
			return glm::dot(q, q);
		},
		"normalize", [](glm::quat& q) {
			return glm::normalize(q);
		},
		"conjugate", [](const glm::quat& q) {
			return glm::conjugate(q);// 共轭和逆
		},
		"inverse", [](const glm::quat& q) {
			return glm::inverse(q);
		},
		"to_euler", [](const glm::quat& q) {
			return glm::eulerAngles(q);// 将四元数转换为欧拉角，返回弧度制 Vector3
		},
		"rotate", [](const glm::quat& q, const glm::vec3& v) {
			return q * v;	// 四元数旋转向量
		},

		// 四元数乘法
		sol::meta_function::multiplication,
		sol::overload(
			[](const glm::quat& a, const glm::quat& b) {
				return a * b;
			},
			[](const glm::quat& q, const glm::vec3& v) {
				return q * v;
			}
		),
		sol::meta_function::equal_to,
		[](const glm::quat& a, const glm::quat& b) {
			return a == b;
		}
	);

	// 四元数工具函数
	sol::table math = lua["Math"].get_or_create<sol::table>();

	math.set_function(
		"quat_from_euler",
		[](const glm::vec3& eulerRadians) {
			return glm::quat(eulerRadians);
		}
	);

	math.set_function(
		"quat_from_axis_angle",
		[](const glm::vec3& axis, float angleRadians) {
			float len = glm::length(axis);
			if (len <= 0.0f)
				return glm::quat(1, 0, 0, 0);

			return glm::angleAxis(angleRadians, axis / len);
		}
	);

	math.set_function(
		"quat_slerp",
		[](const glm::quat& a, const glm::quat& b, float t) {
			return glm::slerp(a, b, glm::clamp(t, 0.0f, 1.0f));
		}
	);
}

void GLMMathModule::RegisterMatrix4(sol::state_view lua)
{
	lua.new_usertype<glm::mat4>(
		"mat4",

		sol::constructors<
		glm::mat4(),
		glm::mat4(float)
		>(),

		"transpose", [](const glm::mat4& m) {
			return glm::transpose(m);
		},
		"inverse", [](const glm::mat4& m) {
			return glm::inverse(m);
		},
		"determinant", [](const glm::mat4& m) {
			return glm::determinant(m);
		},

		// 获取 / 设置元素，row、col 使用 Lua 的 1-based 索引
		"get", [](const glm::mat4& m, int row, int col) {
			if (row < 1 || row > 4 || col < 1 || col > 4)
				throw std::out_of_range("Matrix4 index out of range");
			return m[col - 1][row - 1];
		},
		"set", [](glm::mat4& m, int row, int col, float value) {
			if (row < 1 || row > 4 || col < 1 || col > 4)
				throw std::out_of_range("Matrix4 index out of range");
			m[col - 1][row - 1] = value;
		},

		"transform_point", [](const glm::mat4& m, const glm::vec3& p) {
			glm::vec4 result = m * glm::vec4(p, 1.0f);

			if (result.w != 0.0f && result.w != 1.0f)
				return glm::vec3(result) / result.w;

			return glm::vec3(result);
		},
		"transform_direction", [](const glm::mat4& m, const glm::vec3& v) {
			return glm::vec3(m * glm::vec4(v, 0.0f));
		},

		"translate", [](const glm::mat4& m, const glm::vec3& offset) {
			return glm::translate(m, offset);
		},

		// 旋转：角度为弧度
		"rotate", [](const glm::mat4& m, float angleRadians, const glm::vec3& axis) {
			float len = glm::length(axis);
			if (len <= 0.0f)
				return m;

			return glm::rotate(m, angleRadians, axis / len);
		},

		"scale", [](const glm::mat4& m, const glm::vec3& amount) {
			return glm::scale(m, amount);
		},

		// 矩阵乘法
		sol::meta_function::multiplication,
		sol::overload(
			[](const glm::mat4& a, const glm::mat4& b) {
				return a * b;
			},
			[](const glm::mat4& m, const glm::vec3& v) {
				return glm::vec3(m * glm::vec4(v, 1.0f));
			}
		)
	);

	// 矩阵工具函数
	sol::table math = lua["Math"].get_or_create<sol::table>();

	math.set_function("matrix_identity", [] {
		return glm::mat4(1.0f);
		});

	math.set_function(
		"matrix_perspective",
		[](float fovRadians, float aspect, float nearPlane, float farPlane) {
			return glm::perspective(
				fovRadians, aspect, nearPlane, farPlane);
		}
	);

	math.set_function(
		"matrix_look_at",
		[](const glm::vec3& eye,
			const glm::vec3& center,
			const glm::vec3& up) {
				return glm::lookAt(eye, center, up);
		}
	);
}

std::tuple<bool, glm::vec3> TransformScriptModule::GetPosition(Entity entity) const
{
	if (!IsValid(_world, entity))
		return { false, glm::vec3(0.0) };

	const Transform* transform = entity.tryGetComponent<Transform>();
	if (!transform)
		return { false, glm::vec3(0.0) };

	return { true, transform->position };
}

std::tuple<bool, glm::vec3> TransformScriptModule::GetDirection(Entity entity) const
{
	if (!IsValid(_world, entity))
		return { false, glm::vec3(0.0) };

	const Transform* transform = entity.tryGetComponent<Transform>();
	if (!transform)
		return { false, glm::vec3(0.0) };

	auto dir = transform->GetDirection();
	return { true, dir };
}