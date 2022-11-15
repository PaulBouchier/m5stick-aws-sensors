/*
  Copyright 2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
  Permission is hereby granted, free of charge, to any person obtaining a copy of this
  software and associated documentation files (the "Software"), to deal in the Software
  without restriction, including without limitation the rights to use, copy, modify,
  merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so.
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
  INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
  PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/


#include "secrets.h"
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>
#include "WiFi.h"
#include <M5StickCPlus.h>
#include <esp_timer.h>

// Display variables
int lineCnt = 0;
int nextLcdUpdateTime = 0;

// button variables
const int button = 37;
int last_value = 0;
int cur_value = 0;

// Flow sensor measurement variables
const float pulsesPerLiter = 60 * 6.6;
const int reportingPeriodSec = 60;  // FIXME set to 10 minutes

bool flowSensorOutput;
bool lastFlowSensorOutput;
int64_t flowCount = 0;  // how many pulses have been seen this period
float litersIn24hrs = 0;
float litersSinceStart = 0;
int nextPeriodTime = reportingPeriodSec;

// water-flow simulator variables
const bool simulateFlow = true;  // FIXME: change to false for real device
const int flowOffDurationSec = 30;  // how long water should flow for
const int flowOnDurationSec = 30;   // how long water should not flow for
const int flowSensorHalfPeriod = 2525; // 2.525 msec for half a cycle at 30L/min

int64_t secondsSinceStart = 0;
int64_t nextSecondTime;
int64_t nextFlowSensorTransition;
bool simFlowSensorOutput = false;
int nextFlowToggleTime;
bool flowState = false;

// The MQTT topics that this device should publish/subscribe
#define AWS_IOT_PUBLISH_TOPIC   "esp32/pub"
#define AWS_IOT_SUBSCRIBE_TOPIC "esp32/sub"

WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(256);

void connectAWS()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Wi-Fi:");
  M5.Lcd.print("Wi-Fi:");

  while (WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
    M5.Lcd.print(".");
  }
  Serial.println("OK");
  M5.Lcd.println("OK");
  lineCnt++;

  // Configure WiFiClientSecure to use the AWS IoT device credentials
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  // Connect to the MQTT broker on the AWS endpoint we defined earlier
  client.begin(AWS_IOT_ENDPOINT, 8883, net);

  // Create a message handler
  client.onMessage(messageHandler);

  Serial.print("AWS IoT: ");
  M5.Lcd.print("AWS IoT: ");

  while (!client.connect(THINGNAME)) {
    Serial.print(".");
    M5.Lcd.print(".");
    delay(100);
  }

  if(!client.connected()){
    Serial.println("Fail: AWS IoT Timeout!");
    return;
  }
  else{
    Serial.println("OK");
    M5.Lcd.println("OK");
    lineCnt++;
  }

  // Subscribe to a topic
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);

  delay(2000);
}

void publishMessage()
{
  StaticJsonDocument<200> doc;
  doc["time"] = millis();
  doc["message"] = "Hello World";
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer); // print to client

  client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);
}

void messageHandler(String &topic, String &payload) {
  Serial.println("incoming: " + topic + " - " + payload);

  StaticJsonDocument<200> doc;
  deserializeJson(doc, payload);
  const char* message = doc["message"];

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, lineCnt*40, 2);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextFont(2);
  M5.Lcd.println(payload);
}

void countSeconds()
{
  // Check if it's time to increment the seconds-counter
  int64_t now_us = esp_timer_get_time();
  if (now_us > nextSecondTime)
  {
    secondsSinceStart++;
    nextSecondTime += 1000000;
    // Serial.printf("%lld toggleTime: %d state %d\n", secondsSinceStart, nextFlowToggleTime, flowState);
  }
}

void flowSim()
{
  int64_t now_us = esp_timer_get_time();

  // Check if it's time to toggle water flow state
  if (secondsSinceStart > nextFlowToggleTime)
  {
    if (flowState == false)
    {
      flowState = true;
      nextFlowSensorTransition = now_us + flowSensorHalfPeriod;
      nextFlowToggleTime = secondsSinceStart + flowOnDurationSec;
      Serial.println("Sim flow started");
    }
    else
    {
      flowState = false;
      nextFlowToggleTime = secondsSinceStart + flowOffDurationSec;
      Serial.println("Sim flow stopped");
    }
  }

  // toggle the flow rate sensor output
  if (flowState == true)
  {
    if (now_us > nextFlowSensorTransition)
    {
      nextFlowSensorTransition = now_us + flowSensorHalfPeriod;
      simFlowSensorOutput = !simFlowSensorOutput;
      // if (simFlowSensorOutput)
      //   Serial.print(".");
    }
  }
}

void setup() {
  pinMode(button, INPUT);

  M5.begin();
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0, 2);
  M5.Lcd.setTextColor(TFT_WHITE,TFT_BLACK);
  M5.Lcd.setTextSize(2);

  Serial.begin(115200);
  connectAWS();

  int64_t now = esp_timer_get_time();
  nextSecondTime = now + 1000000;  // time to increment the seconds counter

  if (simulateFlow)
  {
    flowSensorOutput = simFlowSensorOutput;
    nextFlowToggleTime = secondsSinceStart + flowOffDurationSec; // start with water off
  }
  else
  {
    // FIXME: Read flow sensor into flowSensorOutput here
  }
  lastFlowSensorOutput = flowSensorOutput;
}

void loop() {
  countSeconds();  // increment second counter if needed

  // get flow sensor output
  if (simulateFlow)
  {
    // call the flow simulator
    flowSim();
    flowSensorOutput = simFlowSensorOutput;
  }
  else
  {
    // FIXME: Read flow sensor into flowSensorOutput here
  }

  // Count rising edges from flow sensor
  if (flowSensorOutput && !lastFlowSensorOutput)
  {
    flowCount++;
    // Serial.print("+");
  }
  lastFlowSensorOutput = flowSensorOutput;

  // Check for end of measurment period
  if (secondsSinceStart > nextPeriodTime)
  {
    nextPeriodTime = secondsSinceStart + reportingPeriodSec;
    litersSinceStart = flowCount / pulsesPerLiter;  // assumes 1/60 lit = 6.6 pulses
    Serial.printf("liters since start: %f\n", litersSinceStart);

    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0, 2);
  }

  // read the value of BUTTON
  cur_value = digitalRead(button);

  if(cur_value != last_value){
    if(cur_value==0){
      Serial.println("Button pressed");
      publishMessage();
      Serial.println("Publishing");
    }
    else{
      Serial.println("Button Status: released");
      Serial.println("value:  1");
    }
    last_value = cur_value;
  }

  client.loop();
}
