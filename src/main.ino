#include "Arduino.h"
#include "FS.h"
#include "SD_MMC.h"
#include "driver/rtc_io.h"
#include "esp_camera.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include <EEPROM.h>

// number of bytes
#define EEPROM_SIZE 1
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

int pictureNum = 0;
int buttonGPIO = 12;
int flashGPIO = 4;
bool lastButtonState = HIGH;
bool buttonPressed = false;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

void setup() {
  // disable voltage regulator shit
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);

  // camera settings
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // init camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    while (1)
      delay(1000);
  }
  Serial.println("Camera initialized successfully");

  // init sd card with lower frequency
  if (!SD_MMC.begin("/sdcard", true, false, 5000)) {
    Serial.println("Card Mount Failed");
    while (1)
      delay(1000);
  }
  Serial.println("SD Card initialized successfully");

  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD Card attached");
    while (1)
      delay(1000);
  }

  EEPROM.begin(EEPROM_SIZE);
  pictureNum = EEPROM.read(0);

  warmUpCamera(); // warm-up loop to fix green shit tint - discard first 50
                  // frames

  pinMode(flashGPIO, OUTPUT);
  pinMode(buttonGPIO, INPUT_PULLUP); // Changed to INPUT_PULLUP
}

void loop() {
  int reading = digitalRead(buttonGPIO);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonPressed) {
      buttonPressed = reading;

      if (buttonPressed == LOW) {
        Serial.println("Button pressed, taking picture...");
        digitalWrite(flashGPIO, HIGH);
        rtc_gpio_hold_dis(GPIO_NUM_4);
        takePicture();
        delay(1000);
      }
    }
  }

  if (buttonPressed == HIGH) {
    digitalWrite(flashGPIO, LOW);
    rtc_gpio_hold_en(GPIO_NUM_4);
  }

  lastButtonState = reading;
  delay(10);
}

void takePicture() {
  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("SD Card not available - stopping");
    while (1)
      delay(1000);
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  pictureNum++;
  String path = "/qorwilis-dedamotynuli-suratebi" + String(pictureNum) + ".jpg";
  Serial.printf("Picture file name: %s\n", path.c_str());

  File file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    esp_camera_fb_return(fb);
    return;
  }

  file.write(fb->buf, fb->len);
  file.close();
  esp_camera_fb_return(fb);

  Serial.printf("Picture saved to %s\n", path.c_str());

  // update picture number in flash memory
  EEPROM.write(0, pictureNum);
  EEPROM.commit();

  digitalWrite(flashGPIO, LOW);
  rtc_gpio_hold_en(GPIO_NUM_4);
  delay(1000);
  Serial.println("___________________________");
}

void warmUpCamera() {
  Serial.println("Warming up camera...");
  for (int i = 0; i < 50; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Failed to capture frame during warm-up");
      continue;
    }
    esp_camera_fb_return(fb);
  }
  Serial.println("Camera warm-up complete");
}
