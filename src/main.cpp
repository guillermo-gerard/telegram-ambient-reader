/*
  This PoC reads temperature and humidity using a DHT11 and a second temperature using DS18b20,
  then communicates with a telegram bot

  Created: 4/11/2019 6:46:49 PM
  Author:  Guillermo Gerard
*/

#include <Arduino.h>

#include <DHTesp.h>
#include <ESP8266WiFi.h>
#include "asyncHTTPrequest.h"
#include "ESPAsyncTCP.h"
//CTBOT needs ArduioJson 5.13.5, other versions won't work -->  https://www.arduinolibraries.info/libraries/ct-bot
#include <CTBot.h>
#include <DS18B20.h>
#include <ArduinoJson.h>
#include "WifiConfig"

StaticJsonBuffer<200> jsonBuffer;

DS18B20 ds(D5);
DHTesp dht;
//TaskHandle_t tempTaskHandle = NULL;
int dhtPin = D6;

// Values taken from WifiConfig.h (not pushed)
String ssid = MY_SSID;
String pass = MY_PASS;
String token = MY_TOKEN;

CTBot myBot;
TBMessage msg;

uint8_t address[] = {40, 250, 31, 218, 4, 0, 0, 52};
uint8_t selected;
long myId;

float tempHistory[100];

boolean mute = false;

long interval = 60000;
unsigned long initial = 0;

asyncHTTPrequest request;

const float INVALID_TEMP = -1000;

void setup()
{
  Serial.begin(115200);
  Serial.println("Initializing");
  dht.setup(dhtPin, DHTesp::DHT22);
  myBot.wifiConnect(ssid, pass);
  delay(10);
  Serial.println("wifiConnect");
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  myBot.setTelegramToken(token);
  Serial.println("set telegram");
  if (myBot.testConnection())
  {
    Serial.println("\ntestConnection OK");
  }
  else
  {
    Serial.println("\ntestConnection NOK");
  }
  WiFi.begin(ssid, pass);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  delay(1000);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  selected = ds.select(address);
  Serial.println("End setup");

  Serial.print("waiting for user");

  while (!myBot.getNewMessage(msg))
  {
    Serial.print(".");
    delay(400);
  }
  myId = msg.sender.id;
  Serial.println();
  Serial.print("Id received: ");
  Serial.print(myId);

  for (int i = 0; i < 100; i++)
  {
    tempHistory[i] = INVALID_TEMP;
  }
}

void loop()
{
  float temp = 0.0;

  temp = ds.getTempC();
  Serial.println(temp);
  Push(temp);

  if (!mute)
  {
    myBot.sendMessage(myId, String(ds.getTempC()));
    myBot.sendMessage(myId, String());
  }
  while (millis() - initial < interval)
  {
    TempAndHumidity lastValues = dht.getTempAndHumidity();
    if (myBot.getNewMessage(msg))
    {
      Process(msg, lastValues);
    }
    yield();
  }
  initial = millis();
}

String GetComfortDescription(int comfStatus)
{
  switch (comfStatus)
  {
  case 0:
    return String("OK");
  case 1:
    return String("Too Hot");
  case 2:
    return String("Too Cold");
  case 4:
    return String("Too Dry");
  case 8:
    return String("Too Humid");
  case 9:
    return String("Hot And Humid");
  case 5:
    return String("Hot And Dry");
  case 10:
    return String("Cold And Humid");
  case 6:
    return String("Cold And Dry");
    return String("Error in calculation");
  }
}

void Process(TBMessage message, TempAndHumidity lastValues)
{
  if (message.text.equalsIgnoreCase("/chart100"))
  {
    myBot.sendMessage(myId, GetChartUrl());
    return;
  }
  if (message.text.equalsIgnoreCase("/mute"))
  {
    mute = true;
    myBot.sendMessage(myId, "Muted!");
    return;
  }
  if (message.text.equalsIgnoreCase("/verbose"))
  {
    mute = false;
    myBot.sendMessage(myId, "Ok, start sending values every 1 min starting now");
    return;
  }
  if (message.text.equalsIgnoreCase("/status"))
  {
    ComfortState destComfStatus;
    String stat = "Muted: ";
    stat = stat + (mute ? "yes" : "no");
    stat = stat + " - Interval: " + interval / 1000;
    stat = stat + " - Temp DS: " + ds.getTempC();
    stat = stat + " - Temp DHT: " + lastValues.temperature;
    stat = stat + " - Humidity DHT: " + lastValues.humidity;
    stat = stat + " - Heat Index DHT: " + dht.computeHeatIndex(lastValues.temperature, lastValues.humidity, false);
    stat = stat + " - Dew Point DHT: " + dht.computeDewPoint(lastValues.temperature, lastValues.humidity, false);
    stat = stat + " - Absolute humidity DHT: " + dht.computeAbsoluteHumidity(lastValues.temperature, lastValues.humidity, false);
    stat = stat + " - Absolute humidity DHT(g/m3): " + dht.computeAbsoluteHumidity(lastValues.temperature, lastValues.humidity, false);
    stat = stat + " - ComfortRatio DHT: " + dht.getComfortRatio(destComfStatus, lastValues.temperature, lastValues.humidity, false);
    stat = stat + " - ComfortRatio DHT: " + GetComfortDescription((int)destComfStatus);
    stat = stat + " - Human perception DHT: " + dht.computePerception(lastValues.temperature, lastValues.humidity, false);

    myBot.sendMessage(myId, stat);
    return;
  }
  if (message.text.equalsIgnoreCase("/interval"))
  {
    myBot.sendMessage(myId, "ok, send me a number in seconds, from 20 to 3600 (1 hour) - everything else will void this setting and return to process"); // notificar al remitente
    while (!myBot.getNewMessage(msg))
    {
      yield();
    }

    String newValue = msg.text;
    Serial.println(newValue);
    long value = newValue.toInt();

    if (value == 0 || value < 20 || value > 3600)
      return;

    String newInterval = "New interval: ";
    newInterval = newInterval + value + " seg.";

    myBot.sendMessage(myId, newInterval);

    interval = value * 1000;
    return;
  }
}

void RequestChartUrl()
{
  String str = "{%27chart%27:{%27type%27:%27line%27,%27data%27:{%27labels%27:[";
  for (int i = 1; i <= 100; i++)
  {
    str = str + i;
    if (i < 100)
      str = str + ",";
  }
  str = str + "],%20%27datasets%27:[{%27label%27:%27Temperatura%27,%27data%27:[";
  for (int i = 0; i < 100; i++)
  {
    str = str + tempHistory[i];
    if (i < 99)
      str = str + ",";
  }
  str = str + "]}]}}}";
  Serial.print("url: ");
  Serial.println(str);

  request.setDebug(true);
  request.onReadyStateChange(requestCallBack);
  sendRequest(str);
}

String GetChartUrl()
{
  //https://quickchart.io/chart?c=
  //{type:%27bar%27,
  //data:{
  //     labels:[%27uno%27,%27February%27,%20%27March%27,%27April%27,%20%27May%27],
  //  %20datasets:[
  //     {label:%27Dogs%27,
  //       data:[50,60,70,180,190]},
  //{label:%27Cats%27,data:[100,200,300,400,500]}]}}
  String str = "https://quickchart.io/chart?c={type:%27line%27,data:{labels:[";
  for (int i = 1; i <= 100; i++)
  {
    if (tempHistory[i] == INVALID_TEMP)
      continue;
    str = str + i;
    if (i < 100)
      str = str + ",";
  }
  str = str + "],%20datasets:[{label:%27Temperatura%27,data:[";
  for (int i = 0; i < 100; i++)
  {
    if (tempHistory[i] == INVALID_TEMP)
      continue;
    str = str + tempHistory[i];
    if (i < 99)
      str = str + ",";
  }

  //example
  //https://quickchart.io/chart?c={type:%27line%27,data:{labels:[99,100],datasets:[{label:%27Temperatura%27,data:[28.31,28.31]}]},options:{scales:{yAxes:[{ticks:{min:25.00,max:35.00,stepSize:1}}]}}}

  float min = GetMin() - 2;
  float max = GetMax() + 2;
  str = str + "]}]},options:{scales:{yAxes:[{ticks:{min:" + min + ",max:" + max + ",stepSize:1}}]}}}";
  Serial.print("url: ");
  Serial.println(str);
  request.setDebug(true);
  request.onReadyStateChange(requestCallBack);
  sendRequest(str);

  return str;
}

float GetMin()
{
  float minimum = 1000;
  for (int i = 0; i < 100; i++)
  {
    if (tempHistory[i] == INVALID_TEMP)
      continue;
    minimum = min(minimum, tempHistory[i]);
  }
  return minimum;
}

float GetMax()
{
  float maximum = -1000;
  for (int i = 0; i < 100; i++)
  {
    if (tempHistory[i] == INVALID_TEMP)
      continue;
    maximum = max(maximum, tempHistory[i]);
  }
  return maximum;
}

void sendRequest(String data)
{
  Serial.println("Initializing send request");
  if (request.readyState() == 0 || request.readyState() == 4)
  {
    Serial.println("open request");
    request.open("POST", "https://quickchart.io/chart/create");
    Serial.println("adding headers");
    request.setReqHeader("Content-Type", "application/json");
    Serial.println("send request");
    request.send("{%27chart%27:{%27type%27:%27line%27,%27data%27:{%27labels%27:[1,2],%20%27datasets%27:[{%27label%27:%27Temperatura%27,%27data%27:[27.69,27.75]}]}}}");
    Serial.println("after send");
  }
}

void requestCallBack(void *optParm, asyncHTTPrequest *request, int readyState)
{
  if (readyState == 4)
  {
    Serial.println(request->responseText());
    Serial.println();
    request->setDebug(false);

    JsonObject &root = jsonBuffer.parseObject(request->responseText());
    if (!root.success())
    {
      Serial.println("parseObject() failed");
      return;
    }
    const char *url = root["url"];
    Serial.print("shortened url: ");
    Serial.println(url);
    myBot.sendMessage(myId, url);
  }
}

void Push(float newValue)
{
  for (int i = 1; i < 100; i++)
  {
    tempHistory[i - 1] = tempHistory[i];
  }
  tempHistory[99] = newValue;
}
