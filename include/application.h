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

#include "timer.h"
#include "renderer.h"
#include "camera_path.h"

using namespace glm;

class Application {
public:
	void run();

	float32 lastX = 0.0f, lastY = 0.0f, deltaTime = 0.0f, lastFrame = 0.0f;
	uint32 sizeX = 0.0, sizeY = 0.0;

	bool mouseCaught = true;
	bool mouseMoved = false;
	bool firstMouse = true;

	CameraPath cameraPaths[10];
	int32 activePathIdx = -1;
	char cameraPathFileName[256] = "../../res/camera_paths.json";

	Renderer renderer;
private:
	void init();
	void initWindow();
	void initOpenGL();
	void initImgui();

	void mainLoop();
	void renderImGuiFrame();

	void updateCameraPath(float32 delta);

	void cleanup();

	GLFWwindow* window;

	bool enableVSync = false;
	bool enableWireframe = false;
};