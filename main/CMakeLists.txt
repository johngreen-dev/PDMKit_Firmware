idf_component_register(SRCS "main.cpp"
                              "helper/storage.cpp"
                              "helper/gpio.cpp"
                              "helper/expr.cpp"
                              "helper/io_config.cpp"
                              "helper/io_rules.cpp"
                              "helper/can_store.cpp"
                              "tasks/task.cpp"
                              "tasks/remote_setup_task.cpp"
                              "tasks/main_controller.cpp"
                              "tasks/can_task.cpp"
                        INCLUDE_DIRS "helper" "tasks"
                        REQUIRES nvs_flash esp_tinyusb esp_driver_gpio esp_adc
                                 esp_driver_ledc json driver
                        PRIV_REQUIRES spi_flash)
