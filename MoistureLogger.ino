#include <TFT_ILI9341.h> // Graphics and font library for ILI9341 driver chip
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <SD.h>
#include <dht.h>

#define CS_PIN  9

XPT2046_Touchscreen ts(CS_PIN);
//XPT2046_Touchscreen ts(CS_PIN);  // Param 2 - NULL - No interrupts
//XPT2046_Touchscreen ts(CS_PIN, 255);  // Param 2 - 255 - No interrupts
//XPT2046_Touchscreen ts(CS_PIN, TIRQ_PIN);  // Param 2 - Touch IRQ Pin - interrupt enabled polling

Sd2Card card;
SdVolume volume;
SdFile root;
const int sdChipSelect = 10;

//DHT Constants
dht DHT;
int dht1 = A0;
int dht2 = A1;

typedef struct {
  float temp;
  float humid;
  float dew;
  int error;
}  DhtData;
DhtData ReadDht(int dht, float factorHumidA, float factorHumidB);

class Printer {
  public:
    void Print();
    virtual void PrintData(File file) = 0;
};
class TablePrinter: public Printer {
  public:
    void PrintData(File file) override;
};
class ChartPrinter: public Printer {
  public:
    void PrintData(File file) override;
};

TFT_ILI9341 tft = TFT_ILI9341();  // Invoke library

#define TIRQ_PIN 2

void setup() {
  pinMode(3, OUTPUT);
  digitalWrite(3, HIGH);
  pinMode(A2, OUTPUT); // DHT
  digitalWrite(A2, LOW);

  Serial.begin(115200);
  tft.begin();
  tft.setRotation(2);
  tft.fillScreen(ILI9341_BLACK);
  ts.begin();
  while (!Serial && (millis() <= 1000));

  tft.setCursor(2, 2);
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextFont(2);

  if (!SD.begin(sdChipSelect)) {
    tft.println("initialization failed!");
  } else {
    tft.println("initialization done.");
  }

  (new TablePrinter())->Print();
}

unsigned long lastDhtMeasureMillis = 0;
unsigned long dhtIntervalMillis = 2L * 60L * 1000L;
unsigned long dhtStartupMillis = 1500;
int hdtWriteCount = 1;

unsigned long tftOnMillis = 0;
unsigned long tftOffIntervalMillis = 20L * 1000L;

int touchCount = 0;
bool isDisplayOn = false;
void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastDhtMeasureMillis >= dhtIntervalMillis) {
    digitalWrite(A2, HIGH);
  }
  if (currentMillis - lastDhtMeasureMillis >= dhtIntervalMillis + dhtStartupMillis) {
    lastDhtMeasureMillis = currentMillis;
    WriteDhtToFile();
    UpdatePrint(touchCount);
  }

  if (isDisplayOn && (currentMillis - tftOnMillis >= tftOffIntervalMillis)) {
    // Display off
    digitalWrite(3, HIGH);
    isDisplayOn = false;
    touchCount = 0;
  }

  boolean istouched = ts.touched();
  if (istouched) {
    if (isDisplayOn) {
      touchCount++;
    }

    // Display on
    tftOnMillis = currentMillis;
    digitalWrite(3, LOW);
    isDisplayOn = true;

    TS_Point p = ts.getPoint();

    UpdatePrint(touchCount);
    if (touchCount == 1) {
      touchCount = -1;
    }
  }
  //Serial.println(freeRam());

  delay(100);
}

void UpdatePrint(int touchCount) {
  Printer *printer;
  printer = new ChartPrinter();
  if (touchCount == 0) {
    printer = new ChartPrinter();
  }
  if (touchCount == 1) {
    printer = new TablePrinter();
  }
  printer->Print();
}

int freeRam () {
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}

void WriteDhtToFile() {
  DhtData data1 = ReadDht(dht1, 0.821, 20.6);
  DhtData data2 = ReadDht(dht2, 0.821, 19);
  digitalWrite(A2, LOW);

  File dataFile = SD.open("datalog.txt", FILE_WRITE);
  if (dataFile) {
    dataFile.print(data1.temp, 1);
    dataFile.print(" ");
    dataFile.print(data1.humid, 1);
    dataFile.print(" ");
    dataFile.print(data1.dew, 1);
    dataFile.print(" ");
    dataFile.print(data2.temp, 1);
    dataFile.print(" ");
    dataFile.print(data2.humid, 1);
    dataFile.print(" ");
    dataFile.print(data2.dew, 1);
    dataFile.println("");
    dataFile.close();

    tft.setTextColor(ILI9341_YELLOW);
    tft.setCursor(60, 80);
    tft.fillRect(60, 80, 100, 15, ILI9341_BLACK);
    tft.print("sd written ");
    tft.print(hdtWriteCount++);
  }
}

double Taupunkt(double temp, double rel) {
  // aus http://www.wetterochs.de/wetter/feuchte.html
  // Taupunkt
  double a, b;
  if (temp > 0 ) {
    a = 7.5; b = 237.3;
  } else {
    a = 9.5; b = 265.5;
  }
  double SDD = 6.1078 * pow(10, (a * temp) / (b + temp));
  double DD = rel / 100 * SDD;
  double v = log10(DD / 6.1078);
  double TD = b * v / (a - v);
  return TD;
}

DhtData ReadDht(int dht, float factorHumidA, float factorHumidB) {
  int chk = DHT.read22(dht);
  DhtData data;
  if (chk != 0) {
    data.error = chk;
    return data;
  }
  data.temp = DHT.temperature;
  data.humid = DHT.humidity * factorHumidA + factorHumidB;
  data.dew = Taupunkt(data.temp, data.humid);
  return data;
}

void TablePrinter::PrintData(File dataFile) {
  WindFileToRowsFromEnd(dataFile, 19, true);

  while (dataFile.available()) {
    tft.write(dataFile.read());
  }
}

void WindFileToRowsFromEnd(File dataFile, int rows, bool printInfo) {
  int datacount = 0;

  while (dataFile.available()) {
    if (dataFile.read() == '\n') {
      datacount++;
    }
  }

  if (printInfo) {
    tft.print("Data rows found ");
    tft.print(datacount);
    tft.print(" last ");
    tft.print(rows);
    tft.println(" rows:");
  }

  dataFile.seek(0);
  while (dataFile.available() && datacount > rows) {
    if (dataFile.read() == '\n') {
      datacount--;
    }
  }

}

float GetValue(File dataFile) {
  char c = ' ';
  // Go to value start
  while ((c == ' ' ||  c == '\n') && dataFile.available()) {
    c = dataFile.read();
  }

  // Read value
  char data[4];
  int charIndex = 0;
  while (c != ' ' &&  c != '\n' && dataFile.available()) {
    data[charIndex++] = c;
    c = dataFile.read();
  }

  float value = String(data).toFloat();
  return value;
}

void ChartPrinter::PrintData(File dataFile) {
  const int pointSize = 2;
  int maxDatapoints = 360 / 2;
  WindFileToRowsFromEnd(dataFile, maxDatapoints, false);

  tft.drawLine(120, 1, 120, 360 , ILI9341_WHITE);

  int row = 1;
  DhtData dhtDataBack[2];
  while (dataFile.available()) {
    DhtData dhtData[2];
    for (int valueIndex = 0; valueIndex < 6; valueIndex++) {
      float value = GetValue(dataFile);
      switch (valueIndex) {
        case 0:
          dhtData[0].temp = value;
          break;
        case 1:
          dhtData[0].humid = value;
          break;
        case 2:
          dhtData[0].dew = value;
          break;
        case 3:
          dhtData[1].temp = value;
          break;
        case 4:
          dhtData[1].humid = value;
          break;
        case 5:
          dhtData[1].dew = value;
          break;
        case 6:
          valueIndex = 0;
          break;
      }
    }
    //    tft.print(dhtData[0].temp, 1);
    //    tft.print(" ");
    //    tft.print(dhtData[0].humid, 1);
    //    tft.print(" ");
    //    tft.print(dhtData[0].dew, 1);
    //    tft.print(" ");
    //    tft.print(dhtData[1].temp, 1);
    //    tft.print(" ");
    //    tft.print(dhtData[1].humid, 1);
    //    tft.print(" ");
    //    tft.println(dhtData[1].dew, 1);

    if (row != 1) {
      // DewIn - DewOut
      tft.drawLine(map(dhtDataBack[0].dew - dhtDataBack[1].dew, -10, 35, 0, 120), row, map(dhtData[0].dew - dhtData[1].dew, -10, 35, 0, 120), row + pointSize, ILI9341_YELLOW);
      // Temp Wall
      tft.drawLine(map((dhtDataBack[0].temp - dhtDataBack[1].temp) / 1.5L, -10, 35, 0, 120), row, map((dhtData[0].temp - dhtData[1].temp) / 1.5L, -10, 35, 0, 120), row + pointSize, ILI9341_MAGENTA);
      // In
      tft.drawLine(map(dhtDataBack[0].temp, -10, 35, 0, 120), row, map(dhtData[0].temp, -10, 35, 0, 120), row + pointSize, ILI9341_RED);
      tft.drawLine(map(dhtDataBack[0].humid, 0, 100, 0, 120), row, map(dhtData[0].humid, 0, 100, 0, 120), row + pointSize, ILI9341_BLUE);
      tft.drawLine(map(dhtDataBack[0].dew, -10, 35, 0, 120), row, map(dhtData[0].dew, -10, 35, 0, 120), row + pointSize, ILI9341_GREEN);
      // Out
      tft.drawLine(map(dhtDataBack[1].temp, -10, 35, 120, 240), row, map(dhtData[1].temp, -10, 35, 120, 240), row + pointSize, ILI9341_RED);
      tft.drawLine(map(dhtDataBack[1].humid, 0, 100, 120, 240), row, map(dhtData[1].humid, 0, 100, 120, 240), row + pointSize, ILI9341_BLUE);
      tft.drawLine(map(dhtDataBack[1].dew, -10, 35, 120, 240), row, map(dhtData[1].dew, -10, 35, 120, 240), row + pointSize, ILI9341_GREEN);
    }
    row += pointSize;

    dhtDataBack[0] = dhtData[0];
    dhtDataBack[1] = dhtData[1];
  }
}

void Printer::Print() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextFont(2);
  tft.setCursor(0, 0);

  File dataFile = SD.open("datalog.txt", FILE_READ);
  if (dataFile) {

    PrintData(dataFile);

    dataFile.close();
  } else {
    tft.print("No Data");
  }
  //SD.remove("datalog.txt");
}



