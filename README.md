EQ-3 Radiator valve control application for ESP-32

Tested on ESP-WROOM-32

Be aware this code is far from optimized for speed or size. It was cobbled together from various
modules so could do with lots of tidying up. There are likely to be numerous stuck-state possibilities
in the various state machines especially the GATTC code.
Numerous error conditions are also not handled properly.
But it sort-of works!

This application uses BLE to communicate with EQ-3 TRVs and makes configuration possible
via MQTT over WiFi. This device can act as a 'hub' to talk to TRVs from a central location 
due to the limited range of Bluetooth.

EQ-3 valves must first be paired to the CalorBT app on a smartphone which leaves them in
an authenticated state so any BLE client can connect (dodgy security implementation but helpful
for this application).

This application first scans for EQ-3 valves and publishes their addresses to the MQTT broker. A scan
can be initiated at any time by publishing to the /espradin/scan topic. Scan results are published to 
/espradout/devlist.

Control of valves is carried out by publishing to the espradin/trv topic with a payload consisting of:
ab:cd:ef:gh:ij:kl <command> [parm]
where the device is indicated by its bluetooth address (MAC)
commands are: boost, unboost, manual, auto, settemp. settemp has an additional parameter of the temperature
(integer) to set.

In response to every successful command a status message is published to /espradout/status containing json-encoded
details of address, temperature set point, boost state, mode and lock state. This can be used as an acknowledgement
of a successful command to remote mqtt clients.

On first boot this application uses Kolbans bootwifi code to create a wifi AP to enable configuration via web interface.
This interface allows setting of AP details, static IP details and MQTT broker connection parameters.
Once configuration is complete and on subsequent boots the same details are used for connection.
If connection fails the application reverts to AP mode where the web interface is used to reconfigure
The application can be forced into config mode by pressing and holding the BOOT key AFTER the EN key has been released.
IP address for configuration interface is 192.168.4.1

web server is part of Mongoose - https://github.com/cesanta/mongoose
MQTT library is https://github.com/tuanpmt/espmqtt

use xxd to convert html file to c array 
