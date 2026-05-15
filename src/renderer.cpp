#include "renderer.h"

const float NEAR = 0.1f;
const float FAR = 10000.0f;

const vec3 CAM_POS(60.0f, 200.0f, 60.0f);
const vec3 VUP(0.0f, 1.0f, 0.0f);
const float FOV = 45.0f;
const float YAW = 225.0f;
const float PITCH = -20.0f;

const int MAX_STEPS = 256;

uint32 dispatchSizeX = 0;
uint32 dispatchSizeY = 0;

Renderer::Renderer(GLFWwindow* appWindow, const char* path, float32* appDelta, bool* appMouseCaught, bool* appMouseMoved, uint32 screenSizeX, uint32 screenSizeY) : window(appWindow), deltaTime(appDelta), mouseCaught(appMouseCaught), mouseMoved(appMouseMoved), sizeX(screenSizeX), sizeY(screenSizeY)
{
    // texture generation with DSA
    glCreateTextures(GL_TEXTURE_2D, 1, &screenTexture);

    glTextureParameteri(screenTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(screenTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(screenTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(screenTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTextureStorage2D(screenTexture, 1, GL_RGBA32F, sizeX, sizeY);
    glBindImageTexture(0, screenTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

    cam = Camera(sizeX, sizeY, CAM_POS, VUP, FOV, YAW, PITCH);
    cam.init();

    screenShader = Shader("../../shaders/screenShader.vert", "../../shaders/screenShader.frag");
    renderCompute = ComputeShader("../../shaders/renderShader.comp");

    dispatchSizeX = (sizeX + 15) / 16;
    dispatchSizeY = (sizeY + 15) / 16;

    renderCompute.use();
    renderCompute.setInt("MAX_STEPS", MAX_STEPS);
    renderCompute.setVec3("light_direction", -0.45f, -0.7f, -0.2f);

    // empty VAO
    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    scene.load(path, renderCompute);
    renderCompute.setUInt("biggest_level_size", scene.biggestLevelSize);
}

void Renderer::renderFrame()
{
    // Input handling
    if (*mouseCaught) {
        cam.process_input(window, *deltaTime);

        if (*mouseMoved) {
            cam.process_mouse(xoffset, yoffset);
            xoffset = 0.0;
            yoffset = 0.0;
        }
    }

    *mouseMoved = false;
    
    cam.update_data();

    renderCompute.use();

    glDispatchCompute(dispatchSizeX, dispatchSizeY, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    glBindTextureUnit(0, screenTexture);

    screenShader.use();

    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void Renderer::cleanup()
{
    scene.cleanup();
    cam.cleanup();

    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (screenTexture) glDeleteTextures(1, &screenTexture);
}
