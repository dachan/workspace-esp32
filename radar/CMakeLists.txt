cmake_minimum_required(VERSION 3.16)

set(RADAR_BOARD "default" CACHE STRING "Radar hardware board profile")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(radar)
