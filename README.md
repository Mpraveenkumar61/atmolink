# 🌡️ AtmoLink — ESP32 Weather Station

> A compact, WiFi-enabled ambient monitor for the ESP32 that reads a **DHT22** sensor, displays live data on a **SSD1306 OLED**, serves a **dark-themed web dashboard**, and cross-references readings against the **OpenWeatherMap (OWM)** API.

---

## 📸 Sample Output

### OLED Display (128×64)
```
   AtmoLink
-------------
T: 31.4 C
H: 72.3%
OWM ref:
T: 30.1 C
dT: +1.3 C
```

### Web Dashboard (auto-refreshes every 5 s)
```
AtmoLink
Eluru · auto-refresh 5s

DHT22 — live
┌─────────────┬─────────────┐
│   31.4°C    │   72.3%     │
│ Temperature │  Humidity   │
└─────────────┴─────────────┘

OpenWeatherMap — Eluru reference
┌─────────────┬─────────────┐
│   30.1°C    │   68.0%     │
│  OWM temp   │ OWM humidity│
└─────────────┴─────────────┘
overcast clouds
DHT22 vs OWM — ΔT = +1.3°C   ΔH = +4.3%
```

### Serial Monitor (ESP-IDF log)
```
I (3201) atmolink: IP: 192.168.1.42
I (8412) atmolink: DHT22: 31.4 C  72.3%
I (9053) atmolink: OWM: 30.1 C  68.0%  overcast clouds
I (9054) atmolink: Web server up on port 80
```

---

## ✨ Features

- **Live sensor reading** — DHT22 temperature & humidity polled every 3 s
- **SSD1306 OLED** — local display with sensor values and OWM delta over I²C
- **Web dashboard** — dark-themed, mobile-responsive, auto-refreshes at `http://<device-ip>/`
- **OWM integration** — fetches real-world reference data for Eluru (or any lat/lon) every 10 min
- **Delta comparison** — shows ΔT and ΔH between DHT22 and OWM at a glance
- **FreeRTOS multitasking** — DHT, OLED, OWM, and HTTP run as independent tasks with mutex-protected shared state
- **NVS-backed WiFi** — standard ESP-IDF WiFi station with retry logic

---

## 🧰 Hardware

| Component | Detail |
|-----------|--------|
| MCU | ESP32 (any DevKit) |
| Sensor | DHT22 (AM2302) on **GPIO 4** |
| Display | SSD1306 128×64 OLED — SDA **GPIO 21**, SCL **GPIO 22** |
| Power | 3.3 V / USB |

Wiring summary:
- DHT22 DATA → GPIO 4 (4.7 kΩ pull-up to 3.3 V recommended)
- OLED SDA → GPIO 21
- OLED SCL → GPIO 22
- OLED VCC → 3.3 V, GND → GND

---

## 🛠️ Software / Dependencies

| Dependency | Notes |
|-----------|-------|
| **ESP-IDF** | v5.x recommended |
| **dht22.h** | Your own proven DHT22 driver (place in `components/dht22/`) |
| **lwIP** | Bundled with ESP-IDF |
| **esp_http_client** | Bundled with ESP-IDF |
| **esp_http_server** | Bundled with ESP-IDF |
| **OpenWeatherMap API** | Free tier is sufficient (1 call / 10 min) |

---

## ⚙️ Configuration

Edit the `#define` block at the top of `main.c`:

```c
#define WIFI_SSID      "your_ssid"
#define WIFI_PASSWORD  "your_password"
#define OWM_API_KEY    "your_owm_api_key"
#define DHT_GPIO       GPIO_NUM_4

// Coordinates — defaults are set to Eluru, Andhra Pradesh
#define CITY_LAT  "16.7107"
#define CITY_LON  "81.0952"
```

Get a free OWM API key at [openweathermap.org/api](https://openweathermap.org/api).

---

## 🚀 Build & Flash

```bash
# Clone / place project
git clone https://github.com/your-username/atmolink.git
cd atmolink

# Set target and build
idf.py set-target esp32
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

Once running, open a browser to the IP shown in the serial log:
```
http://192.168.x.x/
```

---

## 🗂️ Project Structure

```
atmolink/
├── main/
│   └── main.c              # Full application source
├── components/
│   └── dht22/
│       ├── dht22.h         # DHT22 driver header
│       └── dht22.c         # DHT22 driver implementation
├── CMakeLists.txt
├── sdkconfig
└── README.md
```

---

## 🔄 Task Architecture

```
app_main
├── task_dht   (priority 5)  — reads DHT22 every 3 s
├── task_oled  (priority 4)  — updates display every 2 s
├── task_owm   (priority 3)  — fetches OWM every 10 min
└── HTTP server              — serves dashboard on port 80
         └── root_handler    — renders live HTML on GET /
```

All tasks share `g_sensor` and `g_owm` structs protected by a **FreeRTOS mutex** (`g_mutex`).

---

## 🔧 Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| `DHT22 err: ESP_ERR_TIMEOUT` | Missing pull-up / bad wiring | Add 4.7 kΩ pull-up; check GPIO |
| OLED blank | Wrong I²C address | Try `0x3D` instead of `0x3C` |
| OWM shows `N/A` | Invalid API key or no WiFi | Verify key and network |
| Web page not loading | Firewall / wrong IP | Check serial for assigned IP |
| Stack overflow crash | Task stack too small | Increase stack in `xTaskCreate` |

---

## 📄 License

MIT — free to use, modify, and distribute. Attribution appreciated.

---

## 🙏 Acknowledgements

- [OpenWeatherMap](https://openweathermap.org) for the free weather API
- [ESP-IDF](https://github.com/espressif/esp-idf) by Espressif Systems
- SSD1306 font bitmap adapted from Adafruit GFX library
