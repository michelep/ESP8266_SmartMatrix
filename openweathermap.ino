// Written by Michele <o-zone@zerozone.it> Pinassi
// Released under GPLv3 - No any warranty
//
// Code inspired by the work from https://randomnerdtutorials.com/esp8266-weather-forecaster/
//
// Example JSON reply:
// {"cod":"200","message":0.0027,"cnt":2,"list":[
//  {"dt":1546560000,
//      "main": {"temp":-4.25,"temp_min":-7.89,"temp_max":-4.25,"pressure":1001.73,"sea_level":1042.64,"grnd_level":1001.73,"humidity":71,"temp_kf":3.64},
//      "weather":[{"id":800,"main":"Clear","description":"clear sky","icon":"02n"}],
//      "clouds":{"all":8},
//      "wind":{"speed":0.66,"deg":252.503},
//      "sys":{"pod":"n"},
//      "dt_txt":"2019-01-04 00:00:00"
//  },{"dt":1546570800,
//      "main":{"temp":-5.65,"temp_min":-8.38,"temp_max":-5.65,"pressure":1001.58,"sea_level":1042.56,"grnd_level":1001.58,"humidity":74,"temp_kf":2.73},
//      "weather":[{"id":800,"main":"Clear","description":"clear sky","icon":"01n"}],
//      "clouds":{"all":0},
//      "wind":{"speed":0.97,"deg":332.503},
//      "sys":{"pod":"n"},
//      "dt_txt":"2019-01-04 03:00:00"
//  }],"city":{"id":3166548,"name":"Siena","coord":{"lat":43.32,"lon":11.3328},"country":"IT"}}
#include <ArduinoJson.h>

void owmCallback() {
  int jsonend = 0;
  boolean startJson = false;
  String text;

  DEBUG_PRINT("owmCallback()");

  if(strlen(config.owm_apikey) > 0) {
    WiFiClient client;
  
    if (client.connect("api.openweathermap.org", 80)) {
      client.println("GET /data/2.5/forecast?id=" + String(config.owm_cityid) + "&APPID=" + String(config.owm_apikey) + "&mode=json&units=metric&cnt=2 HTTP/1.1");
      client.println("Host: api.openweathermap.org");
      client.println("User-Agent: "+String(FW_NAME)+"/"+String(FW_VERSION));
      client.println("Connection: close");
      client.println();
    
      unsigned long timeout = millis();
      while (client.available() == 0) {
        if (millis() - timeout > 5000) {
          owmStatus = "[FATAL] Client timeout while connecting to OpenWeatherMap servers";
          client.stop();
          return;
        }
      }
    
      char c = 0;
      while (client.available()) {
        c = client.read();
        // since json contains equal number of open and close curly brackets, this means we can determine when a json is completely received  by counting
        // the open and close occurences,
        // Serial.print(c);
        if (c == '{') {
          startJson = true;         // set startJson true to indicate json message has started
          jsonend++;
        }
        if (c == '}') {
          jsonend--;
        }
        if (startJson == true) {
          text += c;
        }
        // if jsonend = 0 then we have have received equal number of curly braces 
        if (jsonend == 0 && startJson == true) {
          parseJson(text.c_str());  // parse c string text in parseJson function
          text = "";                // clear text string for the next time
          startJson = false;        // set startJson to false to indicate that a new message has not yet started
        }
      }
    } else {
      // if no connction was made:
      owmStatus = "[FATAL] Connection to OpenWeatherMap failed";
      return;
    }
  }
}

void parseJson(const char * jsonString) {
  //StaticJsonBuffer<4000> jsonBuffer;
  const size_t bufferSize = 2*JSON_ARRAY_SIZE(1) + JSON_ARRAY_SIZE(2) + 4*JSON_OBJECT_SIZE(1) + 3*JSON_OBJECT_SIZE(2) + 3*JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(5) + 2*JSON_OBJECT_SIZE(7) + 2*JSON_OBJECT_SIZE(8) + 720;
  DynamicJsonBuffer jsonBuffer(bufferSize);
  char val[16];

  // FIND FIELDS IN JSON TREE
  JsonObject& root = jsonBuffer.parseObject(jsonString);
  if (!root.success()) {
    owmStatus = "[FATAL] parseObject() failed";
    return;
  }

  JsonArray& list = root["list"];
  JsonObject& nowT = list[0];
  JsonObject& later = list[1];

  // nowT.printTo(Serial);
  // later.printTo(Serial); 

  // including temperature and humidity for those who may wish to hack it in
  
  String owmCityName = root["city"]["name"];

  owmNowT = nowT["main"]["temp"].as<String>();
  owmNowH = nowT["main"]["humidity"].as<String>();
  owmNow = nowT["weather"][0]["description"].as<String>();

  owmForecastT = later["main"]["temp"].as<String>();
  owmForecastH = later["main"]["humidity"].as<String>();
  owmForecast = later["weather"][0]["description"].as<String>();
  
  owmStatus="OK";
}



