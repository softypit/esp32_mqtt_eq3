const char selectap[] = "<!DOCTYPE html> \
<html> \
<head> \
<meta charset=\"UTF-8\"> \
<title>Select WiFi</title> \
</head> \
<body> \
<div style=\"zoom: 350%%;\"> \
<h1>Select WiFi</h1> \
<form action=\"ssidSelected\" method=\"post\"> \
<table> \
<tbody> \
<tr><td>SSID:</td><td><input type=\"text\" autocorrect=\"off\" autocapitalize=\"none\" name=\"ssid\" value=\"%s\" /></td></tr> \
<tr><td>Password:</td><td><input type=\"text\" autocorrect=\"off\" autocapitalize=\"none\" name=\"password\" value=\"%s\" /></td></tr> \
<tr><td>MQTT URL:</td><td><input type=\"text\" autocorrect=\"off\" autocapitalize=\"none\" name=\"mqtturl\" value=\"%s\" /></td></tr> \
<tr><td>MQTT username:</td><td><input type=\"text\" autocorrect=\"off\" autocapitalize=\"none\" name=\"mqttuser\" value=\"%s\" /></td></tr> \
<tr><td>MQTT password:</td><td><input type=\"password\" autocorrect=\"off\" autocapitalize=\"none\" name=\"mqttpass\" value=\"%s\" /></td></tr> \
<tr><td>MQTT ID:</td><td><input type=\"text\" autocorrect=\"off\" autocapitalize=\"none\" name=\"mqttid\" value=\"%s\" /></td></tr> \
<tr><td>IP address:</td><td><input type=\"text\" autocorrect=\"off\" autocapitalize=\"none\" name=\"ip\" value=\"%s\" /></td></tr> \
<tr><td>Gateway address:</td><td><input type=\"text\" autocorrect=\"off\" autocapitalize=\"none\" name=\"gw\" value=\"%s\" /></td></tr> \
<tr><td>Netmask:</td><td><input type=\"text\" autocorrect=\"off\" autocapitalize=\"none\" name=\"netmask\" value=\"%s\" /></td></tr> \
</tbody> \
</table> \
 <p> \
<input type=\"submit\" value=\"Submit\"> \
</p> \
</form> \
<div style=\"margin: 6px;\"> \
The IP address, gateway address and netmask are optional.  If not supplied \
these values will be issued by the WiFi access point. \
</div> \
</div> \
</body> \
</html>" ;


const char devlisthead[] = "<!DOCTYPE html> \
<html> \
<head> \
<meta charset=\"UTF-8\"> \
<title>EQ3 devices</title> \
</head> \
<body> \
<div style=\"zoom: 350%%;\"> \
<h1>EQ3 devices found</h1> \
<form action=\"ssidSelected\" method=\"post\"> \
<table> \
<tbody> ";

const char devlistentry[] = "<tr><td>Device:</td><td>%02X:%02X:%02X:%02X:%02X:%02X</td><td>rssi</td><td>%d</td></tr>";

const char devlistfoot[] = "</tbody> \
</table> \
</div> \
</body> \
</html>" ;

const char connectedstatus[] = "<!DOCTYPE html> \
<html> \
<head> \
<meta charset=\"UTF-8\"> \
<title>EQ3 status</title> \
</head> \
<body> \
<div style=\"zoom: 350%%;\"> \
<h1>EQ3 relay status</h1> \
<table> \
<tr><td>MQTT URL</td><td>%s</td></tr> \
<tr><td>MQTT user</td><td>%s</td></tr> \
<tr><td>MQTT pass</td><td>%s</td></tr> \
<tr><td>MQTT ID</td><td>%s</td></tr> \
<tr><td>MQTT status</td><td>%s</td></tr> \
</table> \
List of found EQ3 devices are <a href=\"/getdevices\">here</a> \
<br> \
Configuration is <a href=\"/config\">here</a> \
</div> \
</body> \
</html>";

const char apstatus[] = "<!DOCTYPE html> \
<html> \
<head> \
<meta charset=\"UTF-8\"> \
<title>Please configure me</title> \
</head> \
<body> \
<div style=\"zoom: 350%%;\"> \
<h1>Please configure me</h1> \
</body> \
</html>";

