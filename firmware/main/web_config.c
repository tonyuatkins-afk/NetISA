/**
 * web_config.c - HTTP server for web configuration UI
 *
 * Tiny HTTP server using esp_http_server. Provides a web UI for
 * WiFi configuration, status monitoring, and OTA firmware updates.
 * Accessible at 192.168.4.1 when in AP mode, or at the station IP
 * when connected to a network.
 */

#include "web_config.h"
#include "wifi_mgr.h"
#include "http_client.h"
#include "status.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "web_config";
static httpd_handle_t server = NULL;

/* Embedded HTML for the config page */
static const char config_page_html[] =
"<!DOCTYPE html>"
"<html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>NetISA Configuration</title>"
"<style>"
"body{font-family:sans-serif;max-width:600px;margin:0 auto;padding:20px;"
"background:#1a1a2e;color:#e0e0e0}"
"h1{color:#0f0;font-size:1.5em;border-bottom:2px solid #0f0;padding-bottom:8px}"
"h2{color:#0f0;font-size:1.2em;margin-top:24px}"
".status{background:#16213e;padding:12px;border-radius:6px;margin:12px 0}"
".status span{display:inline-block;min-width:120px;color:#888}"
".status b{color:#0f0}"
"input[type=text],input[type=password]{width:100%;padding:8px;margin:4px 0;"
"background:#0d1117;color:#e0e0e0;border:1px solid #333;border-radius:4px;box-sizing:border-box}"
"button{background:#0f0;color:#000;border:none;padding:10px 24px;margin:4px;"
"border-radius:4px;cursor:pointer;font-weight:bold}"
"button:hover{background:#0c0}"
"button.danger{background:#f00;color:#fff}"
"button.danger:hover{background:#c00}"
".wifi-list{list-style:none;padding:0}"
".wifi-list li{padding:8px;margin:4px 0;background:#16213e;border-radius:4px;"
"cursor:pointer;display:flex;justify-content:space-between}"
".wifi-list li:hover{background:#1a2744}"
".signal{color:#0f0}"
"#log{background:#0d1117;padding:8px;border-radius:4px;font-family:monospace;"
"font-size:0.85em;max-height:200px;overflow-y:auto;white-space:pre-wrap}"
"</style></head><body>"
"<h1>&#x25C6; NetISA Configuration</h1>"
"<div class='status' id='st'>"
"<span>Status:</span> <b id='st-conn'>Loading...</b><br>"
"<span>SSID:</span> <b id='st-ssid'>-</b><br>"
"<span>IP:</span> <b id='st-ip'>-</b><br>"
"<span>Signal:</span> <b id='st-rssi'>-</b><br>"
"<span>Firmware:</span> <b id='st-fw'>-</b><br>"
"<span>Uptime:</span> <b id='st-up'>-</b><br>"
"<span>Sessions:</span> <b id='st-sess'>-</b>"
"</div>"
"<h2>WiFi Networks</h2>"
"<button onclick='scan()'>Scan</button>"
"<ul class='wifi-list' id='wl'></ul>"
"<h2>Connect</h2>"
"<input type='text' id='ssid' placeholder='SSID'>"
"<input type='password' id='pass' placeholder='Password'>"
"<button onclick='conn()'>Connect</button>"
"<button class='danger' onclick='disc()'>Disconnect</button>"
"<h2>Firmware Update</h2>"
"<input type='file' id='fw' accept='.bin'>"
"<button onclick='ota()'>Upload &amp; Update</button>"
"<h2>System</h2>"
"<button class='danger' onclick='if(confirm(\"Restart?\"))fetch(\"/restart\")'>Restart</button>"
"<div id='log'></div>"
"<script>"
"function log(m){let e=document.getElementById('log');e.textContent+=m+'\\n';e.scrollTop=e.scrollHeight}"
"function $(i){return document.getElementById(i)}"
"async function status(){"
"try{let r=await fetch('/status');let d=await r.json();"
"$('st-conn').textContent=d.connected?'Connected':'Disconnected';"
"$('st-ssid').textContent=d.ssid||'-';"
"$('st-ip').textContent=d.ip||'-';"
"$('st-rssi').textContent=d.rssi?d.rssi+'dBm':'-';"
"$('st-fw').textContent=d.fw_version||'-';"
"$('st-up').textContent=d.uptime?Math.floor(d.uptime/60)+'m':'-';"
"$('st-sess').textContent=d.sessions||'0';"
"}catch(e){log('Status error: '+e)}}"
"async function scan(){"
"log('Scanning...');$('wl').innerHTML='';"
"try{let r=await fetch('/scan');let d=await r.json();"
"d.forEach(n=>{"
"let li=document.createElement('li');"
"li.innerHTML=n.ssid+' <span class=\"signal\">'+n.rssi+'dBm ('+n.auth+')</span>';"
"li.onclick=()=>{$('ssid').value=n.ssid;$('pass').focus()};"
"$('wl').appendChild(li)});"
"log('Found '+d.length+' networks');"
"}catch(e){log('Scan error: '+e)}}"
"async function conn(){"
"let s=$('ssid').value,p=$('pass').value;"
"if(!s){log('Enter SSID');return}"
"log('Connecting to '+s+'...');"
"try{let r=await fetch('/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
"body:'ssid='+encodeURIComponent(s)+'&password='+encodeURIComponent(p)});"
"let d=await r.json();log(d.status);setTimeout(status,3000);"
"}catch(e){log('Connect error: '+e)}}"
"async function disc(){"
"try{await fetch('/disconnect',{method:'POST'});log('Disconnected');setTimeout(status,1000);"
"}catch(e){log('Error: '+e)}}"
"async function ota(){"
"let f=$('fw').files[0];if(!f){log('Select firmware file');return}"
"log('Uploading '+f.name+' ('+f.size+' bytes)...');"
"try{let r=await fetch('/update',{method:'POST',body:f});"
"let d=await r.json();log(d.status);"
"}catch(e){log('OTA error: '+e)}}"
"status();setInterval(status,5000);"
"</script></body></html>";

/* GET / - Serve config page */
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, config_page_html, sizeof(config_page_html) - 1);
}

/* GET /status - JSON status */
static esp_err_t status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    char ssid[33] = {0};
    uint32_t ip = 0;
    int8_t rssi = 0;
    wifi_state_t state = wifi_mgr_get_state();
    char ip_str[16];
    char fw_ver[16];

    cJSON_AddBoolToObject(root, "connected", state == WIFI_STATE_CONNECTED);

    if (state == WIFI_STATE_CONNECTED) {
        wifi_mgr_get_info(ssid, &ip, &rssi);
        cJSON_AddStringToObject(root, "ssid", ssid);
        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                 ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
        cJSON_AddStringToObject(root, "ip", ip_str);
        cJSON_AddNumberToObject(root, "rssi", rssi);
    }

    snprintf(fw_ver, sizeof(fw_ver), "%d.%d.%d",
             FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    cJSON_AddStringToObject(root, "fw_version", fw_ver);
    cJSON_AddNumberToObject(root, "uptime", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(root, "sessions", http_get_active_count());

    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free((void *)json);
    cJSON_Delete(root);
    return ESP_OK;
}

/* GET /scan - JSON WiFi scan results */
static esp_err_t scan_handler(httpd_req_t *req)
{
    wifi_ap_record_t ap_records[20];
    uint16_t count = 20;

    esp_err_t ret = wifi_mgr_scan(ap_records, &count);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan failed");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", (char *)ap_records[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", ap_records[i].rssi);

        const char *auth;
        switch (ap_records[i].authmode) {
        case WIFI_AUTH_OPEN:         auth = "Open"; break;
        case WIFI_AUTH_WEP:          auth = "WEP"; break;
        case WIFI_AUTH_WPA_PSK:      auth = "WPA"; break;
        case WIFI_AUTH_WPA2_PSK:     auth = "WPA2"; break;
        case WIFI_AUTH_WPA3_PSK:     auth = "WPA3"; break;
        case WIFI_AUTH_WPA_WPA2_PSK: auth = "WPA/WPA2"; break;
        default:                     auth = "Other"; break;
        }
        cJSON_AddStringToObject(ap, "auth", auth);
        cJSON_AddItemToArray(root, ap);
    }

    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free((void *)json);
    cJSON_Delete(root);
    return ESP_OK;
}

/* POST /connect - Connect to WiFi */
static esp_err_t connect_handler(httpd_req_t *req)
{
    char buf[256];
    char ssid[33] = {0};
    char password[65] = {0};
    int ret;

    ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    /* Parse ssid= and password= from URL-encoded form data */
    if (httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
        return ESP_FAIL;
    }
    httpd_query_key_value(buf, "password", password, sizeof(password));

    cJSON *root = cJSON_CreateObject();
    esp_err_t err = wifi_mgr_connect(ssid, password);
    if (err == ESP_OK) {
        cJSON_AddStringToObject(root, "status", "Connected successfully");
    } else {
        cJSON_AddStringToObject(root, "status", "Connection failed");
    }

    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free((void *)json);
    cJSON_Delete(root);
    return ESP_OK;
}

/* POST /disconnect */
static esp_err_t disconnect_handler(httpd_req_t *req)
{
    wifi_mgr_disconnect();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "Disconnected");
    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free((void *)json);
    cJSON_Delete(root);
    return ESP_OK;
}

/* POST /update - OTA firmware update */
static esp_err_t update_handler(httpd_req_t *req)
{
    esp_ota_handle_t ota_handle;
    const esp_partition_t *update_partition;
    char buf[1024];
    int received;
    int remaining = req->content_len;
    bool first_chunk = true;
    esp_err_t err;

    update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update: %d bytes to partition %s",
             remaining, update_partition->label);

    while (remaining > 0) {
        received = httpd_req_recv(req, buf, sizeof(buf));
        if (received <= 0) {
            ESP_LOGE(TAG, "OTA receive error");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            return ESP_FAIL;
        }

        if (first_chunk) {
            err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES,
                                &ota_handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "OTA begin failed");
                return ESP_FAIL;
            }
            first_chunk = false;
        }

        err = esp_ota_write(ota_handle, buf, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "OTA write failed");
            return ESP_FAIL;
        }

        remaining -= received;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA set boot failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Set boot partition failed");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "Update complete. Restarting...");
    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    cJSON_free((void *)json);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "OTA complete, restarting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

/* GET /restart */
static esp_err_t restart_handler(httpd_req_t *req)
{
    httpd_resp_sendstr(req, "Restarting...");
    ESP_LOGI(TAG, "Restart requested via web config");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

esp_err_t web_config_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size = 8192;

    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register URI handlers */
    const httpd_uri_t uris[] = {
        { .uri = "/",           .method = HTTP_GET,  .handler = root_handler },
        { .uri = "/status",     .method = HTTP_GET,  .handler = status_handler },
        { .uri = "/scan",       .method = HTTP_GET,  .handler = scan_handler },
        { .uri = "/connect",    .method = HTTP_POST, .handler = connect_handler },
        { .uri = "/disconnect", .method = HTTP_POST, .handler = disconnect_handler },
        { .uri = "/update",     .method = HTTP_POST, .handler = update_handler },
        { .uri = "/restart",    .method = HTTP_GET,  .handler = restart_handler },
    };

    for (int i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }

    ESP_LOGI(TAG, "Web config server started on port 80");
    return ESP_OK;
}

esp_err_t web_config_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web config server stopped");
    }
    return ESP_OK;
}
