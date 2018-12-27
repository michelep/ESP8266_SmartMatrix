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
  if(var=="ntp_server") {
    return String(config.ntp_server);
  }
  if(var=="ntp_timezone") {
    return String(config.ntp_timezone);
  }
  if(var=="syslog_server") {
    return String(config.syslog_server);
  }
  if(var=="syslog_port") {
    return String(config.syslog_port);
  }

  if(var.startsWith("display_")) {
    uint8_t str_idx;
    str_idx = atoi(var.substring(var.lastIndexOf('_')+1).c_str());
    return config.display[str_idx];
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
          displayStringChanged = true;
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
    if(request->hasParam("syslog_server", true)) {
        strcpy(config.syslog_server,request->getParam("syslog_server", true)->value().c_str());
    }
    if(request->hasParam("syslog_port", true)) {
        config.syslog_port = atoi(request->getParam("syslog_port", true)->value().c_str());
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
    Serial.printf("NOT_FOUND: ");
    if(request->method() == HTTP_GET)
      Serial.printf("GET");
    else if(request->method() == HTTP_POST)
      Serial.printf("POST");
    else if(request->method() == HTTP_DELETE)
      Serial.printf("DELETE");
    else if(request->method() == HTTP_PUT)
      Serial.printf("PUT");
    else if(request->method() == HTTP_PATCH)
      Serial.printf("PATCH");
    else if(request->method() == HTTP_HEAD)
      Serial.printf("HEAD");
    else if(request->method() == HTTP_OPTIONS)
      Serial.printf("OPTIONS");
    else
      Serial.printf("UNKNOWN");
    Serial.printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

    if(request->contentLength()){
      Serial.printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
      Serial.printf("_CONTENT_LENGTH: %u\n", request->contentLength());
    }

    int headers = request->headers();
    int i;
    for(i=0;i<headers;i++){
      AsyncWebHeader* h = request->getHeader(i);
      Serial.printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    int params = request->params();
    for(i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isFile()){
        Serial.printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
      } else if(p->isPost()){
        Serial.printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      } else {
        Serial.printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }

    request->send(404);
  });

  server.begin();
}
