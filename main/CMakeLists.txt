idf_component_register(SRCS "hello_world_main.c" "helper/storage.c"
                        INCLUDE_DIRS "helper"
                        REQUIRES nvs_flash
                        PRIV_REQUIRES spi_flash)
