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

// TDS settings
#define TDS_PIN 4
#define VREF 3.3
#define SCOUNT 30

int analogBuffer[SCOUNT];
int analogBufferIndex = 0;
float temperature = 25.0;

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
  display.println("TDS Meter Ready");
  display.display();
  delay(1500);
}

void loop() {
  // collect samples
  static unsigned long sampleTime = millis();
  if (millis() - sampleTime > 40U) {
    sampleTime = millis();
    analogBuffer[analogBufferIndex++] = analogRead(TDS_PIN);
    if (analogBufferIndex == SCOUNT) analogBufferIndex = 0;
  }

  // update display every 800ms
  static unsigned long displayTime = millis();
  if (millis() - displayTime > 800U) {
    displayTime = millis();

    int medianVal = getMedian(analogBuffer, SCOUNT);
    float voltage = medianVal * (VREF / 4095.0); // 12-bit ADC

    // temperature compensation
    float compCoeff = 1.0 + 0.02 * (temperature - 25.0);
    float compVoltage = voltage / compCoeff;

    // TDS formula
    float tds = (133.42 * pow(compVoltage, 3)
               - 255.86 * pow(compVoltage, 2)
               + 857.39 * compVoltage) * 0.5;

    // water quality label
    String quality;
    if (tds < 300) quality = "Excellent";
    else if (tds < 600) quality = "Good";
    else if (tds < 900) quality = "Fair";
    else if (tds < 1200) quality = "Poor";
    else quality = "Unsafe";

    // Serial output
    Serial.print("TDS: "); Serial.print(tds); Serial.println(" ppm");

    // OLED output
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("-- TDS Monitor --");

    display.setTextSize(2);
    display.setCursor(0, 20);
    display.print((int)tds);
    display.println(" ppm");

    display.setTextSize(1);
    display.setCursor(0, 50);
    display.print("Quality: ");
    display.println(quality);

    display.display();
  }
}