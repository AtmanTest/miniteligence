/*  main.cpp — MiniTelligence : la carte, et rien d'autre.
 *
 *  Alimentation, e-paper, carte SD, puis l'application (mt_app.cpp).
 *  Le métier est dans mt_model.cpp, le dessin dans mt_ui.cpp : les deux
 *  compilent aussi sur le PC (tests/run_unit.sh).
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_err.h"

esp_err_t mt_epd_start(void);
bool     mt_sd_start(void);
void     mt_app_start(const char *ssid, bool sd_ok, void (*envoie)(const uint8_t *));
void     mt_app_boucle(void);

#include "user_config.h"

static const char *TAG = "minitel";

extern "C" void app_main(void)
{
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_LOGI(TAG, "MiniTelligence : demarrage");
    mt_epd_start();
    ESP_LOGI(TAG, "e-paper pret (200x200)");

    bool sd = mt_sd_start();
    ESP_LOGI(TAG, "carte SD : %s", sd ? "montee" : "absente");

    mt_app_start(WIFI_SSID, sd, NULL);
    mt_app_boucle();
}
