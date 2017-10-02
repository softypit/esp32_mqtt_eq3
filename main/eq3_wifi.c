/* 
 * EQ-3 WiFi support code.
 * 
 * Uses bootwifi to enable first-time configuration
 * Could instead use Espressiv smartconfig to connect wifi - see bottom of this file 
 * Connects to mqtt broker (no security currently) to send/receive status/commands
 * Ensure CONFIG_MQTT_SECURITY_ON is not defined in MQTT config file 
 * 
 * (C) Paul Tupper 2017
 */
/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "mqtt.h"

#ifdef removed
#include "esp_smartconfig.h"
#endif

#include "eq3_wifi.h"
#include "eq3_gap.h"

#ifdef removed
/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const char *TAG = "sc-wifi";
void smartconfig_task(void * parm);
#endif

static const char *MQTT_TAG = "mqtt";

/* =========================================
 * MQTT support code
 */

static mqtt_client *repclient = NULL;
static char *devlist = NULL;

/* MQTT connected callback */
void connected_cb(mqtt_client *client, mqtt_event_data_t *event_data){
    repclient = client;
    /* Subscribe to /espradin/# for commands */
    mqtt_subscribe(client, "/espradin/#", 0);
    /* Publish welcome message to /espradout */
    mqtt_publish(client, "/espradout", "Heating control active", 22, 0, 0);
    if(devlist != NULL){
        /* Publish discovered EQ-3 device list to /espradout/devlist */
        mqtt_publish(client, "/espradout/devlist", devlist, strlen(devlist), 0, 0);
	free(devlist);
	devlist = NULL;
    }
}
/* MQTT disconnected */
void disconnected_cb(mqtt_client *client, mqtt_event_data_t *event_data){
    repclient = NULL;
}
/* Subscribed */
void subscribe_cb(mqtt_client *client, mqtt_event_data_t *event_data){
    ESP_LOGI(MQTT_TAG, "[APP] Subscribe ok, test publish msg");
}

void publish_cb(mqtt_client *client, mqtt_event_data_t *event_data){

}

/* MQTT data received (subscribed topic receives data) */
void data_cb(mqtt_client *client, mqtt_event_data_t *event_data){
    bool trvcmd = false, trvscan = false;
    if(event_data->data_offset == 0) {
        char *topic = malloc(event_data->topic_length + 1);
        memcpy(topic, event_data->topic, event_data->topic_length);
        topic[event_data->topic_length] = 0;
	if(strstr(topic, "/trv") != NULL)
	    trvcmd = true;
	if(strstr(topic, "/scan") != NULL)
	    trvscan = true;
        ESP_LOGI(MQTT_TAG, "[APP] Publish topic: %s", topic);
        free(topic);
    }

    if(trvcmd == true){
        char *data = malloc(event_data->data_length + 1);
        memcpy(data, event_data->data, event_data->data_length);
        data[event_data->data_length] = 0;
        handle_request(data);
	free(data);
	ESP_LOGI(MQTT_TAG, "Queue trv msg");
    }
    
    if(trvscan == true){
        start_scan();
    }
    
}

mqtt_settings settings = {
#if defined(CONFIG_MQTT_SECURITY_ON)
    .port = 8883, // encrypted
#else
    .port = 1883, // unencrypted
#endif
    .client_id = "esp32mqtt_client_id",
    .clean_session = 0,
    .keepalive = 60,
    .lwt_topic = "/espradout",
    .lwt_msg = "Heating control offline",
    .lwt_qos = 0,
    .lwt_retain = 0,
    .connected_cb = connected_cb,
    .disconnected_cb = disconnected_cb,
    .subscribe_cb = subscribe_cb,
    .publish_cb = publish_cb,
    .data_cb = data_cb
};

/* Publish a status message */
int send_trv_status(char *status){
    if(repclient != NULL){
        mqtt_publish(repclient, "/espradout/status", status, strlen(status), 0, 0);
    }
    return 0;
}

/* Publish a discovered device list */
int send_device_list(char *list){
    if(repclient != NULL){
        mqtt_publish(repclient, "/espradout/devlist", list, strlen(list), 0, 0);
	free(list);
    }else{
        if(devlist != NULL)
	    free(devlist);
	ESP_LOGI(MQTT_TAG, "Queue device list message to publish");
        devlist = list;
    }
    return 0;
}

int connect_server(char *url, char *user, char *password){
    if(url != NULL)
        strcpy(settings.host, url);
    if(user != NULL)
        strcpy(settings.username, user);
    if(password != NULL)
        strcpy(settings.password, password);
    mqtt_start(&settings);
    return 0;
}

#ifdef removed
/* ==================================
 * WiFi support code 
 */

static esp_err_t event_handler(void *ctx, system_event_t *event){
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 3, NULL);
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
//	mqtt_config();
	mqtt_start(&settings);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
	mqtt_stop();
        break;
    default:
        break;
    }
    return ESP_OK;
}

void initialise_wifi(void){
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

/* Smartconfig handling */
static void sc_callback(smartconfig_status_t status, void *pdata){
    switch (status) {
        case SC_STATUS_WAIT:
            ESP_LOGI(TAG, "SC_STATUS_WAIT");
            break;
        case SC_STATUS_FIND_CHANNEL:
            ESP_LOGI(TAG, "SC_STATUS_FINDING_CHANNEL");
            break;
        case SC_STATUS_GETTING_SSID_PSWD:
            ESP_LOGI(TAG, "SC_STATUS_GETTING_SSID_PSWD");
            break;
        case SC_STATUS_LINK:
            ESP_LOGI(TAG, "SC_STATUS_LINK");
            wifi_config_t *wifi_config = pdata;
            ESP_LOGI(TAG, "SSID:%s", wifi_config->sta.ssid);
            ESP_LOGI(TAG, "PASSWORD:%s", wifi_config->sta.password);
            ESP_ERROR_CHECK( esp_wifi_disconnect() );
            ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, wifi_config) );
            ESP_ERROR_CHECK( esp_wifi_connect() );
            break;
        case SC_STATUS_LINK_OVER:
            ESP_LOGI(TAG, "SC_STATUS_LINK_OVER");
            if (pdata != NULL) {
                uint8_t phone_ip[4] = { 0 };
                memcpy(phone_ip, (uint8_t* )pdata, 4);
                ESP_LOGI(TAG, "Phone ip: %d.%d.%d.%d\n", phone_ip[0], phone_ip[1], phone_ip[2], phone_ip[3]);
            }
            xEventGroupSetBits(wifi_event_group, ESPTOUCH_DONE_BIT);
            break;
        default:
            break;
    }
}

void smartconfig_task(void * parm){
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    ESP_ERROR_CHECK( esp_smartconfig_start(sc_callback) );
    while (1) {
        uxBits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY); 
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}
#endif

