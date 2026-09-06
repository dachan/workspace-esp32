idf_component_register(
    SRCS "main.c" "display.c" "canvas.c" "font.c" "model_parse.c" "serial_model.c" "model_nvs.c"
    INCLUDE_DIRS "."
)

set(BUILD_NUMBER_H "${CMAKE_CURRENT_BINARY_DIR}/build_number.h")
add_custom_command(
    OUTPUT "${BUILD_NUMBER_H}"
    COMMAND ${PYTHON} "${CMAKE_SOURCE_DIR}/scripts/bump_build.py" "${BUILD_NUMBER_H}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Bump firmware build number when sources change"
    VERBATIM
)
add_custom_target(firmware_build_number DEPENDS "${BUILD_NUMBER_H}")
add_dependencies(${COMPONENT_LIB} firmware_build_number)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# After the final app binary exists, copy into dist/ with build number.
add_custom_command(
    TARGET ${COMPONENT_LIB}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Build number header ready"
    VERBATIM
)
