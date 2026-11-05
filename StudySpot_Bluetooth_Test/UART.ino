
/*
  StudySpot Finder
  BLE Scanner Helper Functions

  BlueNRG-MS ST BLE TinyShield
  Repeating General Discovery
*/

// ====================================
// INITIALIZE BLUETOOTH ONCE
// ====================================

bool BLEscannerSetup() {

  HCI_Init();
  BNRG_SPI_Init();
  BlueNRG_RST();

  uint8_t bdaddr[] = {
    0x12, 0x34, 0x00,
    0xE1, 0x80, 0x02
  };

  int status = aci_hal_write_config_data(
    CONFIG_DATA_PUBADDR_OFFSET,
    CONFIG_DATA_PUBADDR_LEN,
    bdaddr
  );

  SerialUSB.print("Address status: ");
  SerialUSB.println(status);

  if (status != BLE_STATUS_SUCCESS) {
    return false;
  }

  status = aci_gatt_init();

  SerialUSB.print("GATT status: ");
  SerialUSB.println(status);

  if (status != BLE_STATUS_SUCCESS) {
    return false;
  }

  uint16_t serviceHandle;
  uint16_t nameHandle;
  uint16_t appearanceHandle;

  status = aci_gap_init_IDB05A1(
    GAP_CENTRAL_ROLE_IDB05A1,
    0,
    0x07,
    &serviceHandle,
    &nameHandle,
    &appearanceHandle
  );

  SerialUSB.print("GAP status: ");
  SerialUSB.println(status);

  if (status != BLE_STATUS_SUCCESS) {
    return false;
  }

  SerialUSB.println("Bluetooth initialized.");

  return true;
}

// ====================================
// START DISCOVERY
// ====================================

bool BLEscannerStart() {

  int status = aci_gap_start_general_discovery_proc(
    0x0060,  // 60 ms scan interval
    0x0030,  // 30 ms scan window
    0x00,    // Public address
    0x00     // Duplicate filtering off
  );

  SerialUSB.print("Scan start status: ");
  SerialUSB.println(status);

  return status == BLE_STATUS_SUCCESS;
}

// ====================================
// PROCESS BLUETOOTH EVENTS
// ====================================

void BLEscannerLoop() {

  HCI_Process();
}

// ====================================
// BLUETOOTH EVENT CALLBACK
// ====================================

void HCI_Event_CB(void *packet) {

  hci_uart_pckt *hciPacket =
      (hci_uart_pckt *)packet;

  if (hciPacket->type != HCI_EVENT_PKT) {
    return;
  }

  hci_event_pckt *eventPacket =
      (hci_event_pckt *)hciPacket->data;

  // ==================================
  // A. GENERAL DISCOVERY COMPLETED
  // ==================================

  if (eventPacket->evt == EVT_VENDOR) {

    evt_blue_aci *blueEvent =
        (evt_blue_aci *)eventPacket->data;

    if (blueEvent->ecode ==
        EVT_BLUE_GAP_PROCEDURE_COMPLETE) {

      evt_gap_procedure_complete *complete =
          (evt_gap_procedure_complete *)
          blueEvent->data;

      if (complete->procedure_code == 0x02) {

        // Tell the main loop the scan has ended
        scanProcedureFinished = true;
      }
    }

    return;
  }

  // ==================================
  // B. BLE ADVERTISEMENT REPORT
  // ==================================

  if (eventPacket->evt != EVT_LE_META_EVENT) {
    return;
  }

  evt_le_meta_event *metaEvent =
      (evt_le_meta_event *)eventPacket->data;

  if (metaEvent->subevent !=
      EVT_LE_ADVERTISING_REPORT) {
    return;
  }

  uint8_t numberOfReports =
      metaEvent->data[0];

  uint8_t *reportPtr =
      &metaEvent->data[1];

  for (uint8_t i = 0;
       i < numberOfReports;
       i++) {

    le_advertising_info *report =
        (le_advertising_info *)reportPtr;

    int8_t rssi = (int8_t)
        report->data_RSSI[
          report->data_length
        ];

    recordDevice(
      report->bdaddr,
      report->bdaddr_type,
      rssi
    );

    reportPtr +=
        LE_ADVERTISING_INFO_SIZE +
        report->data_length + 1;
  }
}
