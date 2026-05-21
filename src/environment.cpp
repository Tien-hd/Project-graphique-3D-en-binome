#include "environment.hpp"

// Change these global values to modify the default behavior
// ************************************************************* //
// The initial zoom factor on the GUI
float project::gui_scale = 1.0f;
// Is FPS limited automatically
bool project::fps_limiting = true;
// Maximal default FPS (used only of fps_max is true)
float project::fps_max=60.0f;
// Automatic synchronization of GLFW with the vertical-monitor refresh
bool project::vsync=true;     
// Initial dimension of the OpenGL window (ratio if in [0,1], and absolute pixel size if > 1)
float project::initial_window_size_width  = 0.5f; 
float project::initial_window_size_height = 0.5f;
// ************************************************************* //


// This path will be automatically filled when the program starts
std::string project::path = ""; 




void environment_structure::send_opengl_uniform(opengl_shader_structure const& shader, bool expected) const
{
	opengl_uniform(shader, "projection", camera_projection, expected);
	opengl_uniform(shader, "view", camera_view, expected);
	opengl_uniform(shader, "light", light, false);
	opengl_uniform(shader, "light_intensity", light_intensity, false);
	opengl_uniform(shader, "ambient_tint", ambient_tint, false);
	opengl_uniform(shader, "ambient_intensity", ambient_intensity, false);
	opengl_uniform(shader, "use_fog", use_fog, false);
	opengl_uniform(shader, "fog_density", fog_density, false);
	opengl_uniform(shader, "fog_color", fog_color, false);

	uniform_generic.send_opengl_uniform(shader, expected);

}
