set(APP_MAIN_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../main")

idf_component_register(
    SRCS "dht11_diagnostic.c" "dht11_legacy.c"
    INCLUDE_DIRS "." "${APP_MAIN_DIR}"
    REQUIRES esp_driver_gpio esp_timer
)
