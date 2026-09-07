idf_component_register(
    SRCS "main.c" "display.c" "canvas.c" "font.c" "model_parse.c" "serial_model.c" "model_nvs.c" "encoder.c"
    INCLUDE_DIRS "."
)

set(BUILD_NUMBER_H "${CMAKE_SOURCE_DIR}/main/build_number.h")
add_custom_target(firmware_build_number ALL
    COMMAND ${PYTHON} "${CMAKE_SOURCE_DIR}/scripts/bump_build.py" "${BUILD_NUMBER_H}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    BYPRODUCTS "${BUILD_NUMBER_H}"
    COMMENT "Bump firmware build number when sources change"
    VERBATIM
)
add_dependencies(${COMPONENT_LIB} firmware_build_number)
