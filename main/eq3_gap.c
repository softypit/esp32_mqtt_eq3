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


// PIT 17/09/2017 eq-3 control


/****************************************************************************
*
* EQ-3 thermostatic radiator valve control
* finds devices with CC-RT-M-BLE IDs 
*
****************************************************************************/

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "controller.h"
#include "driver/uart.h"

#include "bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"

#include "eq3_wifi.h"

#define EQ3_DBG_TAG "EQ3_CTRL"

///Declare static functions
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

static void scan_done(void);

static bool gap_scanning = false;

/* Device list handling */
struct found_device {
  esp_bd_addr_t bda;
  struct found_device *next;
};
struct found_device *found_devices = NULL;
int num_devices = 0;

static void free_found_devices(){
    struct found_device *nextdev, *thisdev = found_devices;
    while(thisdev != NULL){
        nextdev = thisdev->next;
	free(thisdev);
	thisdev = nextdev;
    }
    found_devices = NULL;
    num_devices = 0;
}
int add_found_device(esp_bd_addr_t *bda){
    int rc = 0;
    struct found_device *lastdev, *walkdevs = found_devices;
    if(found_devices == NULL){
        walkdevs = malloc(sizeof(struct found_device));
	if(walkdevs != NULL){
	    walkdevs->next = NULL;
	    memcpy(&walkdevs->bda, bda, sizeof(esp_bd_addr_t));
	    found_devices = walkdevs;
	    ESP_LOGI(EQ3_DBG_TAG, "Started list:");
	    num_devices++;
	    esp_log_buffer_hex(EQ3_DBG_TAG, walkdevs->bda, sizeof(esp_bd_addr_t));
	}
    }else{
	lastdev = NULL;
	while(walkdevs != NULL){
	    if(memcmp(&walkdevs->bda, bda, sizeof(esp_bd_addr_t)) == 0)
	        break;
	    lastdev = walkdevs;
	    walkdevs = walkdevs->next;
	}
	if(walkdevs == NULL){
	    walkdevs = malloc(sizeof(struct found_device));
	    if(walkdevs != NULL){
	        walkdevs->next = NULL;
	        memcpy(&walkdevs->bda, bda, sizeof(esp_bd_addr_t));
	        lastdev->next = walkdevs;
		ESP_LOGI(EQ3_DBG_TAG, "Added to list:");
		num_devices++;
		esp_log_buffer_hex(EQ3_DBG_TAG, walkdevs->bda, sizeof(esp_bd_addr_t));
	    }
	}else{
	    rc = 1;
	}
    }
    return rc;
}

/* BT GAP device scanning code */
static const char remote_device_name[] = "CC-RT-M-BLE";

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x50,
    .scan_window            = 0x30
};

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    uint8_t *adv_name = NULL;
    uint8_t adv_name_len = 0;
    
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {
        //the unit of the duration is second
        uint32_t duration = 30;
        esp_ble_gap_start_scanning(duration);
	free_found_devices();
        break;
    }
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        //scan start complete event to indicate scan start successfully or failed
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(EQ3_DBG_TAG, "scan start failed, error status = %x", param->scan_start_cmpl.status);
            break;
        }
        ESP_LOGI(EQ3_DBG_TAG, "scan start success");

        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
        switch (scan_result->scan_rst.search_evt) {
        case ESP_GAP_SEARCH_INQ_RES_EVT:
            //esp_log_buffer_hex(EQ3_DBG_TAG, scan_result->scan_rst.bda, 6);
            //ESP_LOGI(EQ3_DBG_TAG, "searched Adv Data Len %d, Scan Response Len %d", scan_result->scan_rst.adv_data_len, scan_result->scan_rst.scan_rsp_len);
            adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
            //ESP_LOGI(EQ3_DBG_TAG, "Scan found device (len %d)", adv_name_len);
            //esp_log_buffer_char(EQ3_DBG_TAG, adv_name, adv_name_len);
	    //esp_log_buffer_hex(EQ3_DBG_TAG, scan_result->scan_rst.bda, 6);
            if (adv_name != NULL) {
                if (strlen(remote_device_name) == adv_name_len && strncmp((char *)adv_name, remote_device_name, adv_name_len) == 0) {
		    if(add_found_device(&scan_result->scan_rst.bda) == 0){
		      ESP_LOGI(EQ3_DBG_TAG, "Found device %s", remote_device_name);
		      esp_log_buffer_hex(EQ3_DBG_TAG, scan_result->scan_rst.bda, 6);
		    }
                }
            }
            break;
        case ESP_GAP_SEARCH_INQ_CMPL_EVT:
	    gap_scanning = false;
	    scan_done();
            break;
        default:
            break;
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(EQ3_DBG_TAG, "scan stop failed, error status = %x", param->scan_stop_cmpl.status);
            break;
        }
        ESP_LOGI(EQ3_DBG_TAG, "Scan finished successfully");
        break;

    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(EQ3_DBG_TAG, "adv stop failed, error status = %x", param->adv_stop_cmpl.status);
            break;
        }
        ESP_LOGI(EQ3_DBG_TAG, "stop adv successfully");
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
         ESP_LOGI(EQ3_DBG_TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                  param->update_conn_params.status,
                  param->update_conn_params.min_int,
                  param->update_conn_params.max_int,
                  param->update_conn_params.conn_int,
                  param->update_conn_params.latency,
                  param->update_conn_params.timeout);
        break;
    default:
        break;
    }
}

/* Start a scan */
void start_scan(){
    esp_err_t ret;
    //register the  callback function to the gap module
    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret){
        ESP_LOGE(EQ3_DBG_TAG, "%s gap register failed, error code = %x\n", __func__, ret);
        return;
    }

    gap_scanning = true;
    
    ret = esp_ble_gap_set_scan_params(&ble_scan_params);
    
}

/* Scan complete */
static void scan_done(){
    ESP_LOGI(EQ3_DBG_TAG, "Scan complete\nDevices found:\n");

    char *report = malloc((20 * num_devices) + 2);
    struct found_device *devwalk = found_devices;
    if(devwalk != NULL){
        int devnum = 0;
        while(devwalk != NULL){
            ESP_LOGI(EQ3_DBG_TAG, "Device:");
            esp_log_buffer_hex(EQ3_DBG_TAG, devwalk->bda, 6);
            ESP_LOGI(EQ3_DBG_TAG, "\n");
	
	    sprintf(&report[devnum * 20], "{%02X:%02X:%02X:%02X:%02X:%02X},", devwalk->bda[0], devwalk->bda[1], devwalk->bda[2], devwalk->bda[3], devwalk->bda[4], devwalk->bda[5]);
	
	    devnum++;
            devwalk = devwalk->next;
        }
        if(devnum > 0)
            report[(devnum * 20) - 1] = 0;
	send_device_list(report);    
        //free(report);
    }else{
        ESP_LOGI(EQ3_DBG_TAG, "None");
    }
}

/* Poll-able function to see if a scan is underway */
bool scan_complete(){
    return gap_scanning;
}
