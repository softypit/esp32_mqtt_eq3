/****************************************************************************
*
* EQ-3 Thermostatic Radiator Valve control
*
****************************************************************************/

/* by Paul Tupper (C) 2017
 * derived from gatt_client example from Espressif Systems
 *
 * updated by Peter Becker (C) 2019
 */

// Copyright 2015-2017 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/uart.h"

#include "esp_log.h"

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"

#include "esp_sleep.h"
#include "lwip/err.h"
#include "lwip/apps/sntp.h"

#include "eq3_main.h"
#include "eq3_gap.h"
#include "eq3_timer.h"
#include "eq3_wifi.h"

#include "eq3_bootwifi.h"

#define GATTC_TAG "EQ3_MAIN"
#define INVALID_HANDLE   0

#define EQ3_DISCONNECT 0
#define START_WIFI     1
#define RESTART_WIFI   2
#define EQ3_REBOOT     3

/* Request ids for TRV */
#define PROP_ID_QUERY            0x00
#define PROP_ID_RETURN           0x01
#define PROP_INFO_RETURN         0x02
#define PROP_INFO_QUERY          0x03
#define PROP_COMFORT_ECO_CONFIG  0x11
#define PROP_OFFSET              0x13
#define PROP_WINDOW_OPEN_CONFIG  0x14
#define PROP_SCHEDULE_QUERY      0x20
#define PROP_SCHEDULE_RETURN     0x21
#define PROP_MODE_WRITE          0x40
#define PROP_TEMPERATURE_WRITE   0x41
#define PROP_COMFORT             0x43
#define PROP_ECO                 0x44
#define PROP_BOOST               0x45
#define PROP_LOCK                0x80

/* Status bits */
#define AUTO                     0x00
#define MANUAL                   0x01
#define AWAY                     0x02
#define BOOST                    0x04
#define DST                      0x08
#define WINDOW                   0x10
#define LOCKED                   0x20
#define UNKNOWN                  0x40
#define LOW_BATTERY              0x80

/* Allow delay of next GATTC command */
struct tmrcmd{
    bool running;
    int cmd;
    int countdown;
};

static bool wifistartdelay = true;
static bool reboot_requested = false;

static struct tmrcmd nextcmd;

static int setnextcmd(int cmd, int time_s){
    if(nextcmd.running != true){
        nextcmd.cmd = cmd;
        nextcmd.countdown = time_s;
        nextcmd.running = true;
    }else{
        ESP_LOGI(GATTC_TAG, "setnextcmd when timer running!");
    }
    return 0;
}

static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);


#define EQ3_CMD_DONE    0
#define EQ3_CMD_RETRY   1
#define EQ3_CMD_FAILED  2
/* Command complete success/fail acknowledgement */
static int command_complete(bool success);

/* EQ-3 service identifier */
static esp_bt_uuid_t eq3_service_id = {
    .len = ESP_UUID_LEN_128,
    .uuid = {.uuid128 = {0x46, 0x70, 0xb7, 0x5b, 0xff, 0xa6, 0x4a, 0x13, 0x90, 0x90, 0x4f, 0x65, 0x42, 0x51, 0x13, 0x3e},}
};

/* EQ-3 characteristic identifier for setting parameters */
static esp_bt_uuid_t eq3_char_id = {
    .len = ESP_UUID_LEN_128,
    .uuid = {.uuid128 = {0x09, 0xea, 0x79, 0x81, 0xdf, 0xb8, 0x4b, 0xdb, 0xad, 0x3b, 0x4a, 0xce, 0x5a, 0x58, 0xa4, 0x3f},}
};

static esp_bt_uuid_t eq3_filter_char_uuid = {
    .len = ESP_UUID_LEN_128,
    .uuid = {.uuid128 = {0x09, 0xea, 0x79, 0x81, 0xdf, 0xb8, 0x4b, 0xdb, 0xad, 0x3b, 0x4a, 0xce, 0x5a, 0x58, 0xa4, 0x3f},},
};

/* EQ-3 characteristic used to notify settings from trv in response to parameter set */
static esp_bt_uuid_t eq3_resp_char_id = {
    .len = ESP_UUID_LEN_128,
    .uuid = {.uuid128 = {0x2a, 0xeb, 0xe0, 0xf4, 0x90, 0x6c, 0x41, 0xaf, 0x96, 0x09, 0x29, 0xcd, 0x4d, 0x43, 0xe8, 0xd0},}
};

static esp_bt_uuid_t eq3_resp_filter_char_uuid = {
    .len = ESP_UUID_LEN_128,
    .uuid = {.uuid128 = {0x2a, 0xeb, 0xe0, 0xf4, 0x90, 0x6c, 0x41, 0xaf, 0x96, 0x09, 0x29, 0xcd, 0x4d, 0x43, 0xe8, 0xd0},},
};

/* Current TRV command being sent to EQ-3 */ 
uint16_t cmd_len = 0;
uint8_t cmd_val[20] = {0};
esp_bd_addr_t cmd_bleda;   /* BLE Device Address */

static bool get_server = false;
static bool connection_open = false;
static bool ble_operation_in_progress = false;
static bool outstanding_timer = false;

static void runtimer(void){
    if(outstanding_timer == false){
        outstanding_timer = true; 
        start_timer(1000);
    }
}

static esp_gattc_char_elem_t elemres;
static esp_gattc_char_elem_t *char_elem_result = &elemres;

static bool registered = false;

#define PROFILE_NUM 1
#define PROFILE_A_APP_ID 0

/* Profile instance - used to store details of the  current connection profile */
struct gattc_profile_inst {
    esp_gattc_cb_t gattc_cb;
    uint16_t gattc_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t char_handle;
    uint16_t resp_char_handle;
    esp_bd_addr_t remote_bda;
};

/* One gatt-based profile one app_id and one gattc_if, this array will store the gattc_if returned by ESP_GATTS_REG_EVT */
static struct gattc_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gattc_cb = gattc_profile_event_handler,
        .gattc_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

static void gattc_command_error(esp_bd_addr_t bleda, char *error){
    /* Only send the response if there are no retries available */
    if(command_complete(false) == EQ3_CMD_FAILED){
        char statrep[120];
        int statidx = 0;
        statidx += sprintf(&statrep[statidx], "{");
        statidx += sprintf(&statrep[statidx], "\"trv\":\"%02X:%02X:%02X:%02X:%02X:%02X\",", bleda[0], bleda[1], bleda[2], bleda[3], bleda[4], bleda[5]);
        statidx += sprintf(&statrep[statidx], "\"error\":\"%s\"}", error);
        send_trv_status(statrep);
        eq3_add_log(statrep);
    }
    /* 2 second delay until disconnect to allow any background GATTC stuff to complete */
    setnextcmd(EQ3_DISCONNECT, 2);
    runtimer();
}

/* Callback function to handle GATT-Client events */ 
/* While we've discovered the EQ-3 devices with the GAP handler we need to check the service we want is available when we connect
 * so we connect to the device and search its services before we try to set our chosen characteristic */

/* For consideration:
 * do we need to search for the service before attempting to set the characteristic. This will likely have an impact on the EQ-3 battery life
 * although we only comunicate when we need to change something which will likely result in the motor turning which will have a much bigger impact
 * on the batteries. If we use polling (e.g. repeated unboost to poll the current status) this could be something to think about */

static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    uint16_t conn_id = 0;
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;

    switch (event) {
    /* GATT Client registration */
    case ESP_GATTC_REG_EVT:
        /* Registered */
        ESP_LOGI(GATTC_TAG, "REG_EVT");
        registered = true;
        gl_profile_tab[PROFILE_A_APP_ID].char_handle = 0;
        gl_profile_tab[PROFILE_A_APP_ID].resp_char_handle = 0;
        break;
    case ESP_GATTC_UNREG_EVT:
        /* Unregistered */
        ESP_LOGI(GATTC_TAG, "UNREG_EVT");
        registered = false;
        esp_err_t ret = esp_ble_gattc_app_register(PROFILE_A_APP_ID);
	    if(ret){
            ESP_LOGE(GATTC_TAG, "%s gattc app register failed, error code = %x\n", __func__, ret);
        }
        break;
        
    /* GATT Client connection to server */
    case ESP_GATTC_CONNECT_EVT:
        /* GATT Client connected to server(EQ-3) */
        //p_data->connect.status always be ESP_GATT_OK
        conn_id = p_data->connect.conn_id;
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_CONNECT_EVT conn_id %d, if %d", conn_id, gattc_if);
        gl_profile_tab[PROFILE_A_APP_ID].conn_id = conn_id;
        memcpy(gl_profile_tab[PROFILE_A_APP_ID].remote_bda, p_data->connect.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(GATTC_TAG, "REMOTE BDA:");
        esp_log_buffer_hex(GATTC_TAG, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, sizeof(esp_bd_addr_t));
        esp_err_t mtu_ret = esp_ble_gattc_send_mtu_req(gattc_if, conn_id);
        if (mtu_ret){
            ESP_LOGE(GATTC_TAG, "config MTU error, error code = %x", mtu_ret);
        }
        break;
    case ESP_GATTC_OPEN_EVT:
        /* Profile connection opened */
        if (param->open.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "open failed, status %d", p_data->open.status); 
            gattc_command_error(cmd_bleda, "TRV not available");
            break;
        }else{
            ESP_LOGI(GATTC_TAG, "open success");
            connection_open = true;
        }
        break;
    case ESP_GATTC_CLOSE_EVT:
        /* Profile connection closed */
        if(param->close.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "close failed, status %d", p_data->close.status);
        }else{
            ESP_LOGI(GATTC_TAG, "close success");
            connection_open = false;
        }
        /* Wait before we connect to the next EQ-3 to send a queued command */
        runtimer();
        break;
    case ESP_GATTC_CFG_MTU_EVT:
        /* MTU has been set */
        if (param->cfg_mtu.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG,"config mtu failed, error status = %x", param->cfg_mtu.status);
            gattc_command_error(gl_profile_tab[PROFILE_A_APP_ID].remote_bda, "TRV error");
            break;
        }
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_CFG_MTU_EVT, Status %d, MTU %d, conn_id %d", param->cfg_mtu.status, param->cfg_mtu.mtu, param->cfg_mtu.conn_id);
        /* Search for the EQ-3 service */
        esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, NULL);

        break;
    case ESP_GATTC_SEARCH_RES_EVT: {
        /* Search result is in */
        esp_gatt_srvc_id_t *srvc_id =(esp_gatt_srvc_id_t *)&p_data->search_res.srvc_id;
        conn_id = p_data->search_res.conn_id;

        if (srvc_id->id.uuid.len == ESP_UUID_LEN_128){
          int checkcount;
          for(checkcount=0; checkcount < ESP_UUID_LEN_128; checkcount++){
            if(srvc_id->id.uuid.uuid.uuid128[checkcount] != eq3_service_id.uuid.uuid128[checkcount]){
              checkcount = -1;
              break;
            }
          }
          if(checkcount == ESP_UUID_LEN_128) {
            get_server = true;
            ESP_LOGI(GATTC_TAG, "Found EQ-3");
            gl_profile_tab[PROFILE_A_APP_ID].service_start_handle = p_data->search_res.start_handle;
            gl_profile_tab[PROFILE_A_APP_ID].service_end_handle = p_data->search_res.end_handle;
          }
        }
        break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT:
        /* Search is complete */
        if (p_data->search_cmpl.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "search service failed, error status = %x", p_data->search_cmpl.status);
            gattc_command_error(gl_profile_tab[PROFILE_A_APP_ID].remote_bda, "TRV error");
            break;
        }
        ESP_LOGI(GATTC_TAG, "Search Complete - get req characteristics");
        if (get_server){
            uint16_t count = 0;
            esp_gatt_status_t status = esp_ble_gattc_get_attr_count( gattc_if, p_data->search_cmpl.conn_id, ESP_GATT_DB_CHARACTERISTIC, gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                                     gl_profile_tab[PROFILE_A_APP_ID].service_end_handle, INVALID_HANDLE, &count);
            if (status != ESP_GATT_OK){
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_attr_count error");
            }      
            if (count > 0){
                uint16_t count2 = 1;
                ESP_LOGI(GATTC_TAG, "%d attributes reported", count);
                    
                /* Get the response characteristic handle */
                status = esp_ble_gattc_get_char_by_uuid( gattc_if, p_data->search_cmpl.conn_id, gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                         gl_profile_tab[PROFILE_A_APP_ID].service_end_handle, eq3_resp_filter_char_uuid, char_elem_result, &count2);
                if (status != ESP_GATT_OK){
                    ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error");
                }
                /*  We should only get a single result from our filter */
                if (count2 > 0){
                    uint16_t charwalk;
                    ESP_LOGI(GATTC_TAG, "Found %d filtered attributes", count2);
                    for(charwalk = 0; charwalk < count; charwalk++){
                        if (char_elem_result[charwalk].uuid.len == ESP_UUID_LEN_128){
                            int checkcount;
                                
                                /* Check if the service identifier is the one we're interested in */
                                char printstr[ESP_UUID_LEN_128 * 2 + 1];
                                int bytecount, writecount = 0;
                                for(bytecount=ESP_UUID_LEN_128 - 1; bytecount >= 0; bytecount--, writecount += 2)
                                    sprintf(&printstr[writecount], "%02x", char_elem_result[charwalk].uuid.uuid.uuid128[bytecount] & 0xff);
                                ESP_LOGI(GATTC_TAG, "Found uuid %d UUID128: %s", charwalk, printstr);
                                
                            /* Is this the response characteristic */
                            for(checkcount=0; checkcount < ESP_UUID_LEN_128; checkcount++){
                                if(char_elem_result[charwalk].uuid.uuid.uuid128[checkcount] != eq3_resp_char_id.uuid.uuid128[checkcount]){
                                    checkcount = -1;
                                    break;
                                }
                            }
                            if(checkcount == ESP_UUID_LEN_128 && char_elem_result[charwalk].properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY) {
                                ESP_LOGI(GATTC_TAG, "eq-3 got resp id handle");
                                gl_profile_tab[PROFILE_A_APP_ID].resp_char_handle = char_elem_result[charwalk].char_handle;
                                continue;
                            }
                        }
                    }
                    /* If we got the response characteristic register for notifications */
                    if(gl_profile_tab[PROFILE_A_APP_ID].resp_char_handle != 0){
                        esp_ble_gattc_register_for_notify (gattc_if, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, gl_profile_tab[PROFILE_A_APP_ID].resp_char_handle);
                    }
                }else{
                    ESP_LOGE(GATTC_TAG, "No notification attribute found!");
                }

                count2 = 1;
                /* Get the command characteristic handle */
                status = esp_ble_gattc_get_char_by_uuid( gattc_if, p_data->search_cmpl.conn_id, gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                         gl_profile_tab[PROFILE_A_APP_ID].service_end_handle, eq3_filter_char_uuid, char_elem_result, &count2);
                if (status != ESP_GATT_OK){
                    ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error");
                }
                /*  We should only get a single result from our filter */
                if (count2 > 0){
                    uint16_t charwalk;
                    ESP_LOGI(GATTC_TAG, "Found %d filtered attributes", count2);
                    for(charwalk = 0; charwalk < count; charwalk++){                            
                        if (char_elem_result[charwalk].uuid.len == ESP_UUID_LEN_128){
                            int checkcount;
                            /* Check if the service identifier is the one we're interested in */
                            char printstr[ESP_UUID_LEN_128 * 2 + 1];
                            int bytecount, writecount = 0;
                            for(bytecount=ESP_UUID_LEN_128 - 1; bytecount >= 0; bytecount--, writecount += 2)
                                sprintf(&printstr[writecount], "%02x", char_elem_result[charwalk].uuid.uuid.uuid128[bytecount] & 0xff);
                            ESP_LOGI(GATTC_TAG, "Found uuid %d UUID128: %s", charwalk, printstr);
                            
                            /* Is this the command characteristic */
                            for(checkcount=0; checkcount < ESP_UUID_LEN_128; checkcount++){
                                if(char_elem_result[charwalk].uuid.uuid.uuid128[checkcount] != eq3_char_id.uuid.uuid128[checkcount]){
                                    checkcount = -1;
                                    break;
                                }
                            }
                            if(checkcount == ESP_UUID_LEN_128) {
                                ESP_LOGI(GATTC_TAG, "eq-3 got cmd id handle");
                                gl_profile_tab[PROFILE_A_APP_ID].char_handle = char_elem_result[charwalk].char_handle;
                                continue;
                            }
                        }
                    }
                }else{
                    ESP_LOGE(GATTC_TAG, "No command attribute found!");
                }
                    
            }else{
                ESP_LOGE(GATTC_TAG, "EQ-3 characteristics not found");
                gattc_command_error(gl_profile_tab[PROFILE_A_APP_ID].remote_bda, "Not an EQ-3");
                break;
            }
        }else{
            ESP_LOGE(GATTC_TAG, "EQ-3 service not available from this server!");
            /* Wait 2 seconds for background GATTC operations then disconnect */
            gattc_command_error(gl_profile_tab[PROFILE_A_APP_ID].remote_bda, "Not an EQ-3");
            break;
        }
        break;
	
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        if (p_data->reg_for_notify.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "REG FOR NOTIFY failed: error status = %d", p_data->reg_for_notify.status);
            /* Disconnect */
            gattc_command_error(gl_profile_tab[PROFILE_A_APP_ID].remote_bda, "EQ-3 notify error");
        }else{
            /* Now we're ready to send our command to the EQ-3 trv */
            ESP_LOGI(GATTC_TAG, "Send eq3 command");
            esp_ble_gattc_write_char( gattc_if, gl_profile_tab[PROFILE_A_APP_ID].conn_id, gl_profile_tab[PROFILE_A_APP_ID].char_handle,
                                  cmd_len, cmd_val, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
        }
        break;
    }
    case ESP_GATTC_NOTIFY_EVT:
        /* EQ-3 has sent a notification with its current status */
        /* Decode this and create a json message to send back to the controlling broker to keep state-machine up-to-date and acknowledge settings */
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_NOTIFY_EVT, Receive notify value:");
        esp_log_buffer_hex(GATTC_TAG, p_data->notify.value, p_data->notify.value_len);

        uint8_t tempval, temphalf = 0;
        char statrep[240];
        int statidx = 0;

        statidx += sprintf(&statrep[statidx], "{");
        statidx += sprintf(&statrep[statidx], "\"trv\":\"%02X:%02X:%02X:%02X:%02X:%02X\",", gl_profile_tab[PROFILE_A_APP_ID].remote_bda[0], gl_profile_tab[PROFILE_A_APP_ID].remote_bda[1],
            gl_profile_tab[PROFILE_A_APP_ID].remote_bda[2], gl_profile_tab[PROFILE_A_APP_ID].remote_bda[3], gl_profile_tab[PROFILE_A_APP_ID].remote_bda[4], gl_profile_tab[PROFILE_A_APP_ID].remote_bda[5]);

        if(p_data->notify.value[0] == PROP_INFO_RETURN && p_data->notify.value[1] == 1){
            if(p_data->notify.value_len > 5){
                tempval = p_data->notify.value[5];
                if(tempval & 0x01)
                    temphalf = 5;
                tempval >>= 1;
                ESP_LOGI(GATTC_TAG, "eq3 settemp is %d.%d C", tempval, temphalf);
                statidx += sprintf(&statrep[statidx], "\"temp\":\"%d.%d\"", tempval, temphalf);
            }
            if(p_data->notify.value_len >= 14){
                int8_t offsetval, offsethalf = 0;
                offsetval = p_data->notify.value[14];
                offsetval -= 7; // The offset temperature is encoded in steps of 0.5°C between -3.5°C and 3.5°C

                if(offsetval & 0x01)
                    offsethalf = 5;
                offsetval >>= 1;
                ESP_LOGI(GATTC_TAG, "eq3 offsettemp is %d.%d C", offsetval, offsethalf);
                statidx += sprintf(&statrep[statidx], ",\"offsetTemp\":\"%d.%d\"", offsetval, offsethalf);
            }
            if(p_data->notify.value_len > 3){
                tempval = p_data->notify.value[3];
                ESP_LOGI(GATTC_TAG, "eq3 valve %d%% open\n", tempval);
                statidx += sprintf(&statrep[statidx], ",\"valve\":\"%d%% open\"", tempval);
            }
            if(p_data->notify.value_len > 2){
                tempval = p_data->notify.value[2];
                statidx += sprintf(&statrep[statidx], ",\"mode\":");
                if(tempval & MANUAL){
                    ESP_LOGI(GATTC_TAG, "eq3 set manual");
                    statidx += sprintf(&statrep[statidx], "\"manual\"");
                }else if(tempval & AWAY){
                    ESP_LOGI(GATTC_TAG, "eq3 set holiday");
                    statidx += sprintf(&statrep[statidx], "\"holiday\"");
                }else{
                    ESP_LOGI(GATTC_TAG, "eq3 set auto");
                    statidx += sprintf(&statrep[statidx], "\"auto\"");
                }
                statidx += sprintf(&statrep[statidx], ",\"boost\":");
                if(tempval & BOOST){
                    ESP_LOGI(GATTC_TAG, "eq3 boost");
                    statidx += sprintf(&statrep[statidx], "\"active\"");
                }else{
                    ESP_LOGI(GATTC_TAG, "eq3 no boost");
                    statidx += sprintf(&statrep[statidx], "\"inactive\"");
                }
                statidx += sprintf(&statrep[statidx], ",\"window\":");
                if(tempval & WINDOW){
                    ESP_LOGI(GATTC_TAG, "eq3 window open");
                    statidx += sprintf(&statrep[statidx], "\"open\"");
                }else{
                    ESP_LOGI(GATTC_TAG, "eq3 window closed");
                    statidx += sprintf(&statrep[statidx], "\"closed\"");
                }
                statidx += sprintf(&statrep[statidx], ",\"state\":");
                if(tempval & LOCKED){
                    ESP_LOGI(GATTC_TAG, "eq3 locked");
                    statidx += sprintf(&statrep[statidx], "\"locked\"");
                }else{
                    ESP_LOGI(GATTC_TAG, "eq3 unlocked");
                    statidx += sprintf(&statrep[statidx], "\"unlocked\"");
                }
                statidx += sprintf(&statrep[statidx], ",\"battery\":");
                if(tempval & LOW_BATTERY){
                    ESP_LOGI(GATTC_TAG, "eq3 battery LOW");
                    statidx += sprintf(&statrep[statidx], "\"LOW\"");
                }else{
                    ESP_LOGI(GATTC_TAG, "eq3 battery good");
                    statidx += sprintf(&statrep[statidx], "\"GOOD\"");
                }
            }
            statidx += sprintf(&statrep[statidx], "}");
            /* Send the status report we just collated */
            send_trv_status(statrep);
            /* Add to the log */
            eq3_add_log(statrep);
        }else{
            ESP_LOGI(GATTC_TAG, "eq3 got response 0x%x, 0x%x\n", p_data->notify.value[0], p_data->notify.value[1]);
        }

        /* Notify the successful command */
        command_complete(true);
        /* 2 second delay until disconnect to allow any background GATTC stuff to complete */
        setnextcmd(EQ3_DISCONNECT, 2);
        runtimer();

    break;
    case ESP_GATTC_UNREG_FOR_NOTIFY_EVT: {
        /* Oops - this won't happen
         * forgot to call unregister-for-notify before disconnecting - may need to revisit if it causes problems */
        if (p_data->unreg_for_notify.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "UNREG FOR NOTIFY failed: error status = %d", p_data->unreg_for_notify.status);
            break;
        }

        esp_ble_gattc_close (gl_profile_tab[PROFILE_A_APP_ID].gattc_if, gl_profile_tab[PROFILE_A_APP_ID].conn_id);

        break;
    }  
    case ESP_GATTC_WRITE_CHAR_EVT:
        /* Characteristic write complete */
        if (p_data->write.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "write char failed, error status = %x", p_data->write.status);
            /* Disconnect */
            gattc_command_error(gl_profile_tab[PROFILE_A_APP_ID].remote_bda, "Unable to write to EQ-3");
            break;
        }
        ESP_LOGI(GATTC_TAG, "write char success ");
        break;
    case ESP_GATTC_DISCONNECT_EVT:
        /* Disconnected */
        get_server = false;
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_DISCONNECT_EVT, status = %d", p_data->disconnect.reason);
        //esp_ble_gattc_app_unregister(gl_profile_tab[PROFILE_A_APP_ID].gattc_if);
        ble_operation_in_progress = false;

        if(p_data->disconnect.reason != ESP_GATT_CONN_TERMINATE_LOCAL_HOST)
            gattc_command_error(cmd_bleda, "Device unavailable");

        break;
    default:
        ESP_LOGI(GATTC_TAG, "Unhandled_EVT %d", event);
        break;
    }
}

static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param){
    /* If event is register event, store the gattc_if for each profile */
    if (event == ESP_GATTC_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gattc_if = gattc_if;
        } else {
            ESP_LOGI(GATTC_TAG, "reg app failed, app_id %04x, status %d", param->reg.app_id, param->reg.status);
            return;
        }
    }
    /* If the gattc_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            if (gattc_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                    gattc_if == gl_profile_tab[idx].gattc_if) {
                if (gl_profile_tab[idx].gattc_cb) {
                    gl_profile_tab[idx].gattc_cb(event, gattc_if, param);
                }
            }
        }
    } while (0);
}


#define BUF_SIZE (1024)

#define MAX_CMD_BYTES 6
#define SET_TIME_BYTES 6
#define MAX_CMD_RETRIES 3

/* Message queue of EQ-3 messages */
QueueHandle_t msgQueue = NULL;
QueueHandle_t timer_queue = NULL;

typedef enum {
    EQ3_BOOST = 0,
    EQ3_UNBOOST,
    EQ3_AUTO,
    EQ3_MANUAL,
    EQ3_ECO,
    EQ3_SETTEMP,
    EQ3_OFFSET,
    EQ3_SETTIME,
    EQ3_LOCK,
    EQ3_UNLOCK,
}eq3_bt_cmd;

struct eq3cmd{
    esp_bd_addr_t bleda;
    eq3_bt_cmd cmd;
    unsigned char cmdparms[MAX_CMD_BYTES];
    int retries;
    struct eq3cmd *next;
};

static void enqueue_command(struct eq3cmd *newcmd);

struct eq3cmd *cmdqueue = NULL;

/* Task to handle local UART and accept EQ-3 commands for test/debug */
static void uart_task()
{
    const int uart_num = UART_NUM_0;
    uint8_t cmd_buf[1024] = {0};
    uint16_t cmdidx = 0;

    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    //Configure UART1 parameters
    uart_param_config(uart_num, &uart_config);
    //Set UART1 pins(TX: IO4, RX: I05, RTS: IO18, CTS: IO19)
    //uart_set_pin(uart_num, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS);
    //Install UART driver (we don't need an event queue here)
    //In this example we don't even use a buffer for sending data.
    uart_driver_install(uart_num, BUF_SIZE * 2, 0, 0, NULL, 0);
    
    uint8_t* data = (uint8_t*) malloc(BUF_SIZE);
    while(1) {
        //Read data from UART
        int len = uart_read_bytes(uart_num, data, BUF_SIZE - 1, 20 / portTICK_RATE_MS);
        if(cmdidx + len < 1024){
            int cpylen = 0;
            while(cpylen < len){
                if(data[cpylen] == '\n' || data[cpylen] == '\r'){
                    if(cmdidx > 0 && msgQueue != NULL){
                        uint8_t *newMsg = (uint8_t *)malloc(cmdidx + 1);
                        if(newMsg != NULL){
                            memcpy(newMsg, cmd_buf, cmdidx);
                            newMsg[cmdidx] = 0;
                            ESP_LOGI(GATTC_TAG, "Send");
                            xQueueSend( msgQueue, (void *)&newMsg, ( TickType_t ) 0 );
    
                            /* TODO - check if the above failed */

                        }
                    }
                    cmdidx = 0;
                    cpylen++;
                }else{
                    cmd_buf[cmdidx++] = data[cpylen++];  
                }
            }
        }
        //Write data back to UART
        uart_write_bytes(uart_num, (const char*) data, len);
    }
} 

/* Schedule a reboot after commands have completed or very shortly */ 
void schedule_reboot(void){
    reboot_requested = true;
    if(ble_operation_in_progress == false)
        runtimer();
}

/* Handle an EQ-3 command from uart or mqtt */
int handle_request(char *cmdstr){
    char *cmdptr = cmdstr;
    struct eq3cmd *newcmd;
    eq3_bt_cmd command; 
    unsigned char cmdparms[MAX_CMD_BYTES];  
    bool start = false;

    // Skip the bleaddr
    while(*cmdptr != 0 && !isxdigit((int)*cmdptr))
        cmdptr++;
    while(*cmdptr != 0 && (isxdigit((int)*cmdptr) || *cmdptr == ':'))
        cmdptr++;
    // Skip any spaces
    while(*cmdptr == ' ')
        cmdptr++;    
    if(start == false && strncmp((const char *)cmdptr, "settime", 7) == 0){
        start = true;

        if(cmdptr[8] != 0 && strlen(cmdptr + 8) < 12){
            //free(newcmd);
            /* TODO more validation of time argument */
            ESP_LOGI(GATTC_TAG, "Invalid time argument %s", cmdptr + 8);
            return -1;
        }
        if(isalnum((int)cmdptr[8])){
            char hexdigit[3];
            int dig;
            hexdigit[2] = 0;
            for(dig=0; dig < SET_TIME_BYTES; dig++){
                hexdigit[0] = *(cmdptr + 8 + (dig * 2));
                hexdigit[1] = *(cmdptr + 9 + (dig * 2));
                cmdparms[dig] = (unsigned char)strtol(hexdigit, NULL, 16);
            }
        }else{
            /* TODO - only if time is synchronised */
            if(ntp_enabled() == true){
                time_t now = 0;
                struct tm timeinfo = { 0 };
                time(&now);
                localtime_r(&now, &timeinfo);
                cmdparms[0] = timeinfo.tm_year - 100;
                cmdparms[1] = timeinfo.tm_mon + 1;
                cmdparms[2] = timeinfo.tm_mday;
                cmdparms[3] = timeinfo.tm_hour;
                cmdparms[4] = timeinfo.tm_min;
                cmdparms[5] = timeinfo.tm_sec;
            }else{
                ESP_LOGI(GATTC_TAG, "Cannot set valve time via ntp as ntp is not enabled");
                return -1;
            }
        }
        command = EQ3_SETTIME;
    }
    if(start == false && strncmp((const char *)cmdptr, "boost", 5) == 0){
        start = true;
        command = EQ3_BOOST;
    }
    if(start == false && strncmp((const char *)cmdptr, "unboost", 7) == 0){
        start = true;
        command = EQ3_UNBOOST;
    }
    if(start == false && strncmp((const char *)cmdptr, "auto", 4) == 0){
        start = true;
        command = EQ3_AUTO;
    }
    if(start == false && strncmp((const char *)cmdptr, "manual", 6) == 0){
        start = true;
        command = EQ3_MANUAL;
    }
    if(start == false && strncmp((const char *)cmdptr, "lock", 4) == 0){
        start = true;
        command = EQ3_LOCK;
    }
    if(start == false && strncmp((const char *)cmdptr, "unlock", 6) == 0){
        start = true;
        command = EQ3_UNLOCK;
    }
    if(start == false && strncmp((const char *)cmdptr, "offset", 6) == 0){
        char *endmsg;
        float offset = strtof(cmdptr + 7, &endmsg);
        if(offset < -3.5 || offset > 3.5){
            // Error
            //free(newcmd);
            return -1;
        }
        offset += 3.5;
        offset *= 2;
        cmdparms[0] = (unsigned char)offset;
        start = true;
        command = EQ3_OFFSET;
        ESP_LOGI(GATTC_TAG, "set offset val 0x%x\n", cmdparms[0]);
    }
    if(start == false && strncmp((const char *)cmdptr, "settemp", 7) == 0){
        char *endmsg;
        float temp = strtof(cmdptr + 8, &endmsg);
        int inttemp = (int)temp;
        if(inttemp >= 5 && inttemp < 30){
            start = true;
            command = EQ3_SETTEMP;
            cmdparms[0] = (unsigned char)(inttemp << 1);
            if(temp - (float)inttemp >= 0.5)
                cmdparms[0] |= 0x01;
        }else{
            ESP_LOGI(GATTC_TAG, "Invalid temperature %0.1f requested", temp);
            //free(newcmd);
            return -1;
        }
    }
    if(start == false && strncmp((const char *)cmdptr, "off", 3) == 0){
        /* 'Off' is achieved by setting the required temperature to 4.5 */
        start = true;
        command = EQ3_SETTEMP;
        cmdparms[0] = 0x09; /* (4 << 1) | 0x01 */
    }
    if(start == false && strncmp((const char *)cmdptr, "on", 2) == 0){
        /* 'On' is achieved by setting the required temperature to 30 */
        start = true;
        command = EQ3_SETTEMP;
        cmdparms[0] = 0x3c; /* (30 << 1) */
    }
    
    if(start == true){
        int parm;

        eq3_add_log(cmdstr);
        
        newcmd = malloc(sizeof(struct eq3cmd));

        // TODO - what if malloc fails?

        newcmd->cmd = command;
        for(parm=0; parm < MAX_CMD_BYTES; parm++)
            newcmd->cmdparms[parm] = cmdparms[parm];
        newcmd->retries = MAX_CMD_RETRIES;

        while(*cmdstr != 0 && !isxdigit((int)*cmdstr))
            cmdstr++;
    
        int adidx = ESP_BD_ADDR_LEN;
        while(adidx > 0){
            newcmd->bleda[ESP_BD_ADDR_LEN - adidx] = strtol(cmdstr, &cmdstr, 16);
            while(*cmdstr != 0 && !isxdigit((int)*cmdstr))
                cmdstr++;
            adidx--;
        }
    
        ESP_LOGI(GATTC_TAG, "Requested address:");
        esp_log_buffer_hex(GATTC_TAG, newcmd->bleda, sizeof(esp_bd_addr_t));

        newcmd->next = NULL;
    
        enqueue_command(newcmd);

        if(ble_operation_in_progress == false){
            /* Only schedule the command if ble is currently idle */
            //if(timer_running() == false){
            //    ESP_LOGI(GATTC_TAG, "Timer not running so starting it");
                runtimer();
            //}
        }
    }else{
        ESP_LOGI(GATTC_TAG, "Invalid command %s", cmdptr);
        return -1;
    }
    return 0;
}

static void enqueue_command(struct eq3cmd *newcmd)
{
    struct eq3cmd *qwalk = cmdqueue;
    struct eq3cmd *lastCommandForDevice = NULL;

    if(cmdqueue == NULL)
    {
        cmdqueue = newcmd;
        ESP_LOGI(GATTC_TAG, "Add queue head");
    }
    else
    {
        if(memcmp(qwalk->bleda, newcmd->bleda, sizeof(esp_bd_addr_t)) == 0)
            lastCommandForDevice = qwalk;

        while(qwalk->next != NULL)
        {
            qwalk = qwalk->next;
            if(memcmp(qwalk->bleda, newcmd->bleda, sizeof(esp_bd_addr_t)) == 0)
                lastCommandForDevice = qwalk;
        }

        //don't add the same command again if it already is the last command for a specific device
        if(lastCommandForDevice != NULL
                && lastCommandForDevice->cmd == newcmd->cmd
                && memcmp(lastCommandForDevice->cmdparms, newcmd->cmdparms, MAX_CMD_BYTES) == 0)
        {
            ESP_LOGI(GATTC_TAG, "Command still pending");
            return;
        }

        qwalk->next = newcmd;
        ESP_LOGI(GATTC_TAG, "Add queue end");
     }
}

/* Get the next command off the queue and encode the characteristic parameters */
static int setup_command(void){
    if(cmdqueue != NULL){
        int parm;
        switch(cmdqueue->cmd){
        case EQ3_SETTIME:
            cmd_val[0] = PROP_INFO_QUERY;
            for(parm=0; parm < SET_TIME_BYTES; parm++)
                cmd_val[1 + parm] = cmdqueue->cmdparms[parm];
            cmd_len = 1 + SET_TIME_BYTES;
            break;
        case EQ3_BOOST:
            cmd_val[0] = PROP_BOOST;
            cmd_val[1] = 0x01;
            cmd_len = 2;
            break;
        case EQ3_UNBOOST:
            cmd_val[0] = PROP_BOOST;
            cmd_val[1] = 0x00;
            cmd_len = 2;
            break;
        case EQ3_AUTO:
            cmd_val[0] = PROP_MODE_WRITE;
            cmd_val[1] = 0x00;
            cmd_len = 2;
            break;
        case EQ3_MANUAL:
            cmd_val[0] = PROP_MODE_WRITE;
            cmd_val[1] = 0x40;
            cmd_len = 2;
            break;
        case EQ3_SETTEMP:
            cmd_val[0] = PROP_TEMPERATURE_WRITE;
            cmd_val[1] = cmdqueue->cmdparms[0];
            cmd_len = 2;
            break;
        case EQ3_OFFSET:
            cmd_val[0] = PROP_OFFSET;
            cmd_val[1] = cmdqueue->cmdparms[0];
            cmd_len = 2;
            break;
        case EQ3_LOCK:
            cmd_val[0] = PROP_LOCK;
            cmd_val[1] = 1;
            cmd_len = 2;
            break;
        case EQ3_UNLOCK:
            cmd_val[0] = PROP_LOCK;
            cmd_val[1] = 0;
            cmd_len = 2;
            break;
        default:
            ESP_LOGI(GATTC_TAG, "Can't handle that command yet");
            break;
        }
        memcpy(cmd_bleda, cmdqueue->bleda, sizeof(esp_bd_addr_t));
        //struct eq3cmd *delcmd = cmdqueue;
        //cmdqueue = cmdqueue->next;
        //free(delcmd);
    }
    return 0;
}

#define REQUEUE_RETRY

static int command_complete(bool success){
    bool deletehead = false;
    int rc = EQ3_CMD_RETRY;

    if(success == true){
        deletehead = true;
        rc = EQ3_CMD_DONE;
    }
    else if(cmdqueue == NULL) {
        rc = EQ3_CMD_DONE;
    }
    else{
        /* Command failed - retry if there are any retries left */
        
        /* Normal operation - retry the same command until all attempts are exhausted 
         * OR
         * define REQUEUE_RETRY to push the command to the end of the list to retry once all other currently queued commands are complete. */        
        if(--cmdqueue->retries <= 0){
            deletehead = true;
            ESP_LOGE(GATTC_TAG, "Command failed - retries exhausted");
            rc = EQ3_CMD_FAILED;
        }else{
#ifdef REQUEUE_RETRY
            ESP_LOGE(GATTC_TAG, "Command failed - requeue for retry");
            /* If there are no other queued commands just retry this one */
            if(cmdqueue->next != NULL){
                struct eq3cmd *mvcmd = cmdqueue;
                while(mvcmd->next != NULL)
                    mvcmd = mvcmd->next;
                /* Attach head to tail */
                mvcmd->next = cmdqueue;
                cmdqueue = cmdqueue->next;
                /* Detach head from new tail */
                mvcmd->next->next = NULL;
            }
#else
            ESP_LOGE(GATTC_TAG, "Command failed - retry");
#endif  
        }
    }
    if(deletehead && cmdqueue != NULL){
        /* Delete this command from the queue */
        struct eq3cmd *delcmd = cmdqueue;
        cmdqueue = cmdqueue->next;
        free(delcmd);
    }
    return rc;
}

/* Run the next EQ-3 command from the list */
static int run_command(void){
    if(cmdqueue != NULL){
        ESP_LOGI(GATTC_TAG, "Sending next command");
        setup_command();
        ESP_LOGI(GATTC_TAG, "Open virtual server connection for BLE device:");
        esp_log_buffer_hex(GATTC_TAG, cmd_bleda, sizeof(esp_bd_addr_t));
        ble_operation_in_progress = true;
        esp_ble_gattc_open(gl_profile_tab[PROFILE_A_APP_ID].gattc_if, cmd_bleda, 0x00, true);
        /*
        #define BLE_ADDR_PUBLIC         0x00
        #define BLE_ADDR_RANDOM         0x01
        #define BLE_ADDR_PUBLIC_ID      0x02
        #define BLE_ADDR_RANDOM_ID      0x03
         */
        //TODO: BLE_ADDR_PUBLIC Verify https://github.com/espressif/esp-idf/blob/a0468b2bd64c48d093309a4b3d623a7343c205c0/components/bt/bluedroid/stack/include/stack/bt_types.h
    }
    return 0;
}

/* Callback from config - copy url, username and password for mqtt broker */
static char *usr = NULL, *pass = NULL, *url = NULL, *id = NULL;
void confparms(char *mqtturl, char *mqttuser, char *mqttpass, char *mqttid){
    if(usr != NULL){
        free(usr);
        usr = NULL;
    }
    if(pass != NULL){
        free(pass);
        pass = NULL;
    }
    if(url != NULL){
        free(url);
        url = NULL;
    }
    if(mqtturl != NULL && mqtturl[0] != 0){
        if(strstr(mqtturl, "//") != NULL){
            url = strdup(mqtturl);
        }else{
            url = malloc(strlen(mqtturl) + 8);
            sprintf(url, "mqtt://%s", mqtturl);
        }
        ESP_LOGI(GATTC_TAG, "MQTT config url is %s", url);
    }
    if(mqttuser != NULL && mqttuser[0] != 0){
        usr = strdup(mqttuser);
        ESP_LOGI(GATTC_TAG, "MQTT config user is %s", mqttuser);
    }
    if(mqttpass != NULL && mqttpass[0] != 0){
        pass = strdup(mqttpass);
        ESP_LOGI(GATTC_TAG, "MQTT config pass is %s", mqttpass);
    }
    if(mqttid != NULL && mqttid[0] != 0){
        id = strdup(mqttid);
        ESP_LOGI(GATTC_TAG, "MQTT id is %s\n", mqttid);
    }
}

/* Callback when we're associated with an AP or have fallen back into STA mode */
void wifidone(int rc){
    static bool server_started = false;
    if(rc == 0){
        /* We are an AP */
        ESP_LOGI(GATTC_TAG, "WiFi connection failed - entering AP mode for 5 minutes\n"); 
        /* Max 5 minutes as AP then we retry station mode */
        setnextcmd(RESTART_WIFI, 300);
        runtimer();
    }else{
        /* We are station and connected */
        ESP_LOGI(GATTC_TAG, "WiFi network connected\n");
        
        /* If ntp is configured start up sntp and synchronise the time */
        char *ntpserver = getntpserver();
        char *ntptimezone = getntptimezone();
        if(ntp_enabled() == true && ntpserver[0] != 0){
            ESP_LOGI(GATTC_TAG, "Initializing SNTP");
            sntp_setoperatingmode(SNTP_OPMODE_POLL);
        
            sntp_setservername(0, ntpserver);
            sntp_init();
        
            // wait for time to be set
            time_t now = 0;
            struct tm timeinfo = { 0 };
            int retry = 0;
            const int retry_count = 30;
            while(timeinfo.tm_year < (2019 - 1900) && ++retry < retry_count) {
                ESP_LOGI(GATTC_TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
                vTaskDelay(2000 / portTICK_PERIOD_MS);
                time(&now);
                localtime_r(&now, &timeinfo);
            }
            char strftime_buf[64];
        
//#define TZVAL "GMT0BST,M3.5.0/2,M11.1.0"
//        setenv("TZ", TZVAL, 1);
        
            setenv("TZ", (const char *)ntptimezone, 1);
            tzset();
            localtime_r(&now, &timeinfo);
            strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
            ESP_LOGI(GATTC_TAG, "The current date/time is: %s", strftime_buf);
            eq3_add_log((char *)"WiFi connected");
        }else{
            ESP_LOGI(GATTC_TAG, "SNTP not enabled\n");
        }
        
        if(server_started == false){
            if(connect_server(url, usr, pass, id) == 0)
                server_started = true;
        }
    }
    return;
}

void app_main(){
    // Initialize NVS.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
    
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s initialize controller failed, error code = %x\n", __func__, ret);
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s enable controller failed, error code = %x\n", __func__, ret);
        return;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s init bluetooth failed, error code = %x\n", __func__, ret);
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s enable bluetooth failed, error code = %x\n", __func__, ret);
        return;
    }

    //register the callback function to the gattc module
    ret = esp_ble_gattc_register_callback(esp_gattc_cb);
    if(ret){
        ESP_LOGE(GATTC_TAG, "%s gattc register failed, error code = %x\n", __func__, ret);
        return;
    }

    eq3_log_init();
    eq3_add_log((char *)"Boot");

    /* Start uart task and create msg and timer queues */ 
    xTaskCreate(uart_task, "uart_task", 4096, NULL, 10, NULL);    
    msgQueue = xQueueCreate( 10, sizeof( uint8_t * ) );
    timer_queue = xQueueCreate(10, sizeof(timer_event_t));

    if( msgQueue == NULL ){
        /* Queue was not created and must not be used. */
        ESP_LOGE(GATTC_TAG, "Queue create failed");
    }
    if( timer_queue == NULL ){
        /* Queue was not created and must not be used. */
        ESP_LOGE(GATTC_TAG, "Timer queue create failed");
    }
    
    /* Initialise timer0 */
    init_timer(timer_queue);
    
    /* Register for gatt client usage */
    ret = esp_ble_gattc_app_register(PROFILE_A_APP_ID);
    if (ret){
        ESP_LOGE(GATTC_TAG, "%s gattc app register failed, error code = %x\n", __func__, ret);
    }
    
    if(wifistartdelay == true){
        setnextcmd(START_WIFI, 5);
        runtimer();
    }else{
        ESP_LOGI(GATTC_TAG, "Init wifi");
        //initialise_wifi();
        bootWiFi(wifidone, confparms);
    }
    
    /* Kick off a GAP scan */
    start_scan();
    
    /* Main polling loop */
    uint8_t *msg = NULL;
    timer_event_t evt;
    while(1){
        /* Receive messages from uart */
        if( xQueueReceive( msgQueue, (void *)&msg, (portTICK_PERIOD_MS * 25))){
            char *msgptr = (char *)msg;
            handle_request(msgptr);
            free(msg);	    
        }

        /* Timer message handling */
        if(xQueueReceive(timer_queue, &evt, 0)){
            ESP_LOGI(GATTC_TAG, "Timer0 event (nextcmd.running=%d, ble_operation_in_progress=%d)", nextcmd.running, ble_operation_in_progress);
            outstanding_timer = false;
            
            if(nextcmd.running == true){
                //ESP_LOGI(GATTC_TAG, "countdown is %d\n", nextcmd.countdown);
                if(--nextcmd.countdown <= 0){
                    switch(nextcmd.cmd){
                        case EQ3_DISCONNECT:
                            if(connection_open == true){
                                ESP_LOGI(GATTC_TAG, "Close virtual server connection");
                                esp_ble_gattc_close (gl_profile_tab[PROFILE_A_APP_ID].gattc_if, gl_profile_tab[PROFILE_A_APP_ID].conn_id);
                            }
                            runtimer();
                            break;
                        case START_WIFI:
                            ESP_LOGI(GATTC_TAG, "Init wifi");
                            bootWiFi(wifidone, confparms);
                            break;
                        case RESTART_WIFI:
                            ESP_LOGI(GATTC_TAG, "Becoming WiFi client again\n");
                            restart_station();
                            break;

                    }
                    nextcmd.running = false;
                }else{
                    runtimer();
                }
            }else{
                if(ble_operation_in_progress == false){
                    run_command();
                    /* If there are no outstanding commands we can reboot if required */
                    if(ble_operation_in_progress == false && reboot_requested == true){
                        esp_restart();
                    }
                }else{
                    /* Do we need a failsafe check here in case the BLE state machine is stuck? */
                    runtimer();
                }
            }
        }
        //ESP_LOGI(GATTC_TAG, "Loop");
    }
}

