idf_component_register(
    SRCS "main.cpp" "mt_app.cpp" "mt_ui.cpp" "mt_model.cpp" "art.cpp" "font5.cpp"
    INCLUDE_DIRS "./"
    PRIV_REQUIRES json esp_http_client esp_http_server esp-tls esp_wifi esp_event esp_netif
                  nvs_flash fatfs sdmmc driver esp_timer esp_pm
                  board_power_bsp i2c_bsp audio_bsp epaper_driver_bsp codec_board)
