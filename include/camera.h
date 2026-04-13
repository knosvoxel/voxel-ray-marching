#pragma once

#include <glm/glm.hpp>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

using namespace glm;

typedef struct CamComputeData {
	glm::vec3 pos;
	uint32_t pad;
	glm::vec3 pixel00_loc;
	uint32_t pad2;
	glm::vec3 pixel_delta_u_vec;
	uint32_t pad3;
	glm::vec3 pixel_delta_v_vec;
	uint32_t pad4;
};

enum CameraMovement {
	FORWARD,
	BACKWARD,
	LEFT,
	RIGHT
};

class Camera {
public:
	uint32_t image_width = 1600;
	uint32_t image_height = 900;

	vec3 pos = vec3(0, 0, 0);
	vec3 world_up = vec3(0, 1, 0);

	float32 fov = 90.0;

	float32 movement_speed = 100.0;
	float32 mouse_sensitivity = 0.2;

	float32 yaw = -90.0;
	float32 pitch = 0.0;

	bool isFollowingPath = false;

	Camera() = default;

	Camera(uint32_t width, uint32_t height, glm::vec3 pos, glm::vec3 up, float32 fov, float32 yaw, float32 pitch) : image_width(width), image_height(height), fov(fov), pos(pos), world_up(up), yaw(yaw), pitch(pitch) {};

	~Camera() {};

	void process_input(GLFWwindow* window, float32 delta);
	void process_keyboard(CameraMovement direction, float32 delta);
	void process_mouse(float32 xoffset, float32 yoffset, bool constrain_pitch = true);

	void init();
	void update_data();
	void cleanup();
private:
	float32 aspect_ratio;
	float32 pixel_delta_u;
	float32 pixel_delta_v;

	vec3 pixel00_loc;
	vec3 right, up, front;

	uint32 cameraBuffers[2];
	CamComputeData* cameraPtrs[2];
	CamComputeData* cam_compute_ptr;
	int32 currentBuffer = 0;

	void update_vectors();
};