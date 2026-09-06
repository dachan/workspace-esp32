cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(chatgpt-model-display)

# Archive firmware .bin into dist/ after each successful link of the app.
add_custom_command(
    TARGET app
    POST_BUILD
    COMMAND "${CMAKE_SOURCE_DIR}/scripts/archive_build.sh"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Archive firmware binary into dist/"
    VERBATIM
)
