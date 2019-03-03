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
#include <esp_log.h>
#include <esp_err.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_event_loop.h>
#include <esp_wifi.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <tcpip_adapter.h>
#include <lwip/sockets.h>
#include <mongoose.h>
#include "eq3_bootwifi.h"
#include "sdkconfig.h"
#include "eq3_gap.h"
#include "eq3_wifi.h"

/* Webcontent */
#include "eq3_htmlpages.h"

// If the structure of a record saved for a subsequent reboot changes
// then consider using semver to change the version number or else
// we may try and boot with the wrong data.
#define KEY_VERSION "version"
uint32_t g_version=0x0100;

#define KEY_CONNECTION_INFO "connectionInfo" // Key used in NVS for connection info
#define BOOTWIFI_NAMESPACE "bootwifi" // Namespace in NVS for bootwifi
#define SSID_SIZE (32) // Maximum SSID size
#define USERNAME_SIZE (64) // Maximum username size
#define PASSWORD_SIZE (64) // Maximum password size
#define ID_SIZE       (32)
#define MAX_URL_SIZE  (256) // Maximum url length

typedef struct {
	char ssid[SSID_SIZE];
	char password[PASSWORD_SIZE];
	char mqtturl[MAX_URL_SIZE];
	char mqttuser[USERNAME_SIZE];
	char mqttpass[PASSWORD_SIZE];
	char mqttid[ID_SIZE];
	tcpip_adapter_ip_info_t ipInfo; // Optional static IP information
} connection_info_t;

static connection_info_t connectionInfo;
int getConnectionInfo(connection_info_t *pConnectionInfo);

// Forward declarations
static void saveConnectionInfo(connection_info_t *pConnectionInfo);
static void becomeAccessPoint();
static void bootWiFi2();
static bool haveconninfo = false;
static char tag[] = "websrv";

static bootwifi_callback_t g_callback = NULL; // Callback function to be invoked when we have finished.
static bootwifi_parms_t g_parms = NULL;

bool sta_configured = false; // Are we in sta mode?


/* ===================== Mongoose webserver code ========================= */

static int g_mongooseStarted = 0; // Has the mongoose server started?
static int g_mongooseStopRequest = 0; // Request to stop the mongoose server.

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
		case MG_EV_MQTT_CONNACK_ACCEPTED:			return "MG_EV_MQTT_CONNACK";
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

// Convert a Mongoose string type to a string.
static char *mgStrToStr(struct mg_str mgStr) {
	char *retStr = (char *) malloc(mgStr.len + 1);
	memcpy(retStr, mgStr.p, mgStr.len);
	retStr[mgStr.len] = 0;
	return retStr;
} // mgStrToStr

/* Serve the configuration webpage */
static int mongoose_serve_config_page(struct mg_connection *nc){
    int rc = getConnectionInfo(&connectionInfo);
    char *htmlstr = malloc(strlen(selectap) + 100);
    const char nullstr[] = "";
    char *sptr = (char *)nullstr, *pptr = (char *)nullstr, *murlptr = (char *)nullstr, *muserptr = (char *)nullstr, *mpassptr = (char *)nullstr, *midptr = (char *)nullstr;
    char *ibuf = (char *)nullstr, *gbuf = (char *)nullstr, *mbuf = (char *)nullstr;
    char ipbuf[20], gwbuf[20], maskbuf[20];
    if(rc == 0){
        sptr = connectionInfo.ssid;
        pptr = connectionInfo.password;
        murlptr = connectionInfo.mqtturl;
        muserptr = connectionInfo.mqttuser;
        mpassptr = connectionInfo.mqttpass; 
        midptr = connectionInfo.mqttid;
        ibuf = inet_ntop(AF_INET, &connectionInfo.ipInfo.ip, ipbuf, sizeof(ipbuf));
        gbuf = inet_ntop(AF_INET, &connectionInfo.ipInfo.gw, gwbuf, sizeof(gwbuf));
        mbuf = inet_ntop(AF_INET, &connectionInfo.ipInfo.netmask, maskbuf, sizeof(maskbuf));
    }
    sprintf(htmlstr, selectap, sptr, pptr, murlptr, muserptr, mpassptr, midptr, ibuf == NULL ? nullstr : ipbuf, gbuf == NULL ? nullstr : gbuf, mbuf == NULL ? nullstr : mbuf);
    mg_send_head(nc, 200, strlen(htmlstr), "Content-Type: text/html");
	mg_send(nc, htmlstr, strlen(htmlstr));
    nc->flags |= MG_F_SEND_AND_CLOSE;
    return 0;
}

/* Serve a found device list page */
static int mongoose_serve_device_list(struct mg_connection *nc){
    int wridx = 0;
    struct found_device *devwalk = NULL;
    int numdevices;
    
    if(eq3gap_get_device_list(&devwalk, &numdevices) == EQ3_SCAN_COMPLETE){
        
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
        
            mg_send_head(nc, 200, wridx, "Content-Type: text/html");
            mg_send(nc, devlisthtml, wridx);
        
            free(devlisthtml);
        }else{
            mg_send_head(nc, 501, 0, "Content-Type: text/html");
        }
    }else{
        /* No devices found */
        mg_send_head(nc, 200, 16, "Content-Type: text/html");
        mg_send(nc, "No devices found", 16);
    }
    nc->flags |= MG_F_SEND_AND_CLOSE;
    return 0;
}

static int mongoose_serve_status(struct mg_connection *nc){
    if(sta_configured == false){
        mg_send_head(nc, 200, strlen(apstatus), "Content-Type: text/html");
        mg_send(nc, apstatus, strlen(apstatus));
        nc->flags |= MG_F_SEND_AND_CLOSE;
    }else{
        bool connected = ismqttconnected();
        char *htmlstr = malloc(strlen(connectedstatus) + 100);
        sprintf(htmlstr, connectedstatus, connectionInfo.mqtturl, connectionInfo.mqttuser, connectionInfo.mqttpass, connectionInfo.mqttid, connected == true ? "connected" : "not connected");
        mg_send_head(nc, 200, strlen(htmlstr), "Content-Type: text/html");
        mg_send(nc, htmlstr, strlen(htmlstr));
        nc->flags |= MG_F_SEND_AND_CLOSE;
    }            
    return 0;
}

/**
 * Handle mongoose events.  These are mostly requests to process incoming
 * browser requests.  The ones we handle are:
 * GET / - Send the enter details page.
 * GET /set - Set the connection info (REST request).
 * POST /ssidSelected - Set the connection info (HTML FORM).
 */
static void mongoose_event_handler(struct mg_connection *nc, int ev, void *evData) {
	ESP_LOGD(tag, "- Event: %s", mongoose_eventToString(ev));
	switch (ev) {
		case MG_EV_HTTP_REQUEST: {
			struct http_message *message = (struct http_message *) evData;
			char *uri = mgStrToStr(message->uri);
			ESP_LOGD(tag, " - uri: %s", uri);

			if (strcmp(uri, "/set") ==0 ) {
				connection_info_t connectionInfo;
//fix
				saveConnectionInfo(&connectionInfo);
				ESP_LOGD(tag, "- Set the new connection info to ssid: %s, password: %s",
					connectionInfo.ssid, connectionInfo.password);
				mg_send_head(nc, 200, 0, "Content-Type: text/plain");
                nc->flags |= MG_F_SEND_AND_CLOSE;
			}else if (strcmp(uri, "/") == 0) {
                if(sta_configured == false)
				    mongoose_serve_config_page(nc);
                else
                    mongoose_serve_status(nc);
			}else if(strcmp(uri, "/ssidSelected") == 0) {
                // Handle /ssidSelected
			    // This is an incoming form with properties:
			    // * ssid - The ssid of the network to connect against.
			    // * password - the password to use to connect.
			    // * ip - Static IP address ... may be empty
			    // * gw - Static GW address ... may be empty
			    // * netmask - Static netmask ... may be empty
				ESP_LOGD(tag, "- body: %.*s", message->body.len, message->body.p);
				connection_info_t connectionInfo;
				mg_get_http_var(&message->body, "ssid",	connectionInfo.ssid, SSID_SIZE);
				mg_get_http_var(&message->body, "password", connectionInfo.password, PASSWORD_SIZE);

				mg_get_http_var(&message->body, "mqtturl", connectionInfo.mqtturl, SSID_SIZE);
				mg_get_http_var(&message->body, "mqttuser", connectionInfo.mqttuser, SSID_SIZE);
				mg_get_http_var(&message->body, "mqttpass", connectionInfo.mqttpass, SSID_SIZE);
				mg_get_http_var(&message->body, "mqttid", connectionInfo.mqttid, ID_SIZE);
				
				char ipBuf[20];
				if (mg_get_http_var(&message->body, "ip", ipBuf, sizeof(ipBuf)) > 0) {
					inet_pton(AF_INET, ipBuf, &connectionInfo.ipInfo.ip);
				} else {
					connectionInfo.ipInfo.ip.addr = 0;
				}

				if (mg_get_http_var(&message->body, "gw", ipBuf, sizeof(ipBuf)) > 0) {
					inet_pton(AF_INET, ipBuf, &connectionInfo.ipInfo.gw);
				}
				else {
					connectionInfo.ipInfo.gw.addr = 0;
				}

				if (mg_get_http_var(&message->body, "netmask", ipBuf, sizeof(ipBuf)) > 0) {
					inet_pton(AF_INET, ipBuf, &connectionInfo.ipInfo.netmask);
				}
				else {
					connectionInfo.ipInfo.netmask.addr = 0;
				}

				ESP_LOGD(tag, "ssid: %s, password: %s", connectionInfo.ssid, connectionInfo.password);

				mg_send_head(nc, 200, 0, "Content-Type: text/plain");
				saveConnectionInfo(&connectionInfo);
				bootWiFi2();
                nc->flags |= MG_F_SEND_AND_CLOSE;
			}else if(strcmp(uri, "/getdevices") == 0){
                mongoose_serve_device_list(nc);
            }else if(strcmp(uri, "/status") == 0){
                mongoose_serve_status(nc);
            }
			// Else ... unknown URL
			else {
                mongoose_serve_config_page(nc);
				//mg_send_head(nc, 404, 0, "Content-Type: text/plain");
                //nc->flags |= MG_F_SEND_AND_CLOSE;
			}
			//nc->flags |= MG_F_SEND_AND_CLOSE;
			free(uri);
			break;
		} // MG_EV_HTTP_REQUEST
	} // End of switch
} // End of mongoose_event_handler


// FreeRTOS task to start Mongoose.
static void mongooseTask(void *data) {
	struct mg_mgr mgr;
	struct mg_connection *connection;

	ESP_LOGD(tag, ">> mongooseTask");
	g_mongooseStopRequest = 0; // Unset the stop request since we are being asked to start.

	mg_mgr_init(&mgr, NULL);

	connection = mg_bind(&mgr, ":80", mongoose_event_handler);

	if (connection == NULL) {
		ESP_LOGE(tag, "No connection from the mg_bind().");
		mg_mgr_free(&mgr);
		ESP_LOGD(tag, "<< mongooseTask");
		vTaskDelete(NULL);
		return;
	}
	mg_set_protocol_http_websocket(connection);

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
#ifdef STATUS_LED_GPIO
	gpio_pad_select_gpio(STATUS_LED_GPIO);
	gpio_set_direction(STATUS_LED_GPIO, GPIO_MODE_OUTPUT);
	//gpio_set_pull_mode(BOOTWIFI_OVERRIDE_GPIO, GPIO_PULLUP_ONLY);
	return gpio_set_level(STATUS_LED_GPIO, on);
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
static esp_err_t esp32_wifi_eventHandler(void *ctx, system_event_t *event) {
	// Your event handling code here...
	switch(event->event_id) {
		// When we have started being an access point, then start being a web server.
		case SYSTEM_EVENT_AP_START: { // Handle the AP start event
			tcpip_adapter_ip_info_t ip_info;
			tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info);
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
		} // SYSTEM_EVENT_AP_START

		// If we fail to connect to an access point as a station, become an access point.
		case SYSTEM_EVENT_STA_DISCONNECTED: {
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
		} // SYSTEM_EVENT_AP_START

		// If we connected as a station then we are done and we can stop being a
		// web server.
		case SYSTEM_EVENT_STA_GOT_IP: {
			ESP_LOGI(tag, "********************************************");
			ESP_LOGI(tag, "* We are now connected and ready to do work!");
			ESP_LOGI(tag, "* - Our IP address is: " IPSTR, IP2STR(&event->event_info.got_ip.ip_info.ip));
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
		} // SYSTEM_EVENT_STA_GOTIP

		default: // Ignore the other event types
			break;
	} // Switch event

	return ESP_OK;
} // esp32_wifi_eventHandler


/**
 * Retrieve the connection info.  A rc==0 means ok.
 */
//static int getConnectionInfo(connection_info_t *pConnectionInfo) {
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

	// Get the version that the data was saved against.
	err = nvs_get_u32(handle, KEY_VERSION, &version);
	if (err != ESP_OK) {
		ESP_LOGD(tag, "No version record found (%d).", err);
		nvs_close(handle);
		return -1;
	}

	// Check the versions match
	if ((version & 0xff00) != (g_version & 0xff00)) {
		ESP_LOGD(tag, "Incompatible versions ... current is %x, found is %x", version, g_version);
		nvs_close(handle);
		return -1;
	}

	size = sizeof(connection_info_t);
	err = nvs_get_blob(handle, KEY_CONNECTION_INFO, pConnectionInfo, &size);
	if (err != ESP_OK) {
		ESP_LOGD(tag, "No connection record found (%d).", err);
		nvs_close(handle);
		return -1;
	}
	if (err != ESP_OK) {
		ESP_LOGE(tag, "nvs_open: %x", err);
		nvs_close(handle);
		return -1;
	}

	// Cleanup
	nvs_close(handle);

	// Do a sanity check on the SSID
	if (strlen(pConnectionInfo->ssid) == 0) {
		ESP_LOGD(tag, "NULL ssid detected");
		return -1;
	}
	return 0;
} // getConnectionInfo


/**
 * Save our connection info for retrieval on a subsequent restart.
 */
static void saveConnectionInfo(connection_info_t *pConnectionInfo) {
	nvs_handle handle;
	ESP_ERROR_CHECK(nvs_open(BOOTWIFI_NAMESPACE, NVS_READWRITE, &handle));
	ESP_ERROR_CHECK(nvs_set_blob(handle, KEY_CONNECTION_INFO, pConnectionInfo,
			sizeof(connection_info_t)));
	ESP_ERROR_CHECK(nvs_set_u32(handle, KEY_VERSION, g_version));
	ESP_ERROR_CHECK(nvs_commit(handle));
	nvs_close(handle);
} // setConnectionInfo

/**
 * Become a station connecting to an existing access point.
 */
static void becomeStation(connection_info_t *pConnectionInfo) {
	ESP_LOGD(tag, "- Connecting to access point \"%s\" ...", pConnectionInfo->ssid);
	assert(strlen(pConnectionInfo->ssid) > 0);

    if(sta_configured == false){
	    // If we have a static IP address information, use that.
	    if (pConnectionInfo->ipInfo.ip.addr != 0) {
		    ESP_LOGD(tag, " - using a static IP address of " IPSTR, IP2STR(&pConnectionInfo->ipInfo.ip));
		    tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA);
		    tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &pConnectionInfo->ipInfo);
	    } else {
		    tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA);
	    }

        ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA));
        wifi_config_t sta_config;
        sta_config.sta.bssid_set = 0;
        memcpy(sta_config.sta.ssid, pConnectionInfo->ssid, SSID_SIZE);
        memcpy(sta_config.sta.password, pConnectionInfo->password, PASSWORD_SIZE);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
        sta_configured = true;
    //}
    
    ESP_ERROR_CHECK(esp_wifi_start());
    }
    ESP_ERROR_CHECK(esp_wifi_connect());
} // becomeStation


/**
 * Become an access point.
 */
static void becomeAccessPoint() {
	ESP_LOGD(tag, "- Starting being an access point ...");
	// We don't have connection info so be an access point!
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    sta_configured = false;
	wifi_config_t apConfig = {
		.ap = {
			.ssid="HeatingController",
			.ssid_len=0,
			.password="password",
			.channel=0,
			.authmode=WIFI_AUTH_OPEN,
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
#ifdef BOOTWIFI_OVERRIDE_GPIO
	gpio_pad_select_gpio(BOOTWIFI_OVERRIDE_GPIO);
	gpio_set_direction(BOOTWIFI_OVERRIDE_GPIO, GPIO_MODE_INPUT);
	gpio_set_pull_mode(BOOTWIFI_OVERRIDE_GPIO, GPIO_PULLUP_ONLY);
	return gpio_get_level(BOOTWIFI_OVERRIDE_GPIO);
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
		ESP_LOGD(tag, "- GPIO override detected");
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
            ESP_LOGD(tag, "Network config present - becoming client");
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
            ESP_LOGD(tag, "No network config - becoming AP");
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

/**
 * Main entry into bootWiFi
 */
void bootWiFi(bootwifi_callback_t callback, bootwifi_parms_t parmcb) {
	ESP_LOGD(tag, ">> bootWiFi");
	g_callback = callback;
	g_parms = parmcb;
	nvs_flash_init();
	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_init(esp32_wifi_eventHandler, NULL));
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

	bootWiFi2();

	ESP_LOGD(tag, "<< bootWiFi");
} // bootWiFi
