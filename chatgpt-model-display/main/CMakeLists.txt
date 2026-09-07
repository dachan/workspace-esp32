idf_component_register(
    SRCS "main.c" "display.c" "canvas.c" "font.c" "ui.c" "clock.c" "front_title.c" "queue_status.c" "catalog.c"
         "model_parse.c" "serial_model.c" "serial_sync.c" "model_nvs.c" "encoder.c" "touch.c"
         "calibrate.c"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_BINARY_DIR}"
)

set(BUILD_NUMBER_H "${CMAKE_CURRENT_BINARY_DIR}/build_number.h")
add_custom_command(
    OUTPUT "${BUILD_NUMBER_H}"
    COMMAND "${PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/generate_build_header.py"
            "${CMAKE_SOURCE_DIR}/VERSION" "${BUILD_NUMBER_H}"
    DEPENDS "${CMAKE_SOURCE_DIR}/VERSION" "${CMAKE_SOURCE_DIR}/scripts/generate_build_header.py"
    COMMENT "Generate firmware version header"
    VERBATIM
)
add_custom_target(firmware_build_number DEPENDS "${BUILD_NUMBER_H}")
add_dependencies(${COMPONENT_LIB} firmware_build_number)
