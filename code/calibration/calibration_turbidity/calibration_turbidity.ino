#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SDA_PIN 8
#define SCL_PIN 9
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Turbidity sensor settings
#define TURBIDITY_PIN 4
#define SCOUNT 30

int analogBuffer[SCOUNT];
int analogBufferIndex = 0;

int getMedian(int* buffer, int size) {
  int sorted[size];
  for (int i = 0; i < size; i++) sorted[i] = buffer[i];
  for (int i = 0; i < size - 1; i++)
    for (int j = 0; j < size - i - 1; j++)
      if (sorted[j] > sorted[j+1]) {
        int t = sorted[j]; sorted[j] = sorted[j+1]; sorted[j+1] = t;
      }
  return sorted[size / 2];
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found");
    while (true);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Turbidity Ready");
  display.display();
  delay(1500);
}

void loop() {
  static unsigned long sampleTime = millis();
  if (millis() - sampleTime > 40U) {
    sampleTime = millis();
    analogBuffer[analogBufferIndex++] = analogRead(TURBIDITY_PIN);
    if (analogBufferIndex == SCOUNT) analogBufferIndex = 0;
  }

  static unsigned long displayTime = millis();
  if (millis() - displayTime > 800U) {
    displayTime = millis();

    int medianVal = getMedian(analogBuffer, SCOUNT);

    // voltage divider compensation: actual voltage is 2x ADC reading
    float voltage = (medianVal * (3.3 / 4095.0)) * 2.0;

    float ntu;
    if (voltage >= 4.2) {
      ntu = 0;
    } else if (voltage <= 1.0) {
      ntu = 3000;
    } else {
      ntu = -1120.4 * sq(voltage) + 5742.3 * voltage - 4352.9;
      if (ntu < 0) ntu = 0;
    }

    String quality;
    if (ntu < 5) quality = "Clear";
    else if (ntu < 50) quality = "Slight";
    else if (ntu < 500) quality = "Turbid";
    else quality = "Very turbid";

    Serial.print("Voltage: "); Serial.print(voltage);
    Serial.print(" | NTU: "); Serial.println(ntu);

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("-- Turbidity --");

    display.setTextSize(2);
    display.setCursor(0, 20);
    display.print((int)ntu);
    display.println(" NTU");

    display.setTextSize(1);
    display.setCursor(0, 50);
    display.print("Quality: ");
    display.println(quality);

    display.display();
  }
}