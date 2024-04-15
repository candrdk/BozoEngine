#pragma once

#include "../Core/Device.h"

#include <glm/glm.hpp>

// TODO: reconsider allowing Camera to own its own UBO. This should be moved out to user rendering code.

class Camera {
public:
	Camera(glm::vec3 startPos, float speed, float fov, float aspect, float zNear, float pitch, float yaw);
	~Camera();

	void Update(float deltaTime);
	void UpdateUBO();

	Handle<BindGroup> GetCameraBindings() { return m_bindgroups[Device::ptr->FrameIdx()]; }

	void ProcessKeyboard(int key, int action);
	void ProcessMouseMovement(double xoffset, double yoffset);

	float fov, aspect;
	glm::vec3 position;
	glm::vec3 direction;

	glm::mat4 view;
	glm::mat4 projection;

private:
	void UpdateDirection();

	void UpdateMatrices();

	// Calculates the reversed z infinite perspetive projection. Taken from FGED 2, Listing 6.3.
	// fovy: Vertical field of view
	// s:	 Aspect ratio
	// n:	 Near plane
	// e:	 Small epsilon value to deal with round-off errors when rendering objects at infinity. Defaults to 2^-20.
	glm::mat4 MatRevInfiniteProjection(float fovy, float s, float n, float e = 1.0f / (1 << 20));

	struct UBO {
		alignas(16) glm::mat4 view;
		alignas(16) glm::mat4 proj;
		alignas(16) glm::vec3 pos;
	};

	Handle<Buffer>			m_ubo;
	Handle<BindGroup>       m_bindgroups[Device::MaxFramesInFlight];
	Handle<BindGroupLayout> m_bindgroupLayout;

	float zNear;

	float speed;
	bool speedBoost = false,
		moveUp = false,
		moveDown = false,
		moveLeft = false,
		moveRight = false,
		moveFront = false,
		moveBack = false;

	double mouseSensitivity = 0.1f;
	double pitch, yaw;
};