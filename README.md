Smart Pill Companion
An Arduino Mega–based medicine-dispensing prototype designed to help people follow scheduled doses through timed dispensing, on-screen reminders, sensor feedback, and audible/visual alerts.
> **Project status:** Working prototype / ongoing development. Features described here are based on the currently shared Arduino sketch and prototype photos. The project has not been validated for clinical use.
The problem
People managing multiple medicines may forget a dose or be unsure whether a dose was dispensed. A simple alarm can remind someone, but it cannot confirm that a medicine packet reached the collection area or flag every dispensing failure.
What this prototype does
Uses an Arduino Mega as the main controller.
Shows menus, time, prompts, and status information on two I²C LCDs.
Provides menu navigation using touch inputs and a potentiometer knob.
Schedules up to three daily dose times using a software clock.
Controls three continuous-rotation servos to operate the dispensing mechanism.
Uses three IR sensors to check for a packet/drop during dispensing.
Reads temperature and humidity using a DHT11 sensor.
Uses an RGB status LED, buzzer, and LCD messages for feedback and alerts.
Saves dose-time and related settings in Arduino EEPROM.
Includes a demo mode for faster demonstrations.
Includes a menu-based dispensing mode with example medicine/illness data in the sketch.
Prototype photos
LCDs, sensors, and controls
![Prototype showing LCDs and IR sensor modules](assets/prototype-lcd-and-sensors-1.jpeg)
![Prototype showing the LCD menu and sensor modules](assets/prototype-lcd-and-sensors-2.jpeg)
Overall setup
![Overall Smart Pill Companion electronics prototype](assets/prototype-overview.jpeg)
How it works
The user sets the software clock and dose schedule through the on-device menu.
At a scheduled time, the controller starts the configured dispensing sequence.
A servo operates the relevant dispensing mechanism.
An IR sensor checks whether a packet/drop is detected.
The LCD, RGB LED, and buzzer communicate status; a missed dose or dispensing issue can trigger an alert.
Temperature and humidity readings are displayed when the DHT11 provides valid readings.
The IR sensor only indicates that an object was detected at the sensor. It does not prove that a person picked up, swallowed, or safely took a medicine.
Hardware visible in the prototype / referenced by the sketch
Arduino Mega
Two 16×2 I²C LCD modules
Three IR sensor modules
DHT11 temperature and humidity sensor
Three continuous-rotation servo motors
Four touch inputs
Potentiometer
RGB LED
Buzzer and supporting wiring/breadboard components
This repository intentionally does not include a separate wiring diagram or a dedicated pinout document. The sketch contains its own configuration comments.
Software dependencies
Install these libraries through the Arduino IDE Library Manager, if they are not already installed:
`LiquidCrystal_I2C`
`DHT sensor library` by Adafruit
`Adafruit Unified Sensor`
`Servo` (normally included with the Arduino AVR board package)
`Wire` and `EEPROM` (included with the Arduino AVR core)
Getting started
Install the Arduino IDE and the Arduino AVR board package.
Open `src/smart_pill_dispenser_final_main.ino`.
Install the libraries listed above.
Select Arduino Mega 2560 and the correct connected port.
Review the configuration constants and LCD addresses near the top of the sketch to match your hardware.
Upload the sketch and test the device with dummy packets only before any real-world use.
The project uses a software clock rather than a real-time clock (RTC). Set the clock after power-up. Demo mode can be started by holding the BACK input while powering on, as described in the sketch comments.
Safety and limitations
This is an educational robotics prototype, not a medical device and not a substitute for advice from a qualified doctor or pharmacist.
The medicine names, dose instructions, illness mappings, and health-condition rules in the source are example data only. Do not use them to choose, prescribe, or administer medication.
Do not load real medicines or rely on this prototype for actual treatment. Test using dummy packets only.
The software clock can drift and must be reset after power-up; an RTC and backup power are potential future improvements.
IR sensing checks for a detected drop/object, not ingestion or adherence.
Servo timing and dispensing reliability depend on the physical mechanism and require calibration and systematic testing.
The source includes planned test criteria, but passing those tests should only be claimed after results have actually been recorded.
The project may need additional electrical protection, enclosure design, and fail-safe testing before any use beyond a demonstration.
Future improvements
Add an RTC module and battery backup.
Record and publish repeatable dispensing, sensor, and power-stability test results.
Improve empty-box detection and fault recovery.
Add a more reliable method to confirm collection, while being clear that collection still does not confirm swallowing.
Add caregiver notifications using an appropriate communications module.
Review all medicine-related logic with qualified medical professionals before any real-world deployment.
Repository structure
```text
smart-pill-companion/
├── README.md
├── LICENSE
├── .gitignore
├── src/
│   └── smart_pill_dispenser_final_main.ino
├── assets/
│   ├── prototype-lcd-and-sensors-1.jpeg
│   ├── prototype-lcd-and-sensors-2.jpeg
│   └── prototype-overview.jpeg
└── docs/
    └── smart-pill-companion-project-presentation.pptx
```
License
This project is released under the MIT License. See `LICENSE` for details.
---
Built as a robotics and embedded-systems project for the Smart Pill Companion concept.
