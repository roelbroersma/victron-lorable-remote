#include "portal.h"
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_app_desc.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "esp_rom_md5.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/ip4_addr.h"
#include "protocol.h"
#include "uart_link.h"
#include "portal_asset.h"

#if CONFIG_PARTITION_TABLE_MD5
#error "Stock ESP-AT 3.3 partition tables have no MD5 entry: disable CONFIG_PARTITION_TABLE_MD5"
#endif

#define HTTP_BODY_MAX 5800
static httpd_handle_t server;
static esp_netif_t *access_point_netif;
static portal_hooks_t portal_hooks;
static char csrf_token[33];
static bool wifi_initialized, wifi_started;
static atomic_bool ota_in_progress;
static SemaphoreHandle_t response_signal;
static portMUX_TYPE response_lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t http_request_id = 0x8000, awaiting_id;
static char response[6144];
static size_t response_used;
static unsigned response_status;
static bool response_bad;

void portal_receive_frame(const protocol_frame_t *frame)
{
    bool done = false;
    portENTER_CRITICAL(&response_lock);
    if (awaiting_id && frame->id == awaiting_id) {
        char *end;
        unsigned long first = strtoul(frame->payload, &end, 10);
        if (end == frame->payload || *end != ':') response_bad = true;
        else if (!strcmp(frame->type, "HTTP_DATA")) {
            const size_t n = strlen(end + 1);
            if (first != response_used || n + response_used >= sizeof(response)) response_bad = true;
            else {memcpy(response + response_used, end + 1, n + 1); response_used += n;}
        } else if (!strcmp(frame->type, "HTTP_END")) {
            char *last;
            const unsigned long total = strtoul(end + 1, &last, 10);
            if (*last || last == end+1 || total != response_used || (first != 200 && first != 400))
                response_bad = true;
            response_status = first;
            done = true;
        }
    }
    portEXIT_CRITICAL(&response_lock);
    if (done && response_signal) xSemaphoreGive(response_signal);
}
static esp_err_t json_error(httpd_req_t *r, const char *status, const char *message)
{
    httpd_resp_set_status(r, status);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    char body[160];
    snprintf(body, sizeof(body), "{\"ok\":false,\"code\":\"%s\"}", message);
    return httpd_resp_sendstr(r, body);
}
static bool protected_request(httpd_req_t *r)
{
    char token[40];
    return httpd_req_get_hdr_value_str(r, "X-LoRaBLE", token, sizeof(token)) == ESP_OK &&
           !strcmp(token, csrf_token);
}
static esp_err_t root_get(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(r, "Content-Security-Policy", "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'");
    return httpd_resp_send(r, (const char *)PORTAL_GZIP, sizeof(PORTAL_GZIP));
}
static esp_err_t session_get(httpd_req_t *r)
{
    portal_status_t status = {0};
    portal_hooks.read_status(&status);
    wifi_sta_list_t clients = {0};
    const bool clients_known = esp_wifi_ap_get_sta_list(&clients) == ESP_OK;
    // AP allows two clients. Report their measured RSSI, not a theoretical PHY rate.
    char rssi[24] = "", count[8] = "null";
    if (clients_known) {
        snprintf(count, sizeof(count), "%d", clients.num);
        size_t used = 0;
        for (int i = 0; i < clients.num && i < 2; ++i) {
            const int value = clients.sta[i].rssi;
            if (value < 0 && value >= -127)
                used += snprintf(rssi + used, sizeof(rssi) - used, "%s%d", used ? "," : "", value);
        }
    }
    char body[512];
    snprintf(body, sizeof(body),
      "{\"token\":\"%s\",\"native\":true,\"esp_firmware\":\"%s\",\"ble_advertisements\":%lu,\"ble_scan_status\":%d,\"wifi_seconds\":%lu,\"ota_target\":\"esp8684\",\"wifi_clients\":%s,\"wifi_rssi_dbm\":[%s],\"ble_target_state\":%u,\"ble_target_mac\":\"%s\",\"ble_target_age_s\":%lu}",
      csrf_token, esp_app_get_description()->version,
      (unsigned long)status.last_ble_advertisements, status.last_ble_status,
      (unsigned long)status.seconds_left, count, rssi,
      status.target_state, status.target_mac, (unsigned long)status.target_age_seconds);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_sendstr(r, body);
}
static esp_err_t proxy_request(httpd_req_t *r)
{
    // ESP HTTP server serializes handlers; only one UART request can be active.
    const bool post = r->method == HTTP_POST;
    if (post && !protected_request(r)) return json_error(r,"403 Forbidden","reload_page");
    if (r->content_len > HTTP_BODY_MAX)
        return json_error(r,"413 Payload Too Large","request_too_large");
    // HTTP handlers are serialized. Keep a large form off the task stack.
    static char body[HTTP_BODY_MAX + 1];
    size_t used = 0;
    while (used < r->content_len) {
        int n = httpd_req_recv(r, body + used, r->content_len - used);
        if (n <= 0) {protocol_secure_zero(body,sizeof(body));return ESP_FAIL;}
        used += n;
    }
    body[used] = 0;
    if (memchr(body, 0, used)) {protocol_secure_zero(body,sizeof(body));return json_error(r,"400 Bad Request","invalid_body");}
    while (xSemaphoreTake(response_signal,0) == pdTRUE) {}
    if (++http_request_id == 0) http_request_id = 0x8000;
    portENTER_CRITICAL(&response_lock);
    awaiting_id = http_request_id;
    response_used = response_status = 0;
    response_bad = false;
    response[0] = 0;
    portEXIT_CRITICAL(&response_lock);
    char chunk[240];
    snprintf(chunk,sizeof(chunk),"%s %s",post?"POST":"GET",r->uri);
    esp_err_t result = uart_link_send("HTTP_BEGIN",http_request_id,chunk);
    for (size_t offset=0; result == ESP_OK && offset<used; offset+=200) {
        snprintf(chunk,sizeof(chunk),"%u:%.*s",(unsigned)offset,
            (int)((used-offset)>200?200:used-offset),body+offset);
        result = uart_link_send("HTTP_DATA",http_request_id,chunk);
    }
    snprintf(chunk,sizeof(chunk),"%u",(unsigned)used);
    if (result==ESP_OK) result=uart_link_send("HTTP_END",http_request_id,chunk);
    protocol_secure_zero(body,sizeof(body));
    protocol_secure_zero(chunk,sizeof(chunk));
    const bool received = result==ESP_OK && xSemaphoreTake(response_signal,pdMS_TO_TICKS(6000))==pdTRUE;
    portENTER_CRITICAL(&response_lock);
    awaiting_id=0;
    const bool valid = received && !response_bad;
    portEXIT_CRITICAL(&response_lock);
    if (!valid) return json_error(r,"504 Gateway Timeout","stm32_no_response");
    httpd_resp_set_type(r,"application/json");
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    if(response_status != 200) httpd_resp_set_status(r,"400 Bad Request");
    return httpd_resp_send(r,response,response_used);
}
static void restart_after_reply(void *ignored)
{
    (void)ignored;vTaskDelay(pdMS_TO_TICKS(1200));esp_restart();
}
static uint32_t u32le(const uint8_t *p) {return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static esp_err_t ota_post(httpd_req_t *r)
{
    if(!protected_request(r)) return json_error(r,"403 Forbidden","reload_page");
    const esp_partition_t *storage=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x22,"storage");
    const esp_partition_t *app=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_0,NULL);
    if(!storage || storage->address!=0x2a000 || storage->size!=0xa6000 ||
       !app || app->address!=0xd0000 || app->size!=0x130000)
        return json_error(r,"409 Conflict","wrong_partition_layout");
    if(r->content_len<120 || r->content_len>storage->size)
        return json_error(r,"400 Bad Request","wrong_firmware_size");
    // Radio manager refuses BLE/stop during upload; header is committed LAST.
    atomic_store(&ota_in_progress,true);
    uart_link_send("OTA_STATE",0,"1");
    uint8_t header[88],buffer[1024],digest[16];
    size_t used=0;
    const char *failure="upload_interrupted";
    while(used<sizeof(header)) {
        int n=httpd_req_recv(r,(char*)header+used,sizeof(header)-used);
        if(n<=0) goto failed;
        used+=n;
    }
    failure="incompatible_firmware";
    if(memcmp(header,"ESP\0",4) || header[4]!=2 || header[5]!=1 ||
       header[6] || header[7] || memcmp(header+8,"LoRaBLE-C2-26M-",14) ||
       u32le(header+40)!=(uint32_t)r->content_len-sizeof(header) ||
       u32le(header+76) || u32le(header+80) ||
       esp_rom_crc32_le(0,header,84)!=u32le(header+84)) goto failed;
    failure="flash_write_failed";
    if(esp_partition_erase_range(storage,0,(r->content_len+4095)&~4095)!=ESP_OK) goto failed;
    md5_context_t md5;
    esp_rom_md5_init(&md5);
    while(used<r->content_len) {
        const size_t want=r->content_len-used>sizeof(buffer)?sizeof(buffer):r->content_len-used;
        int n=httpd_req_recv(r,(char*)buffer,want);
        if(n<=0) {failure="upload_interrupted";goto failed;}
        // Invalid header remains erased until the ENTIRE stream has passed MD5.
        if(esp_partition_write(storage,used,buffer,n)!=ESP_OK) goto failed;
        esp_rom_md5_update(&md5,buffer,n);
        used+=n;
    }
    esp_rom_md5_final(digest,&md5);
    if(memcmp(digest,header+44,16)) {failure="checksum_failed";goto failed;}
    // Read back flash, not just the network buffer.
    esp_rom_md5_init(&md5);
    for(size_t offset=88;offset<used;) {
        const size_t n=used-offset>sizeof(buffer)?sizeof(buffer):used-offset;
        if(esp_partition_read(storage,offset,buffer,n)!=ESP_OK) goto failed;
        esp_rom_md5_update(&md5,buffer,n);offset+=n;
    }
    esp_rom_md5_final(digest,&md5);
    if(memcmp(digest,header+44,16)) {failure="flash_verify_failed";goto failed;}
    // A valid compressed header is the stock bootloader's update trigger.
    if(esp_partition_write(storage,0,header,sizeof(header))!=ESP_OK) goto failed;
    httpd_resp_set_type(r,"application/json");
    httpd_resp_sendstr(r,"{\"ok\":true,\"code\":\"rebooting\"}");
    if(xTaskCreate(restart_after_reply,"ota_restart",2048,NULL,5,NULL)!=pdPASS) esp_restart();
    return ESP_OK;
failed:
    atomic_store(&ota_in_progress,false);
    uart_link_send("OTA_STATE",0,"0");
    return json_error(r,"400 Bad Request",failure);
}
static esp_err_t start_http_server(void)
{
    httpd_config_t configuration = HTTPD_DEFAULT_CONFIG();
    configuration.stack_size = 8192;
    configuration.max_open_sockets = 2;
    configuration.max_uri_handlers = 9;
    configuration.lru_purge_enable = true;
    configuration.recv_wait_timeout = 5;
    configuration.send_wait_timeout = 5;

    esp_err_t result = httpd_start(&server, &configuration);
    if (result != ESP_OK) {
        return result;
    }

    if (!response_signal) response_signal = xSemaphoreCreateBinary();
    if (!response_signal) return ESP_ERR_NO_MEM;
    const httpd_uri_t routes[] = {
      {.uri="/",.method=HTTP_GET,.handler=root_get},
      {.uri="/session",.method=HTTP_GET,.handler=session_get},
      {.uri="/config",.method=HTTP_GET,.handler=proxy_request},
      {.uri="/status",.method=HTTP_GET,.handler=proxy_request},
      {.uri="/log",.method=HTTP_GET,.handler=proxy_request},
      {.uri="/save",.method=HTTP_POST,.handler=proxy_request},
      {.uri="/action",.method=HTTP_POST,.handler=proxy_request},
      {.uri="/ota",.method=HTTP_POST,.handler=ota_post}
    };
    for (size_t i=0;i<sizeof(routes)/sizeof(routes[0]);++i)
      if((result=httpd_register_uri_handler(server,&routes[i]))!=ESP_OK) return result;
    return ESP_OK;
}

static esp_err_t fail_start(esp_err_t start_status, bool *restart_required)
{
    if (portal_stop() != ESP_OK) {
        *restart_required = true;
    }
    return start_status;
}

esp_err_t portal_start(const char *ssid,
                       const char *password,
                       const portal_hooks_t *hooks,
                       bool *restart_required)
{
    if (restart_required == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *restart_required = false;
    if (server != NULL || wifi_initialized || wifi_started || access_point_netif != NULL) {
        *restart_required = true;
        return ESP_ERR_INVALID_STATE;
    }
    if (ssid == NULL || password == NULL || hooks == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t ssid_length = strlen(ssid);
    const size_t password_length = strlen(password);
    if (ssid_length == 0 || ssid_length > 32 || password_length < 12 || password_length > 63) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t index = 0; index < ssid_length; ++index) {
        const unsigned char byte = (unsigned char)ssid[index];
        if (byte < 0x20 || byte > 0x7e) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    portal_hooks = *hooks;
    uint8_t nonce[16];
    esp_fill_random(nonce, sizeof(nonce));
    for (size_t index = 0; index < sizeof(nonce); ++index) {
        snprintf(csrf_token + index * 2, 3, "%02X", nonce[index]);
    }
    protocol_secure_zero(nonce, sizeof(nonce));

    access_point_netif = esp_netif_create_default_wifi_ap();
    if (access_point_netif == NULL) {
        memset(&portal_hooks, 0, sizeof(portal_hooks));
        protocol_secure_zero(csrf_token, sizeof(csrf_token));
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result;
    esp_netif_ip_info_t ip_info = {0};
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    result = esp_netif_dhcps_stop(access_point_netif);
    if (result == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        result = ESP_OK;
    }
    if (result == ESP_OK) {
        result = esp_netif_set_ip_info(access_point_netif, &ip_info);
    }
    if (result == ESP_OK) {
        result = esp_netif_dhcps_start(access_point_netif);
    }
    if (result != ESP_OK) {
        return fail_start(result, restart_required);
    }

    wifi_init_config_t wifi_initialization = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&wifi_initialization);
    if (result != ESP_OK) {
        return fail_start(result, restart_required);
    }
    wifi_initialized = true;
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) {
        /* Default storage is FLASH: never call set_config after this failure. */
        return fail_start(result, restart_required);
    }

    wifi_config_t wifi_configuration = {0};
    memcpy(wifi_configuration.ap.ssid, ssid, ssid_length);
    wifi_configuration.ap.ssid_len = ssid_length;
    memcpy(wifi_configuration.ap.password, password, password_length);
    wifi_configuration.ap.channel = 1;
    wifi_configuration.ap.max_connection = 2;
    wifi_configuration.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_configuration.ap.pmf_cfg.capable = true;
    /* Optional PMF keeps modern protection without excluding older phones. */
    wifi_configuration.ap.pmf_cfg.required = false;

    result = esp_wifi_set_mode(WIFI_MODE_AP);
    if (result == ESP_OK) {
        result = esp_wifi_set_config(WIFI_IF_AP, &wifi_configuration);
    }
    if (result == ESP_OK) {
        result = esp_wifi_start();
        if (result == ESP_OK) {
            wifi_started = true;
        }
    }
    if (result == ESP_OK) {
        result = start_http_server();
    }
    if (result != ESP_OK) {
        protocol_secure_zero(&wifi_configuration, sizeof(wifi_configuration));
        return fail_start(result, restart_required);
    }
    protocol_secure_zero(&wifi_configuration, sizeof(wifi_configuration));
    return ESP_OK;
}

esp_err_t portal_stop(void)
{
    if (server != NULL) {
        const esp_err_t result = httpd_stop(server);
        if (result != ESP_OK) {
            return result;
        }
        server = NULL;
    }
    if (wifi_started) {
        const esp_err_t result = esp_wifi_stop();
        if (result != ESP_OK) {
            return result;
        }
        wifi_started = false;
    }
    if (wifi_initialized) {
        const esp_err_t result = esp_wifi_deinit();
        if (result != ESP_OK) {
            return result;
        }
        wifi_initialized = false;
    }
    if (access_point_netif != NULL) {
        esp_netif_destroy_default_wifi(access_point_netif);
        access_point_netif = NULL;
    }
    memset(&portal_hooks, 0, sizeof(portal_hooks));
    protocol_secure_zero(csrf_token, sizeof(csrf_token));
    return ESP_OK;
}

bool portal_running(void)
{
    return server != NULL && wifi_initialized && wifi_started;
}

bool portal_ota_in_progress(void)
{
    return atomic_load_explicit(&ota_in_progress, memory_order_acquire);
}
