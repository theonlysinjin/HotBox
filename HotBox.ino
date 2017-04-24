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
String apiKey = THINGSPEAK_api;
const char* server = "api.thingspeak.com";

String talkBackID = THINGSPEAK_talkbackID;
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

///// TIMING /////
long oneMinute = 60000;
long endMsTimer = millis();
int minutes = 0;
int cmdCheckTimer = -1;
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
//Tasks
int time_processCommands = 5000;
Task SC_processCommands(10000, TASK_FOREVER, &processCommands);
int time_wifiConnect = 10;
Task SC_wifiConnect(time_wifiConnect, TASK_FOREVER, &wifiConnect);
int time_doEnv = 900000;
Task SC_doEnv(time_doEnv, TASK_FOREVER, &doEnv);
//Task t3(5000, TASK_FOREVER, &t3Callback);

Scheduler runner;
//////

void doCommand(String command) {
  if (command == "DOENV") {
    doEnv();
  }
// Relay
  if (command == "PUMPON") {
    digitalWrite(pump, HIGH);
  }
  if (command == "PUMPOFF") {
    digitalWrite(pump, LOW);
  }
  if (command == "LIGHTSON") {
    digitalWrite(lights, HIGH);
  }
  if (command == "LIGHTSOFF") {
    digitalWrite(lights, LOW);
  }
}

void processCommands() {
  Serial.println("Fetching commands!");
  HTTPClient http;
  http.begin("http://api.thingspeak.com/talkbacks/" + talkBackID + "/commands/execute.json?api_key=" + apiKey);
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
  if (client.connect(server,80)) { // "184.106.153.149" or api.thingspeak.com
    String postStr = apiKey;
    postStr +="&field1=";
    postStr += String(field1);
    postStr +="&field2=";
    postStr += String(field2);
    postStr +="&field3=";
    postStr += String(field3);
    postStr +="&field4=";
    postStr += String(field4);
    postStr += "\r\n\r\n";
    
    client.print("POST /update HTTP/1.1\n");
    client.print("Host: api.thingspeak.com\n");
    client.print("Connection: close\n");
    client.print("X-THINGSPEAKAPIKEY: "+apiKey+"\n");
    client.print("Content-Type: application/x-www-form-urlencoded\n");
    client.print("Content-Length: ");
    client.print(postStr.length());
    client.print("\n\n");
    client.print(postStr);
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

void setup() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.begin(115200);
  pinMode(pump, OUTPUT);
  pinMode(lights, OUTPUT);
  digitalWrite(pump, LOW);
  digitalWrite(lights, LOW);
  dht.begin();

  runner.init();
  runner.addTask(SC_wifiConnect);
  runner.addTask(SC_processCommands);
  runner.addTask(SC_doEnv);
  SC_wifiConnect.enable();
  SC_processCommands.enable();
  SC_doEnv.enable();
  Serial.println("Initialized scheduler");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    SC_wifiConnect.enable();
  }
  runner.execute();
}


