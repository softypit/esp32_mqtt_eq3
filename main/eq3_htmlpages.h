
const char pageheader[] = "<!DOCTYPE html> \
<html> \
<head> \
<meta charset='utf-8'><meta name=\"viewport\" content=\"width=device-width,initial-scale=1,user-scalable=no\"/> \
<title>EQ3 status</title> \
</head> \
<body> \
<div style='text-align:center;'>";
//<div style=\"zoom: 350%%;\">";



const char pagefooter[] = "<table style=\"margin:1em auto;\"> \
<tr><td><a href=\"/command\">Control EQ3 device</a></td><td> | </td></tr> \
<tr><td><a href=\"/viewlog\">View EQ3 status log</a></td><td> | </td><td><a href=\"/config\" style=\"color: rbg(200,150,0)\"><font color=\"C89600\">Configuration</font></a></td></tr> \
<tr><td><a href=\"/getdevices\">List of EQ3 devices seen</a></td><td> | </td><td><a href=\"/upload\" style=\"color: rbg(200,150,0)\"><font color=\"C89600\">Update software</font></a></td></tr> \
<tr><td><a href=\"/scan\">Rescan for EQ3 devices</a></td><td> | </td><td><a href=\"/restartnow\" style=\"color: rbg(255,0,0)\"><font color=\"FF0000\">Reboot ESP</font></a></td></tr> \
</table> \
</div> \
<div style='text-align:center;font-size:12px;'><hr/><a href='https://github.com/softypit/esp32_mqtt_eq3' target='_blank' style='color:#aaa;'>EQ3-MQTT-ESP32 "EQ3_MAJVER"."EQ3_MINVER""EQ3_EXTRAVER" by SoftyPIT</a></div> \
</body> \
</html>" ;

const char pageemptyfooter[] = "</div> \
</body> \
</html>";

const char selectap[] = "<div style='text-align:center;'><h1>Select WiFi</h1></div> \
<form action=\"ssidSelected\" method=\"post\"> \
<table style=\"margin:1em auto;\"> \
<tbody> \
<tr><td>SSID:</td><td><input type=\"text\" autocorrect=\"off\" autocapitalize=\"none\" name=\"ssid\" value=\"%s\" /></td></tr> \
<tr><td>Password:</td><td><input type=\"password\" autocorrect=\"off\" autocapitalize=\"none\" name=\"password\" value=\"%s\" /></td></tr> \
<tr><td>MQTT URL:</td><td><input type=\"text\" autocorrect=\"off\" autocapitalize=\"none\" name=\"mqtturl\" value=\"%s\" /></td></tr> \
<tr><td>MQTT username:</td><td><input type=\"text\" autocorrect=\"off\" autocapitalize=\"none\" name=\"mqttuser\" value=\"%s\" /></td></tr> \
<tr><td>MQTT password:</td><td><input type=\"password\" autocorrect=\"off\" autocapitalize=\"none\" name=\"mqttpass\" value=\"%s\" /></td></tr> \
<tr><td>MQTT ID:</td><td><input type=\"text\" autocorrect=\"off\" autocapitalize=\"none\" name=\"mqttid\" value=\"%s\" /></td></tr> \
<tr><td>NTP enabled:</td><td><input type=\"checkbox\" name=\"ntpenabled\" value=\"true\" %s/></td></tr> \
<tr><td>NTP server:</td><td><input type=\"text\" autocorrect=\"off\" autocapitalize=\"none\" name=\"ntpserver\" value=\"%s\" /></td></tr> \
<tr><td>Timezone:</td><td><input type=\"text\" autocorrect=\"off\" autocapitalize=\"none\" name=\"ntptimezone\" value=\"%s\" /></td></tr> \
<tr><td>IP address:</td><td><input type=\"text\" autocorrect=\"off\" autocapitalize=\"none\" name=\"ip\" value=\"%s\" /></td></tr> \
<tr><td>Gateway address:</td><td><input type=\"text\" autocorrect=\"off\" autocapitalize=\"none\" name=\"gw\" value=\"%s\" /></td></tr> \
<tr><td>Netmask:</td><td><input type=\"text\" autocorrect=\"off\" autocapitalize=\"none\" name=\"netmask\" value=\"%s\" /></td></tr> \
</tbody> \
</table> \
 <p> \
<div style='text-align:center;'><input type=\"submit\" value=\"Submit\"></div> \
</p> \
</form> \
<div style=\"text-align:center;\"> \
The IP address, gateway address and netmask are optional.  If not supplied \
these values will be issued by the WiFi access point." ;


const char devlisthead[] = "<title>EQ3 devices</title> \
<div style='text-align:center;'><h1>EQ3 devices found</h1></div> \
<form action=\"ssidSelected\" method=\"post\"> \
<table style=\"margin:1em auto;\"> \
<tbody> ";

const char devlistentry[] = "<tr><td>Device:</td><td>%02X:%02X:%02X:%02X:%02X:%02X</td><td>rssi</td><td>%d</td></tr>";

const char devlistfoot[] = "</tbody> \
</table>" ;

const char nodevices[] = "<title>No devices found</title> \
<div style='text-align:center;'><h1>No devices found</h1></div>";

const char loglisthead[] = "<title>EQ3 device log</title> \
<div style='text-align:center;'><h1>EQ3 device log</h1></div> \
<table style=\"margin:1em auto;\"> \
<tbody> ";

//const char loglistentry[] = "<tr><td>%s</td></tr>";

const char loglistfoot[] = "</tbody> \
</table>";

const char connectedstatus[] = "<title>EQ3 status</title> \
<div style='text-align:center;'><h1>EQ3 relay status</h1></div> \
<table style=\"margin:1em auto;\"> \
<tr><td>MQTT URL:</td><td>%s</td></tr> \
<tr><td>MQTT user:</td><td>%s</td></tr> \
<tr><td>MQTT pass:</td><td>%s</td></tr> \
<tr><td>MQTT ID:</td><td>%s</td></tr> \
<tr><td>MQTT status:</td><td>%s</td></tr> \
</table>";

const char apstatus[] = "<title>Please configure me</title> \
<div style='text-align:center;'><h1>Please configure me</h1></div>";

const char upload[] = "<title>upload firmware</title> \
<div style='text-align:center;'><h1>Upload new software</h1></div> \
<table style=\"margin:1em auto;\"><tr><td> \
<form action=\"otaupload\" method=\"POST\" enctype=\"multipart/form-data\"> \
</td></tr><tr><td> \
<input type=\"file\" name=\"fileupload\" value=\"fileupload\" id=\"fileupload\"> \
</td></tr><tr><td> \
<input type=\"submit\" value=\"Upload and apply\"> \
</td></tr> \
</form>";

const char uploadsuccess[] = "<title>uploaded firmware</title> \
<div style='text-align:center;'> \
<h1>Upload status</h1> \
Firmware upload success \
%d bytes transferred \
<br><a href=\"/restartnow\">Reboot ESP</a> to apply new image \
</div>";

const char uploadfailed[] = "<title>uploaded firmware</title> \
<div style='text-align:center;'> \
<h1>Upload status</h1> \
Firmware upload failed \
</div>";

const char uploadcomplete[] = "<meta http-equiv=\"refresh\" content=\"1;URL='/otastatus'\"> \
<title>upload complete</title> \
<div style='text-align:center;'> \
<h1>Upload complete</h1> \
</div>";

const char scanning[] = "<meta http-equiv=\"refresh\" content=\"5;URL='/getdevices'\"> \
<div style='text-align:center;'><h1>Scanning...</h1></div>";

const char rebooting[] = "<meta http-equiv=\"refresh\" content=\"40;URL='/'\"> \
<div style='text-align:center;'><h1>Rebooting...</h1></div>";


const char command_device_head[] = "<title>EQ3 device log</title> \
<div style='text-align:center;'><h1>control EQ-3</h1></div> \
<form action=\"sendCommand\" method=\"post\"> \
<table style=\"margin:1em auto;\"><tr><td><select name=\"device\">";

const char select_device_entry[] = "<option value=\"%s\">%s</option>";

const char command_post_device[] = "</select></td><td><select name=\"command\"> \
<option value=\"unlock\">unlock</option> \
<option value=\"lock\">lock</option> \
<option value=\"boost\">boost</option> \
<option value=\"unboost\">unboost</option> \
<option value=\"auto\">auto</option> \
<option value=\"manual\">manual</option> \
<option value=\"settemp\">settemp</option> \
<option value=\"on\">open (on)</option> \
<option value=\"off\">closed (off)</option> \
<option value=\"offset\">tempoffset</option> \
<option value=\"settime\">settime</option> \
</select> \
</td><td><input type=\"text\" name=\"value\"></td></tr> \
<tr><td><input type=\"submit\" value=\"Submit\"></td></tr> \
</table> \
</form> \
";

const char commandsubmitted[] = "<title>Command submitted</title> \
<div style='text-align:center;'> \
<h1>Command submitted</h1> \
</div>";

const char commanderror[] = "<title>Invalid command</title> \
<div style='text-align:center;'> \
<h1>Invalid command</h1> \
</div>";

