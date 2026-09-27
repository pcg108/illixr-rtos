#include "../../src/node.hpp"
#include "../../src/plugin_registry.hpp"
#include "../../src/pose_prediction.hpp"

// Like its desktop counterpart this plugin is an on-demand service. Its node
// participates in discovery, but it creates no thread or worker barrier entry.
void start_pose_prediction(ILLIXR::phonebook_new& pb) {
    static ILLIXR::Node node;
    node.initialize(pb, "pose_prediction");
    pb.register_plugin("pose_prediction", &node);
    ILLIXR::get_pose_prediction().reset();
}
REGISTER_PLUGIN(pose_prediction)
