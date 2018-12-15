// Written by Michele <o-zone@zerozone.it> Pinassi
// Released under GPLv3 - No any warranty

#include <ArduinoJson.h>

// ************************************
// Config, save and load functions
//
// save and load configuration from config file in SPIFFS. JSON format (need ArduinoJson library)
// ************************************
bool loadConfigFile() {
  DynamicJsonBuffer jsonBuffer;
#ifdef __DEBUG__
  Serial.println("[DEBUG] loadConfigFile()");
#endif
  configFile = SPIFFS.open(CONFIG_FILE, "r");
  if (!configFile) {
    DEBUG_PRINT("[CONFIG] Config file not available");
    return false;
  } else {
    // Get the root object in the document
    JsonObject &root = jsonBuffer.parseObject(configFile);
    if (!root.success()) {
      DEBUG_PRINT("[CONFIG] Failed to read config file");
      return false;
    } else {
      strlcpy(config.wifi_essid, root["wifi_essid"], sizeof(config.wifi_essid));
      strlcpy(config.wifi_password, root["wifi_password"], sizeof(config.wifi_password));
      strlcpy(config.hostname, root["hostname"] | "aiq-sensor", sizeof(config.hostname));
      strlcpy(config.ntp_server, root["ntp_server"] | "time.ien.it", sizeof(config.ntp_server));
      config.ntp_timezone = root["ntp_timezone"] | 1;
      strlcpy(config.syslog_server, root["syslog_server"] | "", sizeof(config.syslog_server));
      config.syslog_port = root["syslog_port"] | 514;
      strlcpy(config.api_key, root["api_key"] | "", sizeof(config.api_key));
      
      strlcpy(config.display_1, root["display_1"] | "", sizeof(config.display_1));
      config.scroll_delay = root["scroll_delay"] | SCROLL_DELAY;

      DEBUG_PRINT("[CONFIG] Configuration loaded");
    }
  }
  configFile.close();
  return true;
}

bool saveConfigFile() {
  DynamicJsonBuffer jsonBuffer;

  DEBUG_PRINT("[DEBUG] saveConfigFile()");

  // Parse the root object
  JsonObject &root = jsonBuffer.createObject();

  root["wifi_essid"] = config.wifi_essid;
  root["wifi_password"] = config.wifi_password;
  root["hostname"] = config.hostname;
  root["ntp_server"] = config.ntp_server;
  root["ntp_timezone"] = config.ntp_timezone;
  root["syslog_server"] = config.syslog_server;
  root["syslog_port"] = config.syslog_port;
  root["api_key"] = config.api_key;

  root["display_1"] = config.display_1;
  root["scroll_delay"] = config.scroll_delay;

  configFile = SPIFFS.open(CONFIG_FILE, "w");
  if(!configFile) {
    DEBUG_PRINT("[CONFIG] Failed to create config file !");
    return false;
  }
  if (root.printTo(configFile) == 0) {
    DEBUG_PRINT("[CONFIG] Failed to save config file !");
  } else {
    DEBUG_PRINT("[CONFIG] Configuration saved !");
  }
  configFile.close();
  return true;
}
