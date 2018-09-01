#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include "DHT.h"
#include "ArduinoJson.h"
#include "secret.h"

///// WIFI /////
WiFiClient client;

const char *ssid =  WIFI_SSID;
const char *pass =  WIFI_Password;
/////

///// THINGSPEAK /////
String talkBackID = THINGSPEAK_talkbackID;
String talkbackAPI = THINGSPEAK_talkbackAPI;

String hotbox_api_key = HOTBOX_api;
String hotbox_channel_key = "261952";
String base_api = "http://api.thingspeak.com";
String update_uri = base_api + "/update";
String command_uri = base_api + "/talkbacks/" + talkBackID + "/commands/execute.json?api_key=" + talkbackAPI;
String light_state_uri = base_api + "/channels/" + hotbox_channel_key + "/fields/5/last.txt?api_key=" + hotbox_api_key;
String pump_state_uri = base_api + "/channels/" + hotbox_channel_key + "/fields/6/last.txt?api_key=" + hotbox_api_key;
/////

///// RELAY /////
int lights = 16;
int pump = 12;
/////

///// DHT /////
int DHTPin = 14;
#define DHTTYPE DHT11
DHT dht(DHTPin, DHTTYPE);
/////

///// JSON DECODING /////
const size_t MAX_CONTENT_SIZE = 512;       // max size of the HTTP response
/////

//// TASK SCHEDULER /////
#include <TaskScheduler.h>

// Callback methods prototypes
void processCommands();
void wifiConnect();
void doEnv();
void checkState();
//Tasks
int time_processCommands = 5000;
Task SC_processCommands(10000, TASK_FOREVER, &processCommands);
int time_wifiConnect = 10;
Task SC_wifiConnect(time_wifiConnect, TASK_FOREVER, &wifiConnect);
int time_doEnv = 900000;
Task SC_doEnv(time_doEnv, TASK_FOREVER, &doEnv);
int time_checkState = 60000;
Task SC_checkState(time_checkState, TASK_FOREVER, &checkState);
//Task t3(5000, TASK_FOREVER, &t3Callback);

Scheduler runner;
//////

void logger_lb(String type, String message, bool append) {
  if (append) {
    Serial.println(message);
  } else {
    Serial.println("[" + type + "] " + message);
  }
}

void logger(String type, String message, bool append) {
  if (append) {
    Serial.print(message);
  } else {
    Serial.print("[" + type + "] " + message);
  }
}

String httpPost(String uri, String apiKey, String postStr) {
  HTTPClient http;
  http.begin(uri);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("X-THINGSPEAKAPIKEY", hotbox_api_key);
  logger_lb("HTTP POST", "Connecting to " + uri, false);
  int httpCode = http.POST(postStr);
  String returnStr;
  logger_lb("HTTP POST", "Status Code: " + String(httpCode), false);
  if(httpCode == HTTP_CODE_OK) {
    returnStr = http.getString();
  } else {
    returnStr = "";
  }
  http.end();
  logger_lb("HTTP POST", "Got: " + returnStr, false);
  return returnStr;
}

String httpGet(String uri, String apiKey, String postStr) {
  uri = uri + postStr;
  HTTPClient http;
  http.begin(uri);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  logger_lb("HTTP POST", "Connecting to " + uri, false);
  int httpCode = http.GET();
  String returnStr;
  logger_lb("HTTP POST", "Status Code: " + String(httpCode), false);
  if(httpCode == HTTP_CODE_OK) {
    returnStr = http.getString();
  } else {
    returnStr = "";
  }
  http.end();
  logger_lb("HTTP POST", "Got: " + returnStr, false);
  return returnStr;
}

void turnOn(int pin) {
  digitalWrite(pin, HIGH);
}

void turnOff(int pin) {
  digitalWrite(pin, LOW);
}

void doCommand(String command) { 
  if (command == "DOENV") {
    doEnv();
  } else if (command == "CHECKSTATE") {
    checkState();
  } else if (command == "PUMPON") {
    turnOn(pump);
  } else if (command == "PUMPOFF") {
    turnOff(pump);
  } else if (command == "LIGHTSON") {
    turnOn(lights);
  } else if (command == "LIGHTSOFF") {
    turnOff(lights);
  }
}

void processCommands() {
  Serial.println("Fetching commands!");
  HTTPClient http;
  http.begin(command_uri);
  int httpCode = http.GET();

  // httpCode will be negative on error
  if(httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);

      // file found at server
      if(httpCode == HTTP_CODE_OK) {
          SC_processCommands.setInterval(time_processCommands);
          String payload = http.getString();
          Serial.print("Raw Command Payload: ");
          Serial.println(payload);


          const size_t BUFFER_SIZE =
          JSON_OBJECT_SIZE(5)    // the root object has 8 elements
           //+ JSON_OBJECT_SIZE(5)  // the "address" object has 5 elements
           //+ JSON_OBJECT_SIZE(2)  // the "geo" object has 2 elements
           //+ JSON_OBJECT_SIZE(3)  // the "company" object has 3 elements
           + MAX_CONTENT_SIZE;    // additional space for strings

          // Allocate a temporary memory pool
          DynamicJsonBuffer jsonBuffer(BUFFER_SIZE);
          JsonObject& root = jsonBuffer.parseObject(payload);

          String command_string = root["command_string"];

          if (command_string.length() != 0) {
            Serial.print("Command String: ");
            Serial.println(command_string);
            doCommand(command_string);
            SC_processCommands.setInterval(1000);
          } else {
            SC_processCommands.setInterval(time_processCommands);
            return;
          }
      }
  } else {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    SC_processCommands.setInterval(1000);
  }
  http.end();
}

void submit(String field1, String field2, String field3, String field4) {
  // TODO: Change to use http module
  // field1 -> Real Feel
  // field2 -> Humidity
  // field3 -> Temperature
  // field4 -> Dew Point
  String postStr = "";
  postStr +="&field1=";
  postStr += String(field1);
  postStr +="&field2=";
  postStr += String(field2);
  postStr +="&field3=";
  postStr += String(field3);
  postStr +="&field4=";
  postStr += String(field4);
  postStr += "\r\n\r\n";
  
  httpPost(update_uri, hotbox_api_key, postStr);
}

void checkState() {
  bool failed = false;
  String light_state = httpGet(light_state_uri, hotbox_api_key, "");
  logger_lb("Light State: ", String(light_state), false);

  if (light_state == "") {
    failed = true;
  } else if (light_state == "1") {
    turnOn(lights);
  } else {
    turnOff(lights);
  }

  String pump_state = httpGet(pump_state_uri, hotbox_api_key, "");
  logger_lb("Pump State: ", String(pump_state), false);
  
  if (pump_state == "") {
    failed = true;
  } else if (pump_state == "1") {
    turnOn(pump);
  } else {
    turnOff(pump);
  }

  if (failed) {
    SC_checkState.setInterval(10);
  } else {
    SC_checkState.setInterval(time_checkState);
  }
}

void doEnv() {
  float h = dht.readHumidity();
  // Read temperature as Celsius
  float t = dht.readTemperature();
  // Read temperature as Fahrenheit
  float f = dht.readTemperature(true);

  if (isnan(h) || isnan(t) || isnan(f)) {
    Serial.println("Failed to read from DHT sensor!");
    // Try again in 10ms
    SC_doEnv.setInterval(10);
  } else {
  // Compute heat index
  // Must send in temp in Fahrenheit!
  float hi = dht.computeHeatIndex(f, h);
  float hiDegC = dht.convertFtoC(hi);
  float dewP = dewPoint(t, h);
  submit(String(hiDegC), String(h), String(t), String(dewP));

  Serial.print("Humidity: ");
  Serial.print(h);
  Serial.println(" %\t");
  Serial.print("Temperature: ");
  Serial.print(t);
  Serial.println(" *C ");
  Serial.print(f);
  Serial.println(" *F\t");
  Serial.print("Heat index: ");
  Serial.print(hiDegC);
  Serial.println(" *C ");
  Serial.print(hi);
  Serial.println(" *F ");
  Serial.print("Dew Point (*C): ");
  Serial.println(dewP);

  // Make sure we're at the correct delay
  SC_doEnv.setInterval(time_doEnv);
  }
}

double dewPoint(double celsius, double humidity)
{
  // John Main added dewpoint code from : http://playground.arduino.cc/main/DHT11Lib
  // Also added DegC output for Heat Index.
  // dewPoint function NOAA
  // reference (1) : http://wahiduddin.net/calc/density_algorithms.htm
  // reference (2) : http://www.colorado.edu/geography/weather_station/Geog_site/about.htm
  //
  // (1) Saturation Vapor Pressure = ESGG(T)
  double RATIO = 373.15 / (273.15 + celsius);
  double RHS = -7.90298 * (RATIO - 1);
  RHS += 5.02808 * log10(RATIO);
  RHS += -1.3816e-7 * (pow(10, (11.344 * (1 - 1 / RATIO ))) - 1) ;
  RHS += 8.1328e-3 * (pow(10, (-3.49149 * (RATIO - 1))) - 1) ;
  RHS += log10(1013.246);

  // factor -3 is to adjust units - Vapor Pressure SVP * humidity
  double VP = pow(10, RHS - 3) * humidity;

  // (2) DEWPOINT = F(Vapor Pressure)
  double T = log(VP / 0.61078); // temp var
  return (241.88 * T) / (17.558 - T);
}

float readMoisture() {
  return analogRead(A0);
}

void wifiConnect () {
  if (WiFi.status() != WL_CONNECTED) {
    SC_wifiConnect.setInterval(5000);
    Serial.print("Connecting via WiFi to ");
    Serial.print(ssid);
    Serial.println("...");

    WiFi.begin(ssid, pass);

    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
      SC_wifiConnect.setInterval(time_wifiConnect);
      return;
    }

    Serial.println("");
    Serial.println("WiFi connect: Success");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    SC_wifiConnect.setInterval(time_wifiConnect);
  }
}

void setPinModes() {
  digitalWrite(pump, LOW);
  digitalWrite(lights, LOW);
  pinMode(pump, OUTPUT);
  pinMode(lights, OUTPUT);
  logger_lb("Setup / Pin Modes", "Outputs set to default.", false);
}

void setMain() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.begin(115200);
  dht.begin();
  setPinModes();
  
  logger_lb("Setup / Main", "Completed main setup.", false);
}

void setScheduler() {
  runner.init();
  runner.addTask(SC_wifiConnect);
  runner.addTask(SC_processCommands);
  runner.addTask(SC_doEnv);
  runner.addTask(SC_checkState);
  SC_wifiConnect.enable();
  SC_processCommands.enable();
  SC_doEnv.enable();
  SC_checkState.enable();
  logger_lb("Setup / Scheduler", "Initialized scheduler.", false);
}

void setup() {
  setMain();
  setScheduler();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    SC_wifiConnect.enable();
  }
  runner.execute();
}


