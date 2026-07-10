#define TURBIDITY_PIN 4
#define SCOUNT 30

int analogBuffer[SCOUNT];
int analogBufferIndex = 0;

// Adjust sensitivity if required
const float SCALE_FACTOR = 1000.0;

float clearVoltage = 0.0;

int getMedian(int *buffer, int size)
{
  int sorted[size];

  for (int i = 0; i < size; i++)
    sorted[i] = buffer[i];

  for (int i = 0; i < size - 1; i++)
  {
    for (int j = 0; j < size - i - 1; j++)
    {
      if (sorted[j] > sorted[j + 1])
      {
        int t = sorted[j];
        sorted[j] = sorted[j + 1];
        sorted[j + 1] = t;
      }
    }
  }

  return sorted[size / 2];
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println(" Turbidity Sensor Calibration");
  Serial.println(" Keep probe in DISTILLED WATER");
  Serial.println("================================");

  // Fill buffer
  for (int i = 0; i < SCOUNT; i++)
  {
    analogBuffer[i] = analogRead(TURBIDITY_PIN);
    delay(20);
  }

  // Average 100 samples for calibration
  long sum = 0;

  for (int i = 0; i < 100; i++)
  {
    sum += analogRead(TURBIDITY_PIN);
    delay(20);
  }

  float avgADC = sum / 100.0;
  clearVoltage = avgADC * (3.3 / 4095.0);

  Serial.print("Baseline Voltage = ");
  Serial.print(clearVoltage, 3);
  Serial.println(" V");

  Serial.println("--------------------------------");
}

void loop()
{
  static unsigned long sampleTime = millis();

  if (millis() - sampleTime > 40)
  {
    sampleTime = millis();

    analogBuffer[analogBufferIndex++] = analogRead(TURBIDITY_PIN);

    if (analogBufferIndex >= SCOUNT)
      analogBufferIndex = 0;
  }

  static unsigned long printTime = millis();

  if (millis() - printTime > 1000)
  {
    printTime = millis();

    int medianVal = getMedian(analogBuffer, SCOUNT);

    float voltage = medianVal * (3.3 / 4095.0);

    float ntu = (clearVoltage - voltage) * SCALE_FACTOR;

    if (ntu < 5)
      ntu = 0;

    if (ntu > 3000)
      ntu = 3000;

    String quality;

    if (ntu < 5)
      quality = "Clear";
    else if (ntu < 50)
      quality = "Slight";
    else if (ntu < 500)
      quality = "Turbid";
    else
      quality = "Very Turbid";

    Serial.print("ADC: ");
    Serial.print(medianVal);

    Serial.print(" | Voltage: ");
    Serial.print(voltage, 3);

    Serial.print(" V");

    Serial.print(" | NTU: ");
    Serial.print(ntu, 1);

    Serial.print(" | ");
    Serial.println(quality);
  }
}