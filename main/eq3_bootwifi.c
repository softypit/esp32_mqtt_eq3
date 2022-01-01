/**
 * Bootwifi - Boot the WiFi environment.
 *
 * Compile with -DBOOTWIFI_OVERRIDE_GPIO=<num> where <num> is a GPIO pin number
 * to use a GPIO override.
 * See the README.md for full information.
 *
 */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_ota_ops.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_netif.h>
#include <lwip/sockets.h>
#include <lwip/apps/sntp.h>
#include <mongoose.h>
#include <sdkconfig.h>
#include "eq3_bootwifi.h"
#include "sdkconfig.h"
#include "eq3_main.h"
#include "eq3_gap.h"
#include "eq3_wifi.h"

/* Webcontent */
#include "eq3_htmlpages.h"

// If the structure of a record saved for a subsequent reboot changes
// then consider using semver to change the version number or else
// we may try and boot with the wrong data.
#define KEY_VERSION "version"
/* Previous config version 1 */
uint32_t g_v1_version=0x0100;
/* Previous config version 2 */
uint32_t g_v2_version=0x0200;
/* Newer config version 3 */
uint32_t g_version=0x0300;

/* esp netif object representing the WIFI AP */
static esp_netif_t *ap_netif = NULL;
static esp_netif_t *sta_netif = NULL;

#define KEY_CONNECTION_INFO "connectionInfo" // Key used in NVS for connection info
#define BOOTWIFI_NAMESPACE "bootwifi"        // Namespace in NVS for bootwifi
#define SSID_SIZE (32)                       // Maximum SSID size
#define USERNAME_SIZE (64)                   // Maximum username size
#define PASSWORD_SIZE (64)                   // Maximum password size
#define ID_SIZE       (32)                   // Maximum MQTT clientID size
#define MAX_URL_SIZE  (256)                  // Maximum url length
#define SERVER_SIZE      (64)                // Maximum length of sntp/dns server url
#define SNTP_TIMEZONE_SIZE (35)              // Maximum length of timezone parameter

/* Non-volatile configuration parameters */

/* This is the older config structure (used here to read config from previous versions of code) */
typedef struct {
    char ssid[SSID_SIZE];
    char password[PASSWORD_SIZE];
    char mqtturl[MAX_URL_SIZE];
    char mqttuser[USERNAME_SIZE];
    char mqttpass[PASSWORD_SIZE];
    char mqttid[ID_SIZE];
    tcpip_adapter_ip_info_t ipInfo;          // Optional static IP information
} v1_connection_info_t;
#define V1_CONNECTION_INFO_SIZE sizeof(v1_connection_info_t);

/* Newer config structure - nvs save will place this structure in flash */
typedef struct {
    char ssid[SSID_SIZE];
    char password[PASSWORD_SIZE];
    char mqtturl[MAX_URL_SIZE];
    char mqttuser[USERNAME_SIZE];
    char mqttpass[PASSWORD_SIZE];
    char mqttid[ID_SIZE];
    tcpip_adapter_ip_info_t ipInfo;          // Optional static IP information
    int ntpenabled;
    char ntpserver[SERVER_SIZE];
    char ntptimezone[SNTP_TIMEZONE_SIZE];
} v2_connection_info_t;
#define V2_CONNECTION_INFO_SIZE sizeof(v2_connection_info_t);

typedef struct {
    char ssid[SSID_SIZE];
    char password[PASSWORD_SIZE];
    char mqtturl[MAX_URL_SIZE];
    char mqttuser[USERNAME_SIZE];
    char mqttpass[PASSWORD_SIZE];
    char mqttid[ID_SIZE];
    esp_netif_ip_info_t ipInfo;          // Optional static IP information
    int ntpenabled;
    char ntpserver[SERVER_SIZE];
    char ntptimezone[SNTP_TIMEZONE_SIZE];
    char ntpserver2[SERVER_SIZE];
    char dnsservers[2][SERVER_SIZE];
    char spare[550];
} connection_info_t;

static connection_info_t connectionInfo;
int getConnectionInfo(connection_info_t *pConnectionInfo);

static void saveConnectionInfo(connection_info_t *pConnectionInfo);
static void becomeAccessPoint();
static void bootWiFi2();

static bool haveconninfo = false;
static char tag[] = "websrv";

static bootwifi_callback_t g_callback = NULL; // Callback function to indicate wifi state change (STA <--> AP).
static bootwifi_parms_t g_parms = NULL;

static bool sta_configured = false;           // Are we in STA mode?

/* ===================== Status log code ================================= */
#define NUM_LOG_ENTRIES 40
static char *log_entries[NUM_LOG_ENTRIES];

/* Initialise the log array */
void eq3_log_init(void){
    for(int i=0; i<NUM_LOG_ENTRIES; i++)
        log_entries[i] = NULL;
}

/* Add a new log entry to the top of the array */
void eq3_add_log(char *log){
    char strftime_buf[64] = {0};
    if(connectionInfo.ntpenabled != 0){
        time_t now = 0;
        struct tm timeinfo = { 0 };
        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    }
    char *newlog = malloc(strlen(log) + strlen(strftime_buf) + 4);
    if(newlog != NULL){
        int stridx = 0;
        if(connectionInfo.ntpenabled != 0)
            stridx += sprintf(newlog, "%s - ", strftime_buf);
        strcpy(&newlog[stridx], log);
        if(log_entries[NUM_LOG_ENTRIES - 1] != NULL)
            free(log_entries[NUM_LOG_ENTRIES - 1]);
        for(int i = NUM_LOG_ENTRIES - 2; i >= 0; i--){
            log_entries[i+1] = log_entries[i];
        }
        log_entries[0] = newlog;
    }
}

/* Calculate the number of log entries in the array */
static int eq3_num_logs(void){
    int ret = 0;
    for(int i = 0; i < (NUM_LOG_ENTRIES - 1); i++){
        if(log_entries[i] != NULL)
            ret++;
    }
    return ret;
}

/* ===================== Mongoose webserver code ========================= */

static int g_mongooseStarted = 0; // Has the mongoose server started?
static int g_mongooseStopRequest = 0; // Request to stop the mongoose server.

#ifdef removed
/**
 * Convert a Mongoose event type to a string.  Used for debugging.
 */
static char *mongoose_eventToString(int ev) {
	switch (ev) {
        case MG_EV_CONNECT:							return "MG_EV_CONNECT";
        case MG_EV_ACCEPT:							return "MG_EV_ACCEPT";
        case MG_EV_CLOSE:							return "MG_EV_CLOSE";
        case MG_EV_SEND:							return "MG_EV_SEND";
        case MG_EV_RECV:							return "MG_EV_RECV";
        case MG_EV_HTTP_REQUEST:					return "MG_EV_HTTP_REQUEST";
        case MG_EV_MQTT_CONNACK:					return "MG_EV_MQTT_CONNACK";
        case MG_EV_MQTT_CONNACK_ACCEPTED:			return "MG_EV_MQTT_CONNACK_ACCEPT";
        case MG_EV_MQTT_CONNECT:					return "MG_EV_MQTT_CONNECT";
        case MG_EV_MQTT_DISCONNECT:					return "MG_EV_MQTT_DISCONNECT";
        case MG_EV_MQTT_PINGREQ:					return "MG_EV_MQTT_PINGREQ";
        case MG_EV_MQTT_PINGRESP:					return "MG_EV_MQTT_PINGRESP";
        case MG_EV_MQTT_PUBACK:						return "MG_EV_MQTT_PUBACK";
        case MG_EV_MQTT_PUBCOMP:					return "MG_EV_MQTT_PUBCOMP";
        case MG_EV_MQTT_PUBLISH:					return "MG_EV_MQTT_PUBLISH";
        case MG_EV_MQTT_PUBREC:						return "MG_EV_MQTT_PUBREC";
        case MG_EV_MQTT_PUBREL:						return "MG_EV_MQTT_PUBREL";
        case MG_EV_MQTT_SUBACK:						return "MG_EV_MQTT_SUBACK";
        case MG_EV_MQTT_SUBSCRIBE:					return "MG_EV_MQTT_SUBSCRIBE";
        case MG_EV_MQTT_UNSUBACK:					return "MG_EV_MQTT_UNSUBACK";
        case MG_EV_MQTT_UNSUBSCRIBE:				return "MG_EV_MQTT_UNSUBSCRIBE";
        case MG_EV_WEBSOCKET_HANDSHAKE_REQUEST:		return "MG_EV_WEBSOCKET_HANDSHAKE_REQUEST";
        case MG_EV_WEBSOCKET_HANDSHAKE_DONE:		return "MG_EV_WEBSOCKET_HANDSHAKE_DONE";
        case MG_EV_WEBSOCKET_FRAME:					return "MG_EV_WEBSOCKET_FRAME";
    }

    static char temp[100];
    sprintf(temp, "Unknown event: %d", ev);
    return temp;
} //eventToString
#endif

// Convert a Mongoose string type to a string.
static char *mgStrToStr(struct mg_str mgStr) {
    if(mgStr.len == 0)
        return NULL;
    char *retStr = (char *) malloc(mgStr.len + 1);
    memcpy(retStr, mgStr.ptr, mgStr.len);
    retStr[mgStr.len] = 0;
    return retStr;
} // mgStrToStr

static int mongoose_serve_content(struct mg_connection *nc, char *content, bool footer){
    int apploc = 0;
    int rc = 0;
    int contlen = 0;
    if(content != NULL)
        contlen = strlen(content);
    /* Hopefully 20 characters is enough for the version */
    char *htmlstr = malloc(strlen(pageheader) + strlen(pagefooter) + contlen + 1);
    if(htmlstr != NULL){
        apploc += sprintf(&htmlstr[apploc], pageheader);
        if(content != NULL){
            //apploc += sprintf(&htmlstr[apploc], content);
            strcpy(&htmlstr[apploc], content);
            apploc += strlen(content);
        }
        if(footer == true)
            apploc += sprintf(&htmlstr[apploc], pagefooter);
        else
            apploc += sprintf(&htmlstr[apploc], pageemptyfooter);
        mg_http_reply(nc, 200, "Content-Type: text/html\n", "%s", htmlstr);
        free(htmlstr);
    }else{
        ESP_LOGI(tag, "No free memory to server web page");
        rc = -1;
    }
    return rc;
}

/* Serve the configuration webpage */
static int mongoose_serve_config_page(struct mg_connection *nc){
    int rc = getConnectionInfo(&connectionInfo);
    //char *htmlstr = malloc(strlen(selectap) + 100);
    char *htmlstr = malloc(3100);
    const char nullstr[] = "";
    char *sptr = (char *)nullstr, *pptr = (char *)nullstr, *murlptr = (char *)nullstr, *muserptr = (char *)nullstr, *mpassptr = (char *)nullstr, *midptr = (char *)nullstr;
    char *ibuf = (char *)nullstr, *gbuf = (char *)nullstr, *mbuf = (char *)nullstr;
    char ipbuf[20], gwbuf[20], maskbuf[20];
    if(rc == 0){
        sptr = connectionInfo.ssid;
        /* DO NOT serve current password as this can be read from the client if the ESP32 has dropped into AP mode when it couldn't connect as STA */
        //pptr = connectionInfo.password;
        murlptr = connectionInfo.mqtturl;
        muserptr = connectionInfo.mqttuser;
        /* DO NOT serve current password as this can be read from the client if the ESP32 has dropped into AP mode when it couldn't connect as STA */
        //mpassptr = connectionInfo.mqttpass;
        midptr = connectionInfo.mqttid;
        if(connectionInfo.ipInfo.ip.addr != 0)
            ibuf = (char *)inet_ntop(AF_INET, &connectionInfo.ipInfo.ip, ipbuf, sizeof(ipbuf));
        if(connectionInfo.ipInfo.gw.addr != 0)
            gbuf = (char *)inet_ntop(AF_INET, &connectionInfo.ipInfo.gw, gwbuf, sizeof(gwbuf));
        if(connectionInfo.ipInfo.netmask.addr != 0)
            mbuf = (char *)inet_ntop(AF_INET, &connectionInfo.ipInfo.netmask, maskbuf, sizeof(maskbuf));
    }
    sprintf(htmlstr, selectap, sptr, pptr, murlptr, muserptr, mpassptr, midptr, connectionInfo.ntpenabled != 0 ? "checked=\"checked\"" : "", connectionInfo.ntpserver, connectionInfo.ntpserver2, connectionInfo.ntptimezone, ibuf == NULL ? nullstr : ibuf, gbuf == NULL ? nullstr : gbuf, mbuf == NULL ? nullstr : mbuf, connectionInfo.dnsservers[0], connectionInfo.dnsservers[1]);
    mongoose_serve_content(nc, htmlstr, true);
    free(htmlstr);
    //nc->flags |= MG_F_SEND_AND_CLOSE;
    return 0;
}

/* Serve a found device list page */
static int mongoose_serve_device_list(struct mg_connection *nc){
    int wridx = 0;
    struct found_device *devwalk = NULL;
    int numdevices;
    enum eq3_scanstate listres = eq3gap_get_device_list(&devwalk, &numdevices);
    if(listres == EQ3_SCAN_COMPLETE){
        
        char *devlisthtml = malloc(strlen(devlisthead) + strlen(devlistfoot) + ((strlen(devlistentry) + 22) * numdevices));
        if(devlisthtml != NULL){
            /* Copy header into buffer */
            wridx += sprintf(&devlisthtml[wridx], devlisthead);
            /* Collate device list in buffer */
            while(devwalk != NULL){
                wridx += sprintf(&devlisthtml[wridx], devlistentry, devwalk->bda[0], devwalk->bda[1], devwalk->bda[2], 
                     devwalk->bda[3], devwalk->bda[4], devwalk->bda[5], devwalk->rssi); 	
                devwalk = devwalk->next;
            }
        
            /* Copy footer into buffer */
            wridx += sprintf(&devlisthtml[wridx], devlistfoot);
            mongoose_serve_content(nc, devlisthtml, true);
            free(devlisthtml);
        }else{
            mongoose_serve_content(nc, NULL, true);
        }
    }else if(listres == EQ3_SCAN_UNDERWAY){
        /* Still scanning */
        mongoose_serve_content(nc, (char *)scanning, true);
    }else{
        /* No devices found */
        mongoose_serve_content(nc, (char *)nodevices, true);
    }
    //nc->flags |= MG_F_SEND_AND_CLOSE;
    return 0;
}

static int mongoose_serve_command_list(struct mg_connection *nc){
    int wridx = 0;
    struct found_device *devwalk = NULL;
    int numdevices;
    enum eq3_scanstate listres = eq3gap_get_device_list(&devwalk, &numdevices);
    if(listres == EQ3_SCAN_COMPLETE){
        char *devlisthtml = malloc(strlen(command_device_head) + strlen(command_post_device) + (((2 * strlen(select_device_entry)) + 30) * numdevices));
        if(devlisthtml != NULL){
            /* Copy header into buffer */
            wridx += sprintf(&devlisthtml[wridx], command_device_head);
            /* Collate device list in buffer */
            while(devwalk != NULL){
                char bleaddr[18];
                sprintf(bleaddr, "%02X:%02X:%02X:%02X:%02X:%02X", devwalk->bda[0], devwalk->bda[1], devwalk->bda[2], devwalk->bda[3], devwalk->bda[4], devwalk->bda[5]);
                wridx += sprintf(&devlisthtml[wridx], select_device_entry, bleaddr, bleaddr);
                devwalk = devwalk->next;
            }
        
            /* Copy footer into buffer */
            wridx += sprintf(&devlisthtml[wridx], command_post_device);
            mongoose_serve_content(nc, devlisthtml, true);
            free(devlisthtml);
        }else{
            mongoose_serve_content(nc, NULL, true);
        }
    }else if(listres == EQ3_SCAN_UNDERWAY){
        /* Still scanning */
        mongoose_serve_content(nc, (char *)scanning, true);
    }else{
        /* No devices found */
        mongoose_serve_content(nc, (char *)nodevices, true);
    }
    //nc->flags |= MG_F_SEND_AND_CLOSE;
    return 0;
}
        
/* Serve the logs page */
static int mongoose_serve_log(struct mg_connection *nc){
    int numlogs = eq3_num_logs();
    int wridx = 0;
    char *loglisthtml = malloc(strlen(loglisthead) + strlen(loglistfoot) + (135 + 18) * numlogs);
    if(loglisthtml != NULL){
        wridx += sprintf(&loglisthtml[wridx], loglisthead);
        for(int logcount = 0; logcount < numlogs; logcount++){
            //wridx += sprintf(&loglisthtml[wridx], loglistentry, log_entries[logcount]);
            wridx += sprintf(&loglisthtml[wridx], "<tr><td>");
            strncpy(&loglisthtml[wridx], log_entries[logcount], strlen(log_entries[logcount]));
            wridx += strlen(log_entries[logcount]);
            wridx += sprintf(&loglisthtml[wridx], "</tr></td>");
        }
        wridx += sprintf(&loglisthtml[wridx], loglistfoot);
    }
    mongoose_serve_content(nc, loglisthtml, true);
    free(loglisthtml);
    //nc->flags |= MG_F_SEND_AND_CLOSE;
    return 0;
}

/* Serve the status page */
static int mongoose_serve_status(struct mg_connection *nc){
    if(sta_configured == false){
        mongoose_serve_content(nc, (char *)apstatus, true);
        //nc->flags |= MG_F_SEND_AND_CLOSE;
    }else{
        char status[14];
        int64_t uptime = esp_timer_get_time();
        uint32_t days;
        uint8_t hours, minutes;
        mqttconnstate connected = ismqttconnected();
        switch(connected){
            case MQTT_CONFIG_ERROR:
                sprintf(status, "Config error");
                break;
            case MQTT_CONNECTED:
                sprintf(status, "Connected");
                break;
            case MQTT_NOT_CONNECTED:
            default:
                sprintf(status, "Not connected");
                break;
        }
        uptime /= 1000000;
        days = uptime / 86400;
        uptime -= (days * 86400);
        hours = uptime / 3600;
        uptime -= (hours * 3600);
        minutes = uptime / 60;
        uptime -= (minutes * 60);
        char *htmlstr = malloc(strlen(connectedstatus) + strlen(connectionInfo.mqtturl) + strlen(connectionInfo.mqttid) + 15 + 10);
        sprintf(htmlstr, connectedstatus, connectionInfo.mqtturl, connectionInfo.mqttid, status, days, hours, minutes, (uint8_t)uptime);
        mongoose_serve_content(nc, htmlstr, true);
        free(htmlstr);
        //nc->flags |= MG_F_SEND_AND_CLOSE;
    }            
    return 0;
}

/* Serve the upload page */
static int mongoose_serve_upload(struct mg_connection *nc){
    mongoose_serve_content(nc, (char *)upload, true);
    //nc->flags |= MG_F_SEND_AND_CLOSE;
    return 0;
}

static esp_ota_handle_t ota_handle;
static bool ota_handle_valid = false;
static const esp_partition_t *update_partition = NULL;
static uint32_t ota_size;
static bool ota_success = false;

/* Server the results page after the OTA completes/fails */
static int mongoose_serve_ota_result(struct mg_connection *nc){
    char *htmlstr = malloc(strlen(uploadsuccess) + 100);
    printf("Report ota result %s\n", ota_success == true ? "success" : "fail");
    if(ota_success == true)
        sprintf(htmlstr, uploadsuccess, ota_size);
    else
        sprintf(htmlstr, uploadfailed);
    mongoose_serve_content(nc, htmlstr, true);
    free(htmlstr);
    //nc->flags |= MG_F_SEND_AND_CLOSE;
    return 0;
}

/* Get an argument value from the url query-string */
static char *getqueryarg(const char *argstr, const char *argname){
    char *tmpptr, *endptr, *retptr = NULL;
    if((tmpptr = strstr(argstr, argname)) != NULL){
        while(*tmpptr != '=' && *tmpptr != 0)
            tmpptr++;
        if(*tmpptr != 0){
            int querylen;
            tmpptr++;
            endptr = tmpptr + 1;
            while(*endptr != '&' && *endptr != 0)
                endptr++;
            querylen = (int)endptr - (int)tmpptr;
            retptr = (char *)malloc(querylen + 1);
            strncpy(retptr, tmpptr, querylen);
            retptr[querylen] = 0;
        }
    }
    return retptr;
}

/**
 * Handle mongoose events.  These are mostly requests to process incoming
 * browser requests.
 */
static void mongoose_event_handler(struct mg_connection *nc, int ev, void *evData, void *fn_data) {
    //ESP_LOGI(tag, "- Event: %s", mongoose_eventToString(ev));
    switch (ev) {
        case MG_EV_HTTP_MSG: {
            struct mg_http_message *message = (struct mg_http_message *) evData;
            char *uri = mgStrToStr(message->uri);
            char *query = mgStrToStr(message->query);
            ESP_LOGI(tag, "http request uri: %s", uri);
            if(query != NULL)
                ESP_LOGI(tag, "http query: %s", query);
            /* ReST API set command */
            if (strcmp(uri, "/set") ==0 ) {
                char *devstr, *cmdstr, *valstr;
                char request[50];
                
                devstr = getqueryarg(query, "device");
                cmdstr = getqueryarg(query, "command");
                valstr = getqueryarg(query, "value");
                if(devstr != NULL && cmdstr != NULL){
                    if(valstr != NULL)
                        sprintf(request, "%s %s %s", devstr, cmdstr, valstr);
                    else
                        sprintf(request, "%s %s", devstr, cmdstr);
                    ESP_LOGI(tag, "Http set command %s\n", request);
                    if(handle_request(request) == 0){
                        mg_http_reply(nc, 200, 0, "Content-Type: text/plain\n", "");
                    }else{
                        mg_http_reply(nc, 400, 0, "Content-Type: text/plain\n", "");
                    }
                }else{
                    mg_http_reply(nc, 400, 0, "Content-Type: text/plain\n", "");
                }
                if(devstr != NULL)
                    free(devstr);
                if(cmdstr != NULL)
                    free(cmdstr);
                if(valstr != NULL)
                    free(valstr);
                //nc->flags |= MG_F_SEND_AND_CLOSE;
            }else if (strcmp(uri, "/") == 0) {
                if(sta_configured == false)
                    mongoose_serve_config_page(nc);
                else
                    mongoose_serve_status(nc);
            }else if(strcmp(uri, "/configSubmit") == 0) {
                char newpass[PASSWORD_SIZE];
                ESP_LOGD(tag, "- body: %.*s", message->body.len, message->body.ptr);
                //connection_info_t connectionInfo;
                mg_http_get_var(&message->body, "ssid",	connectionInfo.ssid, SSID_SIZE);
                newpass[0] = 0;
                mg_http_get_var(&message->body, "password", newpass, PASSWORD_SIZE);
                if(strlen(newpass) > 0){
                    strcpy(connectionInfo.password, newpass);
                    ESP_LOGI(tag, "Set STA password to %s", newpass);
                }
                mg_http_get_var(&message->body, "mqtturl", connectionInfo.mqtturl, SSID_SIZE);
                mg_http_get_var(&message->body, "mqttuser", connectionInfo.mqttuser, SSID_SIZE);
                newpass[0] = 0;
                mg_http_get_var(&message->body, "mqttpass", newpass, PASSWORD_SIZE);
                if(strlen(newpass) > 0){
                    strcpy(connectionInfo.mqttpass, newpass);
                    ESP_LOGI(tag, "Set MQTT password to %s", newpass);
                }
                mg_http_get_var(&message->body, "mqttid", connectionInfo.mqttid, ID_SIZE);
                
                connectionInfo.ntpenabled = 0;
                char enabled[10];
                mg_http_get_var(&message->body, "ntpenabled", enabled, 10);
                if(strncmp(enabled, "true", 4) == 0)
                    connectionInfo.ntpenabled = 1;
                mg_http_get_var(&message->body, "ntpserver1", connectionInfo.ntpserver, SERVER_SIZE);
                mg_http_get_var(&message->body, "ntpserver2", connectionInfo.ntpserver2, SERVER_SIZE);
                mg_http_get_var(&message->body, "ntptimezone", connectionInfo.ntptimezone, SNTP_TIMEZONE_SIZE);

                char ipBuf[20];
                if (mg_http_get_var(&message->body, "ip", ipBuf, sizeof(ipBuf)) > 0) {
                    inet_pton(AF_INET, ipBuf, &connectionInfo.ipInfo.ip);
                }else{
                    connectionInfo.ipInfo.ip.addr = 0;
                }
                if (mg_http_get_var(&message->body, "gw", ipBuf, sizeof(ipBuf)) > 0) {
                    inet_pton(AF_INET, ipBuf, &connectionInfo.ipInfo.gw);
                }else{
                    connectionInfo.ipInfo.gw.addr = 0;
                }
                if (mg_http_get_var(&message->body, "netmask", ipBuf, sizeof(ipBuf)) > 0) {
                    inet_pton(AF_INET, ipBuf, &connectionInfo.ipInfo.netmask);
                }else{
                    connectionInfo.ipInfo.netmask.addr = 0;
                }
                mg_http_get_var(&message->body, "dns1ip", connectionInfo.dnsservers[0], SERVER_SIZE);
                mg_http_get_var(&message->body, "dns2ip", connectionInfo.dnsservers[1], SERVER_SIZE);

                ESP_LOGI(tag, "ssid: %s, password: %s", connectionInfo.ssid, connectionInfo.password);

                saveConnectionInfo(&connectionInfo);
                
                if(sta_configured == false){
                    ESP_LOGI(tag, "Config applied while in AP mode - switch to STA");
                    mg_http_reply(nc, 200, 0, "Content-Type: text/plain\n", "");
                    bootWiFi2();
                }else{
                    ESP_LOGI(tag, "Config applied in STA mode - reboot");
                    mongoose_serve_content(nc, (char *)rebooting, false);
                    schedule_reboot();
                }
                //nc->flags |= MG_F_SEND_AND_CLOSE;
            }else if(strcmp(uri, "/sendCommand") == 0){
                char devstr[19];
                char cmdstr[16];
                char valstr[15];
                char request[50];
                mg_http_get_var(&message->body, "device", devstr, 18);
                mg_http_get_var(&message->body, "command", cmdstr, 15);
                mg_http_get_var(&message->body, "value", valstr, 14);
                sprintf(request, "%s %s %s", devstr, cmdstr, valstr);
                if(handle_request(request) == 0){
                    mongoose_serve_content(nc, (char *)commandsubmitted, true);
                }else{
                    mongoose_serve_content(nc, (char *)commanderror, true);
                }
                //nc->flags |= MG_F_SEND_AND_CLOSE;
			}else if(strcmp(uri, "/getdevices") == 0){
                mongoose_serve_device_list(nc);
            }else if(strcmp(uri, "/status") == 0){
                mongoose_serve_status(nc);
            }else if(strcmp(uri, "/scan") == 0){
                start_scan();
                mongoose_serve_content(nc, (char *)scanning, true);
                //nc->flags |= MG_F_SEND_AND_CLOSE;
            }else if(strcmp(uri, "/upload") == 0){
                mongoose_serve_upload(nc);
            //}else if(strcmp(uri, "/otaupload") == 0){
                /* Response to upload wait for 1 second before refreshing with the status page to allow update pass/fail to be decided */
            //    mongoose_serve_content(nc, (char *)uploadcomplete, true);
            //    nc->is_draining = 1;
                //nc->flags |= MG_F_SEND_AND_CLOSE;
            }else if(strcmp(uri, "/otastatus") == 0){
                mongoose_serve_ota_result(nc);
            }else if(strcmp(uri, "/command") == 0){    
                mongoose_serve_command_list(nc);
            }else if(strcmp(uri, "/viewlog") == 0){
                mongoose_serve_log(nc);
            }else if(strcmp(uri, "/restartnow") == 0){  
                mongoose_serve_content(nc, (char *)rebooting, false);
                //nc->flags |= MG_F_SEND_AND_CLOSE;
                schedule_reboot();
            }
            // Else ... unknown URL
            else {
                mongoose_serve_config_page(nc);
                //mg_send_head(nc, 404, 0, "Content-Type: text/plain");
                //nc->flags |= MG_F_SEND_AND_CLOSE;
            }
            //nc->flags |= MG_F_SEND_AND_CLOSE;
            free(uri);
            if(query != NULL)
                free(query);
            break; 
            
        } // MG_EV_HTTP_REQUEST
        case MG_EV_HTTP_CHUNK: {
            struct mg_http_message *message = (struct mg_http_message *) evData;
            char *uri = mgStrToStr(message->uri);
            //ESP_LOGI(tag, "httpchunk - uri: %s", uri);
            if(strcmp(uri, "/otaupload") == 0){
                esp_err_t err;
                static int remlen = 0;
                int writelen = message->chunk.len;
                uint8_t *start = (uint8_t *)message->chunk.ptr;
                /* MG_EV_HTTP_CHUNK received for each chunk */
                
                if(update_partition == NULL){
                    remlen = message->body.len - writelen;
                    /* On first chunk */
                    update_partition = esp_ota_get_next_update_partition(NULL);
                    ota_handle_valid = false;
                    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
                    if(err == ESP_OK)
                        ota_handle_valid = true;
                    ota_size = 0;
                    ota_success = false;
                    //ESP_LOGI(tag, "Start of file:");
                    //for(int i=0; i < 30; i++)
                    //    ESP_LOGI(tag, "%2X %2x %2x %2x %2x", message->body.p[i * 5], message->body.p[(i * 5) + 1], message->body.p[(i * 5) + 2], message->body.p[(i * 5) + 3], message->body.p[(i * 5) + 4]);
                    if(writelen >= 2 && start[0] == 0x2d && start[1] == 0x2d){
                        writelen -= 2;
                        /* Assume this is a multi-part header */
                        start += 2;
                        ESP_LOGI(tag, "Skipping multi-part header");
                        while(writelen >= 4 && strncmp((char *)start, "\r\n\r\n", 4) != 0){
                            writelen--;
                            start++;
                        }
                        if(writelen < 4){
                            writelen = 0;
                        }else{
                            writelen -= 4;
                            start += 4;
                        }
                    }
                }else{
                    remlen -= writelen;
                }
                if(writelen > 0){
                    if(ota_handle_valid == true){
                        /* For each chunk */
                        //ESP_LOGI(tag, "write %d bytes", writelen);
                        ota_size += writelen;
                        err = esp_ota_write(ota_handle, start, writelen);
                        switch(err){
                            case ESP_ERR_OTA_VALIDATE_FAILED:
                                ESP_LOGI(tag, "Image validate failed");
                                err = esp_ota_end(ota_handle);
                                ota_handle_valid = false;
                                break;
                            case ESP_OK:
                            default:
                                break;
                        }
                    }
                }
                mg_http_delete_chunk(nc, message);
                //ESP_LOGI(tag, "Remaining %d", remlen);
                if(remlen <= 0){
                    //ESP_LOGI(tag, "Bodylen is %d, Msglen is %d", message->body.len, message->message.len);
                    if(ota_handle_valid == true){
                        ESP_LOGI(tag, "OTA complete - %d bytes received", ota_size);
                        ota_success = true;
                        /* If ota_handle is set */
                        err = esp_ota_end(ota_handle);
                        ota_handle_valid = false;
                        err = esp_ota_set_boot_partition(update_partition);
                    }
                    update_partition = NULL;
                    mongoose_serve_content(nc, (char *)uploadcomplete, true);
                    nc->is_draining = 1;
                }
            }else{
                mg_http_delete_chunk(nc, message);
            }
            free(uri);
            break;
        } // MG_EV_HTTP_CHUNK
        default:
            //if(ev != 0 && ev != MG_EV_POLL)
            //    ESP_LOGI(tag, "Event %x", ev);
            break;
    } // End of switch
} // End of mongoose_event_handler


// FreeRTOS task to start Mongoose.
static void mongooseTask(void *data) {
    struct mg_mgr mgr;
    struct mg_connection *connection;

    ESP_LOGD(tag, ">> mongooseTask");
    g_mongooseStopRequest = 0; // Unset the stop request since we are being asked to start.

    mg_mgr_init(&mgr);
    connection = mg_http_listen(&mgr, "http://0.0.0.0:80", mongoose_event_handler, NULL);
    if (connection == NULL) {
        ESP_LOGE(tag, "No connection from the mg_bind().");
        mg_mgr_free(&mgr);
        ESP_LOGD(tag, "<< mongooseTask");
        vTaskDelete(NULL);
        return;
    }

    // Keep processing until we are flagged that there is a stop request.
    while (!g_mongooseStopRequest) {
        mg_mgr_poll(&mgr, 1000);
    }

    // We have received a stop request, so stop being a web server.
    mg_mgr_free(&mgr);
    g_mongooseStarted = 0;

#ifdef OLD_MODE
    // Since we HAVE ended mongoose, time to invoke the callback.
    if (g_callback) {
        g_callback(1);
    }
#endif

    ESP_LOGD(tag, "<< mongooseTask");
    vTaskDelete(NULL);
    return;
} // mongooseTask

#define MAXCONNATTEMPTS 25
static int connattempts = 0;
//static connection_info_t connectionInfo;
static void becomeStation(connection_info_t *pConnectionInfo);
static void becomeAccessPoint();

static int setStatusLed(int on) {
#if defined(CONFIG_ENABLE_STATUS_LED) && defined(CONFIG_STATUS_LED_GPIO)
    gpio_pad_select_gpio(CONFIG_STATUS_LED_GPIO);
    gpio_set_direction(CONFIG_STATUS_LED_GPIO, GPIO_MODE_OUTPUT);
    return gpio_set_level(CONFIG_STATUS_LED_GPIO, on);
#else
    return 1; 
#endif
}

/**
 * An ESP32 WiFi event handler.
 * The types of events that can be received here are:
 *
 * SYSTEM_EVENT_AP_PROBEREQRECVED
 * SYSTEM_EVENT_AP_STACONNECTED
 * SYSTEM_EVENT_AP_STADISCONNECTED
 * SYSTEM_EVENT_AP_START
 * SYSTEM_EVENT_AP_STOP
 * SYSTEM_EVENT_SCAN_DONE
 * SYSTEM_EVENT_STA_AUTHMODE_CHANGE
 * SYSTEM_EVENT_STA_CONNECTED
 * SYSTEM_EVENT_STA_DISCONNECTED
 * SYSTEM_EVENT_STA_GOT_IP
 * SYSTEM_EVENT_STA_START
 * SYSTEM_EVENT_STA_STOP
 * SYSTEM_EVENT_WIFI_READY
 */

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data){
    switch(event_id) {
        // When we have started being an access point, then start being a web server.
        case WIFI_EVENT_AP_START: { // Handle the AP start event
            //tcpip_adapter_ip_info_t ip_info;
            esp_netif_ip_info_t ip_info;
            //tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info);
            esp_netif_get_ip_info(ap_netif, &ip_info);
            ESP_LOGI(tag, "**********************************************");
            ESP_LOGI(tag, "* We are now an access point and you can point");
            ESP_LOGI(tag, "* your browser to http://" IPSTR, IP2STR(&ip_info.ip));
            ESP_LOGI(tag, "**********************************************");
            // Start Mongoose ...
            if (!g_mongooseStarted){
                g_mongooseStarted = 1;
                xTaskCreatePinnedToCore(&mongooseTask, "bootwifi_mongoose_task", 8000, NULL, 5, NULL, 0);
            }
            /* If we have conninfo allow the timer to restart station mode in a little while */
            if (haveconninfo == true && g_callback) {
                g_callback(0);
            }
            break;
        } // WIFI_EVENT_AP_START

        // If we fail to connect to an access point as a station, become an access point.
        case WIFI_EVENT_STA_DISCONNECTED: {
            ESP_LOGD(tag, "Station disconnected started");
            // We think we tried to connect as a station and failed! ... become
            // an access point.
            if(connattempts++ > MAXCONNATTEMPTS){
                connattempts = 0;
                setStatusLed(1);
                becomeAccessPoint();
            }else{
                setStatusLed(0);
                becomeStation(&connectionInfo);
            }
            break;
        } // WIFI_EVENT_AP_START

        // If we connected as a station then we are done and we can stop being a web server.
        case IP_EVENT_STA_GOT_IP: {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
            const esp_netif_ip_info_t *ip_info = &event->ip_info;
            ESP_LOGI(tag, "********************************************");
            ESP_LOGI(tag, "* We are now connected and ready to do work!");
            ESP_LOGI(tag, "* - Our IP address is: " IPSTR, IP2STR(&ip_info->ip));
            ESP_LOGI(tag, "********************************************");
            connattempts = 0;

#ifdef OLD_MODE
            g_mongooseStopRequest = 1; // Stop mongoose (if it is running).
            // Invoke the callback if Mongoose has NOT been started ... otherwise
            // we will invoke the callback when mongoose has ended.
            if (!g_mongooseStarted) {
                if (g_callback) {
                    g_callback(1);
                }
            } // Mongoose was NOT started
#else
            if (!g_mongooseStarted){
                g_mongooseStarted = 1;
                xTaskCreatePinnedToCore(&mongooseTask, "bootwifi_mongoose_task", 8000, NULL, 5, NULL, 0);
            }
            if (g_callback) {
                g_callback(1);
            }
#endif
            break;
        } // IP_EVENT_STA_GOTIP

        default: // Ignore the other event types
            break;
    } // Switch event
} // esp32_wifi_eventHandler

/* Retrieve the connection info.  A rc == 0 means ok. */
int getConnectionInfo(connection_info_t *pConnectionInfo) {
    nvs_handle handle;
    size_t size;
    esp_err_t err;
    uint32_t version;
    err = nvs_open(BOOTWIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err != 0) {
        ESP_LOGE(tag, "nvs_open: %x", err);
        return -1;
    }

    /* Default the newer parameters in case nvs still contains old config */
    pConnectionInfo->ntptimezone[0] = 0;
    pConnectionInfo->ntpenabled = 0;
    sprintf(pConnectionInfo->ntpserver, "pool.ntp.org");
    pConnectionInfo->ntpserver2[0] = 0;
    pConnectionInfo->dnsservers[0][0] = 0;
    pConnectionInfo->dnsservers[1][0] = 0;

    // Get the version that the data was saved against.
    err = nvs_get_u32(handle, KEY_VERSION, &version);
    if (err != ESP_OK) {
        ESP_LOGI(tag, "No version record found (%d).", err);
        nvs_close(handle);
        return -1;
    }

    err = nvs_get_blob(handle, KEY_CONNECTION_INFO, NULL, &size);
    if (err != ESP_OK) {
        ESP_LOGE(tag, "No connection record found (%d).", err);
        nvs_close(handle);
        return -1;
    }
    if ((version & 0xff00) != (g_version & 0xff00)) {
        ESP_LOGI(tag, "Config versions differ ... current is %x, found is %x", g_version, version);
        /* Check for previous config structure */
        ESP_LOGI(tag, "Older version of config (%x), found. New options will be default. Please re-save", version);
        if ((version & 0xff00) == (g_v1_version & 0xff00)) {
            size = V1_CONNECTION_INFO_SIZE;
        }else if ((version & 0xff00) == (g_v2_version & 0xff00)) {
            size = V2_CONNECTION_INFO_SIZE;
        }else{
            ESP_LOGI(tag, "Incompatible versions ... current is %x, found is %x", g_version, version);
            nvs_close(handle);
            return -1;
        }
    }else{
        ESP_LOGI(tag, "Config version %x found", g_version);
        size = sizeof(connection_info_t);
    }

    err = nvs_get_blob(handle, KEY_CONNECTION_INFO, pConnectionInfo, &size);
    if (err != ESP_OK) {
        ESP_LOGI(tag, "No connection record found (%d).", err);
        nvs_close(handle);
        return -1;
    }
    /* If the ntpserver is not configured flag ntp as disabled */
    if(pConnectionInfo->ntpserver[0] == 0)
        pConnectionInfo->ntpenabled = 0;

    // Cleanup
    nvs_close(handle);

    // Do a sanity check on the SSID
    if (strlen(pConnectionInfo->ssid) == 0) {
        ESP_LOGI(tag, "NULL ssid detected");
        return -1;
    }
    return 0;
} // getConnectionInfo

/* Save our connection info for retrieval on a subsequent restart. */
static void saveConnectionInfo(connection_info_t *pConnectionInfo) {
    nvs_handle handle;
    ESP_ERROR_CHECK(nvs_open(BOOTWIFI_NAMESPACE, NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_set_blob(handle, KEY_CONNECTION_INFO, pConnectionInfo, sizeof(connection_info_t)));
    ESP_ERROR_CHECK(nvs_set_u32(handle, KEY_VERSION, g_version));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
} // setConnectionInfo

/* Become a station connecting to an existing access point. */
static void becomeStation(connection_info_t *pConnectionInfo) {
    ESP_LOGI(tag, "- Connecting to access point \"%s\" ...", pConnectionInfo->ssid);
    assert(strlen(pConnectionInfo->ssid) > 0);
    
    /* If this is a retry don't re-initialise sta mode */
    if(sta_configured == false){
        esp_netif_dns_info_t dnsaddr;

        ESP_ERROR_CHECK(esp_wifi_stop());
        /* If we have a static IP address information, use that. */
        if (pConnectionInfo->ipInfo.ip.addr != 0) {
            ESP_LOGI(tag, " - using a static IP address of " IPSTR, IP2STR(&pConnectionInfo->ipInfo.ip));
            esp_netif_dhcpc_stop(sta_netif);
            esp_netif_set_ip_info(sta_netif, &pConnectionInfo->ipInfo);
        } else {
            esp_netif_dhcpc_start(sta_netif);
        }
        if(pConnectionInfo->dnsservers[0][0] != 0){
            inet_pton(AF_INET, pConnectionInfo->dnsservers[0], &dnsaddr.ip.u_addr.ip4.addr);
            dnsaddr.ip.type = ESP_IPADDR_TYPE_V4;
            ESP_LOGI(tag, " - using a static DNS address of %s", pConnectionInfo->dnsservers[0]);
            esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dnsaddr);
        }
        if(pConnectionInfo->dnsservers[1][0] != 0){
            inet_pton(AF_INET, pConnectionInfo->dnsservers[1], &dnsaddr.ip.u_addr.ip4.addr);
            dnsaddr.ip.type = ESP_IPADDR_TYPE_V4;
            ESP_LOGI(tag, " - using a static DNS2 address of %s", pConnectionInfo->dnsservers[1]);
            esp_netif_set_dns_info(sta_netif, ESP_NETIF_DNS_BACKUP, &dnsaddr);
        }
        if(strlen(pConnectionInfo->mqttid) == 0){
            esp_netif_set_hostname(sta_netif, "EQ3-heatcontroller");
            ESP_LOGI(tag, "Hostname EQ3-heatcontroller");
        }else{
            esp_netif_set_hostname(sta_netif, pConnectionInfo->mqttid);
            ESP_LOGI(tag, "Hostname %s", pConnectionInfo->mqttid);
        }
        ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA));
        wifi_config_t sta_config;
        memset(&sta_config, 0, sizeof(wifi_config_t));
        sta_config.sta.bssid_set = 0;
        memcpy(sta_config.sta.ssid, pConnectionInfo->ssid, SSID_SIZE);
        memcpy(sta_config.sta.password, pConnectionInfo->password, PASSWORD_SIZE);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
        sta_configured = true;
        ESP_ERROR_CHECK(esp_wifi_start());
    }
    ESP_ERROR_CHECK(esp_wifi_connect());
} // becomeStation

/* Become an access point. */
static void becomeAccessPoint() {
    ESP_LOGI(tag, "- Starting being an access point ...");
    // We don't have connection info so be an access point!
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    sta_configured = false;
    wifi_config_t apConfig = {
        .ap = {
            .ssid="HeatingController",
            .ssid_len=0,
#ifdef CONFIG_APMODE_PASSWORD
            .password=CONFIG_APMODE_PASSWORD,
            .authmode=WIFI_AUTH_WPA2_PSK,
#else
            .password="password",
            .authmode=WIFI_AUTH_OPEN,
#endif
            .channel=0,
            .ssid_hidden=0,
            .max_connection=4,
            .beacon_interval=100
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &apConfig));
    ESP_ERROR_CHECK(esp_wifi_start());
} // becomeAccessPoint

/**
 * Retrieve the signal level on the OVERRIDE_GPIO pin.  This is used to
 * indicate that we should not attempt to connect to any previously saved
 * access point we may know about.
 */

static int checkOverrideGpio() {
#if defined(CONFIG_ENABLE_AP_OVERRIDE_GPIO) && defined(CONFIG_BOOTWIFI_OVERRIDE_GPIO)
    gpio_pad_select_gpio(CONFIG_BOOTWIFI_OVERRIDE_GPIO);
    gpio_set_direction(CONFIG_BOOTWIFI_OVERRIDE_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(CONFIG_BOOTWIFI_OVERRIDE_GPIO, GPIO_PULLUP_ONLY);
    return gpio_get_level(CONFIG_BOOTWIFI_OVERRIDE_GPIO);
#else
    return 1; // If no boot override, return high
#endif
} // checkOverrideGpio

static void bootWiFi2() {
    ESP_LOGD(tag, ">> bootWiFi2");
    // Check for a GPIO override which occurs when a physical Pin is high
    // during the test.  This can force the ability to check for new configuration
    // even if the existing configured access point is available.
    if (checkOverrideGpio() == 0) {
        setStatusLed(1);
        ESP_LOGI(tag, "- GPIO override detected");
        becomeAccessPoint();
    } else {
        // There was NO GPIO override, proceed as normal.  This means we retrieve
        // our stored access point information of the access point we should connect
        // against.  If that information doesn't exist, then again we become an
        // access point ourselves in order to allow a client to connect and bring
        // up a browser.
        int rc = getConnectionInfo(&connectionInfo);
        if (rc == 0) {
            // We have received connection information, let us now become a station
            // and attempt to connect to the access point.
            ESP_LOGI(tag, "Network config present - becoming client");
            setStatusLed(0);
            haveconninfo = true;
            becomeStation(&connectionInfo);
            if(g_parms != NULL)
                g_parms(connectionInfo.mqtturl, connectionInfo.mqttuser, connectionInfo.mqttpass, connectionInfo.mqttid);

        } else {
            // We do NOT have connection information.  Let us now become an access
            // point that serves up a web server and allow a browser user to specify
            // the details that will be eventually used to allow us to connect
            // as a station.
            ESP_LOGI(tag, "No network config - becoming AP");
            setStatusLed(1);
            becomeAccessPoint();
        } // We do NOT have connection info
    }
    ESP_LOGD(tag, "<< bootWiFi2");
} // bootWiFi2

void restart_station(void){
    setStatusLed(0);
    becomeStation(&connectionInfo);
}

/* Main entry into bootWiFi */
void bootWiFi(bootwifi_callback_t callback, bootwifi_parms_t parmcb) {
    ESP_LOGD(tag, ">> bootWiFi");
    g_callback = callback;
    g_parms = parmcb;
    nvs_flash_init();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ap_netif = esp_netif_create_default_wifi_ap();
    sta_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    bootWiFi2();

    ESP_LOGD(tag, "<< bootWiFi");
} // bootWiFi

/* Is ntp enabled */
bool ntp_enabled(void){
    return connectionInfo.ntpenabled == 0 ? false : true;
}

/* Get ntp server details */
char *getntpserver(int idx){
    if(idx > 0)
        return connectionInfo.ntpserver2;
    return connectionInfo.ntpserver;
}

/* Get the timezone */
char *getntptimezone(void){
    return connectionInfo.ntptimezone;
}
