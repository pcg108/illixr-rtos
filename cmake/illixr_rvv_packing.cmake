# Explicit vector object: application code remains on its accepted scalar ABI flags.
function(illixr_add_rvv_packing target)
  if(NOT CONFIG_RISCV_ISA_EXT_V)
    message(FATAL_ERROR "RVV packing requires vector context support")
  endif()
  get_filename_component(packing_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
  get_filename_component(blas_lib_dir "${ILLIXR_OPENBLAS_ARCHIVE}" DIRECTORY)
  file(READ "${blas_lib_dir}/../manifest.json" packing_manifest)
  string(JSON packing_cc GET "${packing_manifest}" compiler)
  string(JSON packing_sysroot GET "${packing_manifest}" sysroot)
  execute_process(COMMAND "${packing_cc}" -dumpfullversion OUTPUT_VARIABLE packing_version OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  if(NOT packing_version STREQUAL "13.2.0")
    message(FATAL_ERROR "Revalidate RVV packing for a changed compiler")
  endif()
  option(ILLIXR_PACKING_SATURN_COMPAT "Explicit conversion fences and scalar RMM compatibility for existing Saturn images" OFF)
  set(packing_extra_flags)
  if(ILLIXR_PACKING_SATURN_COMPAT)
    list(APPEND packing_extra_flags -DILLIXR_PACKING_SATURN_COMPAT=1)
    target_compile_definitions(${target} PRIVATE ILLIXR_PACKING_SATURN_COMPAT=1)
  endif()
  set(packing_object "${CMAKE_CURRENT_BINARY_DIR}/gemmini_packing_rvv.o")
  add_custom_command(OUTPUT "${packing_object}"
    COMMAND "${packing_cc}" "--sysroot=${packing_sysroot}" -march=rv64gcv_zvl256b -mabi=lp64d -mcmodel=medany -O2 -fno-fast-math -ffp-contract=off -ffunction-sections -fdata-sections
            ${packing_extra_flags} -c "${packing_root}/src/gemmini_packing_rvv.c" -o "${packing_object}"
    DEPENDS "${packing_root}/src/gemmini_packing_rvv.c" "${packing_root}/src/gemmini_packing_rvv.h"
    VERBATIM)
  set_source_files_properties("${packing_object}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
  target_sources(${target} PRIVATE "${packing_object}")
endfunction()
