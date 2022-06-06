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
    delay(500);
  }

  Serial.printf("\nnConnected IP: %s\n", WiFi.localIP().toString());
  u8g2.drawStr(0, 20, WiFi.localIP().toString().c_str());
  u8g2.sendBuffer();
  delay(1000);

  Serial.println("* end setup");
} 

void callback(char* topic, byte* payload, unsigned int length) {
  char buf[256];
  Serial.printf("* topic %s\n", topic);
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

  for (; last_start[0] != '\0' && item_no < ITEM_MAX; last_start += strlen(last_start)+1) {
    int x = (item_no % 2) * 44;	// 36 is half, but numbers are shorter than the labels
    int y = (item_no / 2) * 10;
    Serial.printf("* item '%s' item_no = %d x=%d y=%d last_start='%s'\n", last_start, item_no, x, y, last_start);
    u8g2.drawStr(x, y, last_start);
    u8g2.sendBuffer();
    item_no++;
  }

  if (item_no >= ITEM_MAX) {
    Serial.printf("* truncated items %s", last_start);
  }
}

void setupMQTT() {
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(callback);
}


void setup() {
  Wire.begin(SDA_PIN, SCL_PIN);

  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  u8g2_prepare();

  connectToWiFi();

#if 0
  setup_bme();
#endif

  setupMQTT();
}

void reconnect() {
  Serial.println("Connecting to MQTT Broker...");
  while (!mqttClient.connected()) {
      Serial.println("Reconnecting to MQTT Broker..");
      String clientId = "ESP32Client-";
      clientId += String(random(0xffff), HEX);
      
      if (mqttClient.connect(clientId.c_str())) {
        Serial.println("Connected.");
        // subscribe to topic
        mqttClient.subscribe(mqttTopic);
      }
      
  }
}

void loop() {
  if (!mqttClient.connected())
    reconnect();
  mqttClient.loop();
#if 0
  bmePublish();
#endif

}

void restore_font(void) {
  // u8g2_font_6x10_tf
  // u8g2_font_5x8_tf
  u8g2.setFont(u8g2_font_BBSesque_tf);
  u8g2.setFontMode(1); // Follow docs at https://github.com/olikraus/u8g2/wiki/u8g2reference#setfontmode
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

// BME sensor publishing - from mqtt project
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
