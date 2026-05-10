idf_component_register(SRCS "hello_world_main.cpp" "helper/storage.cpp"
                        INCLUDE_DIRS "helper"
                        REQUIRES nvs_flash
                        PRIV_REQUIRES spi_flash)
