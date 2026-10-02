#include "../../src/node.hpp"
#include "../../src/plugin_registry.hpp"
#include "../../src/eye_tracking.hpp"
void start_eye_tracking(ILLIXR::phonebook_new &pb) {
 static ILLIXR::Node node;
 node.initialize(pb,"eye_tracking");pb.register_plugin("eye_tracking",&node);
 ILLIXR::eye_tracking::initialize();
}
REGISTER_PLUGIN(eye_tracking);
