/****************************************************************************
*
* EQ-3 Thermostatic Radiator Valve control
*
****************************************************************************/

/* by Paul Tupper (C) 2017
 * derived from gatt_client example from Espressif Systems
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
#include "controller.h"
#include "driver/uart.h"

#include "bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"

#include "eq3_gap.h"
#include "eq3_timer.h"
#include "eq3_wifi.h"

#include "eq3_bootwifi.h"

#define GATTC_TAG "EQ3_MAIN"

#define EQ3_DISCONNECT 0

/* Allow delay of next GATTC command */
struct tmrcmd{
    bool running;
    int cmd;
    int countdown;
};

struct tmrcmd nextcmd;

int setnextcmd(int cmd, int time_s){
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

/* EQ-3 service identifier */
static esp_gatt_srvc_id_t eq3_service_id = {
    .id = {
        .uuid = {
            .len = ESP_UUID_LEN_128,
            .uuid = {.uuid128 = {0x46, 0x70, 0xb7, 0x5b, 0xff, 0xa6, 0x4a, 0x13, 0x90, 0x90, 0x4f, 0x65, 0x42, 0x51, 0x13, 0x3e},},
        },
        .inst_id = 0,
    },
    .is_primary = true,
};

/* EQ-3 characteristic identifier for setting parameters */
static esp_gatt_id_t eq3_char_id = {
    .uuid = {
        .len = ESP_UUID_LEN_128,
        .uuid = {.uuid128 = {0x09, 0xea, 0x79, 0x81, 0xdf, 0xb8, 0x4b, 0xdb, 0xad, 0x3b, 0x4a, 0xce, 0x5a, 0x58, 0xa4, 0x3f},},
    },
    .inst_id = 0,
};

/* EQ-3 characteristic used to notify settings from trv in response to parameter set */
static esp_gatt_id_t eq3_resp_char_id = {
    .uuid = {
        .len = ESP_UUID_LEN_128,
        .uuid = {.uuid128 = {0x2a, 0xeb, 0xe0, 0xf4, 0x90, 0x6c, 0x41, 0xaf, 0x96, 0x09, 0x29, 0xcd, 0x4d, 0x43, 0xe8, 0xd0},},
    },
    .inst_id = 0,
};

/* Current TRV command being sent to EQ-3 */ 
uint16_t cmd_len = 0;
uint8_t cmd_val[10] = {0};
esp_bd_addr_t cmd_bleda;   /* BLE Device Address */

static bool get_server = false;
static bool registered = false;

/* Name reported by EQ-3 valves when scanning */
static const char remote_device_name[] = "CC-RT-M-BLE";

#define PROFILE_NUM 1
#define PROFILE_A_APP_ID 0

/* Profile instance - used to store details of the  current connection profile */
struct gattc_profile_inst {
    esp_gattc_cb_t gattc_cb;
    uint16_t gattc_if;
    uint16_t app_id;
    uint16_t conn_id;
    esp_bd_addr_t remote_bda;
};

/* One gatt-based profile one app_id and one gattc_if, this array will store the gattc_if returned by ESP_GATTS_REG_EVT */
static struct gattc_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gattc_cb = gattc_profile_event_handler,
        .gattc_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

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
    case ESP_GATTC_REG_EVT:
        /* Registered */
        ESP_LOGI(GATTC_TAG, "REG_EVT");
	registered = true;
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
    case ESP_GATTC_CONNECT_EVT:
        /* GATT Client connected to server(EQ-3) */
        //p_data->connect.status always be ESP_GATT_OK
        conn_id = p_data->connect.conn_id;
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_CONNECT_EVT conn_id %d, if %d, status %d", conn_id, gattc_if, p_data->connect.status);
        gl_profile_tab[PROFILE_A_APP_ID].conn_id = conn_id;
        memcpy(gl_profile_tab[PROFILE_A_APP_ID].remote_bda, p_data->connect.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(GATTC_TAG, "REMOTE BDA:");
        esp_log_buffer_hex(GATTC_TAG, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, sizeof(esp_bd_addr_t));
        
	ESP_LOGE(GATTC_TAG, "RQ set mtu");
	esp_err_t mtu_ret = esp_ble_gattc_config_mtu (gattc_if, conn_id, 200);
        if (mtu_ret){
            ESP_LOGE(GATTC_TAG, "config MTU error, error code = %x", mtu_ret);
        }
        break;
    case ESP_GATTC_OPEN_EVT:
        /* Profile connection opened */
        if (param->open.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "open failed, status %d", p_data->open.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "open success");
        break;
    case ESP_GATTC_CLOSE_EVT:
        /* Profile connection closed */
        if(param->close.status != ESP_GATT_OK){
	    ESP_LOGE(GATTC_TAG, "close failed, status %d", p_data->close.status);
	}else{
	    ESP_LOGI(GATTC_TAG, "close success");
	}
	/* Wait before we connect to the next EQ-3 to send a queued command */
	start_timer(1000);
        break;
    case ESP_GATTC_CFG_MTU_EVT:
        /* MTU has been set */
        if (param->cfg_mtu.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG,"config mtu failed, error status = %x", param->cfg_mtu.status);
        }
        
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_CFG_MTU_EVT, Status %d, MTU %d, conn_id %d", param->cfg_mtu.status, param->cfg_mtu.mtu, param->cfg_mtu.conn_id);
        
	/* Search for the EQ-3 service we want */
	esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, NULL);
	
        break;
    case ESP_GATTC_SEARCH_RES_EVT: {
        esp_gatt_srvc_id_t *srvc_id = &p_data->search_res.srvc_id;
        conn_id = p_data->search_res.conn_id;
	if (srvc_id->id.uuid.len == ESP_UUID_LEN_32){
	  /* Ignore this service */
	  ESP_LOGI(GATTC_TAG, "Got UUID32: %x", srvc_id->id.uuid.uuid.uuid32);
	}else if (srvc_id->id.uuid.len == ESP_UUID_LEN_16){
	  /* Ignore this service */
	  ESP_LOGI(GATTC_TAG, "Got UUID16: %x", srvc_id->id.uuid.uuid.uuid16);
	}else if (srvc_id->id.uuid.len == ESP_UUID_LEN_128){
	  /* Check if the service identifier is the one we're interested in */
	  char printstr[ESP_UUID_LEN_128 * 2 + 1];
	  int bytecount, writecount = 0;
	  for(bytecount=ESP_UUID_LEN_128 - 1; bytecount >= 0; bytecount--, writecount += 2)
	    sprintf(&printstr[writecount], "%02x", srvc_id->id.uuid.uuid.uuid128[bytecount] & 0xff);
	  ESP_LOGI(GATTC_TAG, "Got UUID128: %s", printstr);
	}
        if (srvc_id->id.uuid.len == ESP_UUID_LEN_128){
	  int checkcount;
	  for(checkcount=0; checkcount < ESP_UUID_LEN_128; checkcount++){
	    if(srvc_id->id.uuid.uuid.uuid128[checkcount] != eq3_service_id.id.uuid.uuid.uuid128[checkcount]){
	      checkcount = -1;
	      break;
	    }
	  }
	  if(checkcount == ESP_UUID_LEN_128) {
            get_server = true;
            ESP_LOGI(GATTC_TAG, "Found EQ-3");
	  }
        }
        break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT:
        /* Search is complete */
        if (p_data->search_cmpl.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "search service failed, error status = %x", p_data->search_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "Search Complete - get characteristic");
        conn_id = p_data->search_cmpl.conn_id;
        if (get_server){
	    ESP_LOGE(GATTC_TAG, "RQ notify");
	    esp_ble_gattc_register_for_notify(gattc_if, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, &eq3_service_id, &eq3_resp_char_id);
        }else{
	    ESP_LOGE(GATTC_TAG, "EQ-3 service not available from this server!");
	    /* Wait 2 seconds for background GATTC operations then disconnect */
	    setnextcmd(EQ3_DISCONNECT, 2);
	    start_timer(1000);
	}
        break;
#ifdef removed	
    case ESP_GATTC_GET_CHAR_EVT:
        if (p_data->get_char.status != ESP_GATT_OK) {
            ESP_LOGE(GATTC_TAG, "get char failed, error status = %x", p_data->get_char.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "get char success");
        ESP_LOGI(GATTC_TAG, "GET CHAR: srvc_id = %04x, char_id = %04x", p_data->get_char.srvc_id.id.uuid.uuid.uuid16, p_data->get_char.char_id.uuid.uuid.uuid16);

        //if (p_data->get_char.char_id.uuid.uuid.uuid16 == REMOTE_NOTIFY_CHAR_UUID) {
            esp_ble_gattc_register_for_notify(gattc_if, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, &eq3_service_id, &p_data->get_char.char_id);
        //}

        //esp_ble_gattc_get_characteristic(gattc_if, conn_id, &gatt_server_demo_service_id, &p_data->get_char.char_id);
	
	//uint16_t boost_len = 4;
        //uint8_t boost_val[] = "4501";
	//ESP_LOGI(GATTC_TAG, "Set eq3 boost");
	//esp_ble_gattc_write_char(gattc_if, gl_profile_tab[PROFILE_A_APP_ID].conn_id, &eq3_service_id, &p_data->get_char.char_id, boost_len, boost_val, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
        break;
#endif	
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        if (p_data->reg_for_notify.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "REG FOR NOTIFY failed: error status = %d", p_data->reg_for_notify.status);
            break;
        }

        ESP_LOGI(GATTC_TAG, "REG FOR_NOTIFY: srvc_id = %04x, char_id = %04x", p_data->reg_for_notify.srvc_id.id.uuid.uuid.uuid16, p_data->reg_for_notify.char_id.uuid.uuid.uuid16);
	
	ESP_LOGI(GATTC_TAG, "Send eq3 command");
	/* This is what we're actually here for :) */
	esp_ble_gattc_write_char(gattc_if, gl_profile_tab[PROFILE_A_APP_ID].conn_id, &eq3_service_id, &eq3_char_id, cmd_len, cmd_val, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
	
        break;
    }
    case ESP_GATTC_NOTIFY_EVT:
        /* EQ-3 has sent a notification with its current status */
	/* Decode this and create a json message to send back to the controlling broker to keep state-machine up-to-date and acknowledge settings */
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_NOTIFY_EVT, Receive notify value:");
        esp_log_buffer_hex(GATTC_TAG, p_data->notify.value, p_data->notify.value_len);
	
	uint8_t tempval, temphalf = 0;
	char statrep[120];
	int statidx = 0;
	
	statidx += sprintf(&statrep[statidx], "{\"trv\":\"%02X:%02X:%02X:%02X:%02X:%02X\"},", gl_profile_tab[PROFILE_A_APP_ID].remote_bda[0], gl_profile_tab[PROFILE_A_APP_ID].remote_bda[1],
			   gl_profile_tab[PROFILE_A_APP_ID].remote_bda[2], gl_profile_tab[PROFILE_A_APP_ID].remote_bda[3], gl_profile_tab[PROFILE_A_APP_ID].remote_bda[4], gl_profile_tab[PROFILE_A_APP_ID].remote_bda[5]);
	
	if(p_data->notify.value_len > 5){
	    tempval = p_data->notify.value[5];
	    if(tempval & 0x01)
	        temphalf = 5;
	    tempval >>= 1;
	    ESP_LOGI(GATTC_TAG, "eq3 settemp is %d.%d C", tempval, temphalf);
	    statidx += sprintf(&statrep[statidx], "{\"temp\":%d.%d}", tempval, temphalf);
	}
	if(p_data->notify.value_len > 3){
	    tempval = p_data->notify.value[3];
	    statidx += sprintf(&statrep[statidx], ",{\"boost\":");
	    if(tempval == 0x50){
	        ESP_LOGI(GATTC_TAG, "eq3 boost active");
		statidx += sprintf(&statrep[statidx], "\"active\"}");
	    }else{
	        ESP_LOGI(GATTC_TAG, "eq3 boost inactive");
		statidx += sprintf(&statrep[statidx], "\"inactive\"}");
	    }
	}
	if(p_data->notify.value_len > 2){
	    tempval = p_data->notify.value[2];
	    statidx += sprintf(&statrep[statidx], ",{\"mode\":");
	    switch(tempval & 0x0f){
	        case 0x08:
	            ESP_LOGI(GATTC_TAG, "eq3 set auto");
		    statidx += sprintf(&statrep[statidx], "\"auto\"}");
	            break;
	        case 0x09:
		    ESP_LOGI(GATTC_TAG, "eq3 set manual");
		    statidx += sprintf(&statrep[statidx], "\"manual\"}");
		    break;
		case 0x0a:
		    ESP_LOGI(GATTC_TAG, "eq3 set eco");
		    statidx += sprintf(&statrep[statidx], "\"eco\"}");
		    break;
	    }
	    statidx += sprintf(&statrep[statidx], ",{\"state\":");
	    if((tempval & 0xf0) == 0x20){
	        ESP_LOGI(GATTC_TAG, "eq3 set locked");
		statidx += sprintf(&statrep[statidx], "\"locked\"}");
	    }else{
	        ESP_LOGI(GATTC_TAG, "eq3 set unlocked");
		statidx += sprintf(&statrep[statidx], "\"unlocked\"}");
	    }
	}
	/* Send the status report we just collated */
	send_trv_status(statrep);
	
	/* 2 second delay until disconnect to allow any background GATTC stuff to complete */
	setnextcmd(EQ3_DISCONNECT, 2);
	start_timer(1000);
	
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
#ifdef removed    
    case ESP_GATTC_WRITE_DESCR_EVT:
        if (p_data->write.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "write descr failed, error status = %x", p_data->write.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "write descr success ");
        break;
    case ESP_GATTC_SRVC_CHG_EVT: {
        esp_bd_addr_t bda;
        memcpy(bda, p_data->srvc_chg.remote_bda, sizeof(esp_bd_addr_t));
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_SRVC_CHG_EVT, bd_addr:");
        esp_log_buffer_hex(GATTC_TAG, bda, sizeof(esp_bd_addr_t));
        break;
    }
#endif    
    case ESP_GATTC_WRITE_CHAR_EVT:
        /* Characteristic write complete */
        if (p_data->write.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "write char failed, error status = %x", p_data->write.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "write char success ");
        break;
    case ESP_GATTC_DISCONNECT_EVT:
        /* Disconnected */
        get_server = false;
        ESP_LOGI(GATTC_TAG, "ESP_GATTC_DISCONNECT_EVT, status = %d", p_data->disconnect.status);
	//esp_ble_gattc_app_unregister(gl_profile_tab[PROFILE_A_APP_ID].gattc_if);
        break;
    default:
        ESP_LOGI(GATTC_TAG, "Unhandled_EVT %d", event);
        break;
    }
}

static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    //ESP_LOGI(GATTC_TAG, "EVT %d, gattc if %d", event, gattc_if);

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
}eq3_bt_cmd;

struct eq3cmd{
    esp_bd_addr_t bleda;
    eq3_bt_cmd cmd;
    unsigned int cmdval;
    struct eq3cmd *next;
};

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

/* Handle an EQ-3 command from uart or mqtt */
int handle_request(char *cmdstr){

    char *cmdptr;
    struct eq3cmd *newcmd;
    eq3_bt_cmd command;
    unsigned int param = 0;
    
    bool start = false;
    //ESP_LOGE(GATTC_TAG, "Got %s", cmdstr);
    
    /* Assume the mac (with colons) comes first */
    cmdptr = (char *)&cmdstr[18];
    
    if(strncmp((const char *)cmdptr, "boost", 5) == 0){
        start = true;
	command = EQ3_BOOST;
    }
    if(strncmp((const char *)cmdptr, "unboost", 7) == 0){
        start = true;
	command = EQ3_UNBOOST;
    }
    if(strncmp((const char *)cmdptr, "auto", 4) == 0){
        start = true;
	command = EQ3_AUTO;
    }
    if(strncmp((const char *)cmdptr, "manual", 6) == 0){
        start = true;
	command = EQ3_MANUAL;
    }
    if(strncmp((const char *)cmdptr, "settemp", 7) == 0){
        char *endmsg;
        uint8_t temp = strtol(cmdptr + 8, &endmsg, 10);
	if(temp > 5 && temp < 30){
            start = true;
            command = EQ3_SETTEMP;
            param = temp << 1;
	}
    }
    
    if(start == true){
        newcmd = malloc(sizeof(struct eq3cmd));
	
	newcmd->cmd = command;
	newcmd->cmdval = param;
	
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
    
        struct eq3cmd *qwalk = cmdqueue;
        if(cmdqueue == NULL){
            cmdqueue = newcmd;
	    ESP_LOGI(GATTC_TAG, "Add queue head");
	}else{
	    while(qwalk->next != NULL)
	        qwalk = qwalk->next;
	    qwalk->next = newcmd;
	    ESP_LOGI(GATTC_TAG, "Add queue end");
	}
	if(timer_running() == true)
	    ESP_LOGI(GATTC_TAG, "Timer still running!!??");
	start_timer(1000);
    }
    return 0;
}    

/* Get the next command off the queue and encode the characteristic parameters */
static int setup_command(void){
    if(cmdqueue != NULL){
        switch(cmdqueue->cmd){
	  case EQ3_BOOST:
            cmd_val[0] = 0x45;
            cmd_val[1] = 0x01;
	    cmd_len = 2;
	    break;
	  case EQ3_UNBOOST:
            cmd_val[0] = 0x45;
            cmd_val[1] = 0x00;
	    cmd_len = 2;
	    break;
	  case EQ3_AUTO:
            cmd_val[0] = 0x40;
            cmd_val[1] = 0x00;
	    cmd_len = 2;
	    break;
	  case EQ3_MANUAL:
            cmd_val[0] = 0x45;
            cmd_val[1] = 0x40;
	    cmd_len = 2;
	    break;
	  case EQ3_SETTEMP:
            cmd_val[0] = 0x41;
            cmd_val[1] = (cmdqueue->cmdval & 0xff);
	    cmd_len = 2;
	    break;
	  default:
	    ESP_LOGI(GATTC_TAG, "Can't handle that command yet");
	    break;
	}
	memcpy(cmd_bleda, cmdqueue->bleda, sizeof(esp_bd_addr_t));
	struct eq3cmd *delcmd = cmdqueue;
	cmdqueue = cmdqueue->next;
	free(delcmd);
    }
    return 0;
}

/* Run the next EQ-3 command from the list */
static int run_command(void){
    if(cmdqueue != NULL){
        ESP_LOGI(GATTC_TAG, "Send next command");
        setup_command();
        esp_ble_gattc_open(gl_profile_tab[PROFILE_A_APP_ID].gattc_if, cmd_bleda, true);
    }
    return 0;
}

/* Callback from config - copy url, username and password for mqtt broker */
static char *usr = NULL, *pass = NULL, *url = NULL;
void confparms(char *mqtturl, char *mqttuser, char *mqttpass){
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
        url = strdup(mqtturl);
        ESP_LOGI(GATTC_TAG, "MQTT config url is %s", mqtturl);
    }
    if(mqttuser != NULL && mqttuser[0] != 0){
        usr = strdup(mqttuser);
        ESP_LOGI(GATTC_TAG, "MQTT config user is %s", mqttuser);
    }
    if(mqttpass != NULL && mqttpass[0] != 0){
        pass = strdup(mqttpass);
        ESP_LOGI(GATTC_TAG, "MQTT config pass is %s", mqttpass);
    }
}

/* Callback when we're associated with an AP */
void wifidone(int rc){
    ESP_LOGI(GATTC_TAG, "Wifi setup done\n");
    connect_server(url, usr, pass);
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

    ESP_LOGI(GATTC_TAG, "Init wifi");
    //initialise_wifi();
 
    bootWiFi(wifidone, confparms);
    
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s initialize controller failed, error code = %x\n", __func__, ret);
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
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
    
    /* Kick off a GAP scan */
    start_scan();
    
    /* Main polling loop */
    uint8_t *msg = NULL;
    timer_event_t evt;
    while(1){
        /* Receive messages from uart */
	if( xQueueReceive( msgQueue, (void *)&msg, 0)){
	    char *msgptr = (char *)msg;
	    handle_request(msgptr);
	    free(msg);	    
	}
	
	/* Timer message handling */
	if(xQueueReceive(timer_queue, &evt, 0)){
            ESP_LOGI(GATTC_TAG, "Timer0 returned");
	    if(nextcmd.running == true){
	        if(--nextcmd.countdown <= 0){
		    switch(nextcmd.cmd){
		      case EQ3_DISCONNECT:
			ESP_LOGE(GATTC_TAG, "RQ close");
	                esp_ble_gattc_close (gl_profile_tab[PROFILE_A_APP_ID].gattc_if, gl_profile_tab[PROFILE_A_APP_ID].conn_id);
			break;
			
		    }
		    nextcmd.running = false;
		}else{
		    start_timer(1000);
		}
	    }else{
	        run_command();
	    }
	}
	//ESP_LOGI(GATTC_TAG, "Loop");
    }
}

