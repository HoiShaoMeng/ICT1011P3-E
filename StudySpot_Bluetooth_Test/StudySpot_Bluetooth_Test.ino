
/*
  ICT1011 StudySpot Finder
  Repeating Bluetooth Scanner

  Scan duration: 10 seconds
  Count: Unique BLE advertising addresses
  Automatically repeats until switched off
*/

#include <SPI.h>
#include <STBLE.h>
#include <string.h>

const unsigned long SCAN_DURATION_MS = 10000UL;
const unsigned long RETRY_INTERVAL_MS = 3000UL;

const uint8_t MAX_DEVICES = 40;

uint8_t seenDevices[MAX_DEVICES][7];
uint8_t deviceCount = 0;

// Updated by the BLE event callback
volatile bool scanProcedureFinished = false;

bool bleReady = false;
bool scanActive = false;
bool stopRequested = false;

unsigned long scanStartedAt = 0;
unsigned long lastScanAttemptAt = 0;

unsigned int scanNumber = 0;

// Functions in UART.ino
bool BLEscannerSetup();
bool BLEscannerStart();
void BLEscannerLoop();

// ====================================
// RECORD UNIQUE BLE DEVICE
// ====================================

void recordDevice(
  const uint8_t address[6],
  uint8_t addressType,
  int8_t rssi
) {

  for (uint8_t i = 0; i < deviceCount; i++) {

    if (seenDevices[i][0] == addressType &&
        memcmp(&seenDevices[i][1], address, 6) == 0) {
      return;
    }
  }

  if (deviceCount >= MAX_DEVICES) {
    return;
  }

  seenDevices[deviceCount][0] = addressType;

  memcpy(
    &seenDevices[deviceCount][1],
    address,
    6
  );

  deviceCount++;

  SerialUSB.print("New BLE device | RSSI: ");
  SerialUSB.print(rssi);
  SerialUSB.print(" dBm | Count: ");
  SerialUSB.println(deviceCount);
}

// ====================================
// START A NEW SCAN CYCLE
// ====================================

void startScanCycle() {

  lastScanAttemptAt = millis();

  scanProcedureFinished = false;
  deviceCount = 0;
  memset(seenDevices, 0, sizeof(seenDevices));

  SerialUSB.println();
  SerialUSB.println("========================");
  SerialUSB.println("STARTING NEW BLE SCAN");

  if (BLEscannerStart()) {

    scanActive = true;
    stopRequested = false;
    scanStartedAt = millis();

    scanNumber++;

    SerialUSB.print("Scan number: ");
    SerialUSB.println(scanNumber);

    SerialUSB.println("Scanning for 10 seconds...");

  } else {

    scanActive = false;
    stopRequested = false;

    SerialUSB.println(
      "Scan failed. Retrying shortly..."
    );
  }
}

// ====================================
// SETUP
// ====================================

void setup() {

  SerialUSB.begin(115200);

  while (!SerialUSB && millis() < 5000) {
    delay(10);
  }

  SerialUSB.println();
  SerialUSB.println("========================");
  SerialUSB.println(" STUDYSPOT BLE SCANNER");
  SerialUSB.println("========================");

  bleReady = BLEscannerSetup();

  if (bleReady) {
    startScanCycle();
  } else {
    SerialUSB.println("BLE setup failed.");
  }
}

// ====================================
// MAIN LOOP
// ====================================

void loop() {

  // Handle Bluetooth events frequently
  BLEscannerLoop();

  unsigned long now = millis();

  // ==================================
  // A. HANDLE COMPLETED SCAN
  // ==================================

  if (scanProcedureFinished) {

    scanProcedureFinished = false;
    scanActive = false;
    stopRequested = false;

    SerialUSB.println();
    SerialUSB.println("------------------------");

    SerialUSB.print("Scan ");
    SerialUSB.print(scanNumber);
    SerialUSB.println(" completed");

    SerialUSB.print("BLE devices detected: ");
    SerialUSB.println(deviceCount);

    SerialUSB.println("------------------------");

    // Start the next scan automatically
    if (bleReady) {
      startScanCycle();
    }

    return;
  }

  // ==================================
  // B. STOP SCAN AFTER 10 SECONDS
  // ==================================

  if (scanActive &&
      !stopRequested &&
      now - scanStartedAt >= SCAN_DURATION_MS) {

    SerialUSB.println("Finishing scan...");

    // 0x02 = General Discovery procedure
    int status = aci_gap_terminate_gap_procedure(
      0x02
    );

    SerialUSB.print("Stop status: ");
    SerialUSB.println(status);

    stopRequested = true;

    // Wait for BLE procedure-complete event
    // before starting the next scan.
  }

  // ==================================
  // C. RETRY A FAILED SCAN
  // ==================================

  if (bleReady &&
      !scanActive &&
      now - lastScanAttemptAt >=
          RETRY_INTERVAL_MS) {

    startScanCycle();
  }
}
