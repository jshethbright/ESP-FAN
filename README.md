# ESP-FAN

## Description

Turns an ESP8266 or ESP32 with a CC1101 module into a MQTT controller hub for most RF controlled fans. 
* Supports WIFI control of all features present on remotes of RF fans, through an MQTT broker.
* Integrates existing RF remotes by scanning RF messages and updating MQTT topics accordingly.
* Works with a single defined frequency so multiple ESP and CC1101 modules will be needed for fans on different frequencies. 
