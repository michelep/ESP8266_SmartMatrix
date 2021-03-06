// Written by Michele <o-zone@zerozone.it> Pinassi
// Released under GPLv3 - No any warranty

#include <ArduinoJson.h>

// ************************************
// Config, save and load functions
//
// save and load configuration from config file in SPIFFS. JSON format (need ArduinoJson library)
// ************************************
bool loadConfigFile() {
  DynamicJsonDocument root(1024);
  char temp[256];
  uint8_t str_idx;
  
  DEBUG("[DEBUG] loadConfigFile()");
  
  configFile = SPIFFS.open(CONFIG_FILE, "r");
  if (!configFile) {
    DEBUG("[CONFIG] Config file not available");
    return false;
  } else {
    // Get the root object in the document
    DeserializationError err = deserializeJson(root, configFile);
    if (err) {
      DEBUG("[CONFIG] Failed to read config file:"+String(err.c_str()));
      return false;
    } else {
      strlcpy(config.wifi_essid, root["wifi_essid"], sizeof(config.wifi_essid));
      strlcpy(config.wifi_password, root["wifi_password"], sizeof(config.wifi_password));
      strlcpy(config.hostname, root["hostname"] | "aiq-sensor", sizeof(config.hostname));
      strlcpy(config.ntp_server, root["ntp_server"] | "time.ien.it", sizeof(config.ntp_server));
      config.ntp_timezone = root["ntp_timezone"] | 1;
      // MQTT broker
      strlcpy(config.broker_host, root["broker_host"] | "", sizeof(config.broker_host));
      config.broker_port = root["broker_port"] | 1883;
      strlcpy(config.client_id, root["client_id"] | "led-matrix", sizeof(config.client_id));
      // OTA
      config.ota_enable = root["ota_enable"] | true; // OTA updates enabled by default
      // Display messages
      strlcpy(config.json_data_url, root["json_data_url"] | "", sizeof(config.json_data_url));
      for(str_idx=0;str_idx<MAX_DISPLAY_MESSAGES;str_idx++) {
        sprintf(temp,"display_%d",str_idx);
        config.display[str_idx] = String(root[temp] | "");
        
        sprintf(temp,"[CONFIG] Load message %d: %s",str_idx,config.display[str_idx].c_str());
        DEBUG(temp);
      }
      
      config.scroll_delay = root["scroll_delay"] | SCROLL_DELAY;
      config.light_trigger = root["light_trigger"] | 0;

      DEBUG("[CONFIG] Configuration loaded");
    }
  }
  configFile.close();
  return true;
}

bool saveConfigFile() {
  DynamicJsonDocument root(1024);
  uint8_t str_idx;
  char temp[16];
  
  DEBUG("[DEBUG] saveConfigFile()");

  root["wifi_essid"] = config.wifi_essid;
  root["wifi_password"] = config.wifi_password;
  root["hostname"] = config.hostname;
  root["ntp_server"] = config.ntp_server;
  root["ntp_timezone"] = config.ntp_timezone;
  root["broker_host"] = config.broker_host;
  root["broker_port"] = config.broker_port;
  root["client_id"] = config.client_id;
  root["ota_enable"] = config.ota_enable;

  root["json_data_url"] = config.json_data_url;

  for(str_idx=0;str_idx<MAX_DISPLAY_MESSAGES;str_idx++) {
    sprintf(temp,"display_%d",str_idx);
    root[temp] = config.display[str_idx].c_str();
  }
  
  root["scroll_delay"] = config.scroll_delay;
  root["light_trigger"] = config.light_trigger;

  configFile = SPIFFS.open(CONFIG_FILE, "w");
  if(!configFile) {
    DEBUG("[CONFIG] Failed to create config file !");
    return false;
  }
  serializeJson(root,configFile);

  configFile.close();
  return true;
}
