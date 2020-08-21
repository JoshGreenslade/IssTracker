#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Configuration
const char* ssid = "VM4514059";
const char* password = "rnk8dddwSzG7";
const String myLat = "52.954784";
const String myLon = "-1.158109";
const String myAlt = "46";
const String nPass = "1";
const int apiPollRate = 5000; //ms
const int screenSwitchRate = 10000; //ms
const int updateRate = 1000; //ms


// Setup  
const size_t capacityIssPass = JSON_ARRAY_SIZE(1) + JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(5) + 110;
const size_t capacityIssPos = JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(3) + 90;
const size_t DEBUGcapacityTime = JSON_OBJECT_SIZE(1) + 30;
String payloadIssPass = "http://api.open-notify.org/iss-pass.json?lat=" + myLat + "&lon=" + myLon + "&alt=" + myAlt + "&n=" + nPass;
String payloadIssPos = "http://api.open-notify.org/iss-now.json";
String DEBUGpayloadUnixTimeStamp = "http://showcase.api.linx.twenty57.net/UnixTime/tounixtimestamp?datetime=now";
DynamicJsonDocument issPass(capacityIssPass);
DynamicJsonDocument issPosOld(capacityIssPos);
DynamicJsonDocument issPosNew(capacityIssPos);
DynamicJsonDocument DEBUGunixTimestamp(DEBUGcapacityTime);
long unixTimestamp;

String systemStates[] = {"displayPos", "displayPass", "issOverhead"};
String systemState = systemStates[0];
int nStates = 2;
int iState = 0;

long prevSwitch = 0;
long prevPoll = 0;
long currentTime = 0;
long dt = 0;
long prevPassPoll = 0;
long timeToPassComplete;
long timeStamp = 0;

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

struct issTel {
  float lat;
  float lon;
  float vlat;
  float vlon;
  long timestamp;
};
float iss_position_latitude = 0;
float iss_position_longitude = 0;
struct issTel issCurrentTel;

int response_0_duration = 0;
long response_0_risetime = 0;


void display_position(float iss_position_latitude, float iss_position_longitude) {
  // Display the position of the ISS
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("ISS Position");
  display.setCursor(0, 11);
  display.print("Lat: " );
  display.print(iss_position_latitude);
  display.setCursor(0, 22);
  display.print("Lon: " );
  display.print(iss_position_longitude);
  display.display();
}


void display_pass(long response_0_risetime, long timestamp) {
  // Display the Next pass
  long nextPass = response_0_risetime - timestamp;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("ISS Next Pass");
  display.setCursor(0, 11);
  display.print(nextPass);
  display.print(" seconds." );
  
  display.display();
}

void display_iss_overhead() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("ISS Overhead!");
  display.display();
}


DynamicJsonDocument make_api_request(DynamicJsonDocument doc, String payload) {
  //  Setup
  HTTPClient http;

  //  Make a call to the API.
  http.begin(payload);
  int httpCode = http.GET();

  //  If okay, return the JsonObject
  if (httpCode > 0)
  {
    String json = http.getString();
    deserializeJson(doc, json);
  }
  return doc;
}


struct issTel getIssPosVel(DynamicJsonDocument issPosNew, DynamicJsonDocument issPosOld) {
  struct issTel issTelInstance;

  const float iss_position_latitude_new = issPosNew["iss_position"]["latitude"]; // "-49.6816"
  const float iss_position_longitude_new = issPosNew["iss_position"]["longitude"]; // "93.3567"
  const float iss_position_latitude_old = issPosOld["iss_position"]["latitude"]; // "-49.6816"
  const float iss_position_longitude_old = issPosOld["iss_position"]["longitude"];

  issTelInstance.vlat = (iss_position_latitude_new - iss_position_latitude_old) / apiPollRate;
  issTelInstance.vlon = (iss_position_longitude_new - iss_position_longitude_old) / apiPollRate;
  issTelInstance.lat = iss_position_latitude_new;
  issTelInstance.lon = iss_position_longitude_new;
  issTelInstance.timestamp = issPosNew["timestamp"];

  return issTelInstance;
}


void setup() {

  Serial.begin(9600);

  //  Setup Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Don't proceed, loop forever
  }
  display.display();
  delay(2000); // Pause for 2 seconds


  //  Setup Wifi
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.println("Connecting...");
  }

  // Initalise the iss positionining system
  issPosOld = make_api_request(issPosOld, payloadIssPos);
  delay(1000);
  issPosNew = make_api_request(issPosNew, payloadIssPos);
  issCurrentTel = getIssPosVel(issPosNew, issPosOld);

  // Initalise the iss pass system
  issPass = make_api_request(issPass, payloadIssPass+"&datetime="+prevPassPoll);
  response_0_duration = issPass["response"][0]["duration"]; // 654
  response_0_risetime = issPass["response"][0]["risetime"]; // 1597576525
  timeToPassComplete = (response_0_risetime + response_0_duration - issCurrentTel.timestamp)*1000;
//  DEBUGunixTimestamp = make_api_request(DEBUGunixTimestamp, DEBUGpayloadUnixTimeStamp);
//  unixTimestamp = DEBUGunixTimestamp["UnixTimeStamp"];

}

void loop()
{
  currentTime = millis();

  // System state changer
  if (millis() - prevSwitch > screenSwitchRate) {
    prevSwitch = millis();
    systemState = systemStates[iState % nStates];
    iState += 1;
    if (iState == nStates) {
      iState = 0;
    }
  }
  if (timeStamp > response_0_risetime) {
    Serial.println("ISS Overhead Detected");
    systemState = "issOverhead";
  }

  // Repoll the Position API
  if (millis() - prevPoll > apiPollRate) {
    Serial.println("Polling Positional API");
    prevPoll = millis();
    issPosOld = issPosNew;
    issPosNew = make_api_request(issPosNew, payloadIssPos);
    issCurrentTel = getIssPosVel(issPosNew, issPosOld);
  }

  // Repoll the pass API
  if (millis() - prevPassPoll > timeToPassComplete) {
    Serial.println("Polling Pass API");
    prevPassPoll = millis();
    issPass = make_api_request(issPass, payloadIssPass+"&datetime="+prevPassPoll);
    response_0_duration = issPass["response"][0]["duration"]; // 654
    response_0_risetime = issPass["response"][0]["risetime"]; // 1597576525
    timeToPassComplete = (response_0_risetime + response_0_duration - issCurrentTel.timestamp)*1000;
    if (response_0_risetime < issCurrentTel.timestamp) {
      timeToPassComplete = 0;
    }

//    DEBUGunixTimestamp = make_api_request(DEBUGunixTimestamp, DEBUGpayloadUnixTimeStamp);
//    unixTimestamp = DEBUGunixTimestamp["UnixTimeStamp"];
//    Serial.print("utp: ");
//    Serial.println(unixTimestamp);
//    response_0_duration = 20;
//    response_0_risetime = unixTimestamp + 50;
//    timeToPassComplete = (response_0_duration + response_0_risetime - unixTimestamp)*1000;
  }

  // Update the position
  dt = (currentTime - prevPoll);
  iss_position_latitude = issCurrentTel.lat + issCurrentTel.vlat * dt;
  iss_position_longitude = issCurrentTel.lon + issCurrentTel.vlon * dt;
  long timeStamp = issCurrentTel.timestamp + dt/1000;

  // System State Iterator
  if (systemState == "displayPos") {
    display_position(iss_position_latitude, iss_position_longitude);
  }
  else if (systemState == "displayPass") {
    display_pass(response_0_risetime, issCurrentTel.timestamp + dt/1000);
  }
  else if (systemState == "issOverhead"){
    display_iss_overhead();
  }
  Serial.println(" ");

  Serial.print("Timestamp: ");
  Serial.println(timeStamp);
  
  Serial.print("System State: ");
  Serial.println(systemState);

  Serial.print("Next pass in in: ");
  Serial.println(response_0_risetime - timeStamp);
  
  Serial.print("Will poll pass API in: ");
  Serial.println((timeToPassComplete - (millis() - prevPassPoll))/1000);

  Serial.print("Will poll pos API in: ");
  Serial.println((apiPollRate - (millis() - prevPoll))/1000);
  
  delay(updateRate);
}
