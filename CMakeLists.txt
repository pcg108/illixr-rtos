cmake_minimum_required(VERSION 3.20.0)

option(ILLIXR_HPM_PROFILE "Account Rocket HPM counters by scheduled plugin" OFF)
if(ILLIXR_HPM_PROFILE)
  list(APPEND EXTRA_CONF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/hpm.conf")
endif()
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(illixr_working)
add_compile_definitions(ILLIXR_HPM_PROFILE=$<BOOL:${ILLIXR_HPM_PROFILE}>)
if(ILLIXR_HPM_PROFILE)
  target_sources(app PRIVATE src/hpm.cpp)
  set_source_files_properties(src/hpm.cpp PROPERTIES COMPILE_OPTIONS "-fno-tree-vectorize")
endif()
include(cmake/illixr_openblas.cmake)
option(ILLIXR_DIAGNOSTIC_FAULTS "Preserve complete CPU fault registers in diagnostic builds" OFF)
option(ILLIXR_DIAGNOSTIC_RAW_TRAP "Capture fatal traps directly over HTIF, without kernel logging" OFF)
option(ILLIXR_DIAGNOSTIC_BLAS_PROGRESS "Trace BLAS self-test call entry/exit for stall diagnosis" OFF)
option(ILLIXR_DIAGNOSTIC_BLAS_IRQ_MASK "Mask IRQs around diagnostic numerical self-test calls" OFF)
option(ILLIXR_DIAGNOSTIC_BLAS_AB "Compare IRQ/fence modes within one diagnostic ELF" OFF)
option(ILLIXR_DIAGNOSTIC_RVV_FOCUSED "Run isolated TRSM tail and active-vector interrupt fixtures" OFF)
option(ILLIXR_DIAGNOSTIC_RVV_REJECT "Test rejected vector instructions using a private trap handler" OFF)
option(ILLIXR_DIAGNOSTIC_RVV_MEMORY "Test vector store followed by scalar load" OFF)
if(ILLIXR_DIAGNOSTIC_RVV_MEMORY)
  if(NOT ILLIXR_DIAGNOSTIC_RVV_FOCUSED)
    message(FATAL_ERROR "Vector memory diagnostic requires focused fixtures")
  endif()
  target_sources(app PRIVATE tests/rvv_focused/memory.S)
  target_compile_definitions(app PRIVATE ILLIXR_DIAGNOSTIC_RVV_MEMORY=1)
endif()
option(ILLIXR_DIAGNOSTIC_RVV_NATIVE_IRQ "Test active vectors with a private timer interrupt handler" OFF)
if(ILLIXR_DIAGNOSTIC_RVV_NATIVE_IRQ)
  if(NOT ILLIXR_DIAGNOSTIC_RVV_FOCUSED)
    message(FATAL_ERROR "Native-vector IRQ diagnostic requires focused fixtures")
  endif()
  target_sources(app PRIVATE tests/rvv_focused/native_irq.S)
  target_compile_definitions(app PRIVATE ILLIXR_DIAGNOSTIC_RVV_NATIVE_IRQ=1)
endif()
option(ILLIXR_DIAGNOSTIC_TRSM_CAPTURE "Capture first failing full-fixture triangular solve operands" OFF)
option(ILLIXR_DIAGNOSTIC_KERNEL_FENCE_AB "Compare fences at the internal RVV GEMM return boundary" OFF)
option(ILLIXR_DIAGNOSTIC_KERNEL_DELAY_AB "Compare internal GEMM fence against delay-only return paths" OFF)
if(ILLIXR_DIAGNOSTIC_KERNEL_DELAY_AB)
  if(NOT ILLIXR_DIAGNOSTIC_KERNEL_FENCE_AB)
    message(FATAL_ERROR "Kernel delay A/B requires the kernel fence diagnostic")
  endif()
  target_compile_definitions(app PRIVATE ILLIXR_DIAGNOSTIC_KERNEL_DELAY_AB=1)
endif()
if(ILLIXR_DIAGNOSTIC_KERNEL_FENCE_AB)
  if(NOT ILLIXR_DIAGNOSTIC_BLAS_AB)
    message(FATAL_ERROR "Kernel fence A/B requires the full BLAS runtime A/B fixture")
  endif()
  target_compile_definitions(app PRIVATE ILLIXR_DIAGNOSTIC_KERNEL_FENCE_AB=1)
endif()
if(ILLIXR_DIAGNOSTIC_TRSM_CAPTURE)
  if(NOT ILLIXR_DIAGNOSTIC_BLAS_AB)
    message(FATAL_ERROR "TRSM capture requires the full runtime A/B fixture")
  endif()
  target_compile_definitions(app PRIVATE ILLIXR_DIAGNOSTIC_TRSM_CAPTURE=1)
endif()
if(ILLIXR_DIAGNOSTIC_RVV_REJECT)
  if(NOT ILLIXR_DIAGNOSTIC_RVV_FOCUSED)
    message(FATAL_ERROR "Rejected-vector diagnostic requires focused fixtures")
  endif()
  target_sources(app PRIVATE tests/rvv_focused/rejected.S)
  target_compile_definitions(app PRIVATE ILLIXR_DIAGNOSTIC_RVV_REJECT=1)
endif()
if(ILLIXR_DIAGNOSTIC_RVV_FOCUSED)
  if(NOT ILLIXR_LINALG_BACKEND STREQUAL "openblas_rvv" OR ILLIXR_DIAGNOSTIC_BLAS_AB)
    message(FATAL_ERROR "Focused RVV diagnostic requires RVV BLAS and disables the full A/B fixture")
  endif()
  target_sources(app PRIVATE tests/rvv_focused/focused.cpp tests/rvv_focused/active.S)
  target_compile_definitions(app PRIVATE ILLIXR_DIAGNOSTIC_RVV_FOCUSED=1)
endif()
if(ILLIXR_DIAGNOSTIC_BLAS_AB)
  if(NOT ILLIXR_DIAGNOSTIC_BLAS_PROGRESS OR ILLIXR_DIAGNOSTIC_BLAS_IRQ_MASK)
    message(FATAL_ERROR "BLAS A/B requires progress and controls IRQ masking at runtime")
  endif()
  target_compile_definitions(app PRIVATE ILLIXR_DIAGNOSTIC_BLAS_AB=1)
endif()
if(ILLIXR_DIAGNOSTIC_BLAS_IRQ_MASK)
  if(NOT ILLIXR_DIAGNOSTIC_BLAS_PROGRESS)
    message(FATAL_ERROR "BLAS IRQ masking is diagnostic-only and requires progress instrumentation")
  endif()
  target_compile_definitions(app PRIVATE ILLIXR_DIAGNOSTIC_BLAS_IRQ_MASK=1)
endif()
if(ILLIXR_DIAGNOSTIC_BLAS_PROGRESS)
  target_compile_definitions(app PRIVATE ILLIXR_DIAGNOSTIC_BLAS_PROGRESS=1)
endif()
option(ILLIXR_DIAGNOSTIC_IMAGE_ADDRESS "Observe image coordinates and ROI addresses" OFF)
option(ILLIXR_DIAGNOSTIC_IMAGE_ARITHMETIC "Replay captured scalar image-coordinate arithmetic" OFF)
option(ILLIXR_DIAGNOSTIC_FPU_TRAP "Verify rejected FP divides have no register side effects" OFF)
if(ILLIXR_DIAGNOSTIC_FPU_TRAP)
  target_sources(app PRIVATE tests/image_address/trap.cpp tests/image_address/trap.S)
  target_compile_definitions(app PRIVATE ILLIXR_DIAGNOSTIC_FPU_TRAP=1)
endif()
option(ILLIXR_DIAGNOSTIC_FPU_BARRIER "A/B test FCSR serialization before FPU exception entry" OFF)
if(ILLIXR_DIAGNOSTIC_FPU_BARRIER)
  if(NOT ILLIXR_DIAGNOSTIC_IMAGE_ARITHMETIC)
    message(FATAL_ERROR "FPU barrier A/B diagnostic requires the image arithmetic fixture")
  endif()
  target_sources(app PRIVATE tests/image_address/fpu_barrier.S)
  target_compile_definitions(app PRIVATE ILLIXR_DIAGNOSTIC_FPU_BARRIER=1)
  zephyr_ld_options("-Wl,--wrap=z_riscv_fpu_enter_exc")
endif()
if(ILLIXR_DIAGNOSTIC_IMAGE_ARITHMETIC)
  target_sources(app PRIVATE tests/image_address/arithmetic.cpp tests/image_address/arithmetic.S)
  target_compile_definitions(app PRIVATE ILLIXR_DIAGNOSTIC_IMAGE_ARITHMETIC=1)
endif()
if(ILLIXR_DIAGNOSTIC_FAULTS)
  target_sources(app PRIVATE src/fault_diagnostics.cpp)
  zephyr_ld_options("-Wl,--wrap=z_riscv_fault")
endif()
if(ILLIXR_DIAGNOSTIC_RAW_TRAP)
  if(ILLIXR_DIAGNOSTIC_FAULTS)
    message(FATAL_ERROR "Select only one fatal diagnostic implementation")
  endif()
  target_sources(app PRIVATE src/raw_trap_diagnostics.cpp)
  zephyr_ld_options("-Wl,--wrap=z_riscv_fault" "-Wl,--wrap=z_riscv_fatal_error"
                    "-Wl,--wrap=z_fatal_error")
endif()


# ============================================================
# === Global toolchain / cross-compile settings (for OpenCV) ==
# ============================================================

# Make OpenCV's try_compile not attempt to run executables
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY CACHE STRING "" FORCE)

# ============================================================
# === Top-level path anchors ================================
# ============================================================

# Zephyr repo root (…/zephyr_ws/zephyr) is in ZEPHYR_BASE.
# Define a stable workspace top one level above it.
get_filename_component(ZEPHYR_TOP "${ZEPHYR_BASE}" DIRECTORY)

# This Zephyr variable is the root of your application (illixr_working).
# (Zephyr sets it; we fall back safely if needed.)
if(NOT DEFINED APPLICATION_SOURCE_DIR)
  set(APPLICATION_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

message(STATUS "ZEPHYR_BASE            = ${ZEPHYR_BASE}")
message(STATUS "ZEPHYR_TOP             = ${ZEPHYR_TOP}")
message(STATUS "APPLICATION_SOURCE_DIR = ${APPLICATION_SOURCE_DIR}")

# Convenience: absolute path to your helper directory
set(ILLIXR_HELPER_DIR "${APPLICATION_SOURCE_DIR}/src/helper")



# OpenCV's "bitness" detection often fails under Zephyr cross builds
if(NOT DEFINED CMAKE_SIZEOF_VOID_P)
  set(CMAKE_SIZEOF_VOID_P 8 CACHE STRING "Pointer size" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# === RISC-V flags (apply to EVERYTHING: app + plugins + OpenCV)
# ============================================================

set(RV64_FLAGS -march=rv64imafdc_zicsr_zifencei -mabi=lp64d)

# Apply globally so subprojects (OpenCV) inherit them
add_compile_options(${RV64_FLAGS})
add_link_options(${RV64_FLAGS})

# ============================================================
# === OpenCV: build from source once at top-level ============
# ============================================================

# Path to OpenCV source
set(OPENCV_SRC_DIR "" CACHE PATH "Pinned OpenCV 4.5.4 source tree")
if(NOT EXISTS "${OPENCV_SRC_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Set OPENCV_SRC_DIR to the OpenCV 4.5.4 source checkout")
endif()
set(ILLIXR_DATASET_DIR "" CACHE PATH "EuRoC mav0 input directory")
set(ILLIXR_DATASET_FRAMES 50 CACHE STRING "Number of embedded stereo pairs")
set(ILLIXR_DATA_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated" CACHE PATH "Generated embedded dataset headers")
set(ILLIXR_CAM_QUEUE_CAPACITY 8 CACHE STRING "Pending stereo frames")
set(ILLIXR_IMU_QUEUE_CAPACITY 4096 CACHE STRING "IMU records per subscriber")
set(ILLIXR_VIO_DELAY_MS 0 CACHE STRING "One-time VIO delay for overload testing")
set(ILLIXR_VIO_DELAY_AFTER_CAM 1 CACHE STRING "Processed camera count before test delay")
option(ILLIXR_PIN_PLUGINS "Use fixed per-plugin CPU affinity" OFF)
option(ILLIXR_PLATFORM_CHECK_ONLY "Exit after hart, atomic and timer checks" OFF)
option(ILLIXR_BATCH_TRACE "Export deferred trace records through batched HTIF writes" ON)
add_compile_definitions(ILLIXR_BATCH_TRACE=$<BOOL:${ILLIXR_BATCH_TRACE}>)
set(ILLIXR_CORE_HZ 0 CACHE STRING "Hardware core clock for timer-ratio validation; zero skips ratio check")
set(ILLIXR_DISPLAY_HZ 120 CACHE STRING "Simulated display refresh rate")
set(ILLIXR_RENDER_DELAY_NS 6944445 CACHE STRING "Asynchronous render latency in target nanoseconds")
set(ILLIXR_TIMEWARP_DELAY_NS 1000000 CACHE STRING "Asynchronous timewarp latency in target nanoseconds")
set(ILLIXR_RENDER_OFFSET_NS 1000000 CACHE STRING "Render start offset after vsync")
set(ILLIXR_TIMEWARP_MARGIN_NS 1000000 CACHE STRING "Timewarp scheduling margin before GPU work")
target_compile_definitions(app PRIVATE
  ILLIXR_PIN_PLUGINS=$<BOOL:${ILLIXR_PIN_PLUGINS}>
  ILLIXR_PLATFORM_CHECK_ONLY=$<BOOL:${ILLIXR_PLATFORM_CHECK_ONLY}>
  ILLIXR_CORE_HZ=${ILLIXR_CORE_HZ})
# Keep neutral definitions stable for third-party objects across replay tests.
# Only queue-owning plugins override these knobs; producers use k_msgq metadata.
add_compile_definitions(
  ILLIXR_CAM_QUEUE_CAPACITY=8
  ILLIXR_IMU_QUEUE_CAPACITY=4096
  ILLIXR_VIO_DELAY_MS=0
  ILLIXR_VIO_DELAY_AFTER_CAM=1)
function(illixr_replay_overrides target)
  set(default_ILLIXR_CAM_QUEUE_CAPACITY 8)
  set(default_ILLIXR_IMU_QUEUE_CAPACITY 4096)
  set(default_ILLIXR_VIO_DELAY_MS 0)
  set(default_ILLIXR_VIO_DELAY_AFTER_CAM 1)
  foreach(knob ILLIXR_CAM_QUEUE_CAPACITY ILLIXR_IMU_QUEUE_CAPACITY
               ILLIXR_VIO_DELAY_MS ILLIXR_VIO_DELAY_AFTER_CAM)
    if(NOT "${${knob}}" STREQUAL "${default_${knob}}")
      target_compile_options(${target} PRIVATE "-U${knob}" "-D${knob}=${${knob}}")
    endif()
  endforeach()
endfunction()
include_directories("${ILLIXR_DATA_INCLUDE_DIR}")
add_custom_command(
  OUTPUT "${ILLIXR_DATA_INCLUDE_DIR}/embedded_imu.hpp"
         "${ILLIXR_DATA_INCLUDE_DIR}/embedded_cam.hpp"
         "${ILLIXR_DATA_INCLUDE_DIR}/embedded_dataset.hpp"
         "${ILLIXR_DATA_INCLUDE_DIR}/dataset_manifest.json"
  COMMAND ${PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/plugins/openvins/embed_euroc_data.py"
          "${ILLIXR_DATASET_DIR}" "${ILLIXR_DATA_INCLUDE_DIR}" ${ILLIXR_DATASET_FRAMES}
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/openvins/embed_euroc_data.py"
          "${ILLIXR_DATASET_DIR}/imu0/data.csv"
          "${ILLIXR_DATASET_DIR}/cam0/data.csv"
          "${ILLIXR_DATASET_DIR}/cam1/data.csv"
  COMMENT "Embedding bounded EuRoC dataset" VERBATIM)
add_custom_target(generate_dataset DEPENDS
  "${ILLIXR_DATA_INCLUDE_DIR}/embedded_imu.hpp"
  "${ILLIXR_DATA_INCLUDE_DIR}/embedded_cam.hpp"
  "${ILLIXR_DATA_INCLUDE_DIR}/embedded_dataset.hpp"
  "${ILLIXR_DATA_INCLUDE_DIR}/dataset_manifest.json")
add_dependencies(app generate_dataset)

# If your OpenCV needs linker script fragments, apply at top-level (final link)
zephyr_linker_sources(SECTIONS
  ${CMAKE_CURRENT_SOURCE_DIR}/plugins/opencv/src/custom.ld
)

# OpenCV build options (static + minimal; matches your working cmake)
set(BUILD_opencv_apps OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_version OFF CACHE BOOL "" FORCE)
set(OPENCV_BUILD_APP_LIST "" CACHE STRING "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_world OFF CACHE BOOL "" FORCE)

option(OPENCV_BUILD_HIGHGUI "Enable building opencv_highgui module (desktop only)" OFF)
set(OPENCV_DEFAULT_BUILD_LIST "core,imgproc,imgcodecs")

if(OPENCV_BUILD_HIGHGUI)
  set(BUILD_LIST "${OPENCV_DEFAULT_BUILD_LIST},highgui" CACHE STRING "" FORCE)
  set(BUILD_opencv_highgui ON CACHE BOOL "" FORCE)
else()
  set(BUILD_LIST "${OPENCV_DEFAULT_BUILD_LIST}" CACHE STRING "" FORCE)
  set(BUILD_opencv_highgui OFF CACHE BOOL "" FORCE)
endif()

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "" FORCE)

foreach(feature OPENCL OPENCLAMDFFT OPENCLAMDBLAS IPP ITT TBB OPENMP LAPACK PROTOBUF
                QUIRC WEBP GDCM V4L FFMPEG GSTREAMER GTK QT CAROTENE)
  set(WITH_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
set(OPENCV_ENABLE_NONFREE OFF CACHE BOOL "" FORCE)
set(OPENCV_GENERATE_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(ENABLE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
set(CV_ENABLE_INTRINSICS OFF CACHE BOOL "" FORCE)

set(CPU_BASELINE "" CACHE STRING "" FORCE)
set(CPU_DISPATCH "" CACHE STRING "" FORCE)
set(WITH_TIFF OFF CACHE BOOL "" FORCE)
set(BUILD_TIFF OFF CACHE BOOL "" FORCE)
set(WITH_OPENEXR OFF CACHE BOOL "" FORCE)
set(WITH_JASPER OFF CACHE BOOL "" FORCE)
set(WITH_ADE OFF CACHE BOOL "" FORCE)
set(BUILD_ADE OFF CACHE BOOL "" FORCE)

set(OPENCV_FORCE_3RDPARTY_BUILD ON CACHE BOOL "" FORCE)
set(BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(BUILD_PNG ON CACHE BOOL "" FORCE)
set(BUILD_JPEG ON CACHE BOOL "" FORCE)
set(WITH_OPENJPEG OFF CACHE BOOL "" FORCE)
set(BUILD_OPENJPEG OFF CACHE BOOL "" FORCE)

# Bare-metal: no pthread support
set(OPENCV_DISABLE_THREAD_SUPPORT ON CACHE BOOL "" FORCE)
set(WITH_PTHREADS_PF OFF CACHE BOOL "" FORCE)
set(OPENCV_DISABLE_PTHREADS_PF ON CACHE BOOL "" FORCE)

# IMPORTANT:
# Keep these as GLOBAL compile definitions so OpenCV subproject sees them
add_compile_definitions(
  OPENCV_DISABLE_THREAD_SUPPORT
  OPENCV_DISABLE_PTHREADS_PF
  M_PI=3.14159265358979323846
  M_E=2.71828182845904523536
)

# Build OpenCV in the main build tree
add_subdirectory(${OPENCV_SRC_DIR} ${CMAKE_CURRENT_BINARY_DIR}/opencv_build)

# SDK GCC 12 emits __atomic_exchange_1 and ships no libatomic.
# One ABI-correct, sequentially consistent LR/SC fallback supplies it.
target_sources(app PRIVATE plugins/opencv/src/atomic_shim.c)
foreach(cv_target opencv_core opencv_imgproc opencv_imgcodecs zlib libpng libjpeg-turbo)
  if(TARGET ${cv_target})
    set_property(TARGET ${cv_target} APPEND PROPERTY LINK_LIBRARIES zephyr_interface)
  endif()
endforeach()
foreach(cv_target opencv_core opencv_imgproc opencv_imgcodecs)
  target_compile_options(${cv_target} PRIVATE
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/plugins/opencv/src/zephyr_compat.hpp")
endforeach()

# ============================================================
# === OpenCV bundle target used by app + plugins =============
# ============================================================

add_library(illixr_opencv INTERFACE)

# Anything linking illixr_opencv gets these include dirs and libs
set(OPENCV_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/opencv_build)

target_include_directories(illixr_opencv INTERFACE
  ${OPENCV_BIN_DIR}/include          # <--- THIS is the big missing one
  ${CMAKE_CURRENT_BINARY_DIR}        # <--- covers build/opencv2 (seen in your tree)
  ${OPENCV_SRC_DIR}/modules/core/include
  ${OPENCV_SRC_DIR}/modules/imgproc/include
  ${OPENCV_SRC_DIR}/modules/imgcodecs/include
)

target_link_libraries(illixr_opencv INTERFACE
  -Wl,--start-group
  opencv_core
  opencv_imgproc
  opencv_imgcodecs
  -Wl,--end-group
)

if(OPENCV_BUILD_HIGHGUI)
  target_link_libraries(illixr_opencv INTERFACE opencv_highgui)
endif()

# Make OpenCV available to non-plugin app code too
target_link_libraries(app PRIVATE illixr_opencv)
target_include_directories(app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${ZEPHYR_BASE}/../modules/lib/eigen")
target_compile_options(app PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${ILLIXR_HELPER_DIR}/opencv_helper.hpp>"
  "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${ILLIXR_HELPER_DIR}/eigen_lib_fix.hpp>")
target_sources(app PRIVATE plugins/opencv/src/zephyr_support.cpp)

# ============================================================
# === YAML CONFIG PARSING ====================================
# ============================================================

if (NOT DEFINED YAML_FILE)
  message(FATAL_ERROR "You must pass: -DYAML_FILE=<path_to_yaml>")
endif()

get_filename_component(YAML_FILE "${YAML_FILE}" ABSOLUTE)
message(STATUS "Using YAML_FILE: ${YAML_FILE}")

set(READ_YAML_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/read_yaml.py")
set(GENERATED_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/generated_config.hpp")

add_custom_command(
  OUTPUT ${GENERATED_CONFIG}
  COMMAND ${PYTHON_EXECUTABLE} ${READ_YAML_SCRIPT} ${YAML_FILE} ${GENERATED_CONFIG}
  DEPENDS ${READ_YAML_SCRIPT} ${YAML_FILE}
  COMMENT "Parsing YAML and generating ${GENERATED_CONFIG}"
)

add_custom_target(generate_yaml ALL DEPENDS ${GENERATED_CONFIG})
add_dependencies(app generate_yaml)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# ============================================================
# === CORE SOURCE FILES ======================================
# ============================================================

option(ILLIXR_ESTIMATOR_REPLAY "Isolated fixed-order Spike estimator test" OFF)
if(ILLIXR_ESTIMATOR_REPLAY)
  if(NOT BOARD STREQUAL "spike_riscv64")
    message(FATAL_ERROR "Streamed estimator replay is Spike-only")
  endif()
  set(ILLIXR_ESTIMATOR_INPUT "" CACHE FILEPATH "Test input stream")
  set(ILLIXR_ESTIMATOR_OUTPUT "" CACHE FILEPATH "Test output state stream")
  if(NOT ILLIXR_ESTIMATOR_INPUT OR NOT ILLIXR_ESTIMATOR_OUTPUT)
    message(FATAL_ERROR "Explicit estimator stream paths required")
  endif()
  add_compile_definitions(ILLIXR_ESTIMATOR_REPLAY=1 ILLIXR_ESTIMATOR_INPUT="${ILLIXR_ESTIMATOR_INPUT}" ILLIXR_ESTIMATOR_OUTPUT="${ILLIXR_ESTIMATOR_OUTPUT}")
  execute_process(COMMAND "${PYTHON_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/extract_config.py" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/estimator_config" COMMAND_ERROR_IS_FATAL ANY)
  target_include_directories(app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/plugins/openvins" "${CMAKE_CURRENT_BINARY_DIR}/estimator_config")
  set(ILLIXR_MAIN tests/estimator_spike/main.cpp)
else()
  set(ILLIXR_MAIN src/main.cpp)
endif()
target_sources(app PRIVATE
  ${ILLIXR_MAIN}
  src/trace_transport.cpp
  src/node.cpp
  src/relative_clock.cpp
  src/phonebook_new.cpp
  src/stoplight.cpp
)

# ============================================================
# === Restore Zephyr output paths (OpenCV can mess with them) =
# ============================================================

set_target_properties(zephyr_final PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/zephyr"
  OUTPUT_NAME "zephyr"
)

if(TARGET zephyr_prebuilt)
  set_target_properties(zephyr_prebuilt PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/zephyr"
  )
endif()

# Ensure Zephyr post-build scripts look for zephyr/zephyr.elf
set(KERNEL_ELF_NAME zephyr.elf CACHE INTERNAL "" FORCE)


# ============================================================
# === READ YAML AND LOAD PLUGIN NAMES ========================
# ============================================================

file(READ "${YAML_FILE}" YAML_CONTENTS)
string(REGEX MATCH "plugins:[ \t]*([^\n\r]+)" PLUGIN_LINE "${YAML_CONTENTS}")

if(PLUGIN_LINE)
  string(REGEX REPLACE "plugins:[ \t]*" "" PLUGIN_LIST_RAW "${PLUGIN_LINE}")
  string(REPLACE "," ";" PLUGIN_NAMES "${PLUGIN_LIST_RAW}")

  set(PLUGIN_NAMES_CLEAN "")
  foreach(P IN LISTS PLUGIN_NAMES)
    string(STRIP "${P}" P_STRIPPED)
    if(P_STRIPPED)
      list(APPEND PLUGIN_NAMES_CLEAN "${P_STRIPPED}")
    endif()
  endforeach()

  message(STATUS "Resolved plugins: ${PLUGIN_NAMES_CLEAN}")

  if("render_loop" IN_LIST PLUGIN_NAMES_CLEAN)
    if(NOT "pose_prediction" IN_LIST PLUGIN_NAMES_CLEAN OR NOT "timewarp" IN_LIST PLUGIN_NAMES_CLEAN)
      message(FATAL_ERROR "The GPU pipeline requires pose_prediction, render_loop and timewarp")
    endif()
    add_compile_definitions(ILLIXR_GPU_PIPELINE=1)
    add_compile_definitions(
      ILLIXR_DISPLAY_HZ=${ILLIXR_DISPLAY_HZ}
      ILLIXR_RENDER_DELAY_NS=${ILLIXR_RENDER_DELAY_NS}
      ILLIXR_TIMEWARP_DELAY_NS=${ILLIXR_TIMEWARP_DELAY_NS}
      ILLIXR_RENDER_OFFSET_NS=${ILLIXR_RENDER_OFFSET_NS}
      ILLIXR_TIMEWARP_MARGIN_NS=${ILLIXR_TIMEWARP_MARGIN_NS})
  else()
    add_compile_definitions(ILLIXR_GPU_PIPELINE=0)
  endif()

  set(ILLIXR_EYE_TRACKING_ENABLED OFF CACHE INTERNAL "Eye plugins compiled into this firmware" FORCE)
  if("eye_tracking" IN_LIST PLUGIN_NAMES_CLEAN OR "offline_eye" IN_LIST PLUGIN_NAMES_CLEAN)
    if(NOT "eye_tracking" IN_LIST PLUGIN_NAMES_CLEAN OR NOT "offline_eye" IN_LIST PLUGIN_NAMES_CLEAN OR NOT "timewarp" IN_LIST PLUGIN_NAMES_CLEAN)
      message(FATAL_ERROR "Eye tracking requires offline_eye, eye_tracking, and timewarp")
    endif()
    if(NOT ILLIXR_DISPLAY_HZ EQUAL 120)
      message(FATAL_ERROR "The eye workload uses the agreed 120 Hz publication rate")
    endif()
    set(ILLIXR_EYE_TRACKING_ENABLED ON CACHE INTERNAL "Eye plugins compiled into this firmware" FORCE)
    add_compile_definitions(ILLIXR_EYE_TRACKING=1)
    include(cmake/illixr_ritnet.cmake)
    target_sources(app PRIVATE src/eye_tracking.cpp)
  endif()

  foreach(P IN LISTS PLUGIN_NAMES_CLEAN)
    set(PLUGIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/plugins/${P}")
    message(STATUS "Adding plugin '${P}' from ${PLUGIN_DIR}")

    add_subdirectory("${PLUGIN_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/plugins/${P}")

    if(TARGET ${P})
      target_compile_definitions(${P} PRIVATE ILLIXR_PIN_PLUGINS=$<BOOL:${ILLIXR_PIN_PLUGINS}>)
      if(P STREQUAL "openvins" OR P STREQUAL "imu_integrator")
        illixr_replay_overrides(${P})
      endif()
      add_dependencies(${P} generate_dataset)
      target_sources(app PRIVATE $<TARGET_OBJECTS:${P}>)
      message(STATUS "Added plugin '${P}' object files into app")
    else()
      message(WARNING "Plugin '${P}' did not define target '${P}'")
    endif()
  endforeach()
endif()
