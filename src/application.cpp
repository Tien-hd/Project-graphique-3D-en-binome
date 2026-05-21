#include "application.hpp"
#include "scene.hpp"

#include <stdexcept>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

using namespace cgp;

namespace {

#ifdef __EMSCRIPTEN__
void emscripten_main_loop(void* arg)
{
    // EMScripten trampoline expects a C callback; forward to member routine.
    static_cast<application_structure*>(arg)->animation_loop();
}
#endif

} // namespace

scene_structure& application_structure::scene()
{
    if (!scene_ptr)
        throw std::runtime_error("application_structure: scene pointer is null");
    return *scene_ptr;
}

scene_structure const& application_structure::scene() const
{
    if (!scene_ptr)
        throw std::runtime_error("application_structure: scene pointer is null");
    return *scene_ptr;
}

void application_structure::initialize(const char* executable_path, scene_structure* scene_arg)
{
    scene_ptr = scene_arg;
    scene(); // ensure a scene is attached before continuing.

    // Bring up the GLFW window and ImGui context.
    initialize_window();

    // Build the asset lookup path and load global shaders/textures.
    project::path = cgp::project_path_find(executable_path, "shaders/");
    initialize_default_shaders();

    // Hand off to the custom scene for user-defined asset initialization.
    std::cout << "Initialize data of the scene ..." << std::endl;
    scene().initialize();
    std::cout << "Initialization finished\n" << std::endl;
}

void application_structure::start_loop()
{
    std::cout << "Start animation loop ..." << std::endl;
    fps_record_.start();

#ifndef __EMSCRIPTEN__
    double lasttime = glfwGetTime();
    while (!glfwWindowShouldClose(scene().window.glfw_window)) {
        animation_loop();

        if (project::fps_limiting) {
            while (glfwGetTime() < lasttime + 1.0 / project::fps_max) {
            }
            lasttime = glfwGetTime();
        }
    }
    std::cout << "\nAnimation loop stopped" << std::endl;
    cleanup();
#else
    emscripten_set_main_loop_arg(emscripten_main_loop, this, 0, 1);
#endif
}

void application_structure::initialize_window()
{
    auto& s = scene();

    // Configure GLFW and create the drawing surface.
    s.window.initialize_glfw();

    int window_width = static_cast<int>(project::initial_window_size_width);
    int window_height = static_cast<int>(project::initial_window_size_height);
    if (project::initial_window_size_width < 1)
        window_width = project::initial_window_size_width * s.window.monitor_width();
    if (project::initial_window_size_height < 1)
        window_height = project::initial_window_size_height * s.window.monitor_height();

    s.window.create_window(window_width, window_height, "CGP Display", CGP_OPENGL_VERSION_MAJOR, CGP_OPENGL_VERSION_MINOR);

    std::cout << "\nWindow (" << s.window.width << "px x " << s.window.height << "px) created" << std::endl;
    std::cout << "Monitor: " << glfwGetMonitorName(s.window.monitor) << " - Resolution (" << s.window.screen_resolution_width << "x" << s.window.screen_resolution_height << ")\n" << std::endl;

    std::cout << "OpenGL Information:" << std::endl;
    std::cout << cgp::opengl_info_display() << std::endl;

    cgp::imgui_init(s.window.glfw_window);
    glfwSetWindowUserPointer(s.window.glfw_window, this);

    // Register callbacks so GLFW can route events back to this instance.
    setup_callbacks();
}

void application_structure::initialize_default_shaders()
{
    std::string default_path_shaders = project::path + "shaders/";

    mesh_drawable::default_shader.load(default_path_shaders + "mesh/mesh.vert.glsl", default_path_shaders + "mesh/mesh.frag.glsl");
    triangles_drawable::default_shader.load(default_path_shaders + "mesh/mesh.vert.glsl", default_path_shaders + "mesh/mesh.frag.glsl");

    image_structure const white_image = image_structure{1, 1, image_color_type::rgba, {255, 255, 255, 255}};
    mesh_drawable::default_texture.initialize_texture_2d_on_gpu(white_image);
    triangles_drawable::default_texture.initialize_texture_2d_on_gpu(white_image);

    curve_drawable::default_shader.load(default_path_shaders + "single_color/single_color.vert.glsl", default_path_shaders + "single_color/single_color.frag.glsl");
}

void application_structure::setup_callbacks()
{
    GLFWwindow* window = scene().window.glfw_window;
    glfwSetMouseButtonCallback(window, mouse_click_callback);
    glfwSetCursorPosCallback(window, mouse_move_callback);
    glfwSetWindowSizeCallback(window, window_size_callback);
    glfwSetKeyCallback(window, keyboard_callback);
    glfwSetScrollCallback(window, mouse_scroll_callback);
    glfwSetWindowFocusCallback(window, window_focus_callback);
    glfwSetCharCallback(window, char_callback);
}

void application_structure::animation_loop()
{
    auto& s = scene();

    emscripten_update_window_size(s.window.width, s.window.height);

    s.camera_projection.aspect_ratio = s.window.aspect_ratio();
    s.environment.camera_projection = s.camera_projection.matrix();
    glViewport(0, 0, s.window.width, s.window.height);

    vec3 const& background_color = s.environment.background_color;
    glClearColor(background_color.x, background_color.y, background_color.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    float const time_interval = fps_record_.update();
    if (fps_record_.event) {
        std::string const title = "CGP Display - " + str(fps_record_.fps) + " fps";
        glfwSetWindowTitle(s.window.glfw_window, title.c_str());
    }

    imgui_create_frame();
    ImGui::GetIO().FontGlobalScale = project::gui_scale;
    ImGui::Begin("GUI", NULL, ImGuiWindowFlags_AlwaysAutoResize);
    s.inputs.time_interval = time_interval;

    display_gui_default();
    s.display_gui();

    s.idle_frame();
    s.display_frame();

    ImGui::End();
    imgui_render_frame(s.window.glfw_window);
    glfwSwapBuffers(s.window.glfw_window);
    glfwPollEvents();
    if (glfwGetWindowAttrib(s.window.glfw_window, GLFW_ICONIFIED) != 0)
        ImGui_ImplGlfw_Sleep(10);
}

void application_structure::display_gui_default()
{
    auto& s = scene();

    std::string fps_txt = str(fps_record_.fps) + " fps";

    if (s.inputs.keyboard.ctrl)
        fps_txt += " [ctrl]";
    if (s.inputs.keyboard.shift)
        fps_txt += " [shift]";

    ImGui::Text("%s", fps_txt.c_str());
    if (ImGui::CollapsingHeader("Window")) {
        ImGui::Indent();
#ifndef __EMSCRIPTEN__
        bool changed_screen_mode = ImGui::Checkbox("Full Screen", &s.window.is_full_screen);
        if (changed_screen_mode) {
            if (s.window.is_full_screen)
                s.window.set_full_screen();
            else
                s.window.set_windowed_screen();
        }
#endif
        ImGui::SliderFloat("Gui Scale", &project::gui_scale, 0.5f, 2.5f);

#ifndef __EMSCRIPTEN__
        ImGui::Checkbox("FPS limiting", &project::fps_limiting);
        if (project::fps_limiting) {
            ImGui::SliderFloat("FPS limit", &project::fps_max, 10, 250);
        }
#endif
        if (ImGui::Checkbox("vsync (screen sync)", &project::vsync)) {
            project::vsync == true ? glfwSwapInterval(1) : glfwSwapInterval(0);
        }

        std::string window_size = "Window " + str(s.window.width) + "px x " + str(s.window.height) + "px";
        ImGui::Text("%s", window_size.c_str());

        ImGui::Unindent();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

    }

}

void application_structure::cleanup()
{
    auto& s = scene();

    cgp::imgui_cleanup();
    glfwDestroyWindow(s.window.glfw_window);
    glfwTerminate();
}

void application_structure::on_window_resize(int width, int height)
{
    auto& s = scene();
    s.window.width = width;
    s.window.height = height;
}

void application_structure::on_mouse_move(double xpos, double ypos)
{
    auto& s = scene();

    ImGui_ImplGlfw_CursorPosCallback(s.window.glfw_window, xpos, ypos);
    vec2 const pos_relative = s.window.convert_pixel_to_relative_coordinates({xpos, ypos});
    s.inputs.mouse.position.update(pos_relative);

    s.inputs.mouse.on_gui = ImGui::GetIO().WantCaptureMouse;
    if (s.inputs.mouse.on_gui)
        return;
    s.mouse_move_event();
}

void application_structure::on_mouse_click(int button, int action, int mods)
{
    auto& s = scene();

    ImGui_ImplGlfw_MouseButtonCallback(s.window.glfw_window, button, action, mods);

    s.inputs.mouse.click.update_from_glfw_click(button, action);

    s.inputs.mouse.on_gui = ImGui::GetIO().WantCaptureMouse;
    if (s.inputs.mouse.on_gui)
        return;
    s.mouse_click_event();
}

void application_structure::on_mouse_scroll(double xoffset, double yoffset)
{
    auto& s = scene();

    ImGui_ImplGlfw_ScrollCallback(s.window.glfw_window, xoffset, yoffset);
    s.inputs.mouse.scroll = yoffset;

    s.inputs.mouse.on_gui = ImGui::GetIO().WantCaptureMouse;
    if (s.inputs.mouse.on_gui)
        return;
    s.mouse_scroll_event();
}

void application_structure::on_keyboard(int key, int scancode, int action, int mods)
{
    auto& s = scene();

    ImGui_ImplGlfw_KeyCallback(s.window.glfw_window, key, scancode, action, mods);
    bool imgui_capture_keyboard = ImGui::GetIO().WantCaptureKeyboard;

    if (!imgui_capture_keyboard) {
        s.inputs.keyboard.update_from_glfw_key(key, action);
        s.keyboard_event();

        if (key == GLFW_KEY_F && action == GLFW_PRESS && s.inputs.keyboard.shift) {
            s.window.is_full_screen = !s.window.is_full_screen;
            if (s.window.is_full_screen)
                s.window.set_full_screen();
            else
                s.window.set_windowed_screen();
        }
        if (key == GLFW_KEY_V && action == GLFW_PRESS && s.inputs.keyboard.shift) {
            auto const camera_model = s.camera_control.camera_model;
            std::cout << "\nDebug camera (position = " << str(camera_model.position()) << "):\n" << std::endl;
            std::cout << "  Frame matrix:" << std::endl;
            std::cout << str_pretty(camera_model.matrix_frame()) << std::endl;
            std::cout << "  View matrix:" << std::endl;
            std::cout << str_pretty(camera_model.matrix_view()) << std::endl;
        }
    }
}

void application_structure::on_window_focus(int focused)
{
    auto& s = scene();

    if (focused) {
        glfwSetInputMode(s.window.glfw_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

void application_structure::on_char(unsigned int codepoint)
{
    ImGui_ImplGlfw_CharCallback(scene().window.glfw_window, codepoint);
}

application_structure* application_structure::from_window(GLFWwindow* window)
{
    return static_cast<application_structure*>(glfwGetWindowUserPointer(window));
}

void application_structure::window_size_callback(GLFWwindow* window, int width, int height)
{
    if (auto* app = from_window(window))
        app->on_window_resize(width, height);
}

void application_structure::mouse_move_callback(GLFWwindow* window, double xpos, double ypos)
{
    if (auto* app = from_window(window))
        app->on_mouse_move(xpos, ypos);
}

void application_structure::mouse_scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    if (auto* app = from_window(window))
        app->on_mouse_scroll(xoffset, yoffset);
}

void application_structure::mouse_click_callback(GLFWwindow* window, int button, int action, int mods)
{
    if (auto* app = from_window(window))
        app->on_mouse_click(button, action, mods);
}

void application_structure::keyboard_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (auto* app = from_window(window))
        app->on_keyboard(key, scancode, action, mods);
}

void application_structure::window_focus_callback(GLFWwindow* window, int focused)
{
    if (auto* app = from_window(window))
        app->on_window_focus(focused);
}

void application_structure::char_callback(GLFWwindow* window, unsigned int codepoint)
{
    if (auto* app = from_window(window))
        app->on_char(codepoint);
}
