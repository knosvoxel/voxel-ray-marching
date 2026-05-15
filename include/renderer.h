#pragma once

#include <iostream>
#include <stdexcept>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "camera.h"
#include "vox_scene.h"
#include "timer.h"

#include "shader.h"
#include "compute.h"

using namespace glm;

class Renderer {
public:
	Renderer(){};
	Renderer(GLFWwindow* appWindow, const char* path, float32* appDelta, bool* appMouseCaught, bool* appMouseMoved, uint32 screenSizeX, uint32 screenSizeY);
	void init();
	void renderFrame();
	void cleanup();

	Camera cam;
	VoxScene scene;

	float32 xoffset = 0.0, yoffset = 0.0;
private:
	float32* deltaTime;
	bool* mouseCaught;
	bool* mouseMoved;

	GLFWwindow* window;
	uint32 sizeX, sizeY;

	uint32 VAO = 0;

	uint32 screenTexture = 0;
	Shader screenShader;
	ComputeShader renderCompute;
};




