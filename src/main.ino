#include "FS.h"
#include "SD_MMC.h"
#include "driver/rtc_io.h"
#include "esp_camera.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include <EEPROM.h>

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

int videoNum = 0;
int buttonGPIO = 12;
int flashGPIO = 4;
bool lastButtonState = HIGH;
bool buttonPressed = false;
bool isRecording = false;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;
unsigned long recordingStartTime = 0;
const unsigned long maxRecordingTime = 30000;
const int targetFPS = 10;
const unsigned long frameInterval = 1000 / targetFPS;

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);

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
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 15;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA; // 320x240
    config.jpeg_quality = 20;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    while (1)
      delay(1000);
  }
  Serial.println("Camera initialized successfully");

  if (!SD_MMC.begin("/sdcard", true, false, 20000)) {
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
  videoNum = EEPROM.read(0);

  warmUpCamera();

  pinMode(flashGPIO, OUTPUT);
  pinMode(buttonGPIO, INPUT_PULLUP);

  Serial.println("Ready to record! Press button to start/stop recording.");
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
        if (!isRecording) {
          startVideoRecording();
        } else {
          stopVideoRecording();
        }
      }
    }
  }

  if (isRecording) {
    static unsigned long lastFrameTime = 0;
    unsigned long currentTime = millis();

    if (currentTime - lastFrameTime >= frameInterval) {
      captureVideoFrame();
      lastFrameTime = currentTime;
    }

    if (currentTime - recordingStartTime >= maxRecordingTime) {
      Serial.println("Max recording time reached, stopping...");
      stopVideoRecording();
    }
  }

  lastButtonState = reading;
  delay(1);
}

void startVideoRecording() {
  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("SD Card not available");
    return;
  }

  videoNum++;
  String folderPath = "/video" + String(videoNum);

  if (!SD_MMC.mkdir(folderPath)) {
    Serial.println("Failed to create video directory");
    return;
  }

  isRecording = true;
  recordingStartTime = millis();
  digitalWrite(flashGPIO, HIGH);

  Serial.println("Recording started! Press button again to stop.");
  Serial.printf("Recording to folder: %s\n", folderPath.c_str());
}

void stopVideoRecording() {
  isRecording = false;
  digitalWrite(flashGPIO, LOW);

  EEPROM.write(0, videoNum);
  EEPROM.commit();

  Serial.println("Recording stopped!");
  Serial.println("You can convert frames to video using ffmpeg:");
  Serial.printf("ffmpeg -r %d -i /video%d/frame_%%04d.jpg -c:v libx264 "
                "-pix_fmt yuv420p video%d.mp4\n",
                targetFPS, videoNum, videoNum);
  Serial.println("___________________________");
}

void captureVideoFrame() {
  static int frameCount = 0;

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  String folderPath = "/video" + String(videoNum);
  String framePath = folderPath + "/frame_" + String(frameCount, 10) + ".jpg";

  if (frameCount < 10)
    framePath = folderPath + "/frame_000" + String(frameCount) + ".jpg";
  else if (frameCount < 100)
    framePath = folderPath + "/frame_00" + String(frameCount) + ".jpg";
  else if (frameCount < 1000)
    framePath = folderPath + "/frame_0" + String(frameCount) + ".jpg";

  File file = SD_MMC.open(framePath, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    esp_camera_fb_return(fb);
    return;
  }

  file.write(fb->buf, fb->len);
  file.close();
  esp_camera_fb_return(fb);

  frameCount++;

  if (frameCount % 50 == 0) {
    Serial.printf("Captured %d frames\n", frameCount);
  }

  if (!isRecording) {
    frameCount = 0;
  }
}

void takePicture() {
  if (!isRecording) {
  }
}

void warmUpCamera() {
  Serial.println("Warming up camera...");
  for (int i = 0; i < 30; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Failed to capture frame during warm-up");
      continue;
    }
    esp_camera_fb_return(fb);
  }
  Serial.println("Camera warm-up complete");
}
