# RoadIoT Project & Technical Manual

**Project:** IoT Toy Car Speed Detection & Overspeed Alert System  
**Platform:** Arduino Uno (ATmega328P)  
**Firmware:** [`roadiot_speed_detector/roadiot_speed_detector.ino`](roadiot_speed_detector/roadiot_speed_detector.ino)

---

## 1. System Overview

RoadIoT is a two-gate optical speed trap built for measuring toy car speeds on a track. Two infrared (IR) obstacle sensors placed at a known distance apart detect when a toy car enters and exits the measurement zone. An Arduino Uno timestamps both events with microsecond precision, calculates speed, renders the result on a **GME12864-4 128×64 I2C OLED display**, streams formatted telemetry to the Serial Monitor, and triggers an LED + buzzer alert whenever the speed exceeds a configurable threshold.

---

## 2. Operating Principle & Speed Math

1. **Gate Spacing ($d$):** IR Sensor 1 (`D2`) and IR Sensor 2 (`D3`) are mounted perpendicular to the track at a fixed center-to-center distance $d$ (default `20.0 cm`).
2. **Start Timestamp ($t_1$):** When the front of the toy car crosses Sensor 1, `onStartSensor()` records $t_1 = \text{micros()}$.
3. **Stop Timestamp ($t_2$):** When the front of the toy car crosses Sensor 2, `onStopSensor()` records $t_2 = \text{micros()}$.
4. **Speed Calculation:**
   $$\Delta t = \frac{t_2 - t_1}{1{,}000{,}000} \quad (\text{seconds})$$
   $$v_{\text{cm/s}} = \frac{d_{\text{cm}}}{\Delta t}, \qquad v_{\text{m/s}} = \frac{v_{\text{cm/s}}}{100}, \qquad v_{\text{km/h}} = v_{\text{m/s}} \times 3.6$$

### Example Reference Table ($d = 20.0\text{ cm}$, Limit $= 50.0\text{ cm/s}$)

| Travel Time ($\Delta t$) | Speed ($\text{cm/s}$) | Speed ($\text{km/h}$) | System Decision |
| :---: | :---: | :---: | :--- |
| `1.000 s` | `20.0 cm/s` | `0.72 km/h` | `NORMAL` (Green LED) |
| `0.500 s` | `40.0 cm/s` | `1.44 km/h` | `NORMAL` (Green LED) |
| `0.400 s` | `50.0 cm/s` | `1.80 km/h` | Threshold boundary (`NORMAL`) |
| `0.250 s` | `80.0 cm/s` | `2.88 km/h` | `OVERSPEED` (Red LED + Buzzer) |
| `0.100 s` | `200.0 cm/s` | `7.20 km/h` | `OVERSPEED` (Red LED + Buzzer) |

---

## 3. Hardware Architecture & Electrical Notes

### 3.1 Pin Allocation

| Pin | Direction | Connected Device | Role |
| :---: | :--- | :--- | :--- |
| `D2` | Input | IR Sensor 1 `OUT` | `INT0` hardware interrupt (start timer) |
| `D3` | Input | IR Sensor 2 `OUT` | `INT1` hardware interrupt (stop timer) |
| `D8` | Output | Red LED `+` (via `220 Ω`) | Flashes on overspeed |
| `D9` | Output | Green LED `+` (via `220 Ω`) | Lights on normal speed |
| `D10` | Output | Buzzer `+` | Pulses `2200 Hz` tone (or `HIGH`) on overspeed |
| `A4` | I2C (`SDA`) | GME12864-4 `SDA` | I2C display data |
| `A5` | I2C (`SCL`) | GME12864-4 `SCL` | I2C display clock |

### 3.2 Electrical Safety Rules

1. **Always Use Current-Limiting Resistors on Bare LEDs:**
   - Arduino Uno digital pins (`D8`, `D9`) output **`5V`** when driven `HIGH`, regardless of whether the breadboard rail uses `3.3V` or `5V`.
   - Even on a `3.3V` rail, a standard Red LED drops only `~1.8V–2.0V` (`~2.0V–2.2V` for Green). Without a resistor, current is unregulated and can burn out the LED or damage the microcontroller pin (`20–40 mA` limit).
   - Use **`220 Ω`** resistors (`150 Ω–1 kΩ` is acceptable if `220 Ω` is unavailable).
2. **Verify GME12864-4 Power Pin Order:**
   - Some 4-pin OLED modules print `GND, VCC, SCL, SDA`; others print `VCC, GND, SCL, SDA`. Always match the printed silkscreen labels before connecting USB power.
3. **Active vs. Passive Buzzer:**
   - **Passive buzzer** requires an AC square wave (`tone(BUZZER_PIN, 2200)`), enabled when `BUZZER_IS_PASSIVE = true`.
   - **Active buzzer** has an internal oscillator and needs only DC `HIGH`/`LOW`, enabled when `BUZZER_IS_PASSIVE = false`.

---

## 4. Firmware Architecture

### 4.1 Interrupt-Driven Timing

I2C screen refreshes (`display.display()`) take tens of milliseconds over `100 kHz` I2C. If speed timing relied on polling `digitalRead()` inside `loop()`, updating the OLED to `"TIMING..."` after Sensor 1 could delay reading Sensor 2 on fast cars.

To avoid this, the firmware attaches hardware interrupts (`attachInterrupt`) to `D2` (`INT0`) and `D3` (`INT1`):
- Interrupt edge defaults to `FALLING` when `IR_TRIGGER_STATE == LOW` (standard LM393 active-low output), or `RISING` if `IR_TRIGGER_STATE == HIGH`.
- On ATmega328P, `EIFR = (1 << INTF0) | (1 << INTF1)` clears pending interrupt flags before arming so boot transitions never cause phantom triggers.
- Multi-byte `unsigned long` timestamps (`startTimeUs`, `stopTimeUs`) are read in `loop()` inside `noInterrupts() ... interrupts()` critical sections to prevent torn reads.

### 4.2 Finite State Machine

```text
   +-------------+   IR1 triggers    +--------------+   IR2 triggers (>= 15 ms)   +----------------+
   | STATE_READY | ----------------> | STATE_TIMING | --------------------------> | STATE_CAPTURED |
   +-------------+                   +--------------+                             +----------------+
          ^                                  |                                             |
          |                                  | >= 3000 ms timeout                          | Calculate speed,
          |                                  v                                             | update OLED/Serial,
          |                          +----------------+                                    | run LED/Buzzer alert
          +------------------------- | STATE_COOLDOWN | <----------------------------------+
            Wait for both sensors    +----------------+
            to clear + re-arm OLED
```

| State | Behavior |
| :--- | :--- |
| `STATE_READY` (`0`) | OLED displays `READY`; waiting for Sensor 1 interrupt. |
| `STATE_TIMING` (`1`) | OLED updates once to `TIMING...`; `loop()` monitors for `TIMING_TIMEOUT_MS` (`3000 ms`). |
| `STATE_CAPTURED` (`2`) | Computes speed (`cm/s`, `km/h`), prints Serial telemetry, updates OLED, triggers alert, transitions to `STATE_COOLDOWN`. |
| `STATE_COOLDOWN` (`3`) | Waits for both IR sensors to clear (`SENSOR_CLEAR_TIMEOUT_MS = 2000 ms` warning guard), refreshes `READY` screen, clears `EIFR`, and re-arms `STATE_READY`. |

### 4.3 I2C Bus Diagnostics & Hang Protection

During `setup()`, `initOledDisplay()`:
1. Initializes `Wire` at `100 kHz` (`Wire.setClock(100000UL)`) for reliable breadboard signal integrity.
2. Enables `Wire.setWireTimeout(3000, true)` (when `WIRE_HAS_TIMEOUT` is defined) so loose `SDA`/`SCL` jumpers cannot hang `Wire.endTransmission()`.
3. Scans I2C addresses `1..126`, logs every detected device to Serial, and attempts `display.begin(SSD1306_SWITCHCAPVCC, addr)` using the discovered `0x3C` or `0x3D` address.

---

## 5. Track Setup & Calibration Guide

### Step 1: Physical Alignment
- Place **IR Sensor 1** and **IR Sensor 2** facing perpendicular across the toy car lane at the same height (aligned with the side body of the toy car, not gaps between wheels).
- Measure the exact distance from the center of Sensor 1's IR LED/photodiode pair to the center of Sensor 2's pair, and set `SENSOR_DISTANCE_CM` in [`roadiot_speed_detector.ino`](roadiot_speed_detector/roadiot_speed_detector.ino) to match.

### Step 2: IR Potentiometer Calibration
1. Power the Arduino Uno via USB and open **Serial Monitor (`9600` baud)**.
2. Ensure no object is in front of either sensor and check the boot printout:
   ```text
   IR1 (D2): CLEAR | IR2 (D3): CLEAR
   ```
3. If either reads `BLOCKED` while empty (or its module obstacle LED stays lit):
   - Slowly turn the blue potentiometer on that IR module until the obstacle LED just turns **OFF** with an empty track.
   - Move your toy car in front of the sensor at track distance (`2–5 cm`) and confirm the obstacle LED turns **ON** solidly.
4. Keep sensors shaded from direct sunlight or strong halogen lamps, which emit infrared noise.

### Step 3: Firmware Timing Parameters

| Constant | Default | Purpose | When to Adjust |
| :--- | :--- | :--- | :--- |
| `SENSOR_DISTANCE_CM` | `20.0` | Gate distance (`cm`) | Whenever you move the sensors closer/farther |
| `SPEED_LIMIT_CM_S` | `50.0` | Overspeed cutoff (`cm/s`) | Lower for slow tracks, raise for fast RC cars |
| `MIN_TRAVEL_US` | `15000UL` | Noise/bounce filter (`15 ms`) | Lower only if measuring speeds above `1333 cm/s` at `20 cm` |
| `TIMING_TIMEOUT_MS` | `3000UL` | Max transit window (`3.0 s`) | Increase if testing very slow crawlers (`< 6.7 cm/s`) |
| `RESULT_HOLD_MS` | `1500UL` | Display & alert duration (`1.5 s`) | Increase to read results longer, decrease for rapid runs |
| `IR_TRIGGER_STATE` | `LOW` | Sensor output polarity | Set to `HIGH` if sensor outputs `HIGH` on detection |
| `BUZZER_IS_PASSIVE` | `true` | Buzzer drive mode | Set to `false` for active 5V buzzers |

---

## 6. OLED Display States

1. **Boot Splash (`0.7 s`):** `ROAD-IOT` / `Speed Trap Booting`
2. **Ready Screen:** Header `ROAD-IOT SPEED TRAP`, large `READY`, bottom line `Dist: 20cm Lim: 50`
3. **Timing Screen:** `SENSOR 1 TRIGGERED` / `TIMING...`
4. **Result Screen:**
   - **Normal:** `SPEED NORMAL`, speed in `cm/s` (size 2), and `km/h` + `t=...s` below.
   - **Overspeed:** Inverted white banner `!! OVERSPEED !!`, speed in `cm/s`, and `km/h` + `t=...s` below.
5. **Timeout Screen:** `TIMEOUT` / `Resetting trap...`

---

## 7. Verification & Acceptance Checklist

- [ ] **Boot Diagnostics:** Serial Monitor (`9600` baud) prints `OLED initialized successfully at 0x3C` (or `0x3D`) and `IR1 (D2): CLEAR | IR2 (D3): CLEAR`.
- [ ] **OLED Check:** `READY` screen appears clearly on the GME12864-4 display.
- [ ] **Timeout Check:** Trigger Sensor 1 alone with your hand and wait `3 s`; OLED shows `TIMEOUT`, Serial prints `[TIMEOUT] Car did not reach Sensor 2 in time. Resetting.`, and trap returns to `READY`.
- [ ] **Normal Speed Check:** Roll the car slowly (`> 0.4 s` between sensors at `20 cm`); Green LED lights for `1.5 s`, buzzer stays silent, OLED shows `SPEED NORMAL`, and Serial prints `[NORMAL] Speed within limit.`.
- [ ] **Overspeed Check:** Push the car quickly (`< 0.4 s` between sensors at `20 cm`); Red LED flashes, Buzzer pulses, OLED shows `!! OVERSPEED !!`, and Serial prints `[OVERSPEED ALERT!] Red LED & Buzzer ON!`.

---

## 8. Future IoT Extensions

The Serial telemetry block in `loop()` can be extended without changing the interrupt timing logic:
- **Desktop Web Dashboard (USB Serial):** Read the `9600` baud Serial output with a Python (`pyserial`) or Node.js (`serialport`) script to log runs to CSV/SQLite and plot live speeds in a browser.
- **Wi-Fi Cloud Telemetry (ESP8266 / ESP32):** Forward speed and alert events over `SoftwareSerial` / UART to an ESP module to publish violations to MQTT, ThingSpeak, or a web dashboard.
