cmake_minimum_required(VERSION 3.16)

set(AI_MODEL_PROFILE "legacy" CACHE STRING "ai-model-control hardware profile")

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
