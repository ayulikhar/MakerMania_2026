#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// ── Calibration ──────────────────────────────────────────
#define CLEAR_ADC      1662
#define SCALE_FACTOR   0.8
#define TDS_OFFSET     6.0
#define TDS_CORRECTION 1.0

// ── WHO Thresholds ────────────────────────────────────────
#define WHO_TDS_LIMIT  600.0   // ppm
#define WHO_NTU_LIMIT  4.0     // NTU

// ── Buzzer ───────────────────────────────────────────────
#define BUZZER_PIN 2

// ── OLED ─────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SDA_PIN       8
#define SCL_PIN       9
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── Pins ─────────────────────────────────────────────────
#define TURBIDITY_PIN 4
#define TDS_PIN       0

// ── Trend config ─────────────────────────────────────────
#define TREND_WINDOW_MS   30000   // 30 second comparison window
#define NTU_TREND_THRESH  20.0    // NTU change to trigger trend
#define TDS_TREND_THRESH  30.0    // TDS change to trigger trend

// ── Trend snapshot struct ─────────────────────────────────
struct Snapshot {
  float ntu;
  float tds;
  unsigned long timestamp;
  bool valid;
};
Snapshot snapshot30s = {0, 0, 0, false};

// ── Trend result ──────────────────────────────────────────
// 0 = stable, 1 = rising, -1 = falling
struct Trend {
  int ntu;  // -1, 0, 1
  int tds;  // -1, 0, 1
};

Trend getTrend(float currentNTU, float currentTDS) {
  Trend t = {0, 0};
  if (!snapshot30s.valid) return t;

  float ntuDiff = currentNTU - snapshot30s.ntu;
  float tdsDiff = currentTDS - snapshot30s.tds;

  if      (ntuDiff >  NTU_TREND_THRESH) t.ntu =  1;
  else if (ntuDiff < -NTU_TREND_THRESH) t.ntu = -1;

  if      (tdsDiff >  TDS_TREND_THRESH) t.tds =  1;
  else if (tdsDiff < -TDS_TREND_THRESH) t.tds = -1;

  return t;
}

// ── ADC read ─────────────────────────────────────────────
int readADC(int pin) {
  long sum = 0;
  for (int i = 0; i < 16; i++) sum += analogRead(pin);
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

// ── Confidence score ──────────────────────────────────────
float getConfidence(int* turbBuf, int* tdsBuf, int size) {
  // Compute variance for turbidity buffer
  float turbMean = 0, tdsMean = 0;
  for (int i = 0; i < size; i++) {
    turbMean += turbBuf[i];
    tdsMean  += tdsBuf[i];
  }
  turbMean /= size;
  tdsMean  /= size;

  float turbVariance = 0, tdsVariance = 0;
  for (int i = 0; i < size; i++) {
    turbVariance += pow(turbBuf[i] - turbMean, 2);
    tdsVariance  += pow(tdsBuf[i]  - tdsMean,  2);
  }
  turbVariance /= size;
  tdsVariance  /= size;

  // Higher variance = lower confidence
  // Tuned for 12-bit ADC (0-4095 range)
  float turbConf = 100.0f - constrain(turbVariance / 20.0f, 0, 100);
  float tdsConf  = 100.0f - constrain(tdsVariance  / 20.0f, 0, 100);

  return (turbConf + tdsConf) / 2.0f;
}

// ── Buzzer patterns ───────────────────────────────────────
void beepStartup() {
  // Single medium beep on new sample
  tone(BUZZER_PIN, 1000, 200);
  delay(200);
  noTone(BUZZER_PIN);
}

void beepSafe() {
  // Two short happy beeps
  tone(BUZZER_PIN, 1200, 100);
  delay(150);
  noTone(BUZZER_PIN);
  delay(80);
  tone(BUZZER_PIN, 1500, 100);
  delay(150);
  noTone(BUZZER_PIN);
}

void beepUnsafe() {
  // Three descending alert beeps
  tone(BUZZER_PIN, 900, 300);
  delay(350);
  noTone(BUZZER_PIN);
  delay(80);
  tone(BUZZER_PIN, 700, 300);
  delay(350);
  noTone(BUZZER_PIN);
  delay(80);
  tone(BUZZER_PIN, 500, 500);
  delay(550);
  noTone(BUZZER_PIN);
}

// ── Combined quality ──────────────────────────────────────
WaterQuality getQuality(float tds, float ntu) {
  int tdsScore, turbScore;

  if      (tds < 50)  tdsScore = 5;
  else if (tds < 150) tdsScore = 4;
  else if (tds < 300) tdsScore = 3;
  else if (tds < 600) tdsScore = 2;
  else                tdsScore = 1;

  if      (ntu < 10)  turbScore = 5;
  else if (ntu < 100) turbScore = 4;
  else if (ntu < 300) turbScore = 3;
  else if (ntu < 500) turbScore = 2;
  else                turbScore = 1;

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

// ── Trend symbol ─────────────────────────────────────────
// Draws a small up/down/stable arrow at (x, y)
void drawTrendSymbol(int x, int y, int trend) {
  if (trend == 1) {
    // Up arrow
    display.drawTriangle(x+4, y, x, y+6, x+8, y+6, WHITE);
  } else if (trend == -1) {
    // Down arrow
    display.drawTriangle(x, y, x+8, y, x+4, y+6, WHITE);
  } else {
    // Stable: horizontal dash
    display.drawFastHLine(x, y+3, 8, WHITE);
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  pinMode(BUZZER_PIN, OUTPUT);
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
    tdsBuffer[i]  = readADC(TDS_PIN);
    delay(20);
  }
}

void loop() {
  // ── Sample every 40ms ──────────────────────────────
  static unsigned long sampleTime = millis();
  if (millis() - sampleTime > 40U) {
    sampleTime = millis();
    turbBuffer[turbIndex++] = readADC(TURBIDITY_PIN);
    if (turbIndex == SCOUNT) turbIndex = 0;
    tdsBuffer[tdsIndex++] = readADC(TDS_PIN);
    if (tdsIndex == SCOUNT) tdsIndex = 0;
  }

  // ── Update every 2s ────────────────────────────────
  static unsigned long displayTime = millis();
  if (millis() - displayTime > 2000U) {
    displayTime = millis();

    // ── TDS ────────────────────────────────────────
    int tdsMedian     = getMedian(tdsBuffer, SCOUNT);
    float tdsVoltage  = tdsMedian * (3.3 / 4095.0);
    float compCoeff   = 1.0 + 0.02 * (temperature - 25.0);
    float compVoltage = tdsVoltage / compCoeff;
    float tds = (133.42 * pow(compVoltage, 3)
               - 255.86 * pow(compVoltage, 2)
               + 857.39 * compVoltage) * 0.5 * TDS_CORRECTION - TDS_OFFSET;
    if (tds < 0) tds = 0;

    // ── Turbidity ───────────────────────────────────
    int turbMedian = getMedian(turbBuffer, SCOUNT);
    float turbVoltage = turbMedian * (3.3 / 4095.0);
    float ntu = (CLEAR_ADC - turbMedian) * SCALE_FACTOR;
    if (ntu < 5)    ntu = 0;
    if (ntu > 3000) ntu = 3000;

    String turbStatus;
    if      (ntu < 10)  turbStatus = "Clear";
    else if (ntu < 100) turbStatus = "Slight";
    else if (ntu < 300) turbStatus = "Turbid";
    else                turbStatus = "Very Turbid";

    // ── Trend ───────────────────────────────────────
    // Save snapshot every 30 seconds
    static unsigned long snapshotTime = 0;
    if (millis() - snapshotTime >= TREND_WINDOW_MS) {
      snapshotTime = millis();
      snapshot30s.ntu       = ntu;
      snapshot30s.tds       = tds;
      snapshot30s.timestamp = millis();
      snapshot30s.valid     = true;
    }
    Trend trend = getTrend(ntu, tds);

    // ── Quality ─────────────────────────────────────
    WaterQuality q = getQuality(tds, ntu);
    float confidence = getConfidence(turbBuffer, tdsBuffer, SCOUNT);

    // ── WHO compliance ───────────────────────────────────────
    bool whoTDSFail = (tds > WHO_TDS_LIMIT);
    bool whoNTUFail = (ntu > WHO_NTU_LIMIT);
    bool whoSafe    = (!whoTDSFail && !whoNTUFail);

    // ── Buzzer logic ─────────────────────────────────────────
    static bool firstReading = true;
    if (firstReading) {
      beepStartup();
      firstReading = false;
    }

    static bool lastWhoSafe = true;
    if (whoSafe != lastWhoSafe || firstReading) {
      // Status changed or first result — beep accordingly
      if (whoSafe) beepSafe();
      else         beepUnsafe();
      lastWhoSafe = whoSafe;
    }

    // ── Serial ──────────────────────────────────────
    Serial.printf("ADC:%d | V:%.3f | NTU:%d | %s | TDS:%d ppm | Quality:%s | NTU_trend:%s | TDS_trend:%s | Conf:%.0f%%\n",
              turbMedian, turbVoltage, (int)ntu, turbStatus.c_str(), (int)tds,
              q.message.c_str(),
              trend.ntu ==  1 ? "rising"  :
              trend.ntu == -1 ? "falling" : "stable",
              trend.tds ==  1 ? "rising"  :
              trend.tds == -1 ? "falling" : "stable",
              confidence,
              whoSafe ? "PASS" : "FAIL");

    // ── OLED ────────────────────────────────────────
    display.clearDisplay();
    display.setTextColor(WHITE);

    if (screen == 0) {
      // ── Screen 0: Readings + trend arrows ────────
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("=== Readings ===");

      // TDS row: value + trend arrow
      display.setCursor(0, 14);
      display.print("TDS:");
      display.setTextSize(2);
      display.print((int)tds);
      display.setTextSize(1);
      display.print("ppm");
      drawTrendSymbol(110, 14, trend.tds); // arrow top-right

      // NTU row: value + trend arrow
      display.setCursor(0, 38);
      display.print("NTU:");
      display.setTextSize(2);
      display.print((int)ntu);
      display.setTextSize(1);
      display.print("NTU");
      drawTrendSymbol(110, 38, trend.ntu); // arrow top-right

    } else if (screen == 1) {
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
    display.print("Conf: ");
    display.print((int)confidence);
    display.print("% | ");
    display.println(turbStatus);
    }  else {
      // ── Screen 2: WHO Compliance ──────────────────────
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("= WHO Check =");

      display.setTextSize(1);
      display.setCursor(0, 16);
      display.print("TDS: ");
      display.setTextSize(2);
      display.println(whoTDSFail ? "FAIL" : "PASS");

      display.setTextSize(1);
      display.setCursor(0, 36);
      display.print("NTU: ");
      display.setTextSize(2);
      display.println(whoNTUFail ? "FAIL" : "PASS");

      display.setTextSize(1);
      display.setCursor(0, 56);
      display.println(whoSafe ? ">> SAFE TO DRINK" : ">> DO NOT DRINK!");
    }

    display.display();
    screen = (screen + 1) % 3;
  }
}