
// CGP library
#include "cgp/cgp.hpp" 

// Running application
#include "application.hpp"

// Custom scene of this code
#include "scene.hpp"


int main(int, char* argv[])
{

	scene_structure scene;
	application_structure app;
	app.initialize(argv[0], &scene);
	
	app.start_loop();

	return 0;
}

