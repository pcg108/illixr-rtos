Example for Illixr. Should be able to add it as an example under /zephyr/samples.

To run, go to your zephyr directory, source the appropriate environment, and build.

The command used is: ``west build -p -b spike_riscv64 samples/illixr_working/ -DYAML_FILE=profiles/default_new.yaml``
You can then proceed to run Spike normally.

After that, can run spike accordingly.


Adding Plugins:
To add plugins, you can add them to the YAML file. For example, if you want to add the "example_plugin", you would add it to the "plugins" section of the YAML file like this:
plugins: "example_plugin"

Writing Plugins:
To write a plugin, you need to create a new C++ file in the "plugins" directory. The file should include the necessary headers and define the plugin class. 
For example, if you wanted to create a plugin called "example_plugin", you would create a file named "example_plugin.cpp" and implement the plugin class within that file.
You should also update the CMakeList.txt in the plugin folder itself. There is no change on the main CMakeList.txt file.

Each plugin class should inherit from the threadloop class and implement the necessary methods for initialization, execution, and cleanup. You can refer to existing plugins in the "plugins" directory for examples of how to structure your plugin.

The data for the offline_cam, offline_imu are not uploaded on github. 
I essentially used embed_euroc_data.py to convert the data into a C++ header file and then included that header file in the respective plugins through CMakeLists.txt. You can do the same for your own data if you want to add more plugins that require data.



Spike clock-paced replay
------------------------

See ``docs/spike-clock-replay.md`` for the isolated toolchain setup, embedded
EuRoC datasets, single/dual-hart builds, runtime semantics, and validation commands.
Measured results and numerical limitations are recorded in ``docs/spike-results.md``.

Rocket RTL validation
---------------------

See ``docs/rocket-validation.md`` for single-, dual-, and quad-core Rocket
Verilator builds, platform checks, plugin affinity, and per-plugin hart evidence.
The Rocket firmware uses the generated hardware timer frequency and keeps the
same bounded 50-pair dataset and estimator math as the Spike validation.
``docs/rocket-results.md`` records verified startup results and links to the
automatically updated workload comparison report.

FireSim validation and IMU transport
-----------------------------------

See ``docs/firesim-validation.md`` for the local U250 setup and preserved
diagnostic history. ``docs/imu-value-transport.md`` describes the application
change that replaces per-sample IMU allocations with independent queue records.
The updated single-, dual-, and quad-core FireSim matrix passes all five workload
cases on the original Zephyr kernel, with unchanged estimator math and dataset.

Prediction and simulated GPU stages
-----------------------------------

The ``gpu_pipeline`` profile adds desktop RK4 pose prediction and independent
render/timewarp workers. Rendering and timewarp model asynchronous GPU delays;
the dummy stereo image remains unchanged. See ``docs/gpu-pipeline.md`` for
interfaces, timing assumptions, bounded storage, and validation details.
``scripts/run_gpu_pipeline.py`` validates dual/quad Spike before allowing the
quad-core FireSim workload.
