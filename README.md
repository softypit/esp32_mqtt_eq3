EQ-3 Radiator valve control application for ESP-32

Tested on ESP-WROOM-32

EQ-3 radiator valves work really well for a home-automation heating system. They are fully configurable vie BLE as well as their front-panel. There are more options available than the calorBT app makes available.

The main problem with centrally controlling them is the limited range of BLE. This makes it impossible to use a single central-controller to talk to all TRVs in a typical house. Therefore multiple 'hubs' are required at distributed locations.

This application acts as a hub and uses BLE to communicate with EQ-3 TRVs and makes configuration possible via MQTT over WiFi. 

When using calorBT some very basic security is employed. This security however lives in the calorBT application and not in the valve. The EQ-3 valves do not require any authentication from the BLE client to obey commands.

On first use the ESP32 will start in access-point mode appearing as 'HeatingController'. Connect to this AP (password is 'password' or unset) and browse to the device IP address (192.168.4.1). The device configuration arameters can be set here:  

- ssid - the AP to connect to for normal operation  
- password - the password for the AP  
- mqtturl - url to access the mqtt broker  
- mqttuser - mqtt broker username  
- mqttpass - mqtt broker password  
- mqttid - the unique id for this device to use with the mqtt broker  
- ip - fixed IP for WiFi network (leave blank to DHCP)  
- gw - gateway IP for WiFi network (leave blank for DHCP)  
- netmask - netmask for WiFi network (leave blank for DHCP)  

Once connected in WiFi STA mode this application first scans for EQ-3 valves and publishes their addresses and rssi to the MQTT broker.  
A scan can be initiated at any time by publishing to the `/<mqttid>radin/scan` topic.  
Scan results are published to `/<mqttid>radout/devlist` in json format.

Control of valves is carried out by publishing to the <mqttid>radin/trv topic with a payload consisting of:
  `ab:cd:ef:gh:ij:kl <command> [parm]`
where the device is indicated by its bluetooth address (MAC)

commands are: settime, boost, unboost, manual, auto, lock, unlock, offset, settemp.

- settime sets the current time on the valve. 
- boost and unboost set and clear boost mode.
- lock and unlock set and clear the front-panel lock.
- manual mode prevents the valve from using its internal temperature/time program.
- offset sets the room-temperature offset (see EQ-3 instructions for what this means).
- settemp sets the required temperature for the valve to open/close at.

settime has an additional parameter of the hexadecimal encoded durrent time. parm is 12 characters hexadecimal yymmddhhMMss 
e.g. 13010c0c0a00 is 2019/Jan/12 12:00.00

offset and settemp have an additional parameter of the temperature to set this can be -3.5 - +3.5 for offset and
5.0 to 29.5 in 0.5 degree increments.

In response to every successful command a status message is published to /<mqttid>radout/status containing json-encoded details of address, temperature set point, valve open percentage, mode, boost state, lock state and battery state. 
This can be used as an acknowledgement of a successful command to remote mqtt clients.
There is no specific command to poll the status of the valve but using any of the commands to re-set the current value will achieve the required result.
Note: It has been observed that using unboost to poll a valve can result in the valve opening as if in boost mode but without reporting boost mode active on the display or status. 
It is probably not advisable to poll the valve with the unboost command.

On first boot this application uses Kolbans bootwifi code to create the wifi AP.  
Once configuration is complete and on subsequent boots the configured details are used for connection. If connection fails the application reverts to AP mode where the web interface is used to reconfigure. The application can be forced into config mode by pressing and holding the BOOT key AFTER the EN key has been released.

web server is part of Mongoose - https://github.com/cesanta/mongoose
MQTT library is https://github.com/tuanpmt/espmqtt

Testing:  
```
# Connect to a mosquitto broker:
mosquitto_sub -h 127.0.0.1 -p 1883 -t "<mqttid>radout/devlist"  # Will display a list of discovered EQ-3 TRVs  
mosquitto_sub -h 127.0.0.1 -p 1883 -t "<mqttid>radout/status"  # will show a status message each time a trv is contacted  
mosquitto_pub -h 127.0.0.1 -p 1883 -t "<mqttid>radin/trv" -m "ab:cd:ef:gh:ij:kl settemp 20.0" # Sets trv temp to 20 degrees
```

