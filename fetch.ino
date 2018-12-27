// Written by Michele <o-zone@zerozone.it> Pinassi
// Released under GPLv3 - No any warranty
#include <ArduinoJson.h>

String fetchDataSource(String data_source) {
  WiFiClient client;
  String result, url;

  // First, remove http:// from data_source
  url = data_source.substring(data_source.indexOf('http:')+3).c_str();

  DEBUG_PRINT("[REST] fetchDataSource() from "+url);

  // Connect to HTTP server
  client.setTimeout(10000);
  if (!client.connect(url, 80)) {
    DEBUG_PRINT("Connection to "+url+" failed");
    return String("ERROR [IDX] Connection to "+url+" failed !");
  }

  // Send HTTP request
  client.println("GET /data.json HTTP/1.1");
  client.println("Host: "+url);
  client.println("Connection: close");
  
  if(client.println() == 0) {
    DEBUG_PRINT("Failed to send request");
    return String("ERROR [IDX] - Failed to send request");
  }

  // Check HTTP status
  char status[32] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
    return String("ERROR [IDX] - Unexpected response: data.json exists ?");
  }

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders)) {
    DEBUG_PRINT("Invalid response");
    return String("ERROR [IDX] - Invalid HTTP response");
  }

  // Allocate JsonBuffer
  // Use arduinojson.org/assistant to compute the capacity.
  const size_t capacity = JSON_OBJECT_SIZE(3) + JSON_ARRAY_SIZE(2) + 60;
  DynamicJsonBuffer jsonBuffer(capacity);

  // Parse JSON object
  JsonObject& root = jsonBuffer.parseObject(client);
  if (!root.success()) {
    DEBUG_PRINT("Parsing failed!");
    return String("ERROR [IDX] - Parsing of json failed");
  }

  // Extract values
  result = String(root["message"].as<char*>());
  DEBUG_PRINT("Response: "+result);
  
  // Disconnect
  client.stop();

  return result;
}
