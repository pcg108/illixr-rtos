cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(illixr_vector_context)
# Use the private Zephyr checkout from scripts/prepare_vector_zephyr.py.
target_sources(app PRIVATE main.cpp ../../src/vector_check.cpp ../../src/vector_check.S)
