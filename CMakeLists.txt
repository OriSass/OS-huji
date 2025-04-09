cmake_minimum_required(VERSION 3.10)
project(ex1_new)

set(CMAKE_CXX_STANDARD 11)

add_executable(ex1_new memory_latency.cpp memory_latency.h measure.cpp measure.h)
