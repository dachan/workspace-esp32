cmake_minimum_required(VERSION 3.16)

set(AI_MODEL_PROFILE "legacy" CACHE STRING "ai-model-control hardware profile")
set(AI_MODEL_SUPERMINI_DISPLAY_ORDER "legacy" CACHE STRING
    "SuperMini display wiring: legacy or header")
set(AI_MODEL_MODEL_ENCODER_DIRECTION "-1" CACHE STRING
    "Model encoder catalog direction: -1 or 1")

if(NOT AI_MODEL_SUPERMINI_DISPLAY_ORDER STREQUAL "legacy"
   AND NOT AI_MODEL_SUPERMINI_DISPLAY_ORDER STREQUAL "header")
    message(FATAL_ERROR "AI_MODEL_SUPERMINI_DISPLAY_ORDER must be legacy or header")
endif()
if(NOT AI_MODEL_MODEL_ENCODER_DIRECTION STREQUAL "-1"
   AND NOT AI_MODEL_MODEL_ENCODER_DIRECTION STREQUAL "1")
    message(FATAL_ERROR "AI_MODEL_MODEL_ENCODER_DIRECTION must be -1 or 1")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ai-model-control)

# Archive the active build artifact only after binary generation succeeds.
add_custom_command(
    TARGET app
    POST_BUILD
    COMMAND "${CMAKE_SOURCE_DIR}/scripts/archive_build.sh"
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin"
            "${CMAKE_SOURCE_DIR}/VERSION" "${CMAKE_BINARY_DIR}/dist"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Archive firmware binary into dist/"
    VERBATIM
)
