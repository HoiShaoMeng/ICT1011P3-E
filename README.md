# ICT1011P3-


## 1. Required Arduino Libraries

The following libraries are required for the StudySpot Finder project using TinyScreen+.

| Library | Purpose | Installation Method |
|---|---|---|
| `Wireling` | Interface with Wireling sensors | Arduino Library Manager |
| `TinyCircuits_PCT2075` | Temperature Sensor Wireling (AST1021) | TinyCircuits ZIP |
| `WiFi101` | Wi-Fi TinyShield (ASD2123) | Arduino Library Manager |
| `STBLE` | Bluetooth LE TinyShield (ASD2116) | TinyCircuits ZIP |
| `TinyScreen` | OLED display and buttons | Arduino Library Manager |
| `SPI` | SPI communication | Built-in Arduino library |
| `Wire` | I2C communication | Built-in Arduino library |

### Installation Instructions

1. Open **Arduino IDE**.
2. Navigate to **Sketch > Include Library > Manage Libraries**.
3. Search for and install:
   - `Wireling`
   - `WiFi101`
   - `TinyScreen`
4. Download the following libraries from [TinyCircuits](https://learn.tinycircuits.com/):
   - `TinyCircuits_PCT2075`
   - `STBLE`
5. Install the downloaded ZIP files using **Sketch > Include Library > Add .ZIP Library**.

### Additional Notes

- The Microphone Wireling (AST1030) does not require an additional library. It uses `analogRead()`.
- The `SPI` and `Wire` libraries are included with the Arduino platform.
- Ensure the **TinyCircuits SAMD Boards** package is installed and **TinyScreen+** is selected in Arduino IDE.
- The Wi-Fi and Bluetooth TinyShields share an interrupt pin by default and may require additional hardware configuration for simultaneous operation.
