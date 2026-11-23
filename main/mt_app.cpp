/*  mt_app.cpp — MiniTelligence : le carnet qui pense, sur la carte BATMino.
 *
 *  Ce que l'application fait réellement :
 *    - lit le micro SD (carnet), les capteurs I2C (SHTC3), l'horloge RTC ;
 *    - classe les notes par TF-IDF, localement, sans réseau (mt_model) ;
 *    - planifie les révisions avec SM-2, localement (mt_model) ;
 *    - interroge un LLM (Groq, sinon DeepSeek) avec seulement les notes
 *      pertinentes, et affiche une réponse courte ;
 *    - expose une API HTTP de recette (/note /ask /etat /ecran /bouton) ;
 *    - dessine 6 écrans e-paper et écoute deux boutons.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

#include "user_config.h"
#include "art.h"
#include "geste.h"
#include "mt_model.h"
#include "mt_ui.h"
#include "mt_tests_stats.h"

#include "board_power_bsp.h"
#include "epaper_driver_bsp.h"
#include "i2c_bsp.h"

static const char *TAG = "minitel";

/* ------------------------------------------------------------------ */
/* état                                                              */
/* ------------------------------------------------------------------ */
static mt_store_t      g_store;
static SemaphoreHandle_t g_mux;
static mt_vue_t        g_vue;
static uint8_t         g_fb[ART_FB_LEN];
static uint8_t         g_fb_dernier[ART_FB_LEN];
static bool            g_fb_pret;

static char  g_question[MT_QUEST_L];
static char  g_reponse[420];
static mt_etat_t g_etat = MT_REPOS;

static bool  g_wifi, g_heure, g_sd_ok;
static int   g_rssi, g_bat = -1;
static float g_temp = NAN, g_hum = NAN;
static int   g_ecran = MT_ECRAN_BRIEF, g_curseur;

static int   g_rev_idx = -1, g_rev_n, g_rev_total;
static bool  g_rev_visible;

static char  g_message[64];
static int64_t g_message_fin;
static int   g_rendu_ms, g_ecran_ms;
static int   g_llm_statut = -1;
static char  g_llm_info[160];
static char  g_info_echappee[200];
static int   g_llm_url;          /* 1 = le chemin LLM a ete pris */
static int   g_cle_len;

static epaper_driver_display *g_epd;
static board_power_bsp_t *g_pwr;

/* nom 8.3 : la carte est montee sans noms longs (FATFS LFN desactive) */
#define FICHIER_CARNET "/sdcard/mt.db"
#define NVS_NS "minitel"

static int64_t maintenant(void)
{
    time_t t = time(NULL);
    if (!g_heure || t < 1600000000) return 0;
    return (int64_t)t;
}

static void dit(const char *txt)
{
    snprintf(g_message, sizeof(g_message), "%s", txt ? txt : "");
    g_message_fin = esp_timer_get_time() / 1000 + 2600;
}

/* ------------------------------------------------------------------ */
/* écran                                                             */
/* ------------------------------------------------------------------ */
static void ecran_envoie(const uint8_t *fb)
{
    if (!g_epd) return;
    int64_t t0 = esp_timer_get_time() / 1000;
    g_epd->EPD_ShowFullClean(fb, ART_FB_LEN);
    g_ecran_ms = (int)(esp_timer_get_time() / 1000 - t0);
}

static void affiche(bool plein)
{
    if (xSemaphoreTake(g_mux, pdMS_TO_TICKS(3000)) != pdTRUE) return;

    mt_vue_t *v = &g_vue;
    memset(v, 0, sizeof(*v));
    v->ecran = (uint8_t)g_ecran;
    v->curseur = g_curseur;
    v->store = &g_store;
    v->now = maintenant();
    v->heure_ok = g_heure;
    v->wifi = g_wifi;
    v->sd = g_sd_ok;
    v->rssi = g_rssi;
    v->bat_pct = g_bat;
    v->temp_c = g_temp;
    v->hum = g_hum;
    v->dues = 0;
    v->rendu_ms = g_rendu_ms;
    v->ecran_ms = g_ecran_ms;
    v->heap = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    v->psram = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    v->message = (g_message[0] && esp_timer_get_time() / 1000 < g_message_fin) ? g_message : NULL;
    v->etat = g_etat;
    v->question = g_question;
    v->reponse = g_reponse;
    v->rev_idx = g_rev_idx;
    v->rev_n = g_rev_n;
    v->rev_total = g_rev_total;
    v->rev_visible = g_rev_visible;

    int d = 0;
    mt_resume(&g_store, v->now, &d, NULL);
    v->dues = d;

    int64_t t0 = esp_timer_get_time() / 1000;
    mt_ui_dessine(v, g_fb);
    g_rendu_ms = (int)(esp_timer_get_time() / 1000 - t0);

    memcpy(g_fb_dernier, g_fb, ART_FB_LEN);
    g_fb_pret = true;

    if (plein) ecran_envoie(g_fb);
    xSemaphoreGive(g_mux);
}

/* ------------------------------------------------------------------ */
/* carnet : SD + mémoire                                             */
/* ------------------------------------------------------------------ */
static void carnet_sauve(void)
{
    if (!g_sd_ok) return;
    char *buf = (char *)heap_caps_malloc(24 * 1024, MALLOC_CAP_SPIRAM);
    if (!buf) return;
    size_t n = mt_serialise(&g_store, buf, 24 * 1024);
    FILE *f = fopen(FICHIER_CARNET, "wb");
    if (f) {
        size_t e = fwrite(buf, 1, n, f);
        fclose(f);
        if (e != n) ESP_LOGW(TAG, "carnet : ecriture partielle %u/%u", (unsigned)e, (unsigned)n);
    } else {
        ESP_LOGW(TAG, "carnet : %s non ouvrable", FICHIER_CARNET);
    }
    free(buf);
}

static void carnet_charge(void)
{
    if (!g_sd_ok) return;
    FILE *f = fopen(FICHIER_CARNET, "rb");
    if (!f) { ESP_LOGI(TAG, "carnet neuf : %s absent", FICHIER_CARNET); return; }
    static char buf[24 * 1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    if (mt_deserialise(&g_store, buf)) ESP_LOGI(TAG, "carnet : %d notes", g_store.n);
    else ESP_LOGW(TAG, "carnet illisible, on repart a vide");
}

static bool sd_monte(void)
{
    sdmmc_card_t *card;
    esp_vfs_fat_sdmmc_mount_config_t cfg = {
        .format_if_mount_failed = false,
        .max_files = 6,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = SDMMC_FREQ_PROBING;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = SD_CLK_PIN;
    slot.cmd = SD_MOSI_CMD_PIN;
    slot.d0 = SD_MISO_D0_PIN;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    return esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &cfg, &card) == ESP_OK;
}

/* ------------------------------------------------------------------ */
/* capteurs                                                          */
/* ------------------------------------------------------------------ */
static bool shtc3_lit(void)
{
    if (!shtc3_handle) return false;
    uint8_t cmd[2] = { 0x7C, 0xA2 };      /* mesure T puis RH, sans veille */
    if (i2c_master_transmit(shtc3_handle, cmd, 2, 200) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    uint8_t d[6];
    if (i2c_master_receive(shtc3_handle, d, 6, 200) != ESP_OK) return false;
    uint16_t rt = (uint16_t)((d[0] << 8) | d[1]);
    uint16_t rh = (uint16_t)((d[3] << 8) | d[4]);
    float t = -45.0f + 175.0f * (float)rt / 65535.0f;
    float h = 100.0f * (float)rh / 65535.0f;
    if (t < -40.0f || t > 85.0f || h < 0.0f || h > 100.0f) return false;
    g_temp = t;
    g_hum = h;
    return true;
}

static int bcd(int v) { return ((v / 10) << 4) | (v % 10); }
static int debcd(int v) { return (v >> 4) * 10 + (v & 0x0F); }

static bool rtc_lit(struct tm *out)
{
    if (!rtc_dev_handle) return false;
    uint8_t r[7];
    if (i2c_read_buff(rtc_dev_handle, 0x04, r, 7) != 0) return false;
    memset(out, 0, sizeof(*out));
    out->tm_sec = debcd(r[0] & 0x7F);
    out->tm_min = debcd(r[1] & 0x7F);
    out->tm_hour = debcd(r[2] & 0x3F);
    out->tm_mday = debcd(r[3] & 0x3F);
    out->tm_mon = debcd(r[5] & 0x1F) - 1;
    out->tm_year = debcd(r[6]) + 100;
    if (out->tm_year < 124 || out->tm_mon > 11 || out->tm_mday < 1 || out->tm_mday > 31) return false;
    return true;
}

static void rtc_ecrit(const struct tm *t)
{
    if (!rtc_dev_handle) return;
    uint8_t r[7] = {
        (uint8_t)bcd(t->tm_sec), (uint8_t)bcd(t->tm_min), (uint8_t)bcd(t->tm_hour),
        (uint8_t)bcd(t->tm_mday), (uint8_t)t->tm_wday,
        (uint8_t)bcd(t->tm_mon + 1), (uint8_t)bcd(t->tm_year % 100)
    };
    i2c_write_buff(rtc_dev_handle, 0x04, r, 7);
}

/* ------------------------------------------------------------------ */
/* réseau                                                            */
/* ------------------------------------------------------------------ */
typedef struct { const char *ssid, *pass; } wifi_t;
static const wifi_t g_reseaux[] = {
    { WIFI_SSID, WIFI_PASS },
#ifdef WIFI_SSID_ALT
    { WIFI_SSID_ALT, WIFI_PASS_ALT },
#endif
#ifdef WIFI_SSID_ALT2
    { WIFI_SSID_ALT2, WIFI_PASS_ALT2 },
#endif
};
static const int g_reseaux_n = (int)(sizeof(g_reseaux) / sizeof(g_reseaux[0]));
static int g_reseau_idx;
static EventGroupHandle_t g_wifi_eg;
#define WIFI_OK_BIT 0x1

static void wifi_bascule(void)
{
    g_reseau_idx = (g_reseau_idx + 1) % g_reseaux_n;
    wifi_config_t wc = {};
    snprintf((char *)wc.sta.ssid, sizeof(wc.sta.ssid), "%s", g_reseaux[g_reseau_idx].ssid);
    snprintf((char *)wc.sta.password, sizeof(wc.sta.password), "%s", g_reseaux[g_reseau_idx].pass);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_connect();
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        g_wifi = false;
        if (g_wifi_eg) xEventGroupClearBits(g_wifi_eg, WIFI_OK_BIT);
        wifi_bascule();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        g_wifi = true;
        if (g_wifi_eg) xEventGroupSetBits(g_wifi_eg, WIFI_OK_BIT);
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) g_rssi = ap.rssi;
        ESP_LOGI(TAG, "wifi : %s, %d dBm", g_reseaux[g_reseau_idx].ssid, g_rssi);
    }
}

static void wifi_demarre(void)
{
    g_wifi_eg = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL, NULL));

    wifi_config_t wc = {};
    snprintf((char *)wc.sta.ssid, sizeof(wc.sta.ssid), "%s", g_reseaux[0].ssid);
    snprintf((char *)wc.sta.password, sizeof(wc.sta.password), "%s", g_reseaux[0].pass);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY);
    esp_wifi_set_country_code("FR", true);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp);
}

/* ------------------------------------------------------------------ */
/* HTTP : lecture d'une réponse entière, puis POST JSON              */
/* ------------------------------------------------------------------ */
/* collecte du corps par evenement : c'est la seule facon fiable de
   recuperer une reponse POST avec esp_http_client. */
struct recu_t { char *buf; size_t cap, n; };

static esp_err_t http_evenement(esp_http_client_event_t *e)
{
    struct recu_t *r = (struct recu_t *)e->user_data;
    if (!r) return ESP_OK;
    if (e->event_id == HTTP_EVENT_ON_DATA && e->data_len > 0) {
        size_t place = r->cap - 1 - r->n;
        size_t n = (size_t)e->data_len < place ? (size_t)e->data_len : place;
        memcpy(r->buf + r->n, e->data, n);
        r->n += n;
        r->buf[r->n] = 0;
    }
    return ESP_OK;
}

static char *http_appel(const char *url, const char *cle, const char *corps, size_t cap, int *statut)
{
    char *buf = (char *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) return NULL;
    buf[0] = 0;
    if (statut) *statut = -100;

    struct recu_t recu = { buf, cap, 0 };

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 20000;
    cfg.buffer_size = 2048;
    cfg.buffer_size_tx = 2048;
    cfg.event_handler = http_evenement;
    cfg.user_data = &recu;

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) { free(buf); return NULL; }
    esp_http_client_set_method(c, HTTP_METHOD_POST);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_header(c, "Accept", "application/json");
    if (cle && cle[0]) {
        static char auth[192];
        snprintf(auth, sizeof(auth), "Bearer %s", cle);
        esp_http_client_set_header(c, "Authorization", auth);
    }
    if (corps) esp_http_client_set_post_field(c, corps, (int)strlen(corps));

    esp_err_t r = esp_http_client_perform(c);
    if (statut) *statut = (r == ESP_OK) ? esp_http_client_get_status_code(c) : (int)r;
    esp_http_client_close(c);
    esp_http_client_cleanup(c);

    if (recu.n == 0) { free(buf); return NULL; }
    return buf;
}

/* échappe une chaîne pour le JSON du prompt */
static void json_echappe(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    for (const char *p = in ? in : ""; *p && o + 6 < cap; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
        else if (c == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
        else if (c < 0x20) { out[o++] = ' '; }
        else out[o++] = (char)c;
    }
    out[o] = 0;
}

/* ------------------------------------------------------------------ */
/* le dialogue : recherche locale, puis LLM                           */
/* ------------------------------------------------------------------ */
static QueueHandle_t g_q_ia;

/* la question part dans une file : la tâche IA fait le travail lourd
   (TLS + modèle), avec sa propre pile ; le serveur HTTP ne bloque jamais. */
static void repond(const char *question)
{
    if (!question || !question[0]) { dit("QUESTION VIDE"); return; }
    snprintf(g_question, sizeof(g_question), "%s", question);
    g_etat = MT_REFLEXION;
    g_ecran = MT_ECRAN_QUESTION;
    affiche(true);
    if (g_q_ia) xQueueSend(g_q_ia, g_question, pdMS_TO_TICKS(200));
}

/* la tâche : prompt borné, appel du modèle, sinon recherche locale seule */
static void ia_tache(void *arg)
{
    (void)arg;
    char question[MT_QUEST_L];
    for (;;) {
        if (xQueueReceive(g_q_ia, question, portMAX_DELAY) != pdTRUE) continue;
        g_llm_statut = -3;

        char prompt[1600];
        mt_prompt(&g_store, question, prompt, sizeof(prompt));

        char *p_ech = (char *)malloc(2400);
        char *corps = (char *)malloc(2600);
        char *json = NULL;
        if (p_ech && corps) {
            json_echappe(prompt, p_ech, 2400);
            const char *cle = NULL, *url = NULL, *modele = NULL;
#ifdef GROQ_KEY
            if (strlen(GROQ_KEY) > 10) { cle = GROQ_KEY; url = GROQ_LLM_URL; modele = GROQ_LLM_MODEL; }
#endif
#ifdef DEEPSEEK_KEY
            if (!cle && strlen(DEEPSEEK_KEY) > 10) { cle = DEEPSEEK_KEY; url = DS_URL; modele = DS_MODEL; }
#endif
            g_llm_url = url ? 1 : 0;
            g_cle_len = (url && cle) ? (int)strlen(cle) : 0;
            if (url) {
                snprintf(corps, 2600,
                    "{\"model\":\"%s\",\"temperature\":0.2,\"max_tokens\":220,"
                    "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}", modele, p_ech);
                int st = 0;
                json = http_appel(url, cle, corps, 32 * 1024, &st);
                g_llm_statut = st;
                snprintf(g_llm_info, sizeof(g_llm_info), "%s", json ? json : "(vide)");
            json_echappe(g_llm_info, g_info_echappee, sizeof(g_info_echappee));
                ESP_LOGI(TAG, "LLM HTTP %d : %.80s", st, json ? json : "(vide)");
            }
        }
        free(p_ech);
        free(corps);

        if (json && mt_extrait_reponse(json, g_reponse, sizeof(g_reponse))) {
            g_etat = MT_REPONSE;
            dit("REPONSE DU MODELE");
        } else {
            uint16_t res[3];
            float sc[3];
            int n = mt_recherche(&g_store, question, res, sc, 3);
            if (n > 0) {
                snprintf(g_reponse, sizeof(g_reponse), "Sans reseau, voici ce que je sais : [%s] %s",
                         g_store.notes[res[0]].tag, g_store.notes[res[0]].txt);
                g_etat = MT_REPONSE;
            } else {
                snprintf(g_reponse, sizeof(g_reponse),
                         "Aucune note ne repond a cette question, et le modele est injoignable.");
                g_etat = MT_ERREUR;
            }
        }
        if (json) free(json);
        affiche(true);
    }
}

/* ------------------------------------------------------------------ */
/* révision                                                          */
/* ------------------------------------------------------------------ */
static void revision_demarre(void)
{
    uint16_t res[MT_NOTES_MAX];
    int n = mt_dues(&g_store, maintenant(), res, MT_NOTES_MAX);
    if (n == 0) { dit("AUCUNE CARTE DUE"); g_ecran = MT_ECRAN_MEMOIRE; return; }
    g_rev_idx = res[0];
    g_rev_n = 0;
    g_rev_total = n;
    g_rev_visible = false;
    g_ecran = MT_ECRAN_REVISION;
}

static void revision_note(int qualite)
{
    if (g_rev_idx < 0 || g_rev_idx >= g_store.n) { revision_demarre(); return; }
    mt_sm2(&g_store.notes[g_rev_idx], qualite, maintenant());
    carnet_sauve();

    int suite = mt_prochaine_due(&g_store, maintenant());
    char m[52];
    if (suite == 0) snprintf(m, sizeof(m), qualite >= 3 ? "BIEN : REVUE DANS 24 H" : "A REVOIR DANS 1 H");
    else            snprintf(m, sizeof(m), "PROCHAINE DANS %d JOUR%s", suite, suite > 1 ? "S" : "");
    dit(m);

    uint16_t res[MT_NOTES_MAX];
    int n = mt_dues(&g_store, maintenant(), res, MT_NOTES_MAX);
    if (n > 0) { g_rev_idx = res[0]; g_rev_n++; g_rev_visible = false; }
    else { g_rev_idx = -1; g_rev_total = 0; g_ecran = MT_ECRAN_BRIEF; dit("REVISION TERMINEE"); }
}

/* ------------------------------------------------------------------ */
/* boutons                                                           */
/* ------------------------------------------------------------------ */
static void action_secondaire(void)     /* B1 court */
{
    switch (g_ecran) {
    case MT_ECRAN_REVISION:
        if (!g_rev_visible) g_rev_visible = true;
        else revision_note(5);
        break;
    case MT_ECRAN_BRIEF:
        if (g_vue.dues > 0) revision_demarre();
        else { g_ecran = MT_ECRAN_MEMOIRE; g_curseur = 0; }
        break;
    case MT_ECRAN_SYSTEME:
        dit("APPUI LONG POUR EFFACER");
        break;
    default:
        dit("RIEN ICI");
        break;
    }
}

static void action_principale(void)     /* B2 double */
{
    switch (g_ecran) {
    case MT_ECRAN_BRIEF:
        if (g_vue.dues > 0) revision_demarre();
        else { g_ecran = MT_ECRAN_MEMOIRE; g_curseur = 0; }
        break;
    case MT_ECRAN_QUESTION:
        if (g_question[0]) repond(g_question);
        else dit("POSEZ UNE QUESTION PAR LE WEB");
        break;
    case MT_ECRAN_REVISION:
        g_rev_visible = true;
        break;
    case MT_ECRAN_CAPTEURS:
        shtc3_lit();
        dit("MESURES REFRAICHIES");
        break;
    case MT_ECRAN_MEMOIRE:
        g_curseur++;
        if (g_curseur >= mt_ecran_items(MT_ECRAN_MEMOIRE, &g_vue)) g_curseur = 0;
        break;
    default:
        break;
    }
}

static void action_tierce(void)         /* B1 double */
{
    if (g_ecran == MT_ECRAN_REVISION) { revision_note(2); return; }
    if (g_ecran == MT_ECRAN_SYSTEME) {
        static int64_t dernier_efface;
        int64_t t = esp_timer_get_time() / 1000;
        if (t - dernier_efface < 4000) {
            memset(&g_store, 0, sizeof(g_store));
            mt_store_init(&g_store, (uint32_t)t);
            carnet_sauve();
            dit("CARNET EFFACE");
            dernier_efface = 0;
        } else {
            dernier_efface = t;
            dit("APPUYEZ ENCORE POUR EFFACER");
        }
        return;
    }
    if (g_ecran == MT_ECRAN_MEMOIRE) {
        if (g_curseur > 0) g_curseur--;
        return;
    }
    dit("RIEN ICI");
}

static void geste(uint8_t btn, uint8_t type, uint16_t duree)
{
    if (btn == 0) {                          /* B1 */
        if (type == GESTE_LONG) {
            g_ecran = (g_ecran + 1) % MT_ECRAN_NB;
            g_curseur = 0;
            dit(mt_ecran_titre(g_ecran));
        } else if (type == GESTE_DOUBLE) {
            action_tierce();
        } else {
            action_secondaire();
        }
    } else {                                 /* B2 */
        if (type == GESTE_LONG || type == GESTE_REPEAT) {
            g_ecran = (g_ecran + MT_ECRAN_NB - 1) % MT_ECRAN_NB;
            g_curseur = 0;
            dit(mt_ecran_titre(g_ecran));
        } else if (type == GESTE_DOUBLE) {
            action_principale();
        } else {
            g_ecran = (g_ecran + 1) % MT_ECRAN_NB;
            g_curseur = 0;
            dit(mt_ecran_titre(g_ecran));
        }
    }
    affiche(true);
}

/* lecture des deux boutons, avec double et appui long */
static void boutons_tache(void *arg)
{
    (void)arg;
    const gpio_num_t pins[2] = { BOOT_BUTTON_PIN, PWR_BUTTON_PIN };
    bool etat[2] = { false, false };
    int64_t presse[2] = { 0, 0 };
    int64_t dernier[2] = { 0, 0 };
    bool double_vu[2] = { false, false };

    for (int i = 0; i < 2; i++) {
        gpio_config_t c = {};
        c.mode = GPIO_MODE_INPUT;
        c.pin_bit_mask = 1ULL << pins[i];
        c.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_config(&c);
    }

    for (;;) {
        for (int i = 0; i < 2; i++) {
            bool bas = gpio_get_level(pins[i]) == 0;
            int64_t t = esp_timer_get_time() / 1000;
            if (bas && !etat[i]) { etat[i] = true; presse[i] = t; }
            else if (!bas && etat[i]) {
                etat[i] = false;
                uint16_t d = (uint16_t)(t - presse[i]);
                if (d >= 700) {
                    geste((uint8_t)i, GESTE_LONG, d);
                } else if (!double_vu[i] && t - dernier[i] < 380) {
                    double_vu[i] = true;
                    geste((uint8_t)i, GESTE_DOUBLE, d);
                } else {
                    double_vu[i] = false;
                    dernier[i] = t;
                    geste((uint8_t)i, GESTE_COURT, d);
                }
            } else if (bas && etat[i] && esp_timer_get_time() / 1000 - presse[i] > 2000 && !double_vu[i]) {
                double_vu[i] = true;          /* appui très long : une seule fois */
                geste((uint8_t)i, GESTE_LONG, 2000);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

/* ------------------------------------------------------------------ */
/* HTTP                                                              */
/* ------------------------------------------------------------------ */
/* décode %20, %C3%A9... et + dans un paramètre d'URL */
static void url_decode(char *s)
{
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '+') { *o++ = ' '; continue; }
        if (*p == '%' && p[1] && p[2]) {
            int v = 0;
            for (int k = 1; k <= 2; k++) {
                char c = p[k];
                v <<= 4;
                if (c >= '0' && c <= '9') v |= c - '0';
                else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            }
            *o++ = (char)v;
            p += 2;
            continue;
        }
        *o++ = *p;
    }
    *o = 0;
}

static int qval(httpd_req_t *req, const char *cle, char *out, size_t cap)
{
    char q[256];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return -1;
    if (httpd_query_key_value(q, cle, out, cap) != ESP_OK) return -1;
    return 0;
}

static esp_err_t h_etat(httpd_req_t *req)
{
    int d = 0;
    mt_resume(&g_store, maintenant(), &d, NULL);
    static char out[640];
    snprintf(out, sizeof(out),
             "{\"ecran\":%d,\"curseur\":%d,\"notes\":%d,\"dues\":%d,\"wifi\":%s,"
             "\"heure\":%s,\"sd\":%s,\"temp\":%.1f,\"hum\":%.0f,\"rendu_ms\":%d,"
             "\"ecran_ms\":%d,\"llm\":%d,\"url\":%d,\"cle\":%d,\"info\":\"%s\",\"version\":\"%s\"}",
             g_ecran, g_curseur, g_store.n, d, g_wifi ? "true" : "false",
             g_heure ? "true" : "false", g_sd_ok ? "true" : "false",
             isnan(g_temp) ? 0.0 : (double)g_temp, isnan(g_hum) ? 0.0 : (double)g_hum,
             g_rendu_ms, g_ecran_ms, g_llm_statut, g_llm_url, g_cle_len, g_info_echappee, MT_VERSION);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_ecran(httpd_req_t *req)
{
    static const char H[] = "0123456789abcdef";
    static char hex[ART_FB_LEN * 2 + 1];
    if (!g_fb_pret) return httpd_resp_send(req, "", 0);
    for (int i = 0; i < ART_FB_LEN; i++) {
        hex[i * 2] = H[(g_fb_dernier[i] >> 4) & 0xF];
        hex[i * 2 + 1] = H[g_fb_dernier[i] & 0xF];
    }
    hex[ART_FB_LEN * 2] = 0;
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, hex, ART_FB_LEN * 2);
}

static esp_err_t h_bouton(httpd_req_t *req)
{
    char v[16];
    int n = 0, m = 0;
    if (qval(req, "n", v, sizeof(v)) == 0) n = atoi(v);
    if (qval(req, "m", v, sizeof(v)) != 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parametre invalide");
    if      (strcmp(v, "court") == 0) m = GESTE_COURT;
    else if (strcmp(v, "double") == 0) m = GESTE_DOUBLE;
    else if (strcmp(v, "long") == 0)  m = GESTE_LONG;
    else return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parametre invalide");
    geste((uint8_t)(n ? 1 : 0), (uint8_t)m, 80);
    return httpd_resp_send(req, "ok", 2);
}

static esp_err_t h_note(httpd_req_t *req)
{
    char t[MT_TXT_L + 1];
    if (qval(req, "t", t, sizeof(t)) != 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parametre invalide");
    url_decode(t);
    int idx = mt_ajoute(&g_store, t, NULL, maintenant());
    if (idx < 0) {
        const char *m = idx == -3 ? "deja" : (idx == -2 ? "plein" : "refusee");
        return httpd_resp_send(req, m, HTTPD_RESP_USE_STRLEN);
    }
    carnet_sauve();
    dit("NOTE AJOUTEE");
    affiche(true);
    httpd_resp_set_type(req, "application/json");
    char out[96];
    snprintf(out, sizeof(out), "{\"ok\":1,\"idx\":%d,\"tag\":\"%s\"}", idx, g_store.notes[idx].tag);
    return httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_ask(httpd_req_t *req)
{
    char q[MT_QUEST_L + 1];
    if (qval(req, "q", q, sizeof(q)) != 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parametre invalide");
    url_decode(q);
    repond(q);
    httpd_resp_set_type(req, "application/json");
    static char out[768];
    snprintf(out, sizeof(out), "{\"question\":\"%.120s\",\"reponse\":\"%.420s\",\"etat\":%d}",
             g_question, g_reponse, (int)g_etat);
    return httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_supprime(httpd_req_t *req)
{
    char v[8];
    int i = -1;
    if (qval(req, "i", v, sizeof(v)) == 0) i = atoi(v);
    if (!mt_supprime(&g_store, (uint16_t)i)) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parametre invalide");
    carnet_sauve();
    affiche(true);
    return httpd_resp_send(req, "ok", 2);
}

static esp_err_t h_efface(httpd_req_t *req)
{
    memset(&g_store, 0, sizeof(g_store));
    mt_store_init(&g_store, (uint32_t)esp_timer_get_time());
    carnet_sauve();
    dit("CARNET EFFACE");
    affiche(true);
    return httpd_resp_send(req, "ok", 2);
}

static esp_err_t h_carnet(httpd_req_t *req)
{
    static char out[24 * 1024];
    size_t n = mt_serialise(&g_store, out, sizeof(out));
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, out, (ssize_t)n);
}

/* POST /fichier?p=/sdcard/NOM : pose un fichier sur la micro SD
   (corps vide = effacer). Reserve au reseau local, chemin impose. */
static esp_err_t h_fichier(httpd_req_t *req)
{
    char p[96];
    if (qval(req, "p", p, sizeof(p)) != 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "p");
    if (strncmp(p, "/sdcard/", 8) != 0 || strstr(p, ".."))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "chemin");
    if (!g_sd_ok) { httpd_resp_set_status(req, "503 Service Unavailable"); return httpd_resp_send(req, "sd", 2); }
    if (req->content_len == 0) { remove(p); return httpd_resp_send(req, "0", 1); }
    if (req->content_len > 8 * 1024 * 1024) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "taille");
    FILE *f = fopen(p, "wb");
    if (!f) { httpd_resp_set_status(req, "500 Internal Server Error"); return httpd_resp_send(req, "io", 2); }
    char buf[1024];
    int recu = 0;
    while (recu < (int)req->content_len) {
        int n = httpd_req_recv(req, buf, sizeof(buf));
        if (n <= 0) { fclose(f); return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv"); }
        if ((int)fwrite(buf, 1, (size_t)n, f) != n) { fclose(f); return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "write"); }
        recu += n;
    }
    fclose(f);
    char m[24];
    snprintf(m, sizeof(m), "%d", recu);
    return httpd_resp_send(req, m, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_liste(httpd_req_t *req)
{
    if (!g_sd_ok) { httpd_resp_set_status(req, "503 Service Unavailable"); return httpd_resp_send(req, "sd", 2); }
    DIR *d = opendir("/sdcard");
    if (!d) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parametre invalide");
    char out[1400];
    int n = snprintf(out, sizeof(out), "[");
    struct dirent *de;
    int premier = 1;
    while ((de = readdir(d)) != NULL && n < (int)sizeof(out) - 80) {
        if (de->d_name[0] == '.') continue;
        char p[300];
        snprintf(p, sizeof(p), "/sdcard/%s", de->d_name);
        struct stat st;
        if (stat(p, &st) != 0 || S_ISDIR(st.st_mode)) continue;
        n += snprintf(out + n, sizeof(out) - (size_t)n, "%s{\"n\":\"%s\",\"o\":%ld}",
                      premier ? "" : ",", de->d_name, (long)st.st_size);
        premier = 0;
    }
    closedir(d);
    snprintf(out + n, sizeof(out) - (size_t)n, "]");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
}

static void http_demarre(void)
{
    httpd_handle_t srv = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 14;   /* 9 routes : la limite par defaut est 8 */
    cfg.lru_purge_enable = true;
    if (httpd_start(&srv, &cfg) != ESP_OK) { ESP_LOGE(TAG, "HTTP indisponible"); return; }
    httpd_uri_t u[] = {
        { "/etat",     HTTP_GET,  h_etat,     NULL },
        { "/ecran",    HTTP_GET,  h_ecran,    NULL },
        { "/bouton",   HTTP_GET,  h_bouton,   NULL },
        { "/note",     HTTP_GET,  h_note,     NULL },
        { "/ask",      HTTP_GET,  h_ask,      NULL },
        { "/supprime", HTTP_GET,  h_supprime, NULL },
        { "/liste",    HTTP_GET,  h_liste,    NULL },
        { "/efface",   HTTP_GET,  h_efface,   NULL },
        { "/carnet",   HTTP_GET,  h_carnet,   NULL },
        { "/fichier",  HTTP_POST, h_fichier,  NULL },
    };
    for (size_t i = 0; i < sizeof(u) / sizeof(u[0]); i++) httpd_register_uri_handler(srv, &u[i]);
    ESP_LOGI(TAG, "API prete : /etat /ecran /bouton /note /ask /supprime /liste");
}

/* ------------------------------------------------------------------ */
/* démarrage                                                         */
/* ------------------------------------------------------------------ */
void mt_app_start(const char *ssid, bool sd_ok, void (*envoie)(const uint8_t *))
{
    (void)ssid; (void)envoie;
    mt_store_init(&g_store, (uint32_t)esp_timer_get_time());
    g_sd_ok = sd_ok;
    carnet_charge();

    i2c_master_Init();
    shtc3_lit();

    wifi_demarre();
    http_demarre();

    g_q_ia = xQueueCreate(1, MT_QUEST_L);
    xTaskCreate(ia_tache, "mt-ia", 20480, NULL, 5, NULL);
    xTaskCreate(boutons_tache, "mt-boutons", 4096, NULL, 6, NULL);
    xTaskCreate([](void *) {
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            shtc3_lit();
            if (!g_heure) {
                struct tm t;
                if (rtc_lit(&t)) { g_heure = true; }
                else if (g_wifi) {
                    time_t now = time(NULL);
                    if (now > 1600000000) {
                        struct tm *lt = localtime(&now);
                        if (lt) { rtc_ecrit(lt); g_heure = true; }
                    }
                }
            }
        }
    }, "mt-capteurs", 3072, NULL, 3, NULL);

    affiche(true);
}

/* API pour main.cpp : la tache d'affichage a besoin du mutex cree ici */
void mt_app_boucle(void)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        affiche(false);
    }
}

esp_err_t mt_epd_start(void)
{
    g_mux = xSemaphoreCreateMutex();
    g_pwr = new board_power_bsp_t(EPD_PWR_PIN, Audio_PWR_PIN, VBAT_PWR_PIN);
    g_pwr->POWEER_EPD_ON();
    g_pwr->POWEER_Audio_ON();
    g_pwr->VBAT_POWER_ON();
    vTaskDelay(pdMS_TO_TICKS(150));

    custom_lcd_spi_t d = {};
    d.cs = EPD_CS_PIN; d.dc = EPD_DC_PIN; d.rst = EPD_RST_PIN;
    d.busy = EPD_BUSY_PIN; d.mosi = EPD_MOSI_PIN; d.scl = EPD_SCK_PIN;
    d.spi_host = EPD_SPI_NUM; d.buffer_len = 5000;
    g_epd = new epaper_driver_display(EPD_WIDTH, EPD_HEIGHT, d);
    g_epd->EPD_Init();
    g_epd->EPD_Clear();
    g_epd->EPD_Display();
    g_epd->EPD_Init_Partial();
    static uint8_t blanc[ART_FB_LEN];
    memset(blanc, 0xFF, sizeof(blanc));
    g_epd->EPD_ShowBase(blanc, ART_FB_LEN);
    return ESP_OK;
}

bool mt_sd_start(void)
{
    return sd_monte();
}

int64_t mt_maintenant(void) { return maintenant(); }
