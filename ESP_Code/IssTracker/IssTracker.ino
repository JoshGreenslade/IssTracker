#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Configuration
const char *ssid = "VM4514059";
const char *password = "rnk8dddwSzG7";
const String myLat = "52.954784";
const String myLon = "-1.158109";
const String myAlt = "46";
const String nPass = "1";
const int apiPollRate = 5000;      //ms
const int screenSwitchRate = 5000; //ms
const int updateRate = 500;        //ms
const bool DEBUGMode = false;

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
long polledUnixTimestamp;

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

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

struct issTel
{
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

bool fill = false;

void display_position(float iss_position_latitude, float iss_position_longitude)
{
  // Display the position of the ISS
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("Lat:");
  display.print(iss_position_latitude);
  display.setCursor(0, 17);
  display.print("Lon:");
  display.print(iss_position_longitude);
  display.display();
}

void display_pass(long response_0_risetime, long timestamp)
{
  // Display the Next pass
  long nextPass = response_0_risetime - timestamp;

  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(9, 0);
  display.print("Next Pass");
  display.setCursor(42, 17);
  display.print(nextPass);
  display.print("s");

  display.display();
}

bool display_iss_overhead(bool fill)
{
  display.clearDisplay();

  if (fill)
  {
    display.fillScreen(WHITE);
    display.setTextColor(BLACK);
    display.drawFastHLine(2, 3, 128 * (unixTimestamp - response_0_risetime) / (response_0_duration), BLACK);
    display.fillCircle(128 * (unixTimestamp - response_0_risetime) / (response_0_duration), 3, 2, BLACK);
  }
  else
  {
    display.setTextColor(WHITE);
    display.drawFastHLine(2, 3, 128 * (unixTimestamp - response_0_risetime) / (response_0_duration), WHITE);
    display.fillCircle(128 * (unixTimestamp - response_0_risetime) / (response_0_duration), 3, 2, WHITE);
  }
  display.setTextSize(2);
  display.setCursor(10, 8);
  display.print("Overhead!");
  display.display();
  if (fill)
  {
    return false;
  }
  else
  {
    return true;
  }
}

DynamicJsonDocument make_api_request(DynamicJsonDocument doc, String payload)
{
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

long getUnixTimeStamp()
{
  DEBUGunixTimestamp = make_api_request(DEBUGunixTimestamp, DEBUGpayloadUnixTimeStamp);
  unixTimestamp = DEBUGunixTimestamp["UnixTimeStamp"];
  Serial.println("Polling Timestamp");
  return unixTimestamp;
}

struct issTel getIssPosVel(DynamicJsonDocument issPosNew, DynamicJsonDocument issPosOld)
{
  struct issTel issTelInstance;

  const float iss_position_latitude_new = issPosNew["iss_position"]["latitude"];   // "-49.6816"
  const float iss_position_longitude_new = issPosNew["iss_position"]["longitude"]; // "93.3567"
  const float iss_position_latitude_old = issPosOld["iss_position"]["latitude"];   // "-49.6816"
  const float iss_position_longitude_old = issPosOld["iss_position"]["longitude"];

  issTelInstance.vlat = (iss_position_latitude_new - iss_position_latitude_old) / apiPollRate;
  issTelInstance.vlon = (iss_position_longitude_new - iss_position_longitude_old) / apiPollRate;
  issTelInstance.lat = iss_position_latitude_new;
  issTelInstance.lon = iss_position_longitude_new;
  issTelInstance.timestamp = issPosNew["timestamp"];

  return issTelInstance;
}

void setup()
{

  Serial.begin(9600);

  //  Setup Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ; // Don't proceed, loop forever
  }

  if (DEBUGMode)
  {
    display.clearDisplay();
    display.setTextSize(4);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.print("DEBUG");
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
  issPass = make_api_request(issPass, payloadIssPass + "&datetime=" + prevPassPoll);
  response_0_duration = issPass["response"][0]["duration"]; // 654
  response_0_risetime = issPass["response"][0]["risetime"]; // 1597576525
  timeToPassComplete = (response_0_risetime + response_0_duration - issCurrentTel.timestamp) * 1000;

  // If we're in debug mode, just grap the unixTimestamp and set the pass time to be 30 seconds from now
  if (DEBUGMode)
  {
    polledUnixTimestamp = getUnixTimeStamp();
    unixTimestamp = polledUnixTimestamp;
    response_0_duration = 20;
    response_0_risetime = unixTimestamp + 60;
    timeToPassComplete = (response_0_duration + response_0_risetime - unixTimestamp) * 1000;
  }
}

void loop()
{
  // System state changer
  if (millis() - prevSwitch > screenSwitchRate)
  {
    prevSwitch = millis();
    systemState = systemStates[iState % nStates];
    iState += 1;
    if (iState == nStates)
    {
      iState = 0;
    }
  }
  if (unixTimestamp > response_0_risetime)
  {
    Serial.println("ISS Overhead Detected");
    systemState = "issOverhead";
  }

  // Repoll the Position API
  if (millis() - prevPoll > apiPollRate)
  {
    Serial.println("Polling Positional API");
    issPosOld = issPosNew;
    issPosNew = make_api_request(issPosNew, payloadIssPos);
    issCurrentTel = getIssPosVel(issPosNew, issPosOld);
    polledUnixTimestamp = getUnixTimeStamp();
    unixTimestamp = polledUnixTimestamp;
    prevPoll = millis();
  }

  // Repoll the pass API
  if (millis() - prevPassPoll > timeToPassComplete)
  {
    Serial.println("Polling Pass API");
    prevPassPoll = millis();
    issPass = make_api_request(issPass, payloadIssPass + "&datetime=" + prevPassPoll);
    response_0_duration = issPass["response"][0]["duration"]; // 654
    response_0_risetime = issPass["response"][0]["risetime"]; // 1597576525
    timeToPassComplete = (response_0_risetime + response_0_duration - unixTimestamp) * 1000;

    // If the next risetime is in the pass, poll every 10 seconds till we get a new response
    if (response_0_risetime < unixTimestamp)
    {
      timeToPassComplete = 10000;
    }

    // If we're debugging, set the risetime to be in 1 minute, and the duration to be 30 seconds
    if (DEBUGMode)
    {
      response_0_duration = 20;
      response_0_risetime = unixTimestamp + 60;
      timeToPassComplete = (response_0_duration + response_0_risetime - unixTimestamp) * 1000;
    }
  }

  // Update the position
  currentTime = millis();
  dt = (currentTime - prevPoll);
  iss_position_latitude = issCurrentTel.lat + issCurrentTel.vlat * dt;
  iss_position_longitude = issCurrentTel.lon + issCurrentTel.vlon * dt;
  unixTimestamp = polledUnixTimestamp + (dt / 1000);

  // System State Iterator
  if (systemState == "displayPos")
  {
    display_position(iss_position_latitude, iss_position_longitude);
  }
  else if (systemState == "displayPass")
  {
    display_pass(response_0_risetime, unixTimestamp);
  }
  else if (systemState == "issOverhead")
  {
    fill = display_iss_overhead(fill);
  }

  // Printing to the Serial Monitor
  Serial.println(" ");

  Serial.print("Timestamp: ");
  Serial.println(unixTimestamp);

  Serial.print("System State: ");
  Serial.println(systemState);

  Serial.print("Next pass in in: ");
  Serial.println(response_0_risetime - unixTimestamp);

  Serial.print("Will poll pass API in: ");
  Serial.println((timeToPassComplete - (millis() - prevPassPoll)) / 1000);

  Serial.print("Will poll pos API in: ");
  Serial.println((apiPollRate - (millis() - prevPoll)) / 1000);

  delay(updateRate);
}
