#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* WIFI_SSID     = "Orange-60A0";
const char* WIFI_PASSWORD = "B320-323";
const char* SERVER_URL    = "http://192.168.1.104:5000/upload";

#define FLASH_LED_PIN   4
#define PWDN_GPIO_NUM  32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM   0
#define SIOD_GPIO_NUM  26
#define SIOC_GPIO_NUM  27
#define Y9_GPIO_NUM    35
#define Y8_GPIO_NUM    34
#define Y7_GPIO_NUM    39
#define Y6_GPIO_NUM    36
#define Y5_GPIO_NUM    21
#define Y4_GPIO_NUM    19
#define Y3_GPIO_NUM    18
#define Y2_GPIO_NUM     5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM  23
#define PCLK_GPIO_NUM  22

#define UART_BAUD    115200
#define HTTP_TIMEOUT 60000  // CORRECTION : 60 secondes au lieu de 30

String uartBuf = "";

void flashOn()  { digitalWrite(FLASH_LED_PIN, HIGH); }
void flashOff() { digitalWrite(FLASH_LED_PIN, LOW);  }

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connexion WiFi");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500); Serial.print("."); tries++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WIFI CONNECTE");
    Serial.print("IP ESP32 : ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("ERREUR WIFI");
  }
}

bool startCamera() {
  camera_config_t config;
  memset(&config, 0, sizeof(config));
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk  = XCLK_GPIO_NUM;
  config.pin_pclk  = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href  = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  if (psramFound()) {
    Serial.println("PSRAM DETECTEE");
    config.frame_size   = FRAMESIZE_SVGA;
    config.jpeg_quality = 10;
    config.fb_count     = 2;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
  } else {
    Serial.println("PSRAM NON DETECTEE");
    config.frame_size   = FRAMESIZE_QQVGA;
    config.jpeg_quality = 20;
    config.fb_count     = 1;
    config.fb_location  = CAMERA_FB_IN_DRAM;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("ERREUR INIT CAMERA");
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
    s->set_brightness(s, 2);
    s->set_contrast(s, 1);
    s->set_saturation(s, 1);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_gainceiling(s, (gainceiling_t)4);
  }

  Serial.println("CAMERA OK");
  return true;
}

camera_fb_t* prendrePhotoAvecFlash() {
  Serial.println("FLASH ALLUME");
  flashOn();
  delay(3000);
  Serial.println("STABILISATION CAMERA");
  for (int i = 0; i < 3; i++) {
    camera_fb_t* old = esp_camera_fb_get();
    if (old) { esp_camera_fb_return(old); Serial.println("BUFFER OK"); }
    else      { Serial.println("BUFFER VIDE"); }
    delay(150);
  }
  Serial.println("PRISE PHOTO");
  camera_fb_t* fb = esp_camera_fb_get();
  flashOff();
  Serial.println("FLASH ETEINT");
  return fb;
}

void captureAndSend() {
  Serial.println();
  Serial.println("==================================");
  Serial.println("DEBUT CAPTURE");
  Serial.println("==================================");

  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  camera_fb_t* fb = prendrePhotoAvecFlash();

  if (!fb) {
    Serial.println("ECHEC CAPTURE PHOTO");
    Serial.println("RESULTAT : ERREUR CAMERA");
    Serial.println("==================================");
    return;
  }

  Serial.print("TAILLE PHOTO : ");
  Serial.println(fb->len);

  HTTPClient http;
  WiFiClient client;

  // CORRECTION : augmenter buffer TCP
  client.setNoDelay(true);

  http.begin(client, SERVER_URL);
  http.addHeader("Content-Type", "image/jpeg");
  http.setTimeout(HTTP_TIMEOUT);

  Serial.println("ENVOI AU SERVEUR...");

  int code = http.POST(fb->buf, fb->len);
  esp_camera_fb_return(fb);

  Serial.println("----------------------------------");

  if (code == 200) {
    String body = http.getString();
    body.trim();

    Serial.print("REPONSE JSON : ");
    Serial.println(body);
    Serial.println("----------------------------------");

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
      Serial.println("ERREUR PARSING JSON");
      Serial.println("RESULTAT : ERREUR SERVEUR");

    } else {
      bool success        = doc["success"] | false;
      bool match          = doc["match"]   | false;
      const char* message = doc["message"] | "";
      const char* name    = doc["name"]    | "";
      float distance      = doc["distance"] | -1.0f;

      // ============================================
      // CAS 1 : AUCUN VISAGE DETECTE
      // ============================================
      if (strcmp(message, "No face detected") == 0) {
        Serial.println(">>> CAS : AUCUN VISAGE DETECTE <<<");
        Serial.println("DISTRIBUTION MEDICAMENT : NON");
        Serial.println("RESULTAT : AUCUN VISAGE");

      // ============================================
      // CAS 2 : VISAGE RECONNU (bonne personne)
      // ============================================
      } else if (success && match) {
        Serial.println(">>> CAS : VISAGE CORRECT <<<");
        Serial.print("PATIENT RECONNU : ");
        Serial.println(name);
        if (distance >= 0) {
          Serial.print("DISTANCE : ");
          Serial.println(distance, 4);
        }
        Serial.println("DISTRIBUTION MEDICAMENT : OUI");
        Serial.println("RESULTAT : OK");

      // ============================================
      // CAS 3 : VISAGE DETECTE MAIS MAUVAISE PERSONNE
      // ============================================
      } else {
        Serial.println(">>> CAS : VISAGE INCORRECT <<<");
        if (strlen(name) > 0) {
          Serial.print("RESSEMBLE A : ");
          Serial.println(name);
        }
        if (distance >= 0) {
          Serial.print("DISTANCE : ");
          Serial.println(distance, 4);
        }
        Serial.println("DISTRIBUTION MEDICAMENT : NON");
        Serial.println("RESULTAT : ACCES REFUSE");
      }
    }

  } else {
    // ERREUR HTTP (timeout, connexion refusee, etc.)
    Serial.print("ERREUR HTTP CODE : ");
    Serial.println(code);
    if (code == -1)  Serial.println("DETAIL : CONNEXION REFUSEE");
    if (code == -11) Serial.println("DETAIL : TIMEOUT - SERVEUR TROP LENT");
    if (code == -2)  Serial.println("DETAIL : ENVOI ECHOUE");
    Serial.println("DISTRIBUTION MEDICAMENT : NON");
    Serial.println("RESULTAT : ERREUR RESEAU");
  }

  http.end();
  Serial.println("==================================");
  Serial.println("FIN CAPTURE");
  Serial.println("==================================");
}

void readUART() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      uartBuf.trim();
      uartBuf.toUpperCase();
      if (uartBuf == "CAPTURE") captureAndSend();
      uartBuf = "";
    } else {
      uartBuf += c;
    }
  }
}

void setup() {
  Serial.begin(UART_BAUD);
  delay(1000);
  pinMode(FLASH_LED_PIN, OUTPUT);
  flashOff();
  Serial.println("==================================");
  Serial.println("  SMART MED BOX - ESP32-CAM");
  Serial.println("==================================");
  connectWiFi();
  if (!startCamera()) Serial.println("CAMERA NON INITIALISEE");
  Serial.println("PRET - TAPEZ : CAPTURE");
  Serial.println("==================================");
}

void loop() {
  readUART();
  delay(10);
}