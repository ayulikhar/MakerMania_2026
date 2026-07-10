#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// ── Calibration ──────────────────────────────────────────
#define CLEAR_ADC 1662
#define SCALE_FACTOR 0.8
#define TDS_OFFSET     6.0    // distilled water TDS offset
#define TDS_CORRECTION 1.0    // adjust after KCl calibration

// ── OLED ─────────────────────────────────────────────────
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SDA_PIN 8
#define SCL_PIN 9
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── Pins ─────────────────────────────────────────────────
#define TURBIDITY_PIN 4
#define TDS_PIN 0

int readADC(int pin)
{
    long sum = 0;

    for (int i = 0; i < 16; i++)
        sum += analogRead(pin);

    return sum / 16;
}
// ── Sampling ─────────────────────────────────────────────
#define SCOUNT 80
int turbBuffer[SCOUNT];
int tdsBuffer[SCOUNT];
int turbIndex = 0;
int tdsIndex  = 0;
float temperature = 25.0;
int screen = 0;

// ── Struct ───────────────────────────────────────────────
struct WaterQuality {
  String message;
  String stars;
};
WaterQuality getQuality(float tds, float ntu);
// ── Median filter ─────────────────────────────────────────
int getMedian(int* buffer, int size) {
  int sorted[SCOUNT];
  for (int i = 0; i < size; i++) sorted[i] = buffer[i];
  for (int i = 0; i < size - 1; i++)
    for (int j = 0; j < size - i - 1; j++)
      if (sorted[j] > sorted[j+1]) {
        int t = sorted[j]; sorted[j] = sorted[j+1]; sorted[j+1] = t;
      }
  return sorted[size / 2];
}

// ── Star rating ───────────────────────────────────────────
String getStars(int count) {
  String s = "";
  for (int i = 0; i < 5; i++) s += (i < count) ? "*" : ".";
  return s;
}

// ── Combined quality ──────────────────────────────────────
WaterQuality getQuality(float tds, float ntu) {
  int tdsScore, turbScore;

  if      (tds < 50)   tdsScore = 5;
  else if (tds < 150)  tdsScore = 4;
  else if (tds < 300)  tdsScore = 3;
  else if (tds < 600)  tdsScore = 2;
  else                 tdsScore = 1;

  if      (ntu < 10)   turbScore = 5;
  else if (ntu < 100)  turbScore = 4;
  else if (ntu < 300)  turbScore = 3;
  else if (ntu < 500)  turbScore = 2;
  else                 turbScore = 1;

  int overall = (tdsScore + turbScore) / 2;

  WaterQuality q;
  q.stars = getStars(overall);
  switch (overall) {
    case 5: q.message = "Excellent!"; break;
    case 4: q.message = "Good";       break;
    case 3: q.message = "Fair";       break;
    case 2: q.message = "Poor";       break;
    default: q.message = "Unsafe!";   break;
  }
  return q;
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
  display.setCursor(20, 20);
  display.println("Water Quality");
  display.setCursor(25, 35);
  display.println("Monitor v1.0");
  display.display();
  delay(1500);
  for (int i = 0; i < SCOUNT; i++) {
    turbBuffer[i] = readADC(TURBIDITY_PIN);
    tdsBuffer[i] = readADC(TDS_PIN);
    delay(20);
  }
}

void loop() {
  // ── Sample every 40ms ────────────────────────────────
  static unsigned long sampleTime = millis();
  if (millis() - sampleTime > 40U) {
    sampleTime = millis();
    turbBuffer[turbIndex++] = readADC(TURBIDITY_PIN);
    if (turbIndex == SCOUNT) turbIndex = 0;
    tdsBuffer[tdsIndex++] = readADC(TDS_PIN);
    if (tdsIndex == SCOUNT) tdsIndex = 0;
  }

  // ── Update every 2s ──────────────────────────────────
  static unsigned long displayTime = millis();
  if (millis() - displayTime > 2000U) {
    displayTime = millis();

    // ── TDS ──────────────────────────────────────────
    int tdsMedian = getMedian(tdsBuffer, SCOUNT);
    float tdsVoltage = tdsMedian * (3.3 / 4095.0);
    float compCoeff = 1.0 + 0.02 * (temperature - 25.0);
    float compVoltage = tdsVoltage / compCoeff;
    float tds = (133.42 * pow(compVoltage, 3)
               - 255.86 * pow(compVoltage, 2)
               + 857.39 * compVoltage) * 0.5 * TDS_CORRECTION - TDS_OFFSET;
    if (tds < 0) tds = 0;

    // ── Turbidity ────────────────────────────────────

    String turbStatus;

    int turbMedian = getMedian(turbBuffer, SCOUNT);
    float turbVoltage = turbMedian * (3.3 / 4095.0);

    // ADC-based calibration
    float ntu = (CLEAR_ADC - turbMedian) * SCALE_FACTOR;

    if (ntu < 5)
        ntu = 0;

    if (ntu > 3000)
        ntu = 3000;

    if (ntu < 10)
        turbStatus = "Clear";
    else if (ntu < 100)
        turbStatus = "Slight";
    else if (ntu < 300)
        turbStatus = "Turbid";
    else
        turbStatus = "Very Turbid";

    // Create Quality AFTER tds and ntu are known
    WaterQuality q = getQuality(tds, ntu);

    // Debug
    Serial.print("TDS: ");
    Serial.print((int)tds);

    Serial.print(" ppm | Voltage: ");
    Serial.print(turbVoltage, 3);

    Serial.print(" V | NTU: ");
    Serial.print((int)ntu);

    Serial.print(" | ");
    Serial.print(turbStatus);

    Serial.print(" | ");
    Serial.println(q.message);

    // ── OLED ─────────────────────────────────────────
    display.clearDisplay();
    display.setTextColor(WHITE);

    if (screen == 0) {
      // ── Screen 0: Readings ────────────────────────
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("=== Readings ===");

      // TDS
      display.setCursor(0, 14);
      display.print("TDS: ");
      display.setTextSize(2);
      display.print((int)tds);
      display.setTextSize(1);
      display.println(" ppm");

      // NTU
      display.setCursor(0, 36);
      display.print("NTU: ");
      display.setTextSize(2);
      display.print((int)ntu);
      display.setTextSize(1);
      display.println(" NTU");

    } else {
      // ── Screen 1: Quality + Stars ─────────────────
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("= Water Quality =");

      display.setTextSize(2);
      display.setCursor(0, 16);
      display.println(q.message);

      display.setTextSize(2);
      display.setCursor(0, 40);
      display.println(q.stars);

      display.setTextSize(1);
      display.setCursor(0, 56);
      display.print("Turb: ");
      display.println(turbStatus);
    }

    Serial.print("ADC: ");
    Serial.print(turbMedian);

    Serial.print(" | NTU: ");
    Serial.print((int)ntu);

    Serial.print(" | ");
    Serial.print(turbStatus);

    Serial.print(" | TDS: ");
    Serial.print((int)tds);
    Serial.println(" ppm");

    display.display();
    screen = (screen + 1) % 2;
  }
}