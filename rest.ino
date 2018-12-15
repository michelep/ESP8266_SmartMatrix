// Written by Michele <o-zone@zerozone.it> Pinassi
// Released under GPLv3 - No any warranty

// restClient
// https://github.com/DaKaZ/esp8266-restclient
#include "RestClient.h"  

String fetchDataSource(String data_source) {
  char query[128];
  unsigned int statusCode;
  String response="";
  
  RestClient restClient = RestClient(data_source.c_str()); 

  restClient.setContentType("application/x-www-form-urlencoded");

  DEBUG_PRINT("[REST] fetchDataSource() from "+data_source);
  
  if(strlen(config.api_key) > 0) {
    sprintf(query,"key=%s",config.api_key);

    statusCode = restClient.post("/rest/fetch", query, &response);

    DEBUG_PRINT("[REST] Status code from server: "+String(statusCode));
    DEBUG_PRINT("[REST] Response body from server: "+String(response));
    
    if((statusCode >= 200)&&(statusCode < 300)) {
      StaticJsonBuffer<200> jsonBuffer;

      int firstJsonBr = response.indexOf('{');
      int lastJsonBr = response.lastIndexOf('}');
    
      JsonObject& root = jsonBuffer.parseObject(response);
  
      if(root.success()) {
        return root["message"];
      } else {
        DEBUG_PRINT("[REST] Error parsing json response");
      }
    } 
  } else {
    DEBUG_PRINT("[REST] Api key not set");
  }
}
