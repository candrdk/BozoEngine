#pragma once

#include "Common.h"

class Camera {
public:
	Camera(glm::vec3 startPos, float speed, float fov, float aspect, float zNear, float pitch, float yaw)
		: position{startPos}, speed{speed}, fov {fov}, aspect{aspect}, zNear{ zNear }, pitch{pitch}, yaw{yaw}
	{
		UpdateDirection();
		UpdateMatrices();
	}

	void Update(float deltaTime) {
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

	void ProcessKeyboard(int key, int action) {
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

	void ProcessMouseMovement(double xoffset, double yoffset) {
		pitch += mouseSensitivity * yoffset;
		yaw   += mouseSensitivity * xoffset;

		if (pitch >  80.0) pitch =  80.0;
		if (pitch < -80.0) pitch = -80.0;

		UpdateDirection();
	}

	void UpdateDirection() {
		direction = glm::normalize(glm::vec3(
			glm::cos(glm::radians(yaw)) * glm::cos(glm::radians(pitch)), 
			glm::sin(glm::radians(pitch)), 
			glm::sin(glm::radians(yaw)) * glm::cos(glm::radians(pitch))));
	}

	void UpdateMatrices() {
		view = glm::lookAt(position, position + direction, glm::vec3(0.0f, 1.0f, 0.0f));
		projection = MatRevInfiniteProjection(fov, aspect, zNear);
	}

	// Calculates the reversed z infinite perspetive projection. Taken from FGED 2, Listing 6.3.
	// fovy: vertical field of view
	// s: Aspect ratio
	// n: Near plane
	// e: Small epsilon value to deal with round-off errors when rendering objects at infinity. Defaults to 2^-20
	glm::mat4 MatRevInfiniteProjection(float fovy, float s, float n, float e = 1.0f / (1 << 20)) {
		float g = 1.0f / glm::tan(glm::radians(fovy) * 0.5f);

		return glm::mat4(
			g / s,	0.0f,	0.0f,			0.0f,
			0.0f,	-g,		0.0f,			0.0f,
			0.0f,	0.0f,	e,				-1.0f,
			0.0f,	0.0f,   n * (1.0f - e),	0.0f);
	}

	float fov, aspect, zNear;

	float speed;
	bool speedBoost = false,
		moveUp    = false,
		moveDown  = false,
		moveLeft  = false,
		moveRight = false,
		moveFront = false,
		moveBack  = false;

	double mouseSensitivity = 0.1f;
	double pitch, yaw;

	glm::vec3 position;
	glm::vec3 direction;

	glm::mat4 view;
	glm::mat4 projection;
};