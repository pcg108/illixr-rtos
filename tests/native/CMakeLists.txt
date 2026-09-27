cmake_minimum_required(VERSION 3.20)
project(illixr_estimator_replay LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Eigen3 3.4 REQUIRED NO_MODULE)
find_package(OpenCV 4.5.4 EXACT REQUIRED COMPONENTS core imgproc imgcodecs)
get_filename_component(ILLIXR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CONFIG_SOURCE "${ILLIXR_ROOT}/plugins/openvins/plugin.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CONFIG_SOURCE}")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract_config.py"
  "${ILLIXR_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}" COMMAND_ERROR_IS_FATAL ANY)
add_executable(estimator_replay replay.cpp "${ILLIXR_ROOT}/plugins/openvins/SLAMMath.cpp")
target_include_directories(estimator_replay PRIVATE "${ILLIXR_ROOT}/plugins/openvins"
  "${CMAKE_CURRENT_BINARY_DIR}" ${OpenCV_INCLUDE_DIRS})
target_link_libraries(estimator_replay PRIVATE Eigen3::Eigen ${OpenCV_LIBS})
# Keep CPU vectorization/parallel reductions out of native-vs-RV64 comparisons.
# These are numerical compilation choices, not changes to estimator equations.
target_compile_definitions(estimator_replay PRIVATE EIGEN_DONT_PARALLELIZE EIGEN_DONT_VECTORIZE)
target_compile_options(estimator_replay PRIVATE -ffp-contract=off)

enable_testing()
add_executable(test_imu_transport test_imu_transport.cpp)
target_include_directories(test_imu_transport PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/stubs" "${ILLIXR_ROOT}/src")
target_link_libraries(test_imu_transport PRIVATE Eigen3::Eigen)
target_compile_definitions(test_imu_transport PRIVATE EIGEN_RUNTIME_NO_MALLOC)
# Eigen's no-allocation guard uses assertions, including in Release builds.
target_compile_options(test_imu_transport PRIVATE -UNDEBUG)
add_test(NAME imu_transport COMMAND test_imu_transport)

# Pure prediction/timewarp oracles compile without Zephyr or a graphics backend.
find_package(Threads REQUIRED)
add_executable(test_prediction test_prediction.cpp)
target_link_libraries(test_prediction PRIVATE Eigen3::Eigen Threads::Threads)
target_compile_definitions(test_prediction PRIVATE EIGEN_RUNTIME_NO_MALLOC EIGEN_DONT_VECTORIZE)
target_compile_options(test_prediction PRIVATE -UNDEBUG -ffp-contract=off)
add_test(NAME prediction COMMAND test_prediction)
add_executable(prediction_reference prediction_reference.cpp)
target_link_libraries(prediction_reference PRIVATE Eigen3::Eigen)
target_compile_definitions(prediction_reference PRIVATE EIGEN_DONT_VECTORIZE EIGEN_DONT_PARALLELIZE)
target_compile_options(prediction_reference PRIVATE -ffp-contract=off)
add_test(NAME prediction_reference_validation
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_prediction_reference.py")
