idf_component_register(SRCS "main.cpp"
                              "helper/storage.cpp"
                              "tasks/task.cpp"
                              "tasks/remote_setup_task.cpp"
                        INCLUDE_DIRS "helper" "tasks"
                        REQUIRES nvs_flash esp_tinyusb
                        PRIV_REQUIRES spi_flash)
