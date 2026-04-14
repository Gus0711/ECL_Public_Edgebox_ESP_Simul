# Production Integration Plan — EdgeBox-ESP-100

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Transform the Wokwi prototype into production-ready firmware with MQTT, NTP, RTC, and OTA — while keeping the Wokwi simulation fully functional via `#ifdef`.

**Architecture:** Dual build environments in `platformio.ini`: `env:wokwi` (current simulation, WiFi for MQTT demo) and `env:edgebox` (production, 4G via A7670G). All new features (MQTT, NTP, RTC, OTA) are compiled conditionally. On Wokwi, WiFi is used as transport since Wokwi doesn't simulate 4G. Business logic (`calculerEtats`, `gererBoutons`, planning, astronomical calc) is untouched — shared by both environments.

**Tech Stack:** PlatformIO, Arduino framework, ESP32-S3, PubSubClient (MQTT), ArduinoJson, WiFi (Wokwi) / TinyGSM+A7670G (production), ESP32 Preferences (NVS), PCF8563 RTC library.

**Constraints:**
- All testing on Wokwi (no physical EdgeBox available)
- 4G only in production (no WiFi on EdgeBox)
- Public MQTT broker for demo: `broker.hivemq.com:1883`
- Production broker: Mosquitto on OVH server (configured later)
- Single `src/main.cpp` file — follow existing monolithic pattern

---

## Task 0: Dual Build Environments + #ifdef Structure

**Files:**
- Modify: `platformio.ini`
- Modify: `src/main.cpp` (top section, lines 1-50)

This is the foundation. After this task, both `pio run -e wokwi` and `pio run -e edgebox` compile, and the existing Wokwi behavior is 100% preserved.

- [ ] **Step 0.1: Create dual environments in platformio.ini**

Replace the entire `platformio.ini` with:

```ini
; PlatformIO Project Configuration File
; Two environments: wokwi (simulation) and edgebox (production)

[env]
platform = espressif32
framework = arduino
monitor_speed = 115200
lib_deps =
    marcoschwartz/LiquidCrystal_I2C@^1.1.4

[env:wokwi]
board = esp32-s3-devkitc-1
build_flags =
    -DWOKWI
    -DWIFI_SSID=\"Wokwi-GUEST\"
    -DWIFI_PASS=\"\"

[env:edgebox]
board = esp32-s3-devkitc-1
build_flags =
    -DEDGEBOX
```

- [ ] **Step 0.2: Add #ifdef guards to simulation-only code in main.cpp**

Wrap the simulated clock variables (lines 150-159) and potentiometer logic. At the top of `main.cpp`, after the includes, add the platform detection comment. Wrap these sections:

1. The simulated date/time variables block — wrap with `#ifdef WOKWI`:
```cpp
#ifdef WOKWI
// ════════════════════════════════════════════════════════════
//  DATE/HEURE SIMULEE (Wokwi)
// ════════════════════════════════════════════════════════════
int annee  = 2025;
int mois   = 12;
int jour   = 15;
int heure  = 15;
int minute = 30;
int seconde = 0;
int jourSemaine = 1;
#else
// ════════════════════════════════════════════════════════════
//  DATE/HEURE REELLE (production)
// ════════════════════════════════════════════════════════════
int annee, mois, jour, heure, minute, seconde;
int jourSemaine;
#endif
```

2. The `PIN_POT` and `vitesseTemps` — only on Wokwi:
```cpp
#ifdef WOKWI
const int PIN_POT = 4;  // AI0 — potentiometre acceleration temps
float vitesseTemps = 1.0;
#else
const float vitesseTemps = 1.0;  // pas d'acceleration en production
#endif
```

3. The `PIN_BTN_PAGE` GPIO differs:
```cpp
#ifdef WOKWI
const int PIN_BTN_PAGE = 8;   // DI3 Wokwi
#else
const int PIN_BTN_PAGE = 7;   // DI3 EdgeBox production
#endif
```

4. In `loop()`, wrap the potentiometer reading:
```cpp
#ifdef WOKWI
  // 1. VITESSE DU TEMPS
  vitesseTemps = 1.0 + (analogRead(PIN_POT) / 4095.0) * 499.0;
#endif
```

5. The `avancerTemps()` function is Wokwi-only (production uses real clock):
```cpp
#ifdef WOKWI
void avancerTemps(float dtReel) {
  // ... existing code unchanged ...
}
#endif
```

6. In `loop()`, wrap the time advancement:
```cpp
#ifdef WOKWI
  // 2. AVANCER LE TEMPS
  if (now - dernierTick >= 100) {
    float dt = (now - dernierTick) / 1000.0;
    dernierTick = now;
    avancerTemps(dt);
  }
#endif
```

- [ ] **Step 0.3: Verify Wokwi build still compiles**

Run: `pio run -e wokwi`
Expected: SUCCESS, identical behavior to before.

- [ ] **Step 0.4: Verify edgebox build compiles**

Run: `pio run -e edgebox`
Expected: SUCCESS (no MQTT/NTP yet, just the ifdef skeleton).

- [ ] **Step 0.5: Test on Wokwi**

Launch Wokwi simulation. Verify:
- LCD displays date/time and advances
- Potentiometer accelerates time
- Force buttons toggle lines
- Page button cycles LCD pages
- Serial outputs JSON status

- [ ] **Step 0.6: Commit**

```bash
git add platformio.ini src/main.cpp
git commit -m "feat: dual build environments wokwi/edgebox with ifdef guards"
```

---

## Task 1: WiFi Connection (Wokwi) + Connection Abstraction

**Files:**
- Modify: `src/main.cpp`

Wokwi supports WiFi natively. We add WiFi connection for the Wokwi environment so that MQTT and NTP can work in simulation. In production, the A7670G 4G module will be integrated later (requires real hardware to test). For now, `#ifdef EDGEBOX` sections are stubs.

- [ ] **Step 1.1: Add WiFi includes and connection function**

After the existing `#include` block, add:

```cpp
#ifdef WOKWI
#include <WiFi.h>
#endif
```

Add a global variable after the existing globals:

```cpp
bool connecte = false;  // true si connecte au reseau (WiFi ou 4G)
```

Add the WiFi connection function (before `setup()`):

```cpp
#ifdef WOKWI
void connecterWiFi() {
  Serial.print("[WIFI] Connexion a ");
  Serial.print(WIFI_SSID);
  Serial.print("...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int tentatives = 0;
  while (WiFi.status() != WL_CONNECTED && tentatives < 20) {
    delay(500);
    Serial.print(".");
    tentatives++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    connecte = true;
    Serial.print(" OK! IP=");
    Serial.println(WiFi.localIP());
  } else {
    connecte = false;
    Serial.println(" ECHEC");
  }
}
#endif
```

- [ ] **Step 1.2: Call WiFi connection in setup()**

In `setup()`, after the splash screen and before the GPIO init, add:

```cpp
#ifdef WOKWI
  connecterWiFi();
#endif
```

- [ ] **Step 1.3: Add WiFi status to LCD system page**

In `afficherPage2()`, after the DST line, replace the speed display to also show connection:

```cpp
  lcd.setCursor(0, 3);
  bool dst = estHeureDEte(jour, mois, annee);
  lcd.print(dst ? "UTC+2 ETE " : "UTC+1 HIV ");
#ifdef WOKWI
  lcd.print("x");
  lcd.print((int)vitesseTemps);
#endif
  lcd.print(connecte ? " NET" : " ---");
  lcd.print("      ");
```

- [ ] **Step 1.4: Build and test**

Run: `pio run -e wokwi`
Expected: SUCCESS. On Wokwi, Serial shows `[WIFI] Connexion a Wokwi-GUEST... OK! IP=...`

- [ ] **Step 1.5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: WiFi connection for Wokwi simulation"
```

---

## Task 2: MQTT Real — PubSubClient + Publish Status

**Files:**
- Modify: `platformio.ini` (add lib_deps)
- Modify: `src/main.cpp`

Replace the Serial JSON stub with real MQTT publish via PubSubClient. Subscribe to `cmd` and `config` topics. Uses ArduinoJson to build payloads properly.

- [ ] **Step 2.1: Add MQTT and JSON libraries to platformio.ini**

Add to the `[env]` shared section:

```ini
lib_deps =
    marcoschwartz/LiquidCrystal_I2C@^1.1.4
    knolleary/PubSubClient@^2.8
    bblanchon/ArduinoJson@^7
```

- [ ] **Step 2.2: Add MQTT includes, client, and config**

In `main.cpp`, after the WiFi include:

```cpp
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ── MQTT Configuration ──────────────────────────────────────
#ifdef WOKWI
const char* MQTT_SERVER = "broker.hivemq.com";
#else
const char* MQTT_SERVER = "mqtt.datagtb.com";  // broker Mosquitto OVH
#endif
const int   MQTT_PORT   = 1883;

// Topics construits a partir de SITE_ID
char topicStatus[64];
char topicCmd[64];
char topicConfig[64];

WiFiClient espClient;
PubSubClient mqtt(espClient);
```

- [ ] **Step 2.3: Build MQTT topics in setup()**

Add a helper function before `setup()`:

```cpp
void construireTopics() {
  snprintf(topicStatus, sizeof(topicStatus), "eclairage/%s/status", SITE_ID);
  snprintf(topicCmd,    sizeof(topicCmd),    "eclairage/%s/cmd",    SITE_ID);
  snprintf(topicConfig, sizeof(topicConfig), "eclairage/%s/config", SITE_ID);
}
```

Call it in `setup()` after `chargerConfig()`:

```cpp
  construireTopics();
```

- [ ] **Step 2.4: Add MQTT connect and callback functions**

```cpp
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Null-terminate the payload
  char msg[512];
  int len = min((unsigned int)511, length);
  memcpy(msg, payload, len);
  msg[len] = '\0';

  Serial.print("[MQTT] Recu sur "); Serial.print(topic);
  Serial.print(": "); Serial.println(msg);

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, msg);
  if (err) {
    Serial.print("[MQTT] Erreur JSON: "); Serial.println(err.c_str());
    return;
  }

  if (strcmp(topic, topicCmd) == 0) {
    // Forcage distant : { "line": 1, "force": true, "state": true }
    int line = doc["line"] | 0;
    if (line >= 1 && line <= 3) {
      int idx = line - 1;
      bool force = doc["force"] | false;
      if (force) {
        lignes[idx].force_manuel = true;
        lignes[idx].force_etat = doc["state"] | true;
        lignes[idx].force_debut_ms = millis();
        Serial.print("[MQTT] Forcage L"); Serial.print(line);
        Serial.print(" -> "); Serial.println(lignes[idx].force_etat ? "ON" : "OFF");
      } else {
        lignes[idx].force_manuel = false;
        Serial.print("[MQTT] Annulation forcage L"); Serial.println(line);
      }
    }
  }
  else if (strcmp(topic, topicConfig) == 0) {
    // Planning complet depuis Niagara
    if (doc.containsKey("lat")) cfg_latitude = doc["lat"];
    if (doc.containsKey("lon")) cfg_longitude = doc["lon"];
    if (doc.containsKey("offset_set")) cfg_offset_coucher = doc["offset_set"];
    if (doc.containsKey("offset_rise")) cfg_offset_lever = doc["offset_rise"];

    JsonArray jLines = doc["lines"];
    for (int l = 0; l < 3 && l < (int)jLines.size(); l++) {
      JsonObject jl = jLines[l];
      if (jl.containsKey("name")) {
        // nom est const char* -> on ne le modifie pas en runtime
      }
      JsonArray jPlages = jl["plages"];
      for (int p = 0; p < 4 && p < (int)jPlages.size(); p++) {
        lignesCfg[l].plages[p].debut_h = jPlages[p]["h"] | 0;
        lignesCfg[l].plages[p].debut_m = jPlages[p]["m"] | 0;
      }
      JsonArray jPlanning = jl["planning"];
      for (int j = 0; j < 7 && j < (int)jPlanning.size(); j++) {
        JsonArray jJour = jPlanning[j];
        for (int p = 0; p < 4 && p < (int)jJour.size(); p++) {
          lignesCfg[l].planning[j][p] = jJour[p] | false;
        }
      }
    }

    JsonArray jExc = doc["exceptions"];
    // Note: exceptions array is fixed size — update in place
    for (int i = 0; i < NB_EXCEPTIONS && i < (int)jExc.size(); i++) {
      exceptions[i].mois = jExc[i]["m"] | 1;
      exceptions[i].jour = jExc[i]["d"] | 1;
    }

    // Recalculer le soleil avec les nouvelles coordonnees
    calculerSoleil(jour, mois, annee, cfg_latitude, cfg_longitude, leverMin, coucherMin);

    // Sauvegarder en flash
    sauvegarderConfig();

    Serial.println("[MQTT] Config appliquee et sauvegardee");
  }
}

void connecterMQTT() {
  if (!connecte) return;
  if (mqtt.connected()) return;

  Serial.print("[MQTT] Connexion a "); Serial.print(MQTT_SERVER); Serial.print("...");

  // Client ID unique par site
  char clientId[32];
  snprintf(clientId, sizeof(clientId), "ecl_%s", SITE_ID);

  if (mqtt.connect(clientId)) {
    Serial.println(" OK");
    mqtt.subscribe(topicCmd);
    mqtt.subscribe(topicConfig);
    Serial.print("[MQTT] Subscribe: "); Serial.println(topicCmd);
    Serial.print("[MQTT] Subscribe: "); Serial.println(topicConfig);
  } else {
    Serial.print(" ECHEC rc="); Serial.println(mqtt.state());
  }
}
```

- [ ] **Step 2.5: Replace publierMQTT_Serial with real MQTT publish**

Replace the entire `publierMQTT_Serial()` function:

```cpp
void publierStatus() {
  // Construire le JSON avec ArduinoJson
  JsonDocument doc;
  char ts[24];
  snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02d",
           annee, mois, jour, heure, minute, seconde);
  doc["ts"] = ts;
  doc["night"] = estNuit;

  JsonObject sun = doc["sun"].to<JsonObject>();
  char rise[6], set[6];
  snprintf(rise, sizeof(rise), "%02d:%02d", leverMin/60, leverMin%60);
  snprintf(set,  sizeof(set),  "%02d:%02d", coucherMin/60, coucherMin%60);
  sun["rise"] = rise;
  sun["set"] = set;

  doc["dst"] = estHeureDEte(jour, mois, annee);

  JsonArray slots = doc["slot"].to<JsonArray>();
  for (int i = 0; i < 3; i++) slots.add(plageActuelle(i));

  doc["exception"] = estException(jour, mois);

  JsonArray jLines = doc["lines"].to<JsonArray>();
  for (int i = 0; i < 3; i++) {
    JsonObject jl = jLines.add<JsonObject>();
    char id[4];
    snprintf(id, sizeof(id), "L%d", i+1);
    jl["id"] = id;
    jl["name"] = lignesCfg[i].nom;
    jl["auth"] = lignes[i].autorise;
    jl["state"] = lignes[i].etat ? "ON" : "OFF";
    jl["force"] = lignes[i].force_manuel;
    if (lignes[i].force_manuel) {
      jl["force_remaining"] = lignes[i].force_restant_min;
    }
  }

  // Serialiser
  char buffer[512];
  serializeJson(doc, buffer, sizeof(buffer));

  // Publier via MQTT si connecte, sinon Serial seulement
  if (mqtt.connected()) {
    mqtt.publish(topicStatus, buffer);
  }

  // Toujours afficher sur Serial (debug)
  Serial.println(buffer);
}
```

- [ ] **Step 2.6: Wire MQTT into setup() and loop()**

In `setup()`, after `construireTopics()` and after WiFi connection:

```cpp
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(1024);  // payloads config peuvent etre gros
```

In `loop()`, replace the MQTT section:

```cpp
  // 7. MQTT
  if (!mqtt.connected() && connecte) {
    static unsigned long derniereTentative = 0;
    if (now - derniereTentative >= 5000) {
      derniereTentative = now;
      connecterMQTT();
    }
  }
  mqtt.loop();

  if (now - dernierMQTT >= 2000) {
    dernierMQTT = now;
    publierStatus();
  }
```

- [ ] **Step 2.7: Build and test**

Run: `pio run -e wokwi`
Expected: SUCCESS.

On Wokwi: Serial shows WiFi connect, MQTT connect to `broker.hivemq.com`, then JSON status published every 2s. You can verify with any MQTT client (MQTTX, mosquitto_sub) subscribing to `eclairage/reims_parc_leo/status`.

Test command reception: publish `{"line":1,"force":true,"state":true}` to `eclairage/reims_parc_leo/cmd` from an external MQTT client. Verify L1 forces ON.

- [ ] **Step 2.8: Commit**

```bash
git add platformio.ini src/main.cpp
git commit -m "feat: real MQTT via PubSubClient with publish/subscribe"
```

---

## Task 3: NTP Time Sync

**Files:**
- Modify: `src/main.cpp`

Use the ESP32 built-in `configTzTime()` + `getLocalTime()` for NTP. This handles timezone and DST automatically via the POSIX TZ string. On Wokwi, NTP works over WiFi. In production, it will work over 4G once the modem provides internet.

- [ ] **Step 3.1: Add NTP sync function**

Add after the WiFi connection function:

```cpp
void syncNTP() {
  if (!connecte) return;
  Serial.println("[NTP] Synchronisation...");
  // Timezone Europe/Paris : CET-1CEST,M3.5.0/2,M10.5.0/3
  configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3", "pool.ntp.org", "time.google.com");

  struct tm timeinfo;
  int tentatives = 0;
  while (!getLocalTime(&timeinfo) && tentatives < 10) {
    delay(500);
    tentatives++;
  }

  if (getLocalTime(&timeinfo)) {
    annee = timeinfo.tm_year + 1900;
    mois = timeinfo.tm_mon + 1;
    jour = timeinfo.tm_mday;
    heure = timeinfo.tm_hour;
    minute = timeinfo.tm_min;
    seconde = timeinfo.tm_sec;
    // tm_wday: 0=dimanche — meme convention que notre code
    jourSemaine = timeinfo.tm_wday;

    calculerSoleil(jour, mois, annee, cfg_latitude, cfg_longitude, leverMin, coucherMin);

    Serial.print("[NTP] OK: ");
    Serial.print(jour); Serial.print("/"); Serial.print(mois); Serial.print("/"); Serial.print(annee);
    Serial.print(" "); Serial.print(heure); Serial.print(":"); Serial.print(minute);
    Serial.print(" ("); Serial.print(JOURS[jourSemaine]); Serial.println(")");
  } else {
    Serial.println("[NTP] ECHEC — heure non synchronisee");
  }
}
```

- [ ] **Step 3.2: Add periodic NTP re-sync in production mode**

Add a function to refresh time from NTP (and from system time in general) for production mode:

```cpp
#ifndef WOKWI
void mettreAJourHeure() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    int ancienJour = jour;
    annee = timeinfo.tm_year + 1900;
    mois = timeinfo.tm_mon + 1;
    jour = timeinfo.tm_mday;
    heure = timeinfo.tm_hour;
    minute = timeinfo.tm_min;
    seconde = timeinfo.tm_sec;
    jourSemaine = timeinfo.tm_wday;

    // Recalculer le soleil si le jour a change
    if (jour != ancienJour) {
      calculerSoleil(jour, mois, annee, cfg_latitude, cfg_longitude, leverMin, coucherMin);
    }
  }
}
#endif
```

- [ ] **Step 3.3: Call NTP sync in setup() and time update in loop()**

In `setup()`, after WiFi connection and before MQTT setup:

```cpp
#ifndef WOKWI
  syncNTP();
#endif
```

In `loop()`, replace the time advancement section:

```cpp
#ifdef WOKWI
  // 2. AVANCER LE TEMPS (simulation)
  if (now - dernierTick >= 100) {
    float dt = (now - dernierTick) / 1000.0;
    dernierTick = now;
    avancerTemps(dt);
  }
#else
  // 2. LIRE L'HEURE REELLE
  if (now - dernierTick >= 1000) {
    dernierTick = now;
    mettreAJourHeure();
  }
#endif
```

- [ ] **Step 3.4: Build and test both environments**

Run: `pio run -e wokwi`
Expected: SUCCESS. Wokwi still uses simulated time with potentiometer.

Run: `pio run -e edgebox`
Expected: SUCCESS. Production would use NTP time.

On Wokwi: note that `syncNTP()` is NOT called in Wokwi mode — the simulated clock is kept for testing. NTP on Wokwi can be tested manually if needed by temporarily removing the `#ifndef WOKWI` guard.

- [ ] **Step 3.5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: NTP time sync with Europe/Paris timezone"
```

---

## Task 4: RTC PCF8563 Support (Production Only)

**Files:**
- Modify: `platformio.ini` (add lib_dep for edgebox only)
- Modify: `src/main.cpp`

The PCF8563 RTC (I2C address 0x51) maintains time when there's no network. Strategy: boot -> read RTC -> sync NTP when connected -> recalibrate RTC every 6h from NTP.

This is production-only code (`#ifdef EDGEBOX`). Not testable on Wokwi (no PCF8563 in Wokwi component library).

- [ ] **Step 4.1: Add RTC library to edgebox environment**

In `platformio.ini`, add to `[env:edgebox]`:

```ini
lib_deps =
    ${env.lib_deps}
    orbitalair/Rtc_Pcf8563@^2.0.1
```

- [ ] **Step 4.2: Add RTC include and functions**

In `main.cpp`, after the WiFi/MQTT includes:

```cpp
#ifdef EDGEBOX
#include <Rtc_Pcf8563.h>
Rtc_Pcf8563 rtc;

void lireRTC() {
  rtc.getDate();
  rtc.getTime();
  annee = 2000 + rtc.getYear();  // PCF8563 retourne annee sur 2 chiffres
  mois = rtc.getMonth();
  jour = rtc.getDay();
  heure = rtc.getHour();
  minute = rtc.getMinute();
  seconde = rtc.getSecond();
  jourSemaine = rtc.getWeekday();  // 0=dimanche
  calculerSoleil(jour, mois, annee, cfg_latitude, cfg_longitude, leverMin, coucherMin);
  Serial.print("[RTC] Lu: ");
  Serial.print(jour); Serial.print("/"); Serial.print(mois); Serial.print("/"); Serial.print(annee);
  Serial.print(" "); Serial.print(heure); Serial.print(":"); Serial.println(minute);
}

void ecrireRTC() {
  rtc.setDate(jour, jourSemaine, mois, 0, annee % 100);
  rtc.setTime(heure, minute, seconde);
  Serial.println("[RTC] Recalibree depuis NTP");
}
#endif
```

- [ ] **Step 4.3: Integrate RTC in setup() boot sequence**

In `setup()`, after I2C init and before WiFi:

```cpp
#ifdef EDGEBOX
  // Lire RTC au boot — heure disponible immediatement
  lireRTC();
  Serial.println("[BOOT] Heure initialisee depuis RTC");
#endif
```

After NTP sync succeeds, recalibrate the RTC:

```cpp
#ifndef WOKWI
  syncNTP();
  #ifdef EDGEBOX
  // Si NTP a reussi, recaler la RTC
  if (connecte) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      ecrireRTC();
    }
  }
  #endif
#endif
```

- [ ] **Step 4.4: Add periodic RTC recalibration every 6h**

Add a global:

```cpp
#ifdef EDGEBOX
unsigned long dernierRecalRTC = 0;
const unsigned long INTERVAL_RECAL_RTC = 6UL * 3600UL * 1000UL;  // 6 heures en ms
#endif
```

In `loop()`, after NTP time update:

```cpp
#ifdef EDGEBOX
  // Recalibrer RTC toutes les 6h depuis NTP
  if (connecte && (now - dernierRecalRTC >= INTERVAL_RECAL_RTC)) {
    dernierRecalRTC = now;
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      ecrireRTC();
    }
  }
#endif
```

- [ ] **Step 4.5: Build both environments**

Run: `pio run -e wokwi`
Expected: SUCCESS (RTC code excluded).

Run: `pio run -e edgebox`
Expected: SUCCESS (RTC code compiled, will work on real hardware).

- [ ] **Step 4.6: Commit**

```bash
git add platformio.ini src/main.cpp
git commit -m "feat: RTC PCF8563 read/write for production timekeeping"
```

---

## Task 5: OTA HTTPS Update

**Files:**
- Modify: `src/main.cpp`

The ESP32 checks for firmware updates every 24h via HTTPS. Uses the built-in `httpUpdate` (ESP32 Arduino core). Dual OTA partitions handle rollback. The OTA URL is `https://ota.datagtb.com/edgebox/{site}/firmware.bin`.

On Wokwi, OTA is compiled but won't actually trigger (no update server). In production, it will check and auto-update.

- [ ] **Step 5.1: Add OTA includes and configuration**

After existing includes:

```cpp
#include <HTTPUpdate.h>

// ── OTA Configuration ───────────────────────────────────────
const char* FW_VERSION = "2.1.0";
char OTA_URL[128];
const unsigned long INTERVAL_OTA = 24UL * 3600UL * 1000UL;  // 24h
unsigned long dernierCheckOTA = 0;
```

- [ ] **Step 5.2: Add OTA check function**

```cpp
void verifierOTA() {
  if (!connecte) return;

  Serial.print("[OTA] Verification mise a jour v");
  Serial.print(FW_VERSION);
  Serial.print(" sur ");
  Serial.println(OTA_URL);

  WiFiClient otaClient;
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  t_httpUpdate_return ret = httpUpdate.update(otaClient, OTA_URL, FW_VERSION);

  switch (ret) {
    case HTTP_UPDATE_OK:
      Serial.println("[OTA] Mise a jour OK — redemarrage...");
      // Le reboot est automatique
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("[OTA] Firmware a jour");
      break;
    case HTTP_UPDATE_FAILED:
      Serial.print("[OTA] Echec: ");
      Serial.println(httpUpdate.getLastErrorString());
      break;
  }
}
```

- [ ] **Step 5.3: Build OTA URL and wire into setup/loop**

In `setup()`, after `construireTopics()`:

```cpp
  snprintf(OTA_URL, sizeof(OTA_URL), "https://ota.datagtb.com/edgebox/%s/firmware.bin", SITE_ID);
```

In `loop()`, add after MQTT section:

```cpp
  // 8. OTA (toutes les 24h)
  if (connecte && (now - dernierCheckOTA >= INTERVAL_OTA || dernierCheckOTA == 0)) {
    dernierCheckOTA = now;
    verifierOTA();
  }
```

Note: `dernierCheckOTA == 0` triggers a first check shortly after boot.

- [ ] **Step 5.4: Add firmware version to LCD system page and MQTT status**

In `afficherPage2()`, add version on line 0:

```cpp
void afficherPage2() {
  lcd.setCursor(0, 0);
  lcd.print("Site: ");
  lcd.print(SITE_ID);
  lcd.print(" v");
  lcd.print(FW_VERSION);
  // ... rest unchanged
```

In `publierStatus()`, add to the JSON doc before serializing:

```cpp
  doc["fw"] = FW_VERSION;
```

- [ ] **Step 5.5: Build and test**

Run: `pio run -e wokwi`
Expected: SUCCESS. On Wokwi, OTA will attempt at boot, fail gracefully (no server), and continue normally.

Run: `pio run -e edgebox`
Expected: SUCCESS.

- [ ] **Step 5.6: Commit**

```bash
git add src/main.cpp
git commit -m "feat: OTA HTTPS firmware update check every 24h"
```

---

## Task 6: MQTT Status on LCD + Reconnection Robustness

**Files:**
- Modify: `src/main.cpp`

Final polish: show MQTT connection status on LCD, add WiFi reconnection logic, and add a `force_remaining` field to the MQTT status JSON for lines with active override.

- [ ] **Step 6.1: Add MQTT status indicator to LCD page 0**

In `afficherPage0()`, modify line 2 to show MQTT status:

```cpp
  lcd.setCursor(19, 0);
  lcd.print(mqtt.connected() ? "M" : " ");
```

This shows "M" in the top-right corner of the main page when MQTT is connected.

- [ ] **Step 6.2: Add WiFi reconnection in loop()**

In `loop()`, before the MQTT reconnection section:

```cpp
#ifdef WOKWI
  // Reconnexion WiFi si perdue
  if (WiFi.status() != WL_CONNECTED) {
    if (connecte) {
      connecte = false;
      Serial.println("[WIFI] Connexion perdue");
    }
    static unsigned long dernierReconnectWiFi = 0;
    if (now - dernierReconnectWiFi >= 10000) {
      dernierReconnectWiFi = now;
      connecterWiFi();
    }
  } else if (!connecte) {
    connecte = true;
  }
#endif
```

- [ ] **Step 6.3: Build and final test**

Run: `pio run -e wokwi`
Expected: SUCCESS.

Full Wokwi test checklist:
1. Boot: splash screen, WiFi connects, MQTT connects
2. Serial: JSON status every 2s with all fields including `fw` version
3. LCD page 0: "M" indicator top-right when MQTT connected
4. LCD page 4: shows version and NET status
5. Force buttons: still work, MQTT shows `force: true`
6. Page button: cycles through 5 pages
7. Time acceleration: potentiometer still works
8. From external MQTT client: send cmd, verify force activates

Run: `pio run -e edgebox`
Expected: SUCCESS.

- [ ] **Step 6.4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: MQTT/WiFi status on LCD and reconnection robustness"
```

---

## Summary: Final File State

After all tasks, the project has:

- `platformio.ini` — two environments: `wokwi` (WiFi, simulation) and `edgebox` (4G production)
- `src/main.cpp` — single file with `#ifdef WOKWI` / `#ifdef EDGEBOX` guards:
  - Wokwi: simulated clock + potentiometer + WiFi transport + MQTT demo on `broker.hivemq.com`
  - EdgeBox: NTP real clock + RTC PCF8563 + MQTT on `mqtt.datagtb.com` + OTA HTTPS
  - Shared: all business logic (planning, astronomical calc, force, exceptions, LCD, flash)
- `diagram.json` — unchanged (Wokwi schematic)
- `wokwi.toml` — needs `firmware` path updated to `.pio/build/wokwi/firmware.bin`

Dependencies added:
- `knolleary/PubSubClient@^2.8` — MQTT client
- `bblanchon/ArduinoJson@^7` — JSON serialization/deserialization
- `orbitalair/Rtc_Pcf8563@^2.0.1` — RTC driver (edgebox only)
- `HTTPUpdate.h` — built-in ESP32 OTA (no extra lib)
