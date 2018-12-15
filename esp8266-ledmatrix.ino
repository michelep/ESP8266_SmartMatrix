// Connections for ESP8266 hardware SPI are:
// Vcc       3v3     LED matrices seem to work at 3.3V
// GND       GND     GND
// DIN        D7     HSPID or HMOSI
// CS or LD   D8     HSPICS or HCS
// CLK        D5     CLK or HCLK
//

#define __DEBUG__

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

// Syslog logger
// https://github.com/arcao/Syslog/
#include <WiFiUdp.h>
#include <Syslog.h>

WiFiUDP udpClient;
Syslog syslog(udpClient, SYSLOG_PROTO_IETF);

// TaskScheduler
// https://github.com/arkhipenko/TaskScheduler
#include <TaskScheduler.h>

Scheduler runner;

// ArduinoJson
// https://arduinojson.org/
#include <ArduinoJson.h>

// restClient
// https://github.com/DaKaZ/esp8266-restclient
#include "RestClient.h"  

// NTP ClientLib 
// https://github.com/gmag11/NtpClient
#include <NtpClientLib.h>

// NTP
NTPSyncEvent_t ntpEvent;
bool syncEventTriggered = false; // True if a time even has been triggered

// #define HARDWARE_TYPE MD_MAX72XX::PAROLA_HW
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW

#define MAX_DEVICES 8

#define CLK_PIN   D5 // or SCK
#define DATA_PIN  D7 // or MOSI
#define CS_PIN    D8 // or SS

MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
// Arbitrary pins
// MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// Firmware data
const char BUILD[] = __DATE__ " " __TIME__;
#define FW_NAME         "led-module"
#define FW_VERSION      "0.0.1"

// File System
#include <FS.h>   

// Format SPIFFS if mount failed
#define FORMAT_SPIFFS_IF_FAILED 1

// Web server
AsyncWebServer server(80);

// NTP ClientLib 
// https://github.com/gmag11/NtpClient
#include <NtpClientLib.h>

// Config
struct Config {
  // WiFi config
  char wifi_essid[16];
  char wifi_password[16];
  // NTP Config
  char ntp_server[16];
  int8_t ntp_timezone;
  // Syslog Config
  char syslog_server[16];
  unsigned int syslog_port;
  // Display
  char display_1[255];
  uint8_t scroll_delay;
  // Host config
  char hostname[16];
  // API Key
  char api_key[16];
};

#define CONFIG_FILE "/config.json"

File configFile;
Config config; // Global config object
// Global message buffers shared by Wifi and Scrolling functions
const uint8_t MESG_SIZE = 255;
const uint8_t CHAR_SPACING = 1;
const uint8_t SCROLL_DELAY = 75;

char curMessage[MESG_SIZE];
bool displayStringChanged=false;

// ************************************
// DEBUG_PRINT()
//
// send message via RSyslog (if enabled) or fallback on Serial
// ************************************
void DEBUG_PRINT(String message) {
#ifdef __DEBUG__
  if(!syslog.log(LOG_DEBUG,message)) {
    Serial.println(message);
  }
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
// connectToWifi()
//
// connect to configured WiFi network
// ************************************
bool connectToWifi() {
  uint8_t timeout=0;

  if(strlen(config.wifi_essid) > 0) {
    DEBUG_PRINT("[INIT] Connecting to "+String(config.wifi_essid));

    WiFi.begin(config.wifi_essid, config.wifi_password);

    while((WiFi.status() != WL_CONNECTED)&&(timeout < 10)) {
      delay(500);
      timeout++;
    }
    if(WiFi.status() == WL_CONNECTED) {
      DEBUG_PRINT("OK. IP:"+String(WiFi.localIP()));

      if (MDNS.begin(config.hostname)) {
        DEBUG_PRINT("[INIT] MDNS responder started");
        // Add service to MDNS-SD
        MDNS.addService("http", "tcp", 80);
      }
      
      NTP.onNTPSyncEvent([](NTPSyncEvent_t event) {
        ntpEvent = event;
        syncEventTriggered = true;
      });

      // NTP
      DEBUG_PRINT("[INIT] NTP sync time on "+String(config.ntp_server));
      NTP.begin(config.ntp_server, config.ntp_timezone, true, 0);
      NTP.setInterval(63);

      // Syslog server
      syslog.server(config.syslog_server,config.syslog_port);  
      syslog.deviceHostname(config.hostname);
      syslog.appName(config.hostname);
      syslog.defaultPriority(LOG_KERN);

      syslog.log(LOG_INFO, "-- Syslog begin");
      
      return true;  
    } else {
      DEBUG_PRINT("[ERROR] Failed to connect to WiFi");
      return false;
    }
  } else {
    DEBUG_PRINT("[ERROR] Please configure Wifi");
    return false; 
  }
}

String displayString="";

void getDisplayString() {
  DEBUG_PRINT("[DEBUG] updateDisplay()");

  displayString = String(config.display_1);
  if(displayString.startsWith("http")) { // Fetch data from rest server
    displayString = fetchDataSource(displayString);
  } else {
    DEBUG_PRINT("[DISPLAY] Show "+displayString);
  }
}

// ************************************
// updateDisplay()
//
// 
// ************************************

String months[12] = { "Gennaio","Febbraio","Marzo","Aprile","Maggio","Giugno","Luglio","Agosto","Settembre","Ottobre","Novembre","Dicembre" };
String dow[7] = { "Domenica","Lunedi","Martedi","Mercoledi","Giovedi","Venerdi","Sabato" };

void updateDisplay() {
  String displayText;
  time_t t = now(); // store the current time in time variable t

  displayText = displayString;
  
  displayText.replace("[h]",String(hour(t)));
  displayText.replace("[m]",String(minute(t)));
  displayText.replace("[s]",String(second(t)));
  displayText.replace("[D]",String(day(t)));
  displayText.replace("[DD]",dow[weekday(t)-1]);
  displayText.replace("[M]",String(month(t)));
  displayText.replace("[MM]",months[month(t)-1]);
  displayText.replace("[Y]",String(year(t)));
  
  strncpy(curMessage,displayText.c_str(),sizeof(curMessage));

  scrollText();
}

#define UPDATE_DISPLAY 60000*15 // Quarter of hour
Task updateDisplayTask(UPDATE_DISPLAY, TASK_FOREVER, &getDisplayString);

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
  Serial.print(FW_VERSION);
  Serial.println(BUILD);

    // Start scheduler
  runner.init();

  // Initialize SPIFFS
  if(!SPIFFS.begin()){
    DEBUG_PRINT("[SPIFFS] Mount Failed. Try formatting...");
    if(SPIFFS.format()) {
      DEBUG_PRINT("[SPIFFS] Initialized successfully");
    } else {
      DEBUG_PRINT("[SPIFFS] Fatal error: restart!");
      ESP.restart();
    }
  } else {
    DEBUG_PRINT("[SPIFFS] OK");
  }

  // Load configuration
  loadConfigFile();

  // Connect to WiFi network
  connectToWifi();

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
    DEBUG_PRINT(p);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    if(error == OTA_AUTH_ERROR) DEBUG_PRINT("[OTA] Auth Failed");
    else if(error == OTA_BEGIN_ERROR) DEBUG_PRINT("[OTA] Begin Failed");
    else if(error == OTA_CONNECT_ERROR) DEBUG_PRINT("[OTA] Connect Failed");
    else if(error == OTA_RECEIVE_ERROR) DEBUG_PRINT("[OTA] Recieve Failed");
    else if(error == OTA_END_ERROR) DEBUG_PRINT("[OTA] End Failed");
  });
  ArduinoOTA.setHostname(config.hostname);
  ArduinoOTA.begin();

  // Initialize web server on port 80
  initWebServer();
  
  // Display initialization
  mx.begin();
  mx.setShiftDataInCallback(scrollDataSource);
  mx.setShiftDataOutCallback(scrollDataSink);

  // Get display data
  runner.addTask(updateDisplayTask);
  updateDisplayTask.enable();  

  // GO !  
}

unsigned int last=0;

// ************************************
// LOOP()
//
// 
// ************************************
void loop() {
  // Handle OTA
  ArduinoOTA.handle();

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
    last = millis();
  }

  if(displayStringChanged) {
    getDisplayString();
    displayStringChanged = false;
  }
  
  updateDisplay();
}
