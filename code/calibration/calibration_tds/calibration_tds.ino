#include <Wire.h>

// TDS settings
#define TDS_PIN 0        // GPIO0 for ESP32-C3
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
  Serial.println("=== TDS Calibration Mode ===");
  Serial.println("Dip probe and observe readings");
  Serial.println("----------------------------");
}

void loop() {
  // collect samples
  static unsigned long sampleTime = millis();
  if (millis() - sampleTime > 40U) {
    sampleTime = millis();
    analogBuffer[analogBufferIndex++] = analogRead(TDS_PIN);
    if (analogBufferIndex == SCOUNT) analogBufferIndex = 0;
  }

  // print every 1s
  static unsigned long printTime = millis();
  if (millis() - printTime > 1000U) {
    printTime = millis();

    int medianVal = getMedian(analogBuffer, SCOUNT);
    float voltage = medianVal * (VREF / 4095.0); // ESP32-C3 is 12-bit

    float compCoeff = 1.0 + 0.02 * (temperature - 25.0);
    float compVoltage = voltage / compCoeff;

    float tds = (133.42 * pow(compVoltage, 3)
               - 255.86 * pow(compVoltage, 2)
               + 857.39 * compVoltage) * 0.5 ;
    if (tds < 0) tds = 0;

    Serial.print("Raw ADC: ");    Serial.print(medianVal);
    Serial.print(" | Voltage: "); Serial.print(voltage, 3);
    Serial.print("V | TDS: ");    Serial.print(tds, 1);
    Serial.println(" ppm");
  }
}