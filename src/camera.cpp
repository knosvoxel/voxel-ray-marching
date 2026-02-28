#include "camera.h"

void Camera::process_input(GLFWwindow* window, float32 delta) {
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
		process_keyboard(FORWARD, delta);
	}
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
		process_keyboard(BACKWARD, delta);
	}
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
		process_keyboard(LEFT, delta);
	}
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
		process_keyboard(RIGHT, delta);
	}
}

void Camera::process_keyboard(CameraMovement direction, float32 delta) {
	update_vectors();

	float velocity = movement_speed * delta;
	if (direction == FORWARD)
		pos += front * velocity;
	if (direction == BACKWARD)
		pos -= front * velocity;
	if (direction == LEFT)
		pos -= right * velocity;
	if (direction == RIGHT)
		pos += right * velocity;
}

void Camera::process_mouse(float32 xoffset, float32 yoffset, bool constrain_pitch) {
	xoffset *= mouse_sensitivity;
	yoffset *= mouse_sensitivity;

	yaw += xoffset;
	pitch += yoffset;

	if (constrain_pitch)
	{
		if (pitch > 89.0f)
			pitch = 89.0f;
		if (pitch < -89.0f)
			pitch = -89.0f;
	}
}

void Camera::update_data() {
	aspect_ratio = float(image_width) / float(image_height);

	float focal_length = 200.0;

	float theta = glm::radians(fov);
	float h = glm::tan(theta / 2);

	float viewport_height = 2 * h * focal_length;
	float viewport_width = viewport_height * (double(image_width) / image_height);

	update_vectors();

	vec3 viewport_u = viewport_width * right; // Vector across viewport horizontal edge
	vec3 viewport_v = viewport_height * up; // Vector up viewport vertical edge

	vec3 pixel_delta_u_vec = viewport_u / float(image_width); // pixel width
	vec3 pixel_delta_v_vec = viewport_v / float(image_height); // pixel height

	pixel_delta_u = pixel_delta_u_vec.x;
	pixel_delta_v = pixel_delta_v_vec.y;

	vec3 viewport_center = pos + focal_length * front;
	vec3 viewport_lower_left = viewport_center - viewport_u / 2.0f - viewport_v / 2.0f;
	pixel00_loc = viewport_lower_left + 0.5f * (pixel_delta_u_vec + pixel_delta_v_vec); // lower left

	// Swap buffers for double buffering
	currentBuffer = (currentBuffer + 1) % 2;
	cam_compute_ptr = cameraPtrs[currentBuffer];

	cam_compute_ptr->pos = pos;
	cam_compute_ptr->pixel00_loc = pixel00_loc;
	cam_compute_ptr->pixel_delta_u_vec = pixel_delta_u_vec;
	cam_compute_ptr->pixel_delta_v_vec = pixel_delta_v_vec;

	// Bind the currently active buffer
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, cameraBuffers[currentBuffer]);
}

void Camera::init() {
	// Create two persistent-mapped SSBOs for double buffering
	glCreateBuffers(2, cameraBuffers);
	for (int i = 0; i < 2; ++i) {
		glNamedBufferStorage(cameraBuffers[i], sizeof(CamComputeData), nullptr,
			GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
		cameraPtrs[i] = (CamComputeData*)glMapNamedBufferRange(cameraBuffers[i], 0, sizeof(CamComputeData),
			GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
	}

	// Bind the initial buffer
	cam_compute_ptr = cameraPtrs[currentBuffer];
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, cameraBuffers[currentBuffer]);

	update_data();
}

void Camera::update_vectors() {
	front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
	front.y = sin(glm::radians(pitch));
	front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));

	front = glm::normalize(front);

	right = glm::normalize(glm::cross(front, world_up));
	up = glm::normalize(glm::cross(right, front));
}

void Camera::cleanup() {
	glDeleteBuffers(2, cameraBuffers);
}