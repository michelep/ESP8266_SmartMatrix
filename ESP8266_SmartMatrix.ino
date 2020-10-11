//
// ** ESP8266 SmartMatrix **
// by Michele <o-zone@zerozone.it> Pinassi
//
// Connections for ESP8266 hardware SPI are:
// DIN        D7     HSPID or HMOSI
// CS or LD   D8     HSPICS or HCS
// CLK        D5     CLK or HCLK
//
// Tested on a WeMos D1 Mini - 1M SPIFFS - Board
//
// ** CHANGELOG **
//
// 0.1.0 - 11.10.2020
// - Added json_data_url and related [DATA] field 
// - Some other minor improvements and fixes
//

#define __DEBUG__

// Firmware data
const char BUILD[] = __DATE__ " " __TIME__;
#define FW_NAME         "smartmatrix"
#define FW_VERSION      "0.1.0"

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <ESP8266httpUpdate.h>
#include <EEPROM.h>

#include <MD_MAX72xx.h>
#include <SPI.h>

#include <Hash.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

// ArduinoJson
// https://arduinojson.org/
#include <ArduinoJson.h>

// Task Scheduler
#include <TaskScheduler.h>

// Scheduler and tasks...
Scheduler runner;

#define MQTT_INTERVAL 60000*15 // Every 15 minutes...
void mqttTaskCB();
Task mqttTask(MQTT_INTERVAL, TASK_FOREVER, &mqttTaskCB);

#define DATA_INTERVAL 60000*30 // Every 30 minutes...
void dataTaskCB();
Task dataTask(DATA_INTERVAL, TASK_FOREVER, &dataTaskCB);

// MQTT PubSub client
// https://github.com/knolleary/pubsubclient
#include <PubSubClient.h>

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// NTP ClientLib 
// https://github.com/gmag11/NtpClient
#include <NtpClientLib.h>

// NTP
NTPSyncEvent_t ntpEvent;
bool syncEventTriggered = false; // True if a time even has been triggered

// #define HARDWARE_TYPE MD_MAX72XX::PAROLA_HW
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW

#define MAX_DEVICES 4

#define CLK_PIN   D5 // or SCK
#define DATA_PIN  D7 // or MOSI
#define CS_PIN    D8 // or SS

MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
// Arbitrary pins
// MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

#define BUZZER 0 // D3

// File System
#include <FS.h>   

// Format SPIFFS if mount failed
#define FORMAT_SPIFFS_IF_FAILED 1

// Web server
AsyncWebServer server(80);

#define MAX_DISPLAY_MESSAGES 3

// Config
struct Config {
  // WiFi config
  char wifi_essid[24];
  char wifi_password[24];
  // NTP Config
  char ntp_server[16];
  int8_t ntp_timezone;
  // MQTT broker
  char broker_host[24];
  unsigned int broker_port;
  char client_id[24];
  // Display
  char json_data_url[64];
  String display[MAX_DISPLAY_MESSAGES];
  // Display properties
  uint8_t scroll_delay;
  unsigned int light_trigger; 
  // Host config
  char hostname[16];
  bool ota_enable;
};

#define CONFIG_FILE "/config.json"

#define LIGHT_TRIGGER_DELTA 50

File configFile;
Config config; // Global config object
// Global message buffers shared by Wifi and Scrolling functions
const uint8_t MSG_SIZE = 255;
const uint8_t CHAR_SPACING = 1;
const uint8_t SCROLL_DELAY = 75;

char curMessage[MSG_SIZE];
uint8_t messageIdx=0;
bool displayStringChanged=false;

String external_data; // Data fetched from external JSON URL

IPAddress myIP;

unsigned int lightSensorValue=0,lastLightSensorValue=0;
unsigned long displayCycles=0,lastLightSwitch=0;

DynamicJsonDocument env(128);

// ************************************
// DEBUG()
//
// send message to Serial
// ************************************
void DEBUG(String message) {
#ifdef __DEBUG__
  Serial.println(message);
#endif
}

// ************************************
// scrollText()
//
// 
// ************************************
void scrollText(void)
{
  static uint32_t  prevTime = 0;

  // Is it time to scroll the text?
  if (millis() - prevTime >= config.scroll_delay)
  {
    mx.transform(MD_MAX72XX::TSL);  // scroll along - the callback will load all the data
    prevTime = millis();      // starting point for next time
  }
}

void scrollDataSink(uint8_t dev, MD_MAX72XX::transformType_t t, uint8_t col) {
  // Callback function for data that is being scrolled off the display
}

uint8_t scrollDataSource(uint8_t dev, MD_MAX72XX::transformType_t t) {
  // Callback function for data that is required for scrolling into the display
  static enum { S_IDLE, S_NEXT_CHAR, S_SHOW_CHAR, S_SHOW_SPACE } state = S_IDLE;
  static char *p;
  static uint16_t curLen, showLen;
  static uint8_t  cBuf[8];
  uint8_t colData = 0;

  // finite state machine to control what we do on the callback
  switch (state) {
    case S_IDLE: // reset the message pointer and check for new message to load
      p = curMessage;      // reset the pointer to start of message
      state = S_NEXT_CHAR;
      break;
    case S_NEXT_CHAR: // Load the next character from the font table
      if (*p == '\0') {
        // Load next string
        updateDisplayCb();
        displayCycles++;
        // IDLE
        state = S_IDLE;
      } else {
        showLen = mx.getChar(*p++, sizeof(cBuf) / sizeof(cBuf[0]), cBuf);
        curLen = 0;
        state = S_SHOW_CHAR;
      }
      break;
    case S_SHOW_CHAR: // display the next part of the character
      colData = cBuf[curLen++];
      if (curLen < showLen)
        break;
      // set up the inter character spacing
      showLen = (*p != '\0' ? CHAR_SPACING : (MAX_DEVICES*COL_SIZE)/2);
      curLen = 0;
      state = S_SHOW_SPACE;
      // fall through
    case S_SHOW_SPACE:  // display inter-character spacing (blank column)
      curLen++;
      if (curLen == showLen)
        state = S_NEXT_CHAR;
      break;
    default:
      state = S_IDLE;
  }

  return(colData);
}

// ************************************
// mqttConnect()
//
//
// ************************************
void mqttConnect() {
  uint8_t timeout=10;
  if(strlen(config.broker_host) > 0) {
    DEBUG("[MQTT] Attempting connection to "+String(config.broker_host)+":"+String(config.broker_port));
    while((!mqttClient.connected())&&(timeout > 0)) {
      // Attempt to connect
      if (mqttClient.connect(config.client_id)) {
        // Once connected, publish an announcement...
        DEBUG("[MQTT] Connected as "+String(config.client_id));
      } else {
        timeout--;
        delay(500);
      }
    }
    if(!mqttClient.connected()) {
      DEBUG("[MQTT] Connection failed");    
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  DEBUG("Message arrived: "+String(topic));
}

// ************************************
// scanWifi()
//
// scan available WiFi networks
// ************************************
void scanWifi() {
  WiFi.mode(WIFI_STA);
  Serial.println("--------------");
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0) {
    Serial.println("no networks found");
  } else {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i) {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
      delay(10);
    }
  }
  Serial.println("--------------");
}


// ************************************
// connectToWifi()
//
// connect to configured WiFi network
// ************************************
bool connectToWifi() {
  uint8_t timeout=0;
  // Scan WiFi
  // scanWifi();
  //
  if(strlen(config.wifi_essid) > 0) {
    DEBUG("[INIT] Connecting to "+String(config.wifi_essid));
    
    WiFi.mode(WIFI_STA);
    WiFi.hostname(config.hostname);
    WiFi.begin(config.wifi_essid, config.wifi_password);

    while((WiFi.status() != WL_CONNECTED) && (timeout < 10)) {
      delay(250);
      DEBUG(".");
      delay(250);
      timeout++;
    }
    
    if(WiFi.status() == WL_CONNECTED) {
      DEBUG("OK. IP:"+WiFi.localIP().toString());
      DEBUG("Signal strength (RSSI):"+String(WiFi.RSSI())+" dBm");

      if (MDNS.begin(config.hostname)) {
        DEBUG("[INIT] MDNS responder started");
        // Add service to MDNS-SD
        MDNS.addService("http", "tcp", 80);
      }
      
      NTP.onNTPSyncEvent([](NTPSyncEvent_t event) {
        ntpEvent = event;
        syncEventTriggered = true;
      });

      // NTP
      DEBUG("[INIT] NTP sync time on "+String(config.ntp_server));
      NTP.begin(config.ntp_server, config.ntp_timezone, true, 0);
      NTP.setInterval(63);

      // Setup MQTT connection
      mqttClient.setServer(config.broker_host, config.broker_port);
      mqttClient.setCallback(mqttCallback); 

      return true;  
    } else {
      DEBUG("[ERROR] Failed to connect to WiFi");
      WiFi.printDiag(Serial);
      return false;
    }
  } else {
      DEBUG("[ERROR] WiFi configuration missing!");
      return false;
  }
}

// ************************************
// updateDisplay()
//
// 
// ************************************

const char *months[] = { "Gennaio","Febbraio","Marzo","Aprile","Maggio","Giugno","Luglio","Agosto","Settembre","Ottobre","Novembre","Dicembre" };
const char *dow[] = { "Domenica","Lunedi","Martedi","Mercoledi","Giovedi","Venerdi","Sabato" };

String zeroPadding(uint8_t val) {
  if(val > 9) {
    return String(val);
  } else {
    return '0'+String(val);
  }
}

String compileString(String source) {
  time_t t = now(); // store the current time in time variable t
  time_t diff;
  String countdown="",temp="";
  int8_t idx;

  source.replace("[h]",zeroPadding(hour(t)));
  source.replace("[m]",zeroPadding(minute(t)));
  source.replace("[s]",zeroPadding(second(t)));
  source.replace("[D]",String(day(t)));
  source.replace("[DD]",dow[weekday(t)-1]);
  source.replace("[M]",zeroPadding(month(t)));
  source.replace("[MM]",months[month(t)-1]);
  source.replace("[Y]",String(year(t)));
  source.replace("[UPTIME]",String(millis()));
  source.replace("[IDX]",String(messageIdx));
  source.replace("[CYCLES]",String(displayCycles));
  source.replace("[DATA]",external_data);

  return source;
}

void updateDisplayCb() {
  DEBUG("[DEBUG] updateDisplayCb()");
 
  if(WiFi.status() != WL_CONNECTED) { 
    sprintf(curMessage, "Connecting to WiFi %s ...",config.wifi_essid);
    return;
  }

  if(config.display[0].length() == 0) {
    sprintf(curMessage,"Waiting for configuration! IP:%s",WiFi.localIP().toString().c_str());
    return;
  }

  uint8_t b=1;
  while(b) {
    if(config.display[messageIdx].length() > 0) {
      Serial.print("Now display ");
      Serial.println(config.display[messageIdx]);
      strncpy(curMessage,compileString(config.display[messageIdx]).c_str(),MSG_SIZE);
      b=0;
    }
    messageIdx++;
    if(messageIdx >= MAX_DISPLAY_MESSAGES) {
      messageIdx=0;
    }
  }
}

// ************************************
// mqttTaskCB()
//
// 
// ************************************
void mqttTaskCB() {
  char topic[32];
    
  // MQTT not connected? Connect!
  if (!mqttClient.connected()) {
    mqttConnect();
  }

  // Now prepare MQTT message...
  JsonObject root = env.as<JsonObject>();
 
  for (JsonPair keyValue : root) {
    sprintf(topic,"%s/%s",config.client_id,keyValue.key().c_str());
    String value = keyValue.value().as<String>();
    if(mqttClient.publish(topic,value.c_str())) {
      DEBUG("[MQTT] Publish "+String(topic)+":"+value);
    } else {
      DEBUG("[MQTT] Publish failed!");
    }
  }
}

// ************************************
// dataTaskCB()
//
// connect to external URL, if defined, and fetch data to display on [DATA] 
// ************************************
void dataTaskCB() {
  char http[32];
  if(1 > strlen(config.json_data_url)) return;

   // Connect to HTTP server
  WiFiClient client;


  // Split json_data_url in host and path
  uint8_t idx = String(config.json_data_url).indexOf('/');
  String host = String(config.json_data_url).substring(0,idx);
  String path = String(config.json_data_url).substring(idx);

  Serial.print("Host:");
  Serial.println(host);
  Serial.print("Path:");
  Serial.println(path);
  
  // Connect to host...
  client.setTimeout(10000);
  if (!client.connect(host, 80)) {
    DEBUG(F("[ERROR] Connection to host failed"));
    return;
  }

  // Send HTTP request
  sprintf(http,"GET %s HTTP/1.0",path.c_str());
  client.println(http);
  sprintf(http,"Host: %s",host.c_str());
  client.println(http);
  client.println(F("Connection: close"));
  if (client.println() == 0) {
    DEBUG(F("[ERROR] Failed to send request"));
    return;
  }

  // Check HTTP status
  char status[32] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  // It should be "HTTP/1.0 200 OK" or "HTTP/1.1 200 OK"
  if (strcmp(status + 9, "200 OK") != 0) {
    Serial.print(F("Unexpected response: "));
    Serial.println(status);
    return;
  }

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders)) {
    DEBUG(F("[ERROR] Invalid response"));
    return;
  }

  // Allocate the JSON document
  // Use arduinojson.org/v6/assistant to compute the capacity.
  const size_t capacity = JSON_OBJECT_SIZE(3) + JSON_ARRAY_SIZE(2) + 60;
  DynamicJsonDocument doc(capacity);

  // Parse JSON object
  DeserializationError error = deserializeJson(doc, client);
  if (error) {
    DEBUG(F("[ERROR] DeserializeJson() failed"));
    return;
  }

  // Finally, save fetched value!
  external_data = doc["data"].as<String>();

  Serial.println(F("Response:"));
  Serial.println(external_data);

  // Disconnect
  client.stop();
}

// ************************************
// SETUP()
//
// 
// ************************************
void setup() {
  Serial.begin(115200);
  delay(10);

  Serial.println();
  Serial.println();
  Serial.print(FW_NAME);
  Serial.print(" ");
  Serial.print(FW_VERSION);
  Serial.print(" ");  
  Serial.println(BUILD);

  pinMode(BUZZER, OUTPUT);
  analogWrite(BUZZER, 205);
  delay(500);
  analogWrite(BUZZER, 0);

  // Initialize SPIFFS
  if(!SPIFFS.begin()){
    DEBUG("[SPIFFS] Mount Failed. Try formatting...");
    if(SPIFFS.format()) {
      DEBUG("[SPIFFS] Initialized successfully");
    } else {
      DEBUG("[SPIFFS] Fatal error: restart!");
      ESP.restart();
    }
  } else {
    DEBUG("[SPIFFS] OK");
  }

  // Load configuration
  loadConfigFile();

  // Connect to WiFi network
  if(connectToWifi()) {
    // Setup OTA
    ArduinoOTA.onStart([]() { 
      Serial.println("[OTA] Update Start");
    });
    ArduinoOTA.onEnd([]() { 
      Serial.println("[OTA] Update End"); 
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      char p[32];
      sprintf(p, "[OTA] Progress: %u%%\n", (progress/(total/100)));
      DEBUG(p);
    });
    ArduinoOTA.onError([](ota_error_t error) {
      if(error == OTA_AUTH_ERROR) DEBUG("[OTA] Auth Failed");
      else if(error == OTA_BEGIN_ERROR) DEBUG("[OTA] Begin Failed");
      else if(error == OTA_CONNECT_ERROR) DEBUG("[OTA] Connect Failed");
      else if(error == OTA_RECEIVE_ERROR) DEBUG("[OTA] Recieve Failed");
      else if(error == OTA_END_ERROR) DEBUG("[OTA] End Failed");
    });
    ArduinoOTA.setHostname(config.hostname);
    ArduinoOTA.begin();
  }

  // Display initialization
  mx.begin();
  mx.setShiftDataInCallback(scrollDataSource);
  mx.setShiftDataOutCallback(scrollDataSink);
  mx.clear();
  mx.control(MD_MAX72XX::INTENSITY, MAX_INTENSITY/6);
  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);

  // Add environmental sensor data fetch task
  runner.addTask(mqttTask);
  mqttTask.enable();

  runner.addTask(dataTask);
  dataTask.enable();
  
  // Initialize web server on port 80
  initWebServer();
  
  // GO !  
}

unsigned int last=0;

// ************************************
// LOOP()
//
// 
// ************************************
void loop() {
  if(config.ota_enable) {
    // Handle OTA
    ArduinoOTA.handle();
  }
  
  // Scheduler
  runner.execute();

  // NTP ?
  if(syncEventTriggered) {
    processSyncEvent(ntpEvent);
    syncEventTriggered = false;
  }
  
  if((millis() - last) > 5100) {              
    if(WiFi.status() != WL_CONNECTED) {
      connectToWifi();
    }

    env["uptime"] = millis() / 1000;

    last = millis();
  }

  // Tune display brightness
  lightSensorValue = analogRead(A0);
  if(abs(lightSensorValue - lastLightSensorValue) > LIGHT_TRIGGER_DELTA) {
    lastLightSensorValue = lightSensorValue;
    if((millis() - lastLightSwitch) > 60000) {
      lastLightSwitch = millis();
      if(lightSensorValue > config.light_trigger) {
        mx.control(MD_MAX72XX::INTENSITY, MAX_INTENSITY/6);
      } else {
        mx.control(MD_MAX72XX::INTENSITY, MAX_INTENSITY/16);
      }
    }
  }
  
  scrollText();
  
  delay(10);
}
