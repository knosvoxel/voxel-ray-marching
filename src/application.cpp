#include "application.h"

const uint32 WIDTH = 2560;
const uint32 HEIGHT = 1440;

const char* WINDOW_NAME = "Voxel Ray Marching";
const char* VOX_FILE_PATH = "../res/castle.vox";

void framebufferSizeCallback(GLFWwindow* window, int width, int height);
void mouseCallback(GLFWwindow* window, double xposIn, double yposIn);
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
void keyboardCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
void GLAPIENTRY message_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam);
void APIENTRY glDebugOutput(GLenum source, GLenum type, unsigned int id, GLenum severity,
    GLsizei length, const char* message, const void* userParam);

void Application::run()
{
    init();
    mainLoop();
    cleanup();
}

void Application::init()
{
    Timer timer;
    timer.start();

    initWindow();
    initOpenGL();
    initImgui();

    lastX = static_cast<float>(sizeX) / 2.0f;
    lastY = static_cast<float>(sizeY) / 2.0f;

    renderer = Renderer(window, VOX_FILE_PATH, &deltaTime, &mouseCaught, &mouseMoved, WIDTH, HEIGHT);

    timer.stop();
    std::cout << "init total: " << timer.elapsedSeconds() << " s" << std::endl;
}

void Application::initWindow()
{
    // glfw: initialize and configure
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // glfw window creation
    window = glfwCreateWindow(WIDTH, HEIGHT, WINDOW_NAME, nullptr, nullptr);
    if (window == nullptr)
    {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        exit(-1);
    }
    sizeX = WIDTH;
    sizeY = HEIGHT;
    glfwSetWindowUserPointer(window, this);
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetKeyCallback(window, keyboardCallback);

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!enableVSync) {
        glfwSwapInterval(0);
    }
}

void Application::initOpenGL()
{
    // glad: load all OpenGL function pointers
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        exit(-1);
    }

    //// configure global opengl state
    //// -------------------------------------------
    //glEnable(GL_DEBUG_OUTPUT);
    //glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    //glDebugMessageCallback(glDebugOutput, nullptr);
    //glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
    // overdraw debug visuals
    // also adjust the shader in compute_scene.cpp to use overdraw.frag for this to work correctly
    //glEnable(GL_BLEND);
    //glDepthFunc(GL_ALWAYS);
    //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    //int n;
    //glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    //for (int i = 0; i < n; i++) {
    //    const char* extension = (const char*)glGetStringi(GL_EXTENSIONS, i);
    //    if (strstr(extension, "half_float") || strstr(extension, "float16")) {
    //        printf("Supported: %s\n", extension);
    //    }
    //}
}

void Application::initImgui()
{
    // Setup Dear ImGui context
    // -------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "../imgui_config.ini";
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);// Second param install_callback=true will install GLFW callbacks and chain to existing ones.
    ImGui_ImplOpenGL3_Init();
}

void Application::mainLoop()
{
    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        glClearColor(0.20f, 0.20f, 0.20f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        renderer.renderFrame();

        renderImGuiFrame();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }
}

void Application::renderImGuiFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Model Data", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    ImGui::Text("Frametime: %.3f ms (FPS %.1f)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate); // TODO: use other delta
    if (ImGui::Checkbox("VSync", &enableVSync))
    {
        if (enableVSync) {
            glfwSwapInterval(1);
        }
        else {
            glfwSwapInterval(0);
        }
    }
    ImGui::Separator();
    ImGui::Text("Instance Count: %1u", renderer.scene.numInstances);
    ImGui::Separator();
    ImGui::DragFloat3("Position", (float*)&renderer.cam.pos, 0.01f);
    ImGui::DragFloat("Movement Speed", (float*)&renderer.cam.movement_speed, 0.01, 0.0f, 0.0f, "%.1f");
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Application::cleanup()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwTerminate();
}

void framebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

    glViewport(0, 0, width, height);
    app->sizeX = width;
    app->sizeY = height;
}

void mouseCallback(GLFWwindow* window, double xposIn, double yposIn)
{
    auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

    if (!app->mouseCaught) return;

    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (app->firstMouse)
    {
        app->lastX = xpos;
        app->lastY = ypos;
        app->renderer.xoffset = 0.0;
        app->renderer.yoffset = 0.0;
        app->firstMouse = false;
    }

    if (app->lastX != xpos || app->lastY != ypos)
        app->mouseMoved = true;

    app->renderer.xoffset = xpos - app->lastX;
    app->renderer.yoffset = app->lastY - ypos;

    app->lastX = xpos;
    app->lastY = ypos;
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

    if (button == GLFW_MOUSE_BUTTON_RIGHT && !app->mouseCaught)
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        app->mouseCaught = true;
        app->firstMouse = true;
    }
}

void keyboardCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        if (app->mouseCaught)
        {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            app->mouseCaught = false;
        }
        else
        {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            app->mouseCaught = true;
            app->firstMouse = true;
        }
    }
}

void GLAPIENTRY message_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
    fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
        (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""), type, severity, message);
}

void APIENTRY glDebugOutput(GLenum source, GLenum type, unsigned int id, GLenum severity,
    GLsizei length, const char* message, const void* userParam) {
    // Ignore non-significant error/warning codes
    if (id == 131169 || id == 131185 || id == 131218 || id == 131204) return;

    std::cout << "---------------" << std::endl;
    std::cout << "Debug message (" << id << "): " << message << std::endl;

    switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH:         std::cout << "Severity: high"; break;
    case GL_DEBUG_SEVERITY_MEDIUM:       std::cout << "Severity: medium"; break;
    case GL_DEBUG_SEVERITY_LOW:          std::cout << "Severity: low"; break;
    case GL_DEBUG_SEVERITY_NOTIFICATION: std::cout << "Severity: notification"; break;
    } std::cout << std::endl;
}