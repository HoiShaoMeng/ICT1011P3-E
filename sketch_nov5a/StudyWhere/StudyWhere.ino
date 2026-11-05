
/*
  ICT1011 - StudySpot Finder
  Microphone + Temperature + WiFi Test

  Hardware:
  - TinyScreen+
  - Wireling Adapter TinyShield
  - Microphone Wireling on Port 0 (A0)
  - Temperature Wireling on Port 2
  - WiFi TinyShield (ATWINC1500)

  Functions:
  1. Continuously sample microphone
  2. Report noise amplitude every 1 second
  3. Read temperature every 30 minutes
  4. Connect to WiFi
  5. Check WiFi connection and reconnect if needed

  Sensor interfaces based on TinyCircuits tutorials.
*/

#include <Wire.h>
#include <Wireling.h>
#include <TinyCircuits_PCT2075.h>
#include <SPI.h>
#include <WiFi101.h>

// ==================================
// 1. WIFI SETTINGS
// ==================================

char wifiSSID[] = "Shao's S24+";
char wifiPassword[] = "ryan1170";

// ==================================
// 2. SENSOR SETTINGS
// ==================================

const int MIC_PIN = A2;
const int TEMP_PORT = 0;

TinyCircuits_PCT2075 tempSensor;

// ==================================
// 3. TIMING SETTINGS
// ==================================

const unsigned long MIC_WINDOW_MS = 100UL;
const unsigned long REPORT_INTERVAL_MS = 1000UL;

const unsigned long TEMP_INTERVAL_MS =
    30UL * 60UL * 1000UL; // 30 minutes

const unsigned long WIFI_CHECK_MS = 5000UL;
const unsigned long WIFI_RETRY_MS = 30000UL;

// ==================================
// 4. VARIABLES
// ==================================

unsigned long lastMicWindow = 0;
unsigned long lastReport = 0;
unsigned long lastTempRead = 0;
unsigned long lastWiFiCheck = 0;
unsigned long lastWiFiAttempt = 0;

// Microphone
int micMin = 1023;
int micMax = 0;

unsigned long amplitudeTotal = 0;
unsigned int amplitudeWindows = 0;

// Temperature
float latestTempC = 0.0;
bool temperatureValid = false;

// WiFi
bool wifiShieldFound = false;
bool wifiConnected = false;
long wifiRSSI = 0;

// ==================================
// 5. TEMPERATURE FUNCTION
// ==================================

void updateTemperature() {

  Wireling.selectPort(TEMP_PORT);

  float rawTemp = tempSensor.readTemp() / 32.0;
  latestTempC = rawTemp * 0.125;

  temperatureValid = true;
  lastTempRead = millis();

  SerialUSB.print("[TEMP UPDATED] ");
  SerialUSB.print(latestTempC, 2);
  SerialUSB.println(" C");
}

// ==================================
// 6. WIFI CONNECTION FUNCTION
// ==================================

void connectWiFi() {

  if (!wifiShieldFound) {
    return;
  }

  SerialUSB.println();
  SerialUSB.print("Connecting to WiFi: ");
  SerialUSB.println(wifiSSID);

  // This call may take several seconds.
  WiFi.begin(wifiSSID, wifiPassword);

  wifiConnected = (WiFi.status() == WL_CONNECTED);

  if (wifiConnected) {

    wifiRSSI = WiFi.RSSI();

    SerialUSB.println("WiFi CONNECTED!");

    SerialUSB.print("IP Address: ");
    SerialUSB.println(WiFi.localIP());

    SerialUSB.print("Signal: ");
    SerialUSB.print(wifiRSSI);
    SerialUSB.println(" dBm");

  } else {

    SerialUSB.println("WiFi connection failed.");
    SerialUSB.println("Will retry in 30 seconds.");
  }

  lastWiFiAttempt = millis();
  lastWiFiCheck = millis();

  // Restart the microphone sampling window
  // after a potentially long WiFi operation.
  micMin = 1023;
  micMax = 0;
  lastMicWindow = millis();
}

// ==================================
// 7. SETUP
// ==================================

void setup() {

  SerialUSB.begin(115200);

  delay(1500);

  SerialUSB.println();
  SerialUSB.println("==========================");
  SerialUSB.println("   STUDYSPOT FINDER");
  SerialUSB.println("==========================");

  // --------------------------------
  // Microphone setup
  // --------------------------------

  analogReadResolution(10);
  pinMode(MIC_PIN, INPUT);

  // --------------------------------
  // Wireling setup
  // --------------------------------

  Wireling.begin();
  Wire.begin();

  delay(200);

  // --------------------------------
  // Temperature setup
  // --------------------------------

  Wireling.selectPort(TEMP_PORT);

  tempSensor.begin(
    PCT2075_ACTIVE_LOW,
    PCT2075_COMPARATOR,
    PCT2075_NORMAL,
    PCT2075_FAULT_6X
  );

  tempSensor.wake();

  delay(200);

  // First temperature reading
  updateTemperature();

  // --------------------------------
  // WiFi setup
  // --------------------------------

  WiFi.setPins(8, 2, A3, -1);

  wifiShieldFound =
      (WiFi.status() != WL_NO_SHIELD);

  if (wifiShieldFound) {

    SerialUSB.println("WiFi shield detected.");

    connectWiFi();

  } else {

    SerialUSB.println("WiFi shield NOT detected!");
    SerialUSB.println(
      "Microphone and temperature will still run."
    );
  }

  // --------------------------------
  // Start timers
  // --------------------------------

  unsigned long now = millis();

  lastMicWindow = now;
  lastReport = now;
  lastWiFiCheck = now;

  SerialUSB.println();
  SerialUSB.println("Monitoring started.");
}

// ==================================
// 8. MAIN LOOP
// ==================================

void loop() {

  unsigned long currentTime = millis();

  // ==================================
  // A. MICROPHONE SAMPLING
  // ==================================

  int reading = analogRead(MIC_PIN);

  if (reading < micMin) {
    micMin = reading;
  }

  if (reading > micMax) {
    micMax = reading;
  }

  // Finish each 100 ms window
  if (currentTime - lastMicWindow >=
      MIC_WINDOW_MS) {

    int amplitude = micMax - micMin;

    amplitudeTotal += amplitude;
    amplitudeWindows++;

    micMin = 1023;
    micMax = 0;

    lastMicWindow = currentTime;
  }

  // ==================================
  // B. TEMPERATURE EVERY 30 MINUTES
  // ==================================

  if (currentTime - lastTempRead >=
      TEMP_INTERVAL_MS) {

    updateTemperature();
  }

  // ==================================
  // C. CHECK WIFI EVERY 5 SECONDS
  // ==================================

  if (wifiShieldFound &&
      currentTime - lastWiFiCheck >=
      WIFI_CHECK_MS) {

    wifiConnected =
        (WiFi.status() == WL_CONNECTED);

    if (wifiConnected) {
      wifiRSSI = WiFi.RSSI();
    }

    lastWiFiCheck = millis();
  }

  // ==================================
  // D. RECONNECT WIFI WHEN NEEDED
  // ==================================

  if (wifiShieldFound &&
      !wifiConnected &&
      currentTime - lastWiFiAttempt >=
      WIFI_RETRY_MS) {

    connectWiFi();
  }

  // ==================================
  // E. REPORT EVERY 1 SECOND
  // ==================================

  if (currentTime - lastReport >=
      REPORT_INTERVAL_MS) {

    SerialUSB.println();
    SerialUSB.println("--------------------------");

    // Microphone output
    if (amplitudeWindows > 0) {

      float averageAmplitude =
          (float)amplitudeTotal / amplitudeWindows;

      SerialUSB.print("Noise Amplitude: ");
      SerialUSB.println(averageAmplitude, 2);

    } else {

      SerialUSB.println(
        "Noise Amplitude: Sampling..."
      );
    }

    // Temperature output
    SerialUSB.print("Temperature: ");

    if (temperatureValid) {

      SerialUSB.print(latestTempC, 2);
      SerialUSB.println(" C");

    } else {

      SerialUSB.println("Not available");
    }

    // WiFi output
    SerialUSB.print("WiFi: ");

    if (!wifiShieldFound) {

      SerialUSB.println("SHIELD NOT FOUND");

    } else if (wifiConnected) {

      SerialUSB.println("CONNECTED");

      SerialUSB.print("WiFi Signal: ");
      SerialUSB.print(wifiRSSI);
      SerialUSB.println(" dBm");

    } else {

      SerialUSB.println("DISCONNECTED");
    }

    SerialUSB.println("--------------------------");

    // Reset microphone averaging
    amplitudeTotal = 0;
    amplitudeWindows = 0;

    lastReport = millis();
  }
}
