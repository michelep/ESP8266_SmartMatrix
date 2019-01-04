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
  char temp[256];
  uint8_t str_idx;
  
  DEBUG_PRINT("[DEBUG] loadConfigFile()");
  
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
      strlcpy(config.owm_apikey, root["owm_apikey"] | "", sizeof(config.owm_apikey));
      config.owm_cityid = root["owm_cityid"] | 0;

      for(str_idx=0;str_idx<MAX_DISPLAY_MESSAGES;str_idx++) {
        sprintf(temp,"display_%d",str_idx);
        config.display[str_idx] = String(root[temp] | "");
        
        sprintf(temp,"[CONFIG] Load message %d: %s",str_idx,config.display[str_idx].c_str());
        DEBUG_PRINT(temp);
      }
      
      config.scroll_delay = root["scroll_delay"] | SCROLL_DELAY;
      config.light_trigger = root["light_trigger"] | 0;

      DEBUG_PRINT("[CONFIG] Configuration loaded");
    }
  }
  configFile.close();
  return true;
}

bool saveConfigFile() {
  DynamicJsonBuffer jsonBuffer;
  uint8_t str_idx;
  char temp[16];
  
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
  
  for(str_idx=0;str_idx<MAX_DISPLAY_MESSAGES;str_idx++) {
    sprintf(temp,"display_%d",str_idx);
    root[temp] = config.display[str_idx].c_str();
  }
  
  root["scroll_delay"] = config.scroll_delay;
  root["light_trigger"] = config.light_trigger;

  root["owm_apikey"] = config.owm_apikey;
  root["owm_cityid"] = config.owm_cityid;

  configFile = SPIFFS.open(CONFIG_FILE, "w");
  if(!configFile) {
    DEBUG_PRINT("[CONFIG] Failed to create config file !");
    return false;
  }
  if (root.printTo(configFile) == 0) {
    DEBUG_PRINT("[CONFIG] Failed to save config file !");
    return false;
  } else {
    DEBUG_PRINT("[CONFIG] Configuration saved !");
  }
  configFile.close();
  return true;
}
