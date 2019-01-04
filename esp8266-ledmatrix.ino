//
// Led Matrix banner
// Michele <o-zone@zerozone.it> Pinassi
//
// Connections for ESP8266 hardware SPI are:
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

// Task Scheduler
#include <TaskScheduler.h>

// ArduinoJson
// https://arduinojson.org/
#include <ArduinoJson.h>

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
#define FW_VERSION      "0.0.4"

#define DEFAULT_WIFI_ESSID "led-module"
#define DEFAULT_WIFI_PASSWORD ""

// File System
#include <FS.h>   

// Format SPIFFS if mount failed
#define FORMAT_SPIFFS_IF_FAILED 1

// Web server
AsyncWebServer server(80);

// NTP ClientLib 
// https://github.com/gmag11/NtpClient
#include <NtpClientLib.h>

#define MAX_DISPLAY_MESSAGES 4

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
  String display[MAX_DISPLAY_MESSAGES];
  // Display properties
  uint8_t scroll_delay;
  unsigned int light_trigger;
  // OpenWeatherMap
  char owm_apikey[36];
  int owm_cityid;
  // Host config
  char hostname[16];
  // API Key
  char api_key[16];
};

#define CONFIG_FILE "/config.json"

#define LIGHT_TRIGGER_DELTA 50

File configFile;
Config config; // Global config object
// Global message buffers shared by Wifi and Scrolling functions
const uint8_t MESG_SIZE = 255;
const uint8_t CHAR_SPACING = 1;
const uint8_t SCROLL_DELAY = 75;

char curMessage[MESG_SIZE];
uint8_t messageIdx=0;
bool displayStringChanged=false;
bool isAPMode=false;

IPAddress myIP;

unsigned int lightSensorValue=0,lastLightSensorValue=0;
unsigned long displayCycles=0;

String owmStatus="", owmCityName="", owmForecast="", owmForecastT="",owmForecastH="", owmNow="", owmNowT="", owmNowH="";

// Scheduler and tasks...
Scheduler runner;

#define OWM_INTERVAL 60000*30 // Every 30 minutes...
void owmCallback();
Task owmTask(OWM_INTERVAL, TASK_FOREVER, &owmCallback);


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
// runWifiAP()
//
// run Wifi AccessPoint, to let user use and configure without a WiFi AP
// ************************************
void runWifiAP() {
  DEBUG_PRINT("[DEBUG] runWifiAP() ");
  WiFi.mode(WIFI_AP_STA); 
  WiFi.softAP(DEFAULT_WIFI_ESSID,DEFAULT_WIFI_PASSWORD);  
 
  myIP = WiFi.softAPIP(); //Get IP address

  DEBUG_PRINT("[INIT] WiFi Hotspot ESSID: "+String(DEFAULT_WIFI_ESSID));
  DEBUG_PRINT("[INIT] WiFi password: "+String(DEFAULT_WIFI_PASSWORD));
  DEBUG_PRINT("[INIT] Server IP: "+String(myIP));

  WiFi.printDiag(Serial);

  if (MDNS.begin(config.hostname)) {
    DEBUG_PRINT("[INIT] MDNS responder started for hostname "+String(config.hostname));
    // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);
  }
  isAPMode=true;
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

    isAPMode=false;
    
    WiFi.hostname(config.hostname);

    WiFi.begin(config.wifi_essid, config.wifi_password);

    while((WiFi.status() != WL_CONNECTED)&&(timeout < 10)) {
      delay(500);
      timeout++;
    }
    if(WiFi.status() == WL_CONNECTED) {
      DEBUG_PRINT("OK. IP:"+String(WiFi.localIP()));
      DEBUG_PRINT("Signal strength (RSSI):"+String(WiFi.RSSI())+" dBm");

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
    // 
    runWifiAP();
  }
}

String displayString="";

// ************************************
// updateDisplay()
//
// 
// ************************************

String months[12] = { "Gennaio","Febbraio","Marzo","Aprile","Maggio","Giugno","Luglio","Agosto","Settembre","Ottobre","Novembre","Dicembre" };
String dow[7] = { "Domenica","Lunedi","Martedi","Mercoledi","Giovedi","Venerdi","Sabato" };

String compileString(String source) {
  time_t t = now(); // store the current time in time variable t
  time_t diff;
  String countdown="",temp="";
  int8_t idx;

  source.replace("[h]",String(hour(t)));
  source.replace("[m]",String(minute(t)));
  source.replace("[s]",String(second(t)));
  source.replace("[D]",String(day(t)));
  source.replace("[DD]",dow[weekday(t)-1]);
  source.replace("[M]",String(month(t)));
  source.replace("[MM]",months[month(t)-1]);
  source.replace("[Y]",String(year(t)));
  source.replace("[UPTIME]",String(millis()));
  source.replace("[IDX]",String(messageIdx));
  source.replace("[CYCLES]",String(displayCycles));
  
  idx = source.indexOf("[OWM");
  if(idx >= 0) {  
    if(strlen(config.owm_apikey) <= 0) {
      source = "*** Configure OpenWeatherMap API Key ***";
    } else if(owmStatus.compareTo("OK") == 0) {
      source.replace("[OWM-CITY-NAME]",owmCityName);
      source.replace("[OWM-NOW-T]",owmNowT);
      source.replace("[OWM-NOW-H]",owmNowH);
      source.replace("[OWM-NOW]",owmNow);
      source.replace("[OWM-FORECAST-T]",owmForecastT);
      source.replace("[OWM-FORECAST-H]",owmForecastH);
      source.replace("[OWM-FORECAST]",owmForecast);
    } else {
      source = "*** Please wait: "+owmStatus;      
    }
  }
  
  idx = source.indexOf("[COUNTDOWN:");
  if(idx >= 0) {
    temp = source.substring(idx);
    idx = temp.indexOf("]");
    temp = temp.substring(11,idx);
    diff = abs(t - atol(temp.c_str()));

    if(day(diff) > 0) {
      countdown = day(diff)+" giorni";    
    }
    if(hour(diff) > 0) {
      countdown = countdown+", "+hour(diff)+" ore";
    }
    if(minute(diff) > 0) {
      countdown = countdown+", "+minute(diff)+" minuti";
    }      
    if(second(diff) > 0) {
      countdown = countdown+", "+second(diff)+" secondi";
    }
    source.replace("[COUNTDOWN:"+temp+"]",countdown);
  }
  return source;
}

void updateDisplayCb() {
  DEBUG_PRINT("[DEBUG] updateDisplayCb()");
  bool i=true;

  if(isAPMode) {
    // Modalità Access Point abilitata per la prima configurazione
    displayString = "Connect via WiFi to IP "+String(myIP)+" and configure me!";
    return;
  }
  
  if(WiFi.status() != WL_CONNECTED) { 
    displayString = "Connecting to WiFi "+String(config.wifi_essid)+"...";
    return;
  }

  if(config.display[0].length() == 0) {
    displayString = "Need at least one message: connect to "+String(WiFi.localIP());
    return;
  }

  while(i) {
    if(config.display[messageIdx].length() > 0) {
      displayString = config.display[messageIdx];
    
      if(displayString.startsWith("http")) { // Fetch data from rest server
        displayString = fetchDataSource(displayString);
      }
      i = false;
    }
    messageIdx++;
    if(messageIdx >= MAX_DISPLAY_MESSAGES) {
      messageIdx=0;
    }
  }
 
  DEBUG_PRINT("[DISPLAY] Now displaying "+displayString);
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

  // Display initialization
  mx.begin();
  mx.setShiftDataInCallback(scrollDataSource);
  mx.setShiftDataOutCallback(scrollDataSink);
  mx.clear();
  mx.control(MD_MAX72XX::INTENSITY, MAX_INTENSITY/2);
  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);

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

  // Add OpenWeatherMap callback for periodic fetch of weather forecast, every 60xN seconds...
  runner.addTask(owmTask);
  owmTask.enable();
  
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

  // Tune display brightness
  lightSensorValue = analogRead(A0);
  if(abs(lightSensorValue - lastLightSensorValue) > LIGHT_TRIGGER_DELTA) {
    lastLightSensorValue = lightSensorValue;
    if(lightSensorValue > config.light_trigger) {
      mx.control(MD_MAX72XX::INTENSITY, MAX_INTENSITY/2);
    } else {
      mx.control(MD_MAX72XX::INTENSITY, MAX_INTENSITY/8);
    }
  }

  /* Now compile displayString string, that will be showned on display... */
  String displayText = compileString(displayString);
  
  // Now parse meta commands, if present
  if(displayText.length() > 0) {  
    strncpy(curMessage,displayText.c_str(),MESG_SIZE);
  } else {
    sprintf(curMessage,"...");
  }
  
  scrollText();
}
