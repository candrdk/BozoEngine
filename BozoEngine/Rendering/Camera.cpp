#include "Camera.h"

#include "../Core/ResourceManager.h"

#include <GLFW/glfw3.h>
#undef APIENTRY	// fix macro redefinition warning

#include <glm/gtc/matrix_transform.hpp>		// lookAt

Camera::Camera(glm::vec3 startPos, float speed, float fov, float aspect, float zNear, float pitch, float yaw)
	: position{ startPos }, speed{ speed }, fov{ fov }, aspect{ aspect }, zNear{ zNear }, pitch{ pitch }, yaw{ yaw }
{
	UpdateDirection();
	UpdateMatrices();

	ResourceManager* rm = ResourceManager::ptr;

	m_ubo = rm->CreateBuffer({
		.debugName = "Camera UBO",
		.byteSize = arraysize(m_bindgroups) * glm::max(sizeof(UBO), 256ull),	// TODO: 256 should be device.properties.minUniformBufferOffsetAlignment
		.usage = Usage::UNIFORM_BUFFER,
		.memory = Memory::Upload
	});

	m_bindgroupLayout = rm->CreateBindGroupLayout({
		.debugName = "Camera ubo bindgroup layout",
		.bindings = { {.type = Binding::Type::BUFFER, .stages = ShaderStage::VERTEX | ShaderStage::FRAGMENT } }
	});

	for (u32 i = 0; i < arraysize(m_bindgroups); i++) {
		m_bindgroups[i] = rm->CreateBindGroup({
			.debugName = "Camera UBO bindgroup",
			.layout = m_bindgroupLayout,
			.buffers = { {.binding = 0, .buffer = m_ubo, .offset = i * 256, .size = sizeof(UBO)} }	// TODO: 256 should be device.properties.minUniformBufferOffsetAlignment
		});
	}
}

Camera::~Camera() {
	ResourceManager* rm = ResourceManager::ptr;

	rm->DestroyBuffer(m_ubo);
	rm->DestroyBindGroupLayout(m_bindgroupLayout);
}

void Camera::Update(float deltaTime) {
	glm::vec3 move = glm::vec3(0.0f);

	if (moveUp)	   move += glm::vec3(0.0f, 1.0f, 0.0f);
	if (moveDown)  move -= glm::vec3(0.0f, 1.0f, 0.0f);
	if (moveRight) move += glm::normalize(glm::cross(direction, glm::vec3(0.0f, 1.0f, 0.0f)));
	if (moveLeft)  move -= glm::normalize(glm::cross(direction, glm::vec3(0.0f, 1.0f, 0.0f)));
	if (moveFront) move += direction;
	if (moveBack)  move -= direction;

	if (glm::length(move) > 0.01f)
		position += deltaTime * speed * (speedBoost ? 2.0f : 1.0f) * glm::normalize(move);

	UpdateMatrices();
}

void Camera::ProcessKeyboard(int key, int action) {
	if (action == GLFW_REPEAT) return;
	bool move = (action == GLFW_PRESS) ? true : false;

	switch (key) {
	case GLFW_KEY_SPACE:
		moveUp = move;
		break;
	case GLFW_KEY_LEFT_CONTROL:
		moveDown = move;
		break;
	case GLFW_KEY_W:
		moveFront = move;
		break;
	case GLFW_KEY_A:
		moveLeft = move;
		break;
	case GLFW_KEY_S:
		moveBack = move;
		break;
	case GLFW_KEY_D:
		moveRight = move;
		break;
	case GLFW_KEY_LEFT_SHIFT:
		speedBoost = move;
		break;
	}
}

void Camera::ProcessMouseMovement(double xoffset, double yoffset) {
	pitch += mouseSensitivity * yoffset;
	yaw += mouseSensitivity * xoffset;

	if (pitch > 80.0) pitch = 80.0;
	if (pitch < -80.0) pitch = -80.0;

	UpdateDirection();
}

void Camera::UpdateDirection() {
	direction = glm::normalize(glm::vec3(
		glm::cos(glm::radians(yaw)) * glm::cos(glm::radians(pitch)),
		glm::sin(glm::radians(pitch)),
		glm::sin(glm::radians(yaw)) * glm::cos(glm::radians(pitch))));
}

void Camera::UpdateMatrices() {
	// Intermediate transformation aligning the axes with the expected format of Vulkans fixed-function steps.
	// See: https://johannesugb.github.io/gpu-programming/setting-up-a-proper-vulkan-projection-matrix/
	constexpr glm::mat4 X = glm::mat4(
		1.0f,  0.0f,  0.0f, 0.0f,
		0.0f, -1.0f,  0.0f, 0.0f,
		0.0f,  0.0f, -1.0f, 0.0f,
		0.0f,  0.0f,  0.0f, 1.0f);

	view = X * glm::lookAt(position, position + direction, glm::vec3(0.0f, 1.0f, 0.0f));
	projection = MatRevInfiniteProjection(fov, aspect, zNear);
}

glm::mat4 Camera::MatRevInfiniteProjection(float fovy, float s, float n, float e) {
	float g = 1.0f / glm::tan(glm::radians(fovy) * 0.5f);

	return glm::mat4(
		g / s,  0.0f,  0.0f,       0.0f,
		0.0f,   g,     0.0f,       0.0f,
		0.0f,   0.0f,  e,          1.0f,
		0.0f,   0.0f,  n*(1.0f-e), 0.0f);
}

void Camera::UpdateUBO() {
	UBO uboData = {
		.view = view,
		.proj = projection,
		.pos = position
	};

	ResourceManager::ptr->WriteBuffer(m_ubo, &uboData, sizeof(uboData), Device::ptr->FrameIdx() * 256);
}