/**
 * kuzu - small dust sensor display
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

WiFiClient wifiClient;

// MQTT client
#define CLIENT_NAME_PREFIX "kuzu_"
#define MAX_ESP_ID_LEN 16
char esp_id[MAX_ESP_ID_LEN];
PubSubClient mqttClient(wifiClient);
#define MAX_PAYLOAD_SIZE (255)

// Display
// The complete list is available here: https://github.com/olikraus/u8g2/wiki/u8g2setupcpp
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);   // EastRising 0.42" OLED

// On-board Neopixel
#define NUMPIXELS 1
#define PIN_NEOPIXEL 2
int neopixel_color=0;
Adafruit_NeoPixel pixels(NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// BME280 sensor
#if BME_ENABLE
#include <Adafruit_BME280.h>
#define BME_ADDRESS (0x77)
Adafruit_BME280 bme;
const int BME_PUBLISH_INTERVAL = 3*30*1000;
long last_publish_time = 0;
char bme_topic[32];
#endif

// Tables
const int TABLE_ITEM_MAX=8;


typedef void (*kvCallback)(const char *topic, const char *key, const char *value);

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

// called on each MQTT parsed 'k=v;'
// you must inspect topic, key, value
void PM25_kvCallback(const char *topic, const char *key, const char *value) {
  if (strcmp(topic, MQTT_DUST_TOPIC)== 0 && strcmp(key, "PM2.5") == 0) {
    int val = atoi(value);
    if (val < 5) neopixel_color = 0x00ff00;
    else if (val < 20) neopixel_color = 0xffff00;
    else neopixel_color = 0xff0000;
    shine();
  }
}

void BME_kvCallback(const char *topic, const char *key, const char *value) {
  return;
}

void mqtt_event_callback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("* topic %s\n", topic);
  if (topic && strcmp(MQTT_DUST_TOPIC, topic) == 0) {
    displayKVData(topic, payload, length, &PM25_kvCallback);
  }
#if BME_ENABLE
  if (topic && strcmp(bme_topic, topic) == 0) {
    displayKVData(topic, payload, length, &BME_kvCallback);
  }
#endif
}

void displayKVData(char* topic, byte* payload, unsigned int payload_length, kvCallback kvCallback) {
  char buf[MAX_PAYLOAD_SIZE+1];
  u8g2.clearBuffer();
  u8g2.clearDisplay();

  // separate "k=v;"* payload into alternating k and v strings in buf, etc.
  parseKV(buf, sizeof(buf), payload, payload_length);

  // lay out alternating strings in buf as keys and values,
  // in a two-column table: 2 across x 4 down, 1 page.
  drawTable(buf, topic, kvCallback);
}

// separate "k=v;" message into alternating k and v strings
// by replacing separators with NUL, uppercasing keys, etc.
void parseKV(char *buf, int sizeof_buf, byte *payload, int payload_length) {
  memset(buf, 0, sizeof_buf);
  memcpy(buf, payload, min(payload_length, sizeof_buf-1));
  Serial.printf("* parsing payload: %s\n", buf);
  
  boolean iskey = true;
  for (int i = 0; i < sizeof_buf; i++) {
    // pm01=0;pm2_5=1;pm10=1;aqi=4;pm2_5raw=0
    char c = buf[i];
    if (c == '_') {
      buf[i] = '.';
    } else if (c == '=') {
      iskey = false;
      buf[i] = '\0';
    } else if (c == ';' || c == '\r' || c == '\n') {
      iskey = true;
      buf[i] = '\0';
    } else if (iskey) {
      buf[i] = toupper(c);
    }
    // PM01^@0^@PM2.5^@1^@PM10^@1^@AQI^@4^@PM2.5RAW^@0^@
  }
}

// returns number of items drawn
// won't go over TABLE_ITEM_MAX
// pre-measured with font u8g2_font_BBSesque_tf for fit
void drawTable(char *buf, const char *topic, kvCallback kvCallback) {
  char *last_start = buf;
  const char *keyname = "";

  int i = 0;
  for (; last_start[0] != '\0' && i < TABLE_ITEM_MAX; last_start += strlen(last_start)+1) {
    int x = (i % 2) * 44;	// 36 is half, but numbers are shorter than the labels
    int y = (i / 2) * 10;
    if ((i % 2) == 0) {
      keyname = last_start;
    } else {
      Serial.printf("* item %s='%s' i = %d x=%d y=%d\n", keyname, last_start, i, x, y);
      if (kvCallback != NULL) {
	(*kvCallback)(topic, keyname, last_start);
      }
    }
    u8g2.drawStr(x, y, last_start);
    i++;
  }
  u8g2.sendBuffer();

  if (i >= TABLE_ITEM_MAX) {
    Serial.printf("* truncated items %s\n", last_start);
  }
}

void setupMQTT() {
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqtt_event_callback);
  reconnect();
  u8g2.drawStr(0, 30, MQTT_DUST_TOPIC);
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
#if BME_ENABLE
  setupBME();
#endif
  setupMQTT();
  delay(1000);
  Serial.println("* end setup");
}

void reconnect() {
  Serial.println("* Connecting to MQTT Broker");
  while (!mqttClient.connected()) {
    snprintf(esp_id, MAX_ESP_ID_LEN,"%s%08X", CLIENT_NAME_PREFIX, getChipId());
    if (mqttClient.connect(esp_id)) {
      // subscribe to topic
      mqttClient.subscribe(MQTT_DUST_TOPIC);
      Serial.printf("* Subscribed to topic %s as %s\n", MQTT_DUST_TOPIC, esp_id);
#if BME_ENABLE
      snprintf(bme_topic, sizeof(bme_topic)-1, "sensor/bme280/%s", esp_id);
      mqttClient.subscribe(bme_topic);
      Serial.printf("* Subscribed to topic %s as %s\n", bme_topic, esp_id);
#endif
    } else {
      Serial.printf("* Failed to connect to MQTT as %s\n", esp_id);
    }
  }
}

// ESP32 vbersion of ESP8266 function
// copied from arduino-esp32/libraries/ESP32/examples/ChipID/GetChipID/GetChipID.ino
uint32_t getChipId() {
  uint32_t chipId = 0;
  for(int i=0; i<17; i=i+8) {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  return chipId;
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

  if (mqttClient.connected()) {
    mqttClient.loop();
#if BME_ENABLE
    bmeLoop();
#endif
  }
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
#if BME_ENABLE
void setupBME() {
  if (!bme.begin(BME_ADDRESS)) {
    Serial.println("* BME280 Setup Failed");
  } else {
    Serial.println("* BME280 Setup OK");
  }
}

bool bmePublish() {
  char payload[MAX_PAYLOAD_SIZE+1];

  float temperature = bme.readTemperature();
  float humidity = bme.readHumidity();
  float pressure = bme.readPressure() / 100.0;

  if (temperature == 0.0 && humidity == 0.0 && pressure == 0.0) {
    Serial.println("* bmePublish data all zero - skipping");
    return false;
  }

  snprintf(payload, sizeof(payload)-1, "temp=%.3f;hum=%.3f;press=%.3f", temperature, humidity, pressure);

  Serial.printf("* bmePublish: %s %s\n", bme_topic, payload);

  return mqttClient.publish(bme_topic, payload);
}

void bmeLoop() {
  long now = millis();
  if ((last_publish_time == 0) || (now - last_publish_time > BME_PUBLISH_INTERVAL)) {
    bool ok = bmePublish();
    if (! ok) {
      Serial.println("* bmePublish failed!");
    }
    last_publish_time = now;
  }
}
#endif
