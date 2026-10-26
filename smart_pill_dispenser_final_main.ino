/*
  SMART PILL COMPANION - FINAL SKETCH (Arduino Mega)

  Hardware: 2 x I2C LCD, 4 touch pads, 3 IR sensors, 1 RGB LED (4 pin),
            1 DHT11, 3 x 360-degree (continuous rotation) servos.

  Design: 3 vending boxes, each holding ONE medicine in small packets.
          A servo turns the spring coil, one full turn pushes one packet out,
          it falls down the chute past that box's IR sensor into the tray.

  1) VENDING MODE (press OK on the home screen):
          turn the KNOB to pick the person's EXISTING PROBLEM (none / sugar / BP ...)
          -> OK -> scroll the illness list -> OK -> prescription shown on the LCDs
          -> OK again to confirm. If a medicine is flagged for that existing problem the
          machine refuses and says "Ask a doctor". Otherwise the needed boxes dispense
          one packet each -> take them from the tray -> OK.
          A medicine cannot be vended again too soon.
  2) SCHEDULED DOSES (reminders): at each dose time the boxes chosen for that dose
          dispense, then "TAKE MEDICINE" until OK, else a red missed-dose ALERT.

  Touch pads:  UP (D22)  DOWN (D23)  OK (D24)  BACK (D25 = settings menu)
  Knob:        potentiometer middle pin on A0 (outer pins to 5 V and GND)
  RGB light:   green = all good, blue = action needed, red = problem (3 colours only)
  LCD 1:       time, next dose, messages          LCD 2: temperature, humidity, counts

  Hold BACK while powering on for DEMO mode (a dose every 40 s, short timeouts).
  The clock is a software clock (no RTC): set it from the menu after every power-up.
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>          // "DHT sensor library" + "Adafruit Unified Sensor"
#include <Servo.h>
#include <EEPROM.h>

// ---------------- Pins ----------------
const uint8_t PIN_R = 5, PIN_G = 6, PIN_B = 7;
const uint8_t PIN_KEY[4] = {22, 23, 24, 25};     // UP, DOWN, OK, BACK
const uint8_t PIN_IR[3]  = {26, 27, 28};
const uint8_t PIN_DHT    = 29;
const uint8_t PIN_POT    = A0;      // scrolling knob
const uint8_t PIN_SERVO[3] = {8, 9, 10};

// ---------------- Settings you may need to change ----------------
const uint8_t LCD1_ADDR = 0x27;        // from the I2C scanner
const uint8_t LCD2_ADDR = 0x26;
const bool    COMMON_ANODE = false;    // true if the RGB LED's long leg goes to 5 V
const uint8_t TOUCH_ACTIVE = HIGH;     // flip if your touch pads read the other way
const uint8_t IR_ACTIVE    = LOW;      // flip if your IR modules read the other way
#define DHTTYPE DHT11

// 360-degree servo: 90 = stop. Use ~125 to turn one way, ~55 for the other.
const uint8_t SERVO_STOP = 90;
const uint8_t SERVO_RUN  = 125;
const unsigned long TURN_MS = 1200UL;       // time for ONE full coil turn (calibrate!)
const unsigned long DROP_WAIT_MS = 1500UL;  // extra wait for the packet to fall
const uint8_t MAX_TRIES = 2;

const bool UNLIMITED_PACKETS = true;   // true = never says "box empty" (no counting). false = count packets.
const uint8_t PACKETS_PER_BOX = 4;     // only used when UNLIMITED_PACKETS is false
const unsigned long TAKE_TIMEOUT_MS      = 600000UL;  // 10 min to take the dose
const unsigned long TAKE_TIMEOUT_DEMO_MS = 20000UL;
const unsigned long DEMO_GAP_MS          = 40000UL;
// What is stored in each box (edit to match what you really load). Keep texts short.
const char* MED_NAME[3]  = {"Paracetamol", "Cetirizine", "Antacid"};
const char* MED_SHORT[3] = {"Para", "Cetir", "Antac"};
const char* MED_HOW[3]   = {"1 tab, 6h apart", "1 tab at night", "Chew 1 post meal"};

// Illness list for vending mode. mask: bit0 = Box 1, bit1 = Box 2, bit2 = Box 3.
// EXAMPLE DATA ONLY: have a doctor or pharmacist approve the real list.
const uint8_t NUM_ILL = 5;
struct Ill { const char* name; uint8_t mask; const char* note; };
const Ill ILL[NUM_ILL] = {
  {"Fever",        1, "See doctor if >2d"},
  {"Headache",     1, "Drink water"},
  {"Cold & cough", 3, "Rest, warm fluids"},
  {"Acidity",      4, "Avoid spicy food"},
  {"Allergy",      2, "See doctor if worse"}
};
// Existing health problems the person can choose first. 'block' = boxes that need a
// doctor's OK for that problem (bit0 = Box 1, bit1 = Box 2, bit2 = Box 3).
// EXAMPLE DATA ONLY: a doctor or pharmacist must decide the real table. Add rows freely.
const uint8_t NUM_PROF = 4;
struct Prof { const char* name; uint8_t block; };
const Prof PROF[NUM_PROF] = {
  {"None",            0},
  {"Sugar (diabetes)", 4},
  {"High BP",         4},
  {"Sugar + High BP", 4}
};
const unsigned long VEND_COOLDOWN_MS      = 14400000UL;  // same medicine again only after 4 h
const unsigned long VEND_COOLDOWN_DEMO_MS = 20000UL;
const unsigned long VEND_MENU_MS   = 45000UL;            // illness list gives up after this
const unsigned long VEND_TRAY_MS   = 120000UL;           // time to collect from the tray
const int TEMP_MAX = 35;   // storage warning limits
const int HUM_MAX  = 75;

// ---------------- Objects / state ----------------
LiquidCrystal_I2C lcd1(LCD1_ADDR, 16, 2);
LiquidCrystal_I2C lcd2(LCD2_ADDR, 16, 2);
DHT dht(PIN_DHT, DHTTYPE);
Servo servo[3];

bool lcd1On = false, lcd2On = false, demo = false;

uint8_t clkH = 8, clkM = 0, clkS = 0;
uint8_t doseH[3] = {8, 13, 20};
uint8_t doseM[3] = {0, 0, 0};
uint8_t leftN[3] = {PACKETS_PER_BOX, PACKETS_PER_BOX, PACKETS_PER_BOX};
uint8_t doseMask[3] = {1, 2, 4};               // boxes dispensed at each dose time
unsigned long lastVend[3] = {0, 0, 0};
bool vendUsed[3] = {false, false, false};
uint8_t vendCount = 0;
bool fired[3] = {false, false, false};
bool due[3]   = {false, false, false};
uint8_t takenCount = 0, missedCount = 0;

int tempC = 0, hum = 0;
bool dhtOk = false;
unsigned long lastDht = 0, lastDemo = 0;
uint8_t demoIdx = 0;

enum Key { K_NONE = 0, K_UP, K_DOWN, K_OK, K_BACK };

// ---------------- Small helpers ----------------
bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void rgb(bool r, bool g, bool b) {
  digitalWrite(PIN_R, (r != COMMON_ANODE) ? HIGH : LOW);
  digitalWrite(PIN_G, (g != COMMON_ANODE) ? HIGH : LOW);
  digitalWrite(PIN_B, (b != COMMON_ANODE) ? HIGH : LOW);
}

void pr(LiquidCrystal_I2C &lcd, bool on, uint8_t row, const char *text) {
  if (!on) return;
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", text);
  lcd.setCursor(0, row);
  lcd.print(buf);
}
void p1(uint8_t row, const char *t) { pr(lcd1, lcd1On, row, t); }
void p2(uint8_t row, const char *t) { pr(lcd2, lcd2On, row, t); }

bool irActive(uint8_t k) { return digitalRead(PIN_IR[k]) == IR_ACTIVE; }

// ---------------- Saved settings ----------------
void saveAll() {
  EEPROM.update(0, 0xB4);
  for (uint8_t i = 0; i < 3; i++) {
    EEPROM.update(1 + i, doseH[i]);
    EEPROM.update(4 + i, doseM[i]);
    EEPROM.update(7 + i, leftN[i]);
    EEPROM.update(10 + i, doseMask[i]);
  }
}
void loadAll() {
  if (EEPROM.read(0) != 0xB4) { saveAll(); return; }
  for (uint8_t i = 0; i < 3; i++) {
    doseH[i] = EEPROM.read(1 + i);
    doseM[i] = EEPROM.read(4 + i);
    leftN[i] = EEPROM.read(7 + i);
    doseMask[i] = EEPROM.read(10 + i);
    if (doseMask[i] == 0 || doseMask[i] > 7) doseMask[i] = (uint8_t)(1 << i);
    if (doseH[i] > 23 || doseM[i] > 59) { doseH[i] = 8; doseM[i] = 0; }
    if (leftN[i] > PACKETS_PER_BOX) leftN[i] = PACKETS_PER_BOX;
  }
}

// ---------------- Touch keys with debounce and auto-repeat on UP/DOWN ----------------
uint8_t readKey() {
  static bool prev[4] = {false, false, false, false};
  static unsigned long t0[4] = {0, 0, 0, 0}, tr[4] = {0, 0, 0, 0}, lastEv = 0;
  unsigned long ms = millis();
  uint8_t ev = K_NONE;
  for (uint8_t i = 0; i < 4; i++) {
    bool now = (digitalRead(PIN_KEY[i]) == TOUCH_ACTIVE);
    if (now && !prev[i]) { t0[i] = tr[i] = ms; if (!ev) ev = i + 1; }
    else if (now && prev[i] && i < 2 && ms - t0[i] > 400UL && ms - tr[i] > 120UL) { tr[i] = ms; if (!ev) ev = i + 1; }
    prev[i] = now;
  }
  if (ev && ms - lastEv < 60UL) ev = K_NONE;
  if (ev) lastEv = ms;
  return ev;
}

// ---------------- Clock, sensors, dose scheduling ----------------
void service() {
  static unsigned long lastTick = 0;
  if (lastTick == 0) lastTick = millis();
  while (millis() - lastTick >= 1000UL) {
    lastTick += 1000UL;
    if (++clkS >= 60) { clkS = 0; if (++clkM >= 60) { clkM = 0; if (++clkH >= 24) { clkH = 0; for (uint8_t i = 0; i < 3; i++) fired[i] = false; } } }
  }

  if (millis() - lastDht > 2000UL) {
    lastDht = millis();
    float t = dht.readTemperature(), h = dht.readHumidity();
    dhtOk = !(isnan(t) || isnan(h));
    if (dhtOk) { tempC = (int)(t + 0.5); hum = (int)(h + 0.5); }
  }

  if (demo) {
    if (millis() - lastDemo >= DEMO_GAP_MS) { lastDemo = millis(); due[demoIdx] = true; demoIdx = (demoIdx + 1) % 3; }
  } else {
    for (uint8_t i = 0; i < 3; i++)
      if (!fired[i] && clkH == doseH[i] && clkM == doseM[i]) { fired[i] = true; due[i] = true; }
  }
}

// ---------------- Dispensing (one coil turn, IR checks the drop) ----------------
bool dispenseBox(uint8_t k) {
  if (irActive(k)) return false;                       // beam already blocked: do not dispense
  for (uint8_t t = 0; t < MAX_TRIES; t++) {
    bool seen = false;
    servo[k].attach(PIN_SERVO[k]);
    servo[k].write(SERVO_RUN);
    unsigned long t0 = millis();
    while (millis() - t0 < TURN_MS) { if (irActive(k)) seen = true; delay(5); }
    servo[k].write(SERVO_STOP);
    delay(60);
    servo[k].detach();
    t0 = millis();
    while (!seen && millis() - t0 < DROP_WAIT_MS) { if (irActive(k)) seen = true; delay(5); }
    if (seen) return true;
    p1(1, "No drop, retry");
  }
  return false;
}

// ---------------- Knob (potentiometer) ----------------
uint8_t potIndex(uint8_t n) {                       // 0 .. n-1 with a little hysteresis
  static long last = 0;
  long raw = 0;
  for (uint8_t i = 0; i < 4; i++) raw += analogRead(PIN_POT);
  raw /= 4;
  long v = raw * n;
  if (last >= n) last = n - 1;
  if (v < last * 1024L - 90) last = v / 1024L;
  else if (v >= (last + 1) * 1024L + 90) last = v / 1024L;
  if (last >= n) last = n - 1;
  return (uint8_t)last;
}

bool anyDue() { return due[0] || due[1] || due[2]; }

void maskText(uint8_t mask, char *out) {            // "Para+Cetir"
  out[0] = 0;
  for (uint8_t b = 0; b < 3; b++) {
    if (!(mask & (1 << b))) continue;
    if (out[0]) strcat(out, "+");
    strcat(out, MED_SHORT[b]);
  }
}

// ---------------- Alerts ----------------
void alertLoop(const char *a, const char *b) {
  bool last = false;
  p1(0, a); p1(1, "Press OK");
  p2(0, b); p2(1, "");
  while (true) {
    service();
    bool on = (millis() / 400UL) % 2;
    if (on != last) {
      last = on;
      rgb(on, 0, 0);
      if (lcd1On) { if (on) lcd1.backlight(); else lcd1.noBacklight(); }
    }
    if (readKey() == K_OK) break;
  }
  if (lcd1On) lcd1.backlight();
  rgb(0, 0, 0);
}

// 0 = ok, 1 = a box is empty, 2 = an IR beam is blocked. 'bad' = which box.
uint8_t checkMask(uint8_t mask, uint8_t &bad) {
  for (uint8_t b = 0; b < 3; b++) {
    if (!(mask & (1 << b))) continue;
    bad = b;
    if (!UNLIMITED_PACKETS && leftN[b] == 0) return 1;
    if (irActive(b)) return 2;
  }
  return 0;
}

// Dispense one packet from every box in the mask. Returns false (and the box) on failure.
bool dispenseMask(uint8_t mask, uint8_t &failed) {
  char b[17];
  for (uint8_t k = 0; k < 3; k++) {
    if (!(mask & (1 << k))) continue;
    snprintf(b, sizeof(b), "Box %u: %s", k + 1, MED_SHORT[k]);
    rgb(0, 0, 1);
    p1(0, b); p1(1, "Dispensing...");
    p2(0, MED_NAME[k]); p2(1, MED_HOW[k]);
    if (!dispenseBox(k)) { failed = k; return false; }
    if (!UNLIMITED_PACKETS) { leftN[k]--; saveAll(); }
    p1(1, "Dropped OK");
    delay(600);
  }
  return true;
}

// Wait for the person to collect the packets and press OK. Returns true if OK pressed.
bool waitTake(unsigned long lim, const char *l2a) {
  char b[17];
  p1(0, "TAKE MEDICINE"); p1(1, "Press OK");
  p2(0, l2a);
  unsigned long t0 = millis(), lastShow = 0;
  while (millis() - t0 < lim) {
    service();
    rgb(0, 0, (millis() / 500UL) % 2);
    if (millis() - lastShow > 1000UL) {
      lastShow = millis();
      unsigned long s = (lim - (millis() - t0)) / 1000UL;
      snprintf(b, sizeof(b), "Wait %lum%02lus", s / 60UL, s % 60UL);
      p2(1, b);
    }
    if (readKey() == K_OK) return true;
  }
  return false;
}

void runDose(uint8_t k) {
  char b[17], why[17];
  uint8_t bad = 0, failed = 0;
  snprintf(b, sizeof(b), "Dose %u  %02u:%02u", k + 1, doseH[k], doseM[k]);
  rgb(0, 0, 1);
  p1(0, b); p1(1, "Dispensing...");
  p2(0, "Please wait"); p2(1, "");

  uint8_t chk = checkMask(doseMask[k], bad);
  if (chk) {
    missedCount++;
    snprintf(why, sizeof(why), chk == 1 ? "Box %u empty" : "IR %u blocked", bad + 1);
    alertLoop(chk == 1 ? "BOX EMPTY" : "IR BLOCKED", why);
    return;
  }
  if (!dispenseMask(doseMask[k], failed)) {
    missedCount++;
    snprintf(why, sizeof(why), "Box %u no drop", failed + 1);
    alertLoop("DISPENSE FAILED", why);
    return;
  }
  maskText(doseMask[k], why);
  if (waitTake(demo ? TAKE_TIMEOUT_DEMO_MS : TAKE_TIMEOUT_MS, why)) {
    takenCount++;
    rgb(0, 1, 0);
    p1(0, "Well done!"); p1(1, "Dose taken");
    p2(0, ""); p2(1, "");
    delay(2000);
    rgb(0, 0, 0);
    return;
  }
  missedCount++;
  alertLoop("MISSED DOSE", "Family alert");
}

// ---------------- Vending mode: choose the illness with the knob ----------------
void vend() {
  char a[17], b[17];
  uint8_t sel = 0, lastSel = 255, prof = 0;
  bool chosen = false;
  unsigned long t0 = millis();
  rgb(0, 0, 1);

  // step 1: existing health problem (knob + OK)
  p1(0, "Health problem?"); p2(0, "Turn knob, OK"); p2(1, "BACK = cancel");
  while (millis() - t0 < VEND_MENU_MS) {
    service();
    if (anyDue()) return;
    uint8_t pi = potIndex(NUM_PROF);
    if (pi != lastSel) { lastSel = pi; t0 = millis(); p1(1, PROF[pi].name); }
    uint8_t k = readKey();
    if (k == K_BACK) return;
    if (k == K_OK) { prof = pi; chosen = true; break; }
  }
  if (!chosen) return;
  chosen = false; lastSel = 255; t0 = millis();

  // step 2: which illness (knob + OK)
  while (millis() - t0 < VEND_MENU_MS) {
    service();
    if (anyDue()) return;                           // a scheduled dose goes first
    sel = potIndex(NUM_ILL);
    if (sel != lastSel) {
      lastSel = sel; t0 = millis();
      snprintf(a, sizeof(a), "Illness %u/%u", sel + 1, NUM_ILL);
      p1(0, a); p1(1, ILL[sel].name);
      maskText(ILL[sel].mask, b);
      bool blk = (ILL[sel].mask & PROF[prof].block) != 0;
      p2(0, b); p2(1, blk ? "Ask doctor!" : ILL[sel].note);
      rgb(blk, 0, !blk);                              // red = not allowed for this person
    }
    uint8_t k = readKey();
    if (k == K_BACK) return;
    if (k == K_OK) { chosen = true; break; }
  }
  if (!chosen) return;

  // confirm screen
  maskText(ILL[sel].mask, b);
  p1(0, ILL[sel].name); p1(1, "OK=get BACK=no");
  p2(0, "Gives:"); p2(1, b);
  bool go = false;
  t0 = millis();
  while (millis() - t0 < 20000UL) {
    service();
    if (anyDue()) return;
    uint8_t k = readKey();
    if (k == K_BACK) return;
    if (k == K_OK) { go = true; break; }
  }
  if (!go) return;

  uint8_t mask = ILL[sel].mask, bad = 0, failed = 0;

  // flagged for this person's existing problem? refuse the whole prescription
  uint8_t blocked = mask & PROF[prof].block;
  if (blocked) {
    maskText(blocked, b);
    rgb(1, 0, 0);
    p1(0, "NOT ALLOWED"); p1(1, b);
    p2(0, PROF[prof].name); p2(1, "Ask a doctor");
    delay(3500);
    rgb(0, 0, 0);
    return;
  }

  // same medicine too soon?
  unsigned long cd = demo ? VEND_COOLDOWN_DEMO_MS : VEND_COOLDOWN_MS;
  for (uint8_t k = 0; k < 3; k++) {
    if ((mask & (1 << k)) && vendUsed[k] && millis() - lastVend[k] < cd) {
      unsigned long left = cd - (millis() - lastVend[k]);
      if (left >= 60000UL) snprintf(b, sizeof(b), "Wait %luh%02lum", left / 3600000UL, (left / 60000UL) % 60UL);
      else                 snprintf(b, sizeof(b), "Wait %lu s", left / 1000UL);
      snprintf(a, sizeof(a), "%s taken", MED_SHORT[k]);
      rgb(1, 0, 0);
      p1(0, a); p1(1, b);
      p2(0, "Too soon"); p2(1, "Ask a doctor");
      delay(3000);
      return;
    }
  }

  uint8_t chk = checkMask(mask, bad);
  if (chk) {
    snprintf(b, sizeof(b), chk == 1 ? "Box %u empty" : "IR %u blocked", bad + 1);
    rgb(1, 0, 0);
    p1(0, chk == 1 ? "BOX EMPTY" : "IR BLOCKED"); p1(1, b);
    p2(0, "Ask caregiver"); p2(1, "");
    delay(3000);
    return;
  }

  if (!dispenseMask(mask, failed)) {
    snprintf(b, sizeof(b), "Box %u no drop", failed + 1);
    alertLoop("DISPENSE FAILED", b);
    return;
  }
  for (uint8_t k = 0; k < 3; k++) if (mask & (1 << k)) { vendUsed[k] = true; lastVend[k] = millis(); }
  vendCount++;

  maskText(mask, b);
  waitTake(VEND_TRAY_MS, b);
  rgb(0, 1, 0);
  p1(0, "Get well soon!"); p1(1, ILL[sel].name);
  p2(0, ILL[sel].note); p2(1, "");
  delay(2500);
  rgb(0, 0, 0);
}

// ---------------- Idle screens ----------------
void nextDose(uint8_t &idx, unsigned int &mins) {
  unsigned int nowM = clkH * 60U + clkM, best = 24U * 60U + 1U;
  idx = 0;
  for (uint8_t i = 0; i < 3; i++) {
    unsigned int d = doseH[i] * 60U + doseM[i];
    unsigned int diff = (d >= nowM) ? (d - nowM) : (d + 24U * 60U - nowM);
    if (diff < best) { best = diff; idx = i; }
  }
  mins = best;
}

void drawIdle() {
  static unsigned long last = 0;
  if (millis() - last < 400UL) return;
  last = millis();
  char a[17], b[17];
  uint8_t idx; unsigned int mins;
  nextDose(idx, mins);

  snprintf(a, sizeof(a), "%02u:%02u:%02u Nxt B%u", clkH, clkM, clkS, idx + 1);
  p1(0, a);

  bool warn = dhtOk && (tempC > TEMP_MAX || hum > HUM_MAX);
  if (dhtOk) snprintf(b, sizeof(b), "%dC %d%%RH %s", tempC, hum, warn ? (tempC > TEMP_MAX ? "HOT!" : "WET!") : "ok");
  else       snprintf(b, sizeof(b), "Sensor: no data");
  char c[17];
  if (UNLIMITED_PACKETS) snprintf(c, sizeof(c), "Unlimited T%u M%u", takenCount, missedCount);
  else                   snprintf(c, sizeof(c), "L%u/%u/%u T%u M%u", leftN[0], leftN[1], leftN[2], takenCount, missedCount);

  if (lcd2On) {
    p2(0, b); p2(1, c);
    if ((millis() / 3000UL) % 2) p1(1, "OK: get medicine");
    else { snprintf(a, sizeof(a), "Next %02u:%02u", doseH[idx], doseM[idx]); p1(1, a); }
  } else {                                           // only one display: rotate the lower line
    uint8_t ph = (millis() / 3000UL) % 3;
    if (ph == 0)      p1(1, "OK: get medicine");
    else if (ph == 1) { snprintf(a, sizeof(a), "Next %02u:%02u", doseH[idx], doseM[idx]); p1(1, a); }
    else              p1(1, b);
  }
  if (warn) rgb(1, 0, 0); else rgb(0, 1, 0);
}

// ---------------- Menu ----------------
bool editTime(const char *title, uint8_t &h, uint8_t &m) {
  uint8_t th = h, tm = m, field = 0;
  char b[17];
  p1(0, title);
  p2(0, "UP/DOWN change"); p2(1, "OK next BACK exit");
  rgb(0, 0, 1);
  while (true) {
    service();
    snprintf(b, sizeof(b), field == 0 ? ">%02u< : %02u" : " %02u : >%02u<", th, tm);
    p1(1, b);
    uint8_t k = readKey();
    if (k == K_UP)   { if (field == 0) th = (th + 1) % 24; else tm = (tm + 1) % 60; }
    if (k == K_DOWN) { if (field == 0) th = (th + 23) % 24; else tm = (tm + 59) % 60; }
    if (k == K_BACK) return false;
    if (k == K_OK) { if (field == 0) field = 1; else { h = th; m = tm; return true; } }
  }
}

bool editMask(const char *title, uint8_t &mask) {
  uint8_t m = mask;
  char b[17];
  p1(0, title);
  p2(0, "UP/DOWN change"); p2(1, "OK save BACK exit");
  rgb(0, 0, 1);
  while (true) {
    service();
    snprintf(b, sizeof(b), "Boxes: %c %c %c", (m & 1) ? '1' : '-', (m & 2) ? '2' : '-', (m & 4) ? '3' : '-');
    p1(1, b);
    uint8_t k = readKey();
    if (k == K_UP)   m = (m >= 7) ? 1 : m + 1;
    if (k == K_DOWN) m = (m <= 1) ? 7 : m - 1;
    if (k == K_BACK) return false;
    if (k == K_OK) { mask = m; return true; }
  }
}

void testBox(uint8_t k) {
  char b[17];
  snprintf(b, sizeof(b), "Test box %u", k + 1);
  p1(0, b); p1(1, "Running...");
  p2(0, ""); p2(1, "");
  if (!UNLIMITED_PACKETS && leftN[k] == 0) { p1(1, "Box empty"); delay(1500); return; }
  if (dispenseBox(k)) { if (!UNLIMITED_PACKETS) { leftN[k]--; saveAll(); } p1(1, "Drop detected"); rgb(0, 1, 0); }
  else { p1(1, "No drop seen"); rgb(1, 0, 0); }
  delay(1800);
}

void menu() {
  const uint8_t NI = 12;
  const char *items[12] = {"Set clock", "Dose 1 time", "Dose 2 time", "Dose 3 time",
                           "Dose 1 boxes", "Dose 2 boxes", "Dose 3 boxes",
                           "Test box 1", "Test box 2", "Test box 3", "Refill boxes", "Exit"};
  uint8_t sel = 0;
  rgb(0, 0, 1);
  p1(0, "MENU  UP/DOWN/OK");
  p2(0, "BACK = exit"); p2(1, "");
  while (true) {
    service();
    p1(1, items[sel]);
    uint8_t k = readKey();
    if (k == K_UP)   sel = (sel + NI - 1) % NI;
    if (k == K_DOWN) sel = (sel + 1) % NI;
    if (k == K_BACK) return;
    if (k == K_OK) {
      if (sel == 0) { uint8_t h = clkH, m = clkM; if (editTime("Set clock", h, m)) { clkH = h; clkM = m; clkS = 0; for (uint8_t i = 0; i < 3; i++) fired[i] = false; } }
      else if (sel <= 3) { uint8_t i = sel - 1; char t[17]; snprintf(t, sizeof(t), "Dose %u time", i + 1);
                           if (editTime(t, doseH[i], doseM[i])) saveAll(); }
      else if (sel <= 6) { uint8_t i = sel - 4; char t[17]; snprintf(t, sizeof(t), "Dose %u boxes", i + 1);
                           if (editMask(t, doseMask[i])) saveAll(); }
      else if (sel <= 9) testBox(sel - 7);
      else if (sel == 10) { for (uint8_t i = 0; i < 3; i++) leftN[i] = PACKETS_PER_BOX; saveAll(); p1(1, "Boxes refilled"); delay(1200); }
      else return;
      rgb(0, 0, 1);
      p1(0, "MENU  UP/DOWN/OK");
      p2(0, "BACK = exit"); p2(1, "");
    }
  }
}

// ---------------- Setup / loop ----------------
void setup() {
  Serial.begin(9600);
  Wire.begin();
  pinMode(PIN_R, OUTPUT); pinMode(PIN_G, OUTPUT); pinMode(PIN_B, OUTPUT);
  for (uint8_t i = 0; i < 4; i++) pinMode(PIN_KEY[i], INPUT);
  for (uint8_t i = 0; i < 3; i++) pinMode(PIN_IR[i], INPUT);
  for (uint8_t i = 0; i < 3; i++) { servo[i].attach(PIN_SERVO[i]); servo[i].write(SERVO_STOP); delay(300); servo[i].detach(); }
  dht.begin();
  loadAll();

  lcd1On = i2cPresent(LCD1_ADDR);
  lcd2On = i2cPresent(LCD2_ADDR);
  if (lcd1On) { lcd1.init(); lcd1.backlight(); }
  if (lcd2On) { lcd2.init(); lcd2.backlight(); }
  Serial.print(F("LCD1 ")); Serial.println(lcd1On ? F("found") : F("MISSING"));
  Serial.print(F("LCD2 ")); Serial.println(lcd2On ? F("found") : F("MISSING (using LCD1 only)"));

  rgb(1, 0, 0); delay(250); rgb(0, 1, 0); delay(250); rgb(0, 0, 1); delay(250); rgb(0, 0, 0);
  p1(0, "Smart Pill Companion"); p1(1, "Starting...");
  p2(0, "Hello"); p2(1, "Pill dispenser");
  delay(900);

  if (digitalRead(PIN_KEY[3]) == TOUCH_ACTIVE) {
    demo = true;
    p1(0, "DEMO MODE"); p1(1, "dose every 40 s");
    delay(1500);
    lastDemo = millis();
  }
  uint8_t h = clkH, m = clkM;
  if (editTime("Set the time", h, m)) { clkH = h; clkM = m; clkS = 0; }
  lastDht = 0;
}

void loop() {
  service();
  for (uint8_t i = 0; i < 3; i++) {
    if (due[i]) { due[i] = false; runDose(i); return; }
  }
  drawIdle();
  uint8_t k = readKey();
  if (k == K_OK) vend();
  else if (k == K_BACK) menu();
}