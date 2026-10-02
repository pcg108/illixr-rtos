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
The Rocket/FireSim baseline models a 1 GHz CPU with a 1 MHz timer declaration
and 10 kHz Zephyr ticks. It preserves the hardware's 1000:1 CPU/timer ratio,
the bounded 50-pair dataset, and estimator math. See
``docs/clock-experiments.md`` for the historical clock comparison.
``docs/current-baseline.md`` records the accepted combined-setting FireSim run.
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

Bulk trace export uses buffered HTIF writes and host-side JSON formatting.
See ``docs/batched-traces.md`` for the protocol, validation, and measured
FireSim improvement from 33.3 to 8.3 minutes per workload case.
