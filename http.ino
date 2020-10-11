// Written by Michele <o-zone@zerozone.it> Pinassi
// Released under GPLv3 - No any warranty

String templateProcessor(const String& var)
{
  //
  // System values
  //
  if(var == "hostname") {
    return String(config.hostname);
  }
  if(var == "fw_name") {
    return String(FW_NAME);
  }
  if(var=="fw_version") {
    return String(FW_VERSION);
  }
  if(var=="uptime") {
    return String(millis()/1000);
  }
  if(var=="timedate") {
    return NTP.getTimeDateString();
  }
  if(var=="display_cycles") {
    return String(displayCycles);
  }
  
  //
  // Config values
  //
  if(var=="wifi_essid") {
    return String(config.wifi_essid);
  }
  if(var=="wifi_password") {
    return String(config.wifi_password);
  }
  if(var=="wifi_rssi") {
    return String(WiFi.RSSI());
  }
  if(var=="ntp_server") {
    return String(config.ntp_server);
  }
  if(var=="ntp_timezone") {
    return String(config.ntp_timezone);
  }
  if(var=="broker_host") {
    return String(config.broker_host);
  }
  if(var=="broker_port") {
    return String(config.broker_port);
  }
  if(var=="client_id") {
    return String(config.client_id);
  }  
  if(var=="ota_enable") {
    if(config.ota_enable) {
      return String("checked");  
    } else {
      return String("");
    }
  }
  if(var.startsWith("display_")) {
    uint8_t str_idx;
    str_idx = atoi(var.substring(var.lastIndexOf('_')+1).c_str());
    return config.display[str_idx];
  }
  if(var == "json_data_url") {
    return String(config.json_data_url);
  } 
  if(var == "external_data") {
    return external_data;
  } 
  
  if(var == "scroll_delay") {
    return String(config.scroll_delay);
  }
  if(var == "light_trigger") {
    return String(config.light_trigger);
  }
  if(var == "light_value") {
    return String(lightSensorValue);
  }
  return String();
}

// ************************************
// initWebServer
//
// initialize web server
// ************************************
void initWebServer() {
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html").setTemplateProcessor(templateProcessor);

  server.on("/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
    ESP.restart();
  });
    
  server.on("/post", HTTP_POST, [](AsyncWebServerRequest *request) {
    String message;
    uint8_t str_idx;
    char debug[32];
    for(str_idx=0;str_idx<MAX_DISPLAY_MESSAGES;str_idx++) {
      sprintf(debug,"display_%d",str_idx);
      if(request->hasParam(debug, true)) {
          config.display[str_idx] = request->getParam(debug, true)->value();
      }
    }
    if(request->hasParam("scroll_delay", true)) {
        config.scroll_delay = atoi(request->getParam("scroll_delay", true)->value().c_str());
    }
    if(request->hasParam("light_trigger", true)) {
        config.light_trigger = atoi(request->getParam("light_trigger", true)->value().c_str());
    }
    // 
    if(request->hasParam("wifi_essid", true)) {
        strcpy(config.wifi_essid,request->getParam("wifi_essid", true)->value().c_str());
    }
    if(request->hasParam("wifi_password", true)) {
        strcpy(config.wifi_password,request->getParam("wifi_password", true)->value().c_str());
    }
    if(request->hasParam("ntp_server", true)) {
        strcpy(config.ntp_server, request->getParam("ntp_server", true)->value().c_str());
    }
    if(request->hasParam("ntp_timezone", true)) {
        config.ntp_timezone = atoi(request->getParam("ntp_timezone", true)->value().c_str());
    }
    if(request->hasParam("broker_host", true)) {
        strcpy(config.broker_host,request->getParam("broker_host", true)->value().c_str());
    }
    if(request->hasParam("broker_port", true)) {
        config.broker_port = atoi(request->getParam("broker_port", true)->value().c_str());
    }
    if(request->hasParam("client_id", true)) {
        strcpy(config.client_id, request->getParam("client_id", true)->value().c_str());
    }    
    if(request->hasParam("json_data_url", true)) {
        strcpy(config.json_data_url, request->getParam("json_data_url", true)->value().c_str());
    }    
    if(request->hasParam("ota_enable", true)) {
      config.ota_enable=true;        
    } else {
      config.ota_enable=false;
    } 
    if(saveConfigFile()) {
      request->redirect("/?result=ok");
    } else {
      request->redirect("/?result=error");      
    }
  });
  
  server.on("/ajax", HTTP_POST, [] (AsyncWebServerRequest *request) {
    String action,value,response="";

    if (request->hasParam("action", true)) {
      action = request->getParam("action", true)->value();
      Serial.print("ACTION: ");
      if(action.equals("get")) {
        value = request->getParam("value", true)->value();
        Serial.println(value);
      }
    }
    request->send(200, "text/plain", response);
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    Serial.printf("404 NOT FOUND - http://%s%s\n", request->host().c_str(), request->url().c_str());
    request->send(404);
  });

  server.begin();
}
