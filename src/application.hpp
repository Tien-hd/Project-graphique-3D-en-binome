#pragma once

#include "cgp/cgp.hpp"
#include <iostream>

struct scene_structure;

class application_structure {
public:
    application_structure() = default;

    // Top-level setup entry: attach the scene, configure window and load assets.
    void initialize(const char* executable_path, scene_structure* scene);
    // Launch the rendering loop once initialization succeeds.
    void start_loop();


    // Window and rendering bootstrap helpers.
    void initialize_window();
    void initialize_default_shaders();
    void setup_callbacks();
    // Per-frame update invoked by the platform loop.
    void animation_loop();
    // Shared ImGui controls displayed for any scene.
    void display_gui_default();
    // Tidy up GL/GLFW resources on exit (native builds).
    void cleanup();

    // Input plumbing forwarded from GLFW callbacks.
    void on_window_resize(int width, int height);
    void on_mouse_move(double xpos, double ypos);
    void on_mouse_click(int button, int action, int mods);
    void on_mouse_scroll(double xoffset, double yoffset);
    void on_keyboard(int key, int scancode, int action, int mods);
    void on_window_focus(int focused);
    void on_char(unsigned int codepoint);

    static application_structure* from_window(GLFWwindow* window);

    static void window_size_callback(GLFWwindow* window, int width, int height);
    static void mouse_move_callback(GLFWwindow* window, double xpos, double ypos);
    static void mouse_scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
    static void mouse_click_callback(GLFWwindow* window, int button, int action, int mods);
    static void keyboard_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void window_focus_callback(GLFWwindow* window, int focused);
    static void char_callback(GLFWwindow* window, unsigned int codepoint);


    scene_structure& scene();
    scene_structure const& scene() const;


    scene_structure* scene_ptr = nullptr;
    cgp::timer_fps fps_record_;
};
