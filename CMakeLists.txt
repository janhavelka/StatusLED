idf_component_register(
  SRCS
    "src/StatusLed.cpp"
    "src/StatusLedBackendIdf5.cpp"
  INCLUDE_DIRS "include"
  PRIV_INCLUDE_DIRS "src"
  REQUIRES esp_driver_rmt esp_driver_gpio
)

target_compile_definitions(${COMPONENT_LIB}
  PUBLIC
    STATUSLED_BACKEND_IDF_WS2812=0
    STATUSLED_BACKEND_IDF5_WS2812=1
    STATUSLED_BACKEND_NEOPIXELBUS=0
    STATUSLED_BACKEND_NULL=0
)
