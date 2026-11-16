# RoadIoT — Toy Car Speed Detector & Overspeed Alert System

An Arduino Uno–based IoT speed-trap prototype that measures the speed of a moving toy car between two infrared (IR) obstacle sensors, displays live speed telemetry on a 4-pin **GME12864-4 (128×64 I2C OLED)** display, logs speed and alert events over Serial (`9600` baud), and triggers visual (**Red/Green LEDs**) and audible (**Buzzer**) overspeed alerts.

> Looking for the full technical theory, state-machine breakdown, calibration guide, and testing manual? See **[PROJECT_MANUAL.md](PROJECT_MANUAL.md)**.

---

## Features

- **Microsecond Hardware-Interrupt Timing:** Uses Arduino Uno external interrupts (`INT0` on `D2` and `INT1` on `D3`) with `micros()` so I2C OLED updates never skew speed measurements.
- **Live 128×64 OLED Display (`GME12864-4`):** Shows `READY` status, `TIMING...` state, speed in `cm/s` and `km/h`, travel time (`t`), and an inverted `!! OVERSPEED !!` alert banner.
- **Automatic I2C Address Detection:** Scans the I2C bus on startup for `0x3C` / `0x3D` and applies `100 kHz` bus speed with wire-timeout protection.
- **Dual Visual & Audible Alerts:** Flashes a Red LED (`D8`) and pulses a Buzzer (`D10`) on overspeed; lights a Green LED (`D9`) when speed is within limits.
- **Serial Monitor Telemetry (`9600` baud):** Prints startup diagnostics, live IR sensor trigger events, calculated speeds, and alert status.
- **Safety & Anti-Lockup Guards:** Filters IR noise (`< 15 ms`), resets stalled runs after `3.0 s`, and warns if an IR sensor remains stuck blocked.

---

## Bill of Materials (Hardware)

| Qty | Component | Specification / Notes |
| :---: | :--- | :--- |
| 1 | **Arduino Uno** | ATmega328P microcontroller + USB cable |
| 2 | **IR Obstacle Sensors** | LM393-based 3-pin (`VCC`, `GND`, `OUT`), Active `LOW` |
| 1 | **GME12864-4 OLED Display** | 0.96" 128×64 4-pin I2C OLED (SSD1306) |
| 1 | **Red LED** | Overspeed alert indicator |
| 1 | **Green LED** *(Optional)* | Normal speed indicator |
| 2 | **220 Ω Resistors** | Current-limiting resistors for Red & Green LEDs (`150 Ω–1 kΩ` safe) |
| 1 | **Buzzer** | 5V Passive or Active piezo buzzer |
| 1 | **Breadboard & Jumper Wires** | Male-to-Male and Male-to-Female wires |

---

## Circuit Connection Table

> **Important OLED Check:** Before powering on, verify the pin labels printed on your **GME12864-4** PCB. Most modules use `GND - VCC - SCL - SDA`, while some batches swap `VCC` and `GND`. Always wire according to the printed silkscreen labels.

### Power Rails
| Arduino Uno | Breadboard |
| :--- | :--- |
| `5V` | `+` Power Rail |
| `GND` | `-` Ground Rail |

### Component Pin Mapping
| Component | Component Pin | Connect To | Notes |
| :--- | :--- | :--- | :--- |
| **GME12864-4 OLED** | `VCC` (or `VDD`) | Breadboard `+` Rail (`5V`) | Power supply |
| | `GND` | Breadboard `-` Rail (`GND`) | Common ground |
| | `SCL` (or `SCK`) | Arduino **`A5`** | Hardware I2C Clock |
| | `SDA` | Arduino **`A4`** | Hardware I2C Data |
| **IR Sensor 1 (Start)** | `VCC` | Breadboard `+` Rail (`5V`) | Mount at start gate |
| | `GND` | Breadboard `-` Rail (`GND`) | Common ground |
| | `OUT` | Arduino **`D2`** (`INT0`) | Start trigger |
| **IR Sensor 2 (Stop)** | `VCC` | Breadboard `+` Rail (`5V`) | Mount `20 cm` after Sensor 1 |
| | `GND` | Breadboard `-` Rail (`GND`) | Common ground |
| | `OUT` | Arduino **`D3`** (`INT1`) | Stop trigger |
| **Red LED** | `+` Anode (Long leg) | Arduino **`D8`** via **`220 Ω` resistor** | Never connect bare LED without resistor |
| | `-` Cathode (Short leg) | Breadboard `-` Rail (`GND`) | Common ground |
| **Green LED *(Optional)*** | `+` Anode (Long leg) | Arduino **`D9`** via **`220 Ω` resistor** | Normal speed indicator |
| | `-` Cathode (Short leg) | Breadboard `-` Rail (`GND`) | Common ground |
| **Buzzer** | `+` Positive | Arduino **`D10`** | Overspeed alarm |
| | `-` Negative | Breadboard `-` Rail (`GND`) | Common ground |

### Breadboard Wiring Diagram

```text
                        +---------------------------------------+
                        |              ARDUINO UNO              |
                        |                                       |
   +---[ +5V Rail ]<----| 5V                                D2  |<--- IR Sensor 1 (OUT)
   |   [ GND Rail ]<----| GND                               D3  |<--- IR Sensor 2 (OUT)
   |                    |                                       |
   |                    |                                   D8  |---> [220Ω] ---> (+) Red LED (-) ---> [GND Rail]
   |                    |                                   D9  |---> [220Ω] ---> (+) Green LED (-)-> [GND Rail]
   |                    |                                   D10 |---------------> (+) Buzzer (-) ----> [GND Rail]
   |                    |                                       |
   |                    |  A4 (SDA)                   A5 (SCL)  |
   +--------------------+-----+---------------------------+-----+
                              |                           |
                              |                           |
   +--------------------------+---------------------------+--------------------------+
   |                             BREADBOARD CONNECTIONS                              |
   |                                                                                 |
   |  [ +5V Rail (Red) ]  o-----o-------------------o-------------------o            |
   |  [ GND Rail (Blue) ] o-----|--o----------------|--o----------------|--o         |
   |                            |  |                |  |                |  |         |
   |                       VCC GND OUT         VCC GND OUT         VCC GND SCL SDA   |
   |                      +-----------+       +-----------+       +---------------+  |
   |                      | IR Sens 1 |       | IR Sens 2 |       | GME12864-4    |  |
   |                      |  (Start)  |       |  (Stop)   |       | 128x64 OLED   |  |
   |                      +-----------+       +-----------+       +---------------+  |
   +---------------------------------------------------------------------------------+
```

---

## Repository Structure

```text
roadiot/
├── README.md                                  # Quickstart, wiring, and usage guide
├── PROJECT_MANUAL.md                          # Complete engineering & calibration manual
└── roadiot_speed_detector/
    └── roadiot_speed_detector.ino             # Arduino Uno firmware sketch
```

---

## Getting Started (Arduino IDE)

1. **Install Required Libraries**
   - Open **Arduino IDE** → **Sketch** → **Include Library** → **Manage Libraries...**
   - Install **`Adafruit SSD1306`** (click **Install All** when prompted for dependencies such as **`Adafruit GFX Library`** and **`Adafruit BusIO`**).

2. **Open & Configure the Sketch**
   - Open [`roadiot_speed_detector/roadiot_speed_detector.ino`](roadiot_speed_detector/roadiot_speed_detector.ino).
   - Adjust the top configuration constants if needed:
     - `SENSOR_DISTANCE_CM` (`20.0`): Exact center-to-center distance between IR Sensor 1 and Sensor 2 in cm.
     - `SPEED_LIMIT_CM_S` (`50.0`): Overspeed threshold in cm/s (`50.0 cm/s` = `1.80 km/h`).
     - `IR_TRIGGER_STATE` (`LOW`): Set to `HIGH` if your IR module outputs `HIGH` on detection.
     - `BUZZER_IS_PASSIVE` (`true`): Set to `false` if you are using an active buzzer.

3. **Upload & Monitor**
   - Select **Tools → Board → Arduino AVR Boards → Arduino Uno**.
   - Select your serial port under **Tools → Port** and click **Upload**.
   - Open **Tools → Serial Monitor** at **`9600` baud**.

---

## Example Serial Monitor Output

```text
========================================
   ROAD-IOT TOY CAR SPEED DETECTOR
========================================
Scanning I2C bus for GME12864-4 OLED...
  -> Found I2C device at 0x3C
OLED initialized successfully at 0x3C
IR1 (D2): CLEAR | IR2 (D3): CLEAR
Calibration -> Distance: 20.0 cm | Limit: 50.0 cm/s
Trap READY. Roll toy car past Sensor 1 -> Sensor 2.

[IR1] Car detected -> Timer started...
----------------------------------------
Travel Time : 0.312 s
Speed       : 64.1 cm/s  (2.31 km/h)
Speed Limit : 50.0 cm/s
Alert       : [OVERSPEED ALERT!] Red LED & Buzzer ON!
----------------------------------------
```

---

## Quick Troubleshooting

| Symptom | Likely Cause | Fix |
| :--- | :--- | :--- |
| Serial Monitor shows gibberish or nothing | Wrong baud rate | Set Serial Monitor to **`9600` baud** and press Uno **RESET** |
| `No I2C device detected!` in Serial Monitor | Loose/swapped OLED wires | Verify `SDA -> A4`, `SCL -> A5`, `VCC -> 5V`, `GND -> GND` matching board labels |
| OLED detected (`0x3C`/`0x3D`) but stays blank | SH1106 controller variant | Replace `Adafruit_SSD1306` driver with `Adafruit_SH1106G` (`Adafruit_SH110X`) |
| `IR1` or `IR2` prints `BLOCKED` on boot | Potentiometer sensitivity too high | Turn the blue trimmer pot on the IR module until its obstacle LED turns OFF when empty |
| Buzzer only clicks instead of beeping | Active vs. passive mismatch | Toggle `BUZZER_IS_PASSIVE` between `true` and `false` |
