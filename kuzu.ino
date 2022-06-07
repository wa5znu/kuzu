/**
 * kuzo - small dust sensor display
 *
 * Copyright 2022 Leigh L. Klotz, Jr. <leigh@wa5znu.org>
 * MIT License - see file LICENSE
 * Based on code examples:
 * https://github.com/01Space/ESP32-C3-0.42LCD.git
 * https://github.com/survivingwithandroid/ESP32-MQTT.git
 */

#include <Arduino.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif
#define SDA_PIN 5
#define SCL_PIN 6

#include "secrets.h"

long last_time = 0;
char data[100];

// MQTT client
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient); 

// The complete list is available here: https://github.com/olikraus/u8g2/wiki/u8g2setupcpp
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);   // EastRising 0.42" OLED

#define NUMPIXELS 1
#define PIN_NEOPIXEL 2
int neopixel_color=0;
Adafruit_NeoPixel pixels(NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

void connectToWiFi() {
  Serial.print("\nConnecting: ");
  u8g2.drawStr(0, 0, "Connecting");
  u8g2.sendBuffer();

  WiFi.begin(SSID, PWD);
  Serial.println(SSID);
  u8g2.drawStr(0, 10, SSID);
  u8g2.sendBuffer();

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    wink();
    delay(400);
  }

  Serial.printf("\nConnected IP: %s\n", WiFi.localIP().toString());
  u8g2.drawStr(0, 20, WiFi.localIP().toString().c_str());
  u8g2.sendBuffer();
} 

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("* topic %s\n", topic);
  displayData(topic, payload, length);
}

void displayData(char* topic, byte* payload, unsigned int length) {
  char buf[256];
  u8g2.clearBuffer();
  u8g2.clearDisplay();
  memset(buf, 0, sizeof(buf));
  memcpy(buf, payload, min(length, sizeof(buf)-1));
  Serial.println(buf);
  for (int i = 0; i < sizeof(buf); i++) {
    // pm01=0;pm2_5=1;pm10=1;aqi=4;pm2_5raw=0
    char c = buf[i];
    if (c == '_') {
      buf[i] = '.';
    } else if (c == '=') {
      buf[i] = '\0';
    } else if (c == ';' || c == '\r' || c == '\n') {
      buf[i] = '\0';
    } else {
      buf[i] = toupper(c);
    }
  }

  // pre-measured with font and fit: 2 across x 4 down
  const int ITEM_MAX=8;

  int item_no = 0;
  char *last_start = buf;
  char *keyname = "";

  for (; last_start[0] != '\0' && item_no < ITEM_MAX; last_start += strlen(last_start)+1) {
    int x = (item_no % 2) * 44;	// 36 is half, but numbers are shorter than the labels
    int y = (item_no / 2) * 10;
    if ((item_no % 2) == 0) {
      keyname = last_start;
    } else {
      Serial.printf("* item %s='%s' item_no = %d x=%d y=%d\n", keyname, last_start, item_no, x, y);
      if (strcmp(keyname, "PM2.5") == 0) {
	int val = atoi(last_start);
	if (val < 5) neopixel_color = 0x00ff00;
	else if (val < 20) neopixel_color = 0xffff00;
	else neopixel_color = 0xff0000;
	shine();
      }
    }
    u8g2.drawStr(x, y, last_start);
    u8g2.sendBuffer();
    item_no++;
  }

  if (item_no >= ITEM_MAX) {
    Serial.printf("* truncated items %s", last_start);
  }
}

void setupMQTT() {
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(callback);
  reconnect();
  u8g2.drawStr(0, 30, MQTT_TOPIC);
  u8g2.sendBuffer();
}

void setup() {
  Wire.begin(SDA_PIN, SCL_PIN);

  setupNeopixel();

  Serial.begin(115200);
#if 0
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
#endif
  u8g2_prepare();

  connectToWiFi();

#if 0
  setup_bme();
#endif

  setupMQTT();
  delay(1000);
  Serial.println("* end setup");
}

void reconnect() {
  Serial.printf("Connecting to MQTT Broker as ");
  while (!mqttClient.connected()) {
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    Serial.print(clientId);
    Serial.print(" ");
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected");
      // subscribe to topic
      mqttClient.subscribe(MQTT_TOPIC);
      Serial.printf("Subscribed to topic %s\n", MQTT_TOPIC);
    }
  }
}

void setupNeopixel() {
  pixels.begin();
  pixels.setBrightness(0);
  // set color to please
  neopixel_color = 0x000040;
  pixels.fill(neopixel_color);
  pixels.show();
}

void wink() {
  pixels.fill(neopixel_color);
  pixels.setBrightness(50);
  pixels.show();
  delay(100);
  pixels.setBrightness(0);
  pixels.show();
}

void shine() {
  const int brightness = 5;
  Serial.printf("* neopixel shine color=0x%x brightness=%d\n", neopixel_color, brightness);
  pixels.setBrightness(brightness);
  pixels.fill(neopixel_color);
  pixels.show();
}

void loop() {
  if (!mqttClient.connected()) {
    reconnect();
  }
  mqttClient.loop();
#if 0
  bmePublish();
#endif
}

void restore_font(void) {
  // u8g2_font_6x10_tf
  // u8g2_font_5x8_tf
  // Follow docs at https://github.com/olikraus/u8g2/wiki/u8g2reference#setfontmode
  u8g2.setFont(u8g2_font_BBSesque_tf);
  u8g2.setFontMode(1); 
}

void u8g2_prepare(void) {
  u8g2.begin();
  restore_font();
  u8g2.setFontRefHeightExtendedText();
  u8g2.setDrawColor(1);
  u8g2.setFontPosTop();
  u8g2.setFontDirection(0);

  u8g2.clearBuffer();
  u8g2.clearDisplay();
  u8g2.sendBuffer();
}

// BME sensor publishing - from MQTT project
// ordered a sensor just to check it out

#if 0
void bme_setup() {
  if (!bme.begin(0x76)) {
    Serial.println("Problem connecting to BME280");
  }
}

void bmePublish() {
  long now = millis();
  if (now - last_time > 60000) {
    // Send data
    float temp = bme.readTemperature();
    float hum = bme.readHumidity();
    float pres = bme.readPressure() / 100;

    // Publishing data throgh MQTT
    sprintf(data, "%f", temp);
    Serial.println(data);
    mqttClient.publish("/swa/temperature", data);
    sprintf(data, "%f", hum);
    Serial.println(hum);
    mqttClient.publish("/swa/humidity", data);
    sprintf(data, "%f", pres);
    Serial.println(pres);
    mqttClient.publish("/swa/pressure", data);
    last_time = now;
  }
}
#endif
