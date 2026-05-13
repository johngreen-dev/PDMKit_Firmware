idf_component_register(SRCS "main.cpp"
                              "helper/storage.cpp"
                              "helper/gpio.cpp"
                              "tasks/task.cpp"
                              "tasks/remote_setup_task.cpp"
                              "tasks/main_controller.cpp"
                        INCLUDE_DIRS "helper" "tasks"
                        REQUIRES nvs_flash esp_tinyusb esp_driver_gpio esp_adc esp_driver_ledc
                        PRIV_REQUIRES spi_flash)
