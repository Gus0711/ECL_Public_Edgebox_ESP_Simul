# Refactoring Planning Nuit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the 4-plage/matrix planning system with a simpler night-based model (3 modes: eteint/toute la nuit/horaires programmes) and add date-range exceptions with configurable modes.

**Architecture:** Single file `src/main.cpp` refactoring. Replace `Plage[4]` + `bool planning[7][4]` with `NuitConfig nuits[7]` (mode + extinction + rallumage). Replace `ExceptionDate` (single dates, always ON) with `ExceptionConfig` (date ranges, 3 modes). Update `calculerEtats()` to use night-index logic. Update flash storage, MQTT config/status payloads, and LCD display pages.

**Tech Stack:** PlatformIO, Arduino, ESP32-S3, ArduinoJson v7, PubSubClient, Preferences (NVS)

**Constraint:** Logique metier change but everything else stays: GPIO, forcage, boutons, astro calc, OTA, NTP, RTC, WiFi/MQTT connection. Both `wokwi` and `edgebox` environments must compile.

---

## File Structure

Only one file changes: `src/main.cpp`. Sections affected:

| Section | Lines (approx) | Change |
|---------|----------------|--------|
| Forward declarations | 34-44 | Remove `ExceptionDate`, add `NuitConfig`, `ExceptionConfig` |
| Structs + config data | 106-186 | Replace `Plage`, `LigneConfig`, `ExceptionDate` with new structs + defaults |
| `plageActuelle()` | 351-358 | **Delete entirely** |
| `estException()` | 382-387 | Replace with `getNuitConfig()` |
| `calculerEtats()` | 389-429 | Rewrite with night-index + 3-mode logic |
| `mqttCallback` (config) | 624-657 | Parse new JSON format (nights/exceptions) |
| `publierStatus()` | 680-725 | Add `night_index` and `mode` fields |
| `sauvegarderConfig()` | 763-788 | Store NuitConfig + ExceptionConfig |
| `chargerConfig()` | 791-824 | Load NuitConfig + ExceptionConfig |
| `afficherLigne()` | 870-910 | Show mode/ext/ral instead of plage matrix |

---

## Task 0: Replace Structs and Default Config

**Files:**
- Modify: `src/main.cpp:106-186` (structs + config data)
- Modify: `src/main.cpp:34-44` (forward declarations)

- [ ] **Step 0.1: Replace forward declarations**

Replace lines 35-37:
```cpp
// Forward declarations
struct Config;
struct LigneConfig;
struct ExceptionDate;
```
with:
```cpp
// Forward declarations
struct NuitConfig;
struct LigneConfig;
struct ExceptionConfig;
```

- [ ] **Step 0.2: Replace structs and default config**

Replace the entire block from `// ── Planning hebdomadaire par ligne` through the end of the exceptions array (lines 106-186) with:

```cpp
// ── Planning hebdomadaire par ligne ─────────────────────────
// Modele par NUIT : chaque nuit (dim→lun, lun→mar, ...) a un mode
//   mode 0 = eteint toute la nuit
//   mode 1 = allume toute la nuit (coucher→lever)
//   mode 2 = horaires programmes (coucher→ext OFF, ral→lever ON)
// Si mode 2 et ral_h=0, ral_m=0 : pas de rallumage (extinction definitive)

struct NuitConfig {
  int mode;           // 0=eteint, 1=toute la nuit, 2=horaires
  int ext_h, ext_m;   // heure extinction (mode 2 uniquement)
  int ral_h, ral_m;   // heure rallumage  (mode 2 uniquement)
};

struct LigneConfig {
  const char* nom;
  NuitConfig nuits[7];  // 0=dim→lun, 1=lun→mar, ..., 6=sam→dim
};

LigneConfig lignesCfg[3] = {
  // ── LIGNE 1 — Voiries ──────────────────────────────────
  // Semaine : coupure 23:30→05:30, week-end : toute la nuit
  {
    "Voiries",
    {
      { 1, 0,0, 0,0 },       // dim→lun : allume toute la nuit
      { 2, 23,30, 5,30 },    // lun→mar : extinction 23h30, rallumage 5h30
      { 2, 23,30, 5,30 },    // mar→mer
      { 2, 23,30, 5,30 },    // mer→jeu
      { 2, 23,30, 5,30 },    // jeu→ven
      { 2, 23,30, 5,30 },    // ven→sam
      { 1, 0,0, 0,0 },       // sam→dim : allume toute la nuit
    }
  },
  // ── LIGNE 2 — Parkings ─────────────────────────────────
  // Tous les jours : coupure 23:00→06:00
  {
    "Parkings",
    {
      { 2, 23,0, 6,0 },      // dim→lun
      { 2, 23,0, 6,0 },      // lun→mar
      { 2, 23,0, 6,0 },      // mar→mer
      { 2, 23,0, 6,0 },      // mer→jeu
      { 2, 23,0, 6,0 },      // jeu→ven
      { 2, 23,0, 6,0 },      // ven→sam
      { 2, 23,0, 6,0 },      // sam→dim
    }
  },
  // ── LIGNE 3 — Decoratif ────────────────────────────────
  // Extinction a 23h00, pas de rallumage matin
  {
    "Deco",
    {
      { 0, 0,0, 0,0 },       // dim→lun : eteint
      { 2, 23,0, 0,0 },      // lun→mar : extinction 23h, pas de rallumage
      { 2, 23,0, 0,0 },      // mar→mer
      { 2, 23,0, 0,0 },      // mer→jeu
      { 2, 23,0, 0,0 },      // jeu→ven
      { 2, 23,0, 0,0 },      // ven→sam
      { 1, 0,0, 0,0 },       // sam→dim : allume toute la nuit
    }
  },
};

// ── Exceptions (plages de dates) ────────────────────────────
// Override le planning hebdo — globales (3 lignes)
// Meme modes que NuitConfig (0/1/2)
struct ExceptionConfig {
  int from_jour, from_mois;
  int to_jour, to_mois;
  int mode;
  int ext_h, ext_m;
  int ral_h, ral_m;
};

const int NB_EXCEPTIONS_MAX = 10;
ExceptionConfig exceptions[NB_EXCEPTIONS_MAX] = {
  { 24,12, 25,12, 1, 0,0, 0,0 },   // Noel : allume toute la nuit
  { 14,7,  14,7,  1, 0,0, 0,0 },   // 14 juillet : toute la nuit
  {  1,1,   1,1,  1, 0,0, 0,0 },   // Nouvel an : toute la nuit
};
int nbExceptions = 3;
```

- [ ] **Step 0.3: Build both environments**

Run: `pio run -e wokwi` and `pio run -e edgebox`
Expected: FAIL — `plageActuelle`, `estException`, old struct references are broken. This is expected and will be fixed in Task 1.

---

## Task 1: New Core Logic — calculerEtats()

**Files:**
- Modify: `src/main.cpp` — delete `plageActuelle()`, replace `estException()` and `calculerEtats()`

- [ ] **Step 1.1: Delete plageActuelle()**

Delete the entire `plageActuelle()` function (lines ~351-358):
```cpp
int plageActuelle(int ligne) {
  int now = minutesDuJour();
  Plage* pl = lignesCfg[ligne].plages;
  for (int i = 3; i >= 0; i--) {
    int debut = pl[i].debut_h * 60 + pl[i].debut_m;
    if (now >= debut) return i;
  }
  return 3;
}
```

- [ ] **Step 1.2: Replace estException() with date-range and getNuitConfig()**

Replace `estException()` with:

```cpp
// Verifier si une date est dans une plage (gere le chevauchement d'annee)
bool dateEstDansPlage(int j, int m, const ExceptionConfig &exc) {
  int d = m * 100 + j;         // ex: 24 dec = 1224
  int from = exc.from_mois * 100 + exc.from_jour;
  int to = exc.to_mois * 100 + exc.to_jour;
  if (from <= to) {
    return (d >= from && d <= to);  // meme annee : 14/07 → 14/07
  } else {
    return (d >= from || d <= to);  // chevauchement : 26/12 → 02/01
  }
}

// Trouver la config de nuit applicable (exception prioritaire)
NuitConfig getNuitConfig(int ligne, int indexNuit, int j, int m) {
  // 1. Chercher exception
  for (int i = 0; i < nbExceptions; i++) {
    if (dateEstDansPlage(j, m, exceptions[i])) {
      NuitConfig nc;
      nc.mode = exceptions[i].mode;
      nc.ext_h = exceptions[i].ext_h;
      nc.ext_m = exceptions[i].ext_m;
      nc.ral_h = exceptions[i].ral_h;
      nc.ral_m = exceptions[i].ral_m;
      return nc;
    }
  }
  // 2. Sinon planning hebdo
  return lignesCfg[ligne].nuits[indexNuit];
}
```

- [ ] **Step 1.3: Rewrite calculerEtats()**

Replace the entire `calculerEtats()` function with:

```cpp
int indexNuitActuelle = 0;  // variable globale pour LCD/MQTT

void calculerEtats() {
  int now = minutesDuJour();

  // Determiner si c'est la nuit (entre coucher+offset et lever+offset)
  int debutNuit = coucherMin + cfg_offset_coucher;
  int finNuit   = leverMin + cfg_offset_lever;
  if (debutNuit < 0) debutNuit += 1440;
  if (debutNuit >= 1440) debutNuit -= 1440;
  if (finNuit >= 1440) finNuit -= 1440;

  if (debutNuit > finNuit) {
    estNuit = (now >= debutNuit || now < finNuit);
  } else {
    estNuit = (now >= debutNuit && now < finNuit);
  }

  // Determiner l'index de la nuit (quel soir a commence cette nuit)
  if (now >= debutNuit) {
    indexNuitActuelle = jourSemaine;            // soir → nuit commence aujourd'hui
  } else if (now < finNuit) {
    indexNuitActuelle = (jourSemaine + 6) % 7;  // matin → nuit a commence hier
  } else {
    indexNuitActuelle = jourSemaine;             // jour → pas de nuit active
  }

  for (int i = 0; i < 3; i++) {
    lignes[i].nuit = estNuit;

    // Forcage a priorite absolue
    if (lignes[i].force_manuel) {
      lignes[i].etat = lignes[i].force_etat;
      lignes[i].autorise = true;  // pour l'affichage
      digitalWrite(PIN_LED[i], lignes[i].etat ? HIGH : LOW);
      continue;
    }

    if (!estNuit) {
      // Il fait jour → eteint
      lignes[i].autorise = false;
      lignes[i].etat = false;
      digitalWrite(PIN_LED[i], LOW);
      continue;
    }

    // Il fait nuit → appliquer la config de nuit
    NuitConfig cfg = getNuitConfig(i, indexNuitActuelle, jour, mois);

    switch (cfg.mode) {
      case 0:  // Eteint toute la nuit
        lignes[i].autorise = false;
        lignes[i].etat = false;
        break;

      case 1:  // Allume toute la nuit
        lignes[i].autorise = true;
        lignes[i].etat = true;
        break;

      case 2: {  // Horaires programmes
        int ext = cfg.ext_h * 60 + cfg.ext_m;
        int ral = cfg.ral_h * 60 + cfg.ral_m;
        bool pasDeRallumage = (cfg.ral_h == 0 && cfg.ral_m == 0);

        bool dansCreneauOff;
        if (pasDeRallumage) {
          // Extinction definitive : OFF de ext jusqu'au lever
          if (ext > finNuit) {  // ext le soir (ex 23:00), finNuit le matin (ex 08:15)
            dansCreneauOff = (now >= ext || now < finNuit);
          } else {
            dansCreneauOff = (now >= ext);
          }
        } else {
          // Creneau OFF = entre ext et ral
          if (ext > ral) {  // ext=22:00, ral=06:30 → creneau chevauche minuit
            dansCreneauOff = (now >= ext || now < ral);
          } else {
            dansCreneauOff = (now >= ext && now < ral);
          }
        }

        lignes[i].autorise = !dansCreneauOff;
        lignes[i].etat = !dansCreneauOff;
        break;
      }
    }

    digitalWrite(PIN_LED[i], lignes[i].etat ? HIGH : LOW);
  }
}
```

- [ ] **Step 1.4: Add indexNuitActuelle to globals and forward declaration**

Add `extern int indexNuitActuelle;` in the forward declarations section, and add the actual declaration before `calculerEtats()`:

```cpp
int indexNuitActuelle = 0;
```

Also add forward declarations for the new functions:
```cpp
bool dateEstDansPlage(int j, int m, const ExceptionConfig &exc);
NuitConfig getNuitConfig(int ligne, int indexNuit, int j, int m);
```

- [ ] **Step 1.5: Build both environments**

Run: `pio run`
Expected: FAIL — `publierStatus()` still references `plageActuelle()`, `estException()`. Flash save/load still uses old structs. LCD still uses old display. Fixed in next tasks.

---

## Task 2: Update MQTT Status Payload

**Files:**
- Modify: `src/main.cpp` — `publierStatus()` function

- [ ] **Step 2.1: Update publierStatus()**

Replace the slot/exception/lines section inside `publierStatus()`. Find:
```cpp
  JsonArray slots = doc["slot"].to<JsonArray>();
  for (int i = 0; i < 3; i++) slots.add(plageActuelle(i));

  doc["exception"] = estException(jour, mois);
```

Replace with:
```cpp
  doc["night_index"] = indexNuitActuelle;
```

In the lines loop, find:
```cpp
    jl["auth"] = lignes[i].autorise;
    jl["state"] = lignes[i].etat ? "ON" : "OFF";
    jl["force"] = lignes[i].force_manuel;
    if (lignes[i].force_manuel) {
      jl["force_remaining"] = lignes[i].force_restant_min;
    }
```

Replace with:
```cpp
    NuitConfig ncfg = getNuitConfig(i, indexNuitActuelle, jour, mois);
    jl["mode"] = ncfg.mode;
    jl["auth"] = lignes[i].autorise;
    jl["state"] = lignes[i].etat ? "ON" : "OFF";
    jl["force"] = lignes[i].force_manuel;
    if (lignes[i].force_manuel) {
      jl["force_remaining"] = lignes[i].force_restant_min;
    }
    if (ncfg.mode == 2) {
      char ext[6], ral[6];
      snprintf(ext, sizeof(ext), "%02d:%02d", ncfg.ext_h, ncfg.ext_m);
      snprintf(ral, sizeof(ral), "%02d:%02d", ncfg.ral_h, ncfg.ral_m);
      jl["off"] = ext;
      jl["on"] = ral;
    }
```

- [ ] **Step 2.2: Build**

Run: `pio run`
Expected: Still fails on flash save/load and LCD. Next task.

---

## Task 3: Update MQTT Config Reception

**Files:**
- Modify: `src/main.cpp` — `mqttCallback()` config section

- [ ] **Step 3.1: Replace config parsing in mqttCallback()**

Replace the entire `else if (strcmp(topic, topicConfig) == 0)` block with:

```cpp
  else if (strcmp(topic, topicConfig) == 0) {
    // Planning complet depuis Niagara (nouveau format nuits)
    if (doc["lat"].is<float>()) cfg_latitude = doc["lat"];
    if (doc["lon"].is<float>()) cfg_longitude = doc["lon"];
    if (doc["offset_set"].is<int>()) cfg_offset_coucher = doc["offset_set"];
    if (doc["offset_rise"].is<int>()) cfg_offset_lever = doc["offset_rise"];

    JsonArray jLines = doc["lines"];
    for (int l = 0; l < 3 && l < (int)jLines.size(); l++) {
      JsonObject jl = jLines[l];
      if (jl["name"].is<const char*>()) {
        // nom est const char* initial, on ne le modifie pas en runtime
      }
      JsonArray jNights = jl["nights"];
      for (int n = 0; n < 7 && n < (int)jNights.size(); n++) {
        JsonObject jn = jNights[n];
        lignesCfg[l].nuits[n].mode = jn["mode"] | 0;
        // Parser "off": "22:00" → ext_h=22, ext_m=0
        const char* offStr = jn["off"];
        if (offStr) {
          int oh=0, om=0;
          sscanf(offStr, "%d:%d", &oh, &om);
          lignesCfg[l].nuits[n].ext_h = oh;
          lignesCfg[l].nuits[n].ext_m = om;
        } else {
          lignesCfg[l].nuits[n].ext_h = 0;
          lignesCfg[l].nuits[n].ext_m = 0;
        }
        // Parser "on": "06:30" → ral_h=6, ral_m=30
        const char* onStr = jn["on"];
        if (onStr) {
          int rh=0, rm=0;
          sscanf(onStr, "%d:%d", &rh, &rm);
          lignesCfg[l].nuits[n].ral_h = rh;
          lignesCfg[l].nuits[n].ral_m = rm;
        } else {
          lignesCfg[l].nuits[n].ral_h = 0;
          lignesCfg[l].nuits[n].ral_m = 0;
        }
      }
    }

    // Exceptions
    JsonArray jExc = doc["exceptions"];
    nbExceptions = 0;
    for (int i = 0; i < NB_EXCEPTIONS_MAX && i < (int)jExc.size(); i++) {
      JsonObject je = jExc[i];
      // Parser "from": "24/12"
      const char* fromStr = je["from"];
      const char* toStr = je["to"];
      if (fromStr && toStr) {
        sscanf(fromStr, "%d/%d", &exceptions[i].from_jour, &exceptions[i].from_mois);
        sscanf(toStr, "%d/%d", &exceptions[i].to_jour, &exceptions[i].to_mois);
        exceptions[i].mode = je["mode"] | 1;
        const char* eStr = je["off"];
        if (eStr) {
          sscanf(eStr, "%d:%d", &exceptions[i].ext_h, &exceptions[i].ext_m);
        } else {
          exceptions[i].ext_h = 0; exceptions[i].ext_m = 0;
        }
        const char* rStr = je["on"];
        if (rStr) {
          sscanf(rStr, "%d:%d", &exceptions[i].ral_h, &exceptions[i].ral_m);
        } else {
          exceptions[i].ral_h = 0; exceptions[i].ral_m = 0;
        }
        nbExceptions++;
      }
    }

    calculerSoleil(jour, mois, annee, cfg_latitude, cfg_longitude, leverMin, coucherMin);
    sauvegarderConfig();
    Serial.println("[MQTT] Config appliquee et sauvegardee");
  }
```

---

## Task 4: Update Flash Save/Load

**Files:**
- Modify: `src/main.cpp` — `sauvegarderConfig()` and `chargerConfig()`

- [ ] **Step 4.1: Rewrite sauvegarderConfig()**

Replace the entire function:

```cpp
void sauvegarderConfig() {
  prefs.begin("eclairage", false);
  prefs.putFloat("lat", cfg_latitude);
  prefs.putFloat("lon", cfg_longitude);
  prefs.putInt("off_cou", cfg_offset_coucher);
  prefs.putInt("off_lev", cfg_offset_lever);

  for (int l = 0; l < 3; l++) {
    for (int n = 0; n < 7; n++) {
      char key[10];
      // mode (3 bits suffisent, mais on utilise un int pour la clarte)
      snprintf(key, sizeof(key), "m%d_%d", l, n);
      prefs.putInt(key, lignesCfg[l].nuits[n].mode);
      // extinction en minutes
      snprintf(key, sizeof(key), "e%d_%d", l, n);
      prefs.putInt(key, lignesCfg[l].nuits[n].ext_h * 60 + lignesCfg[l].nuits[n].ext_m);
      // rallumage en minutes
      snprintf(key, sizeof(key), "r%d_%d", l, n);
      prefs.putInt(key, lignesCfg[l].nuits[n].ral_h * 60 + lignesCfg[l].nuits[n].ral_m);
    }
  }

  // Exceptions
  prefs.putInt("nbExc", nbExceptions);
  for (int i = 0; i < nbExceptions; i++) {
    char key[10];
    snprintf(key, sizeof(key), "xf%d", i);  // from: mois*100+jour
    prefs.putInt(key, exceptions[i].from_mois * 100 + exceptions[i].from_jour);
    snprintf(key, sizeof(key), "xt%d", i);  // to
    prefs.putInt(key, exceptions[i].to_mois * 100 + exceptions[i].to_jour);
    snprintf(key, sizeof(key), "xm%d", i);  // mode
    prefs.putInt(key, exceptions[i].mode);
    snprintf(key, sizeof(key), "xe%d", i);  // extinction
    prefs.putInt(key, exceptions[i].ext_h * 60 + exceptions[i].ext_m);
    snprintf(key, sizeof(key), "xr%d", i);  // rallumage
    prefs.putInt(key, exceptions[i].ral_h * 60 + exceptions[i].ral_m);
  }

  prefs.end();
  Serial.println("[FLASH] Config sauvegardee");
}
```

- [ ] **Step 4.2: Rewrite chargerConfig()**

Replace the entire function:

```cpp
void chargerConfig() {
  prefs.begin("eclairage", true);
  if (prefs.isKey("lat")) {
    cfg_latitude = prefs.getFloat("lat", cfg_latitude);
    cfg_longitude = prefs.getFloat("lon", cfg_longitude);
    cfg_offset_coucher = prefs.getInt("off_cou", cfg_offset_coucher);
    cfg_offset_lever = prefs.getInt("off_lev", cfg_offset_lever);

    for (int l = 0; l < 3; l++) {
      for (int n = 0; n < 7; n++) {
        char key[10];
        snprintf(key, sizeof(key), "m%d_%d", l, n);
        if (prefs.isKey(key)) {
          lignesCfg[l].nuits[n].mode = prefs.getInt(key, 1);
          snprintf(key, sizeof(key), "e%d_%d", l, n);
          int ext = prefs.getInt(key, 0);
          lignesCfg[l].nuits[n].ext_h = ext / 60;
          lignesCfg[l].nuits[n].ext_m = ext % 60;
          snprintf(key, sizeof(key), "r%d_%d", l, n);
          int ral = prefs.getInt(key, 0);
          lignesCfg[l].nuits[n].ral_h = ral / 60;
          lignesCfg[l].nuits[n].ral_m = ral % 60;
        }
      }
    }

    // Exceptions
    nbExceptions = prefs.getInt("nbExc", 0);
    for (int i = 0; i < nbExceptions && i < NB_EXCEPTIONS_MAX; i++) {
      char key[10];
      snprintf(key, sizeof(key), "xf%d", i);
      int from = prefs.getInt(key, 101);
      exceptions[i].from_mois = from / 100;
      exceptions[i].from_jour = from % 100;
      snprintf(key, sizeof(key), "xt%d", i);
      int to = prefs.getInt(key, 101);
      exceptions[i].to_mois = to / 100;
      exceptions[i].to_jour = to % 100;
      snprintf(key, sizeof(key), "xm%d", i);
      exceptions[i].mode = prefs.getInt(key, 1);
      snprintf(key, sizeof(key), "xe%d", i);
      int ext = prefs.getInt(key, 0);
      exceptions[i].ext_h = ext / 60;
      exceptions[i].ext_m = ext % 60;
      snprintf(key, sizeof(key), "xr%d", i);
      int ral = prefs.getInt(key, 0);
      exceptions[i].ral_h = ral / 60;
      exceptions[i].ral_m = ral % 60;
    }

    Serial.println("[FLASH] Config chargee");
  } else {
    Serial.println("[FLASH] Pas de config, utilise defaut");
  }
  prefs.end();
}
```

- [ ] **Step 4.3: Build both environments**

Run: `pio run`
Expected: FAIL on LCD only (afficherLigne still references old plages). Next task.

---

## Task 5: Update LCD Display

**Files:**
- Modify: `src/main.cpp` — `afficherLigne()` and `afficherPage0()`

- [ ] **Step 5.1: Rewrite afficherLigne()**

Replace the entire `afficherLigne()` function:

```cpp
const char* MODE_LABELS[] = {"Eteint", "Toute nuit", "Horaires"};

void afficherLigne(int li) {
  LigneConfig &cfg = lignesCfg[li];
  NuitConfig nc = getNuitConfig(li, indexNuitActuelle, jour, mois);

  // Ligne 0 : nom + jours de la nuit
  lcd.setCursor(0, 0);
  lcd.print("L"); lcd.print(li+1); lcd.print(" "); lcd.print(cfg.nom);
  lcd.print(" ");
  lcd.print(JOURS[indexNuitActuelle]);
  lcd.print(">");
  lcd.print(JOURS[(indexNuitActuelle+1)%7]);
  lcd.print("    ");

  // Ligne 1 : mode
  lcd.setCursor(0, 1);
  lcd.print("Mode: ");
  lcd.print(MODE_LABELS[nc.mode]);
  lcd.print("          ");

  // Ligne 2 : horaires si mode 2
  lcd.setCursor(0, 2);
  if (nc.mode == 2) {
    lcd.print("Ext:");
    lcdHM(nc.ext_h, nc.ext_m);
    lcd.print(" Ral:");
    if (nc.ral_h == 0 && nc.ral_m == 0) {
      lcd.print("--:--");
    } else {
      lcdHM(nc.ral_h, nc.ral_m);
    }
    lcd.print("  ");
  } else {
    lcd.print("                    ");
  }

  // Ligne 3 : etat
  lcd.setCursor(0, 3);
  lcd.print("Nuit="); lcd.print(estNuit ? "O" : "N");
  lcd.print(" Auth="); lcd.print(lignes[li].autorise ? "O" : "N");
  lcd.print(" -> "); lcd.print(lignes[li].etat ? "ON " : "OFF");
  if (lignes[li].force_manuel) lcd.print("*");
  lcd.print("   ");
}
```

- [ ] **Step 5.2: Update afficherPage0()**

In `afficherPage0()`, find the line that displays plages:
```cpp
  lcd.print(" P:");
  // Afficher plage de chaque ligne compacte
  for (int i = 0; i < 3; i++) {
    lcd.print(plageActuelle(i)+1);
    if (i < 2) lcd.print("/");
  }
  if (estException(jour,mois)) lcd.print(" EXC"); else lcd.print("    ");
```

Replace with:
```cpp
  lcd.print(" N:");
  lcd.print(JOURS[indexNuitActuelle]);
  lcd.print(">");
  lcd.print(JOURS[(indexNuitActuelle+1)%7]);
  lcd.print(" ");
```

- [ ] **Step 5.3: Build both environments**

Run: `pio run`
Expected: SUCCESS for both `wokwi` and `edgebox`.

- [ ] **Step 5.4: Test on Wokwi**

Launch Wokwi simulation. Verify:
1. LCD page 0 shows `N:Lun>Mar` instead of `P:1/1/1`
2. LCD pages 1-3 show mode/ext/ral per line
3. Lines turn ON at sunset, OFF at extinction time, ON at rallumage, OFF at sunrise
4. Force buttons still work
5. MQTT status shows `night_index`, `mode`, `off`/`on` fields
6. Potentiometer accelerates time for testing transitions
7. MQTT config command with new JSON format updates planning

- [ ] **Step 5.5: Update header comment**

Replace the top comment block (lines 1-32) to reflect the new logic:

```cpp
// ============================================================
// EdgeBox-ESP-100 — Eclairage public 3 lignes
// Philosophie : MODE NUIT + IL FAIT NUIT = ALLUME
// Plateforme : Wokwi (simulation) / EdgeBox (production)
// Auteur     : Gus / Fareneit
// ============================================================
//
// LOGIQUE PAR NUIT :
//   mode 0 = eteint toute la nuit
//   mode 1 = allume toute la nuit (coucher→lever)
//   mode 2 = horaires (coucher→ext OFF, ral→lever ON)
//   (sauf si forcage manuel actif)
//
// MQTT :
//   PUB  eclairage/{site}/status   → JSON etats toutes les 2s
//   SUB  eclairage/{site}/cmd      → forcages manuels distants
//   SUB  eclairage/{site}/config   → planning complet (stocke flash)
//
// GPIO MAPPING (Wokwi) :
//   DO0=GPIO40 → Contacteur L1
//   DO1=GPIO39 → Contacteur L2
//   DO2=GPIO38 → Contacteur L3
//   DI0=GPIO5  → Bouton forcage L1
//   DI1=GPIO6  → Bouton forcage L2
//   DI2=GPIO7  → Bouton forcage L3
//   DI3=GPIO8  → Bouton page LCD
//   AI0=GPIO4  → Potentiometre acceleration temps
// ============================================================
```

---

## Task 6: Bump Version + Update CLAUDE.md

**Files:**
- Modify: `src/main.cpp` — FW_VERSION
- Modify: `CLAUDE.md`

- [ ] **Step 6.1: Bump firmware version**

Change:
```cpp
const char* FW_VERSION = "2.1.0";
```
to:
```cpp
const char* FW_VERSION = "3.0.0";
```

- [ ] **Step 6.2: Update CLAUDE.md core logic section**

Replace the "Core Logic" section in CLAUDE.md to describe the new night-based model instead of `autorisation AND nuit = allume`.

- [ ] **Step 6.3: Final build and test**

Run: `pio run`
Expected: SUCCESS for both environments.

Full Wokwi test:
1. Accelerate time with potentiometer
2. Watch L1 Voiries: ON at sunset, OFF at 23:30, ON at 05:30, OFF at sunrise (weekday)
3. Watch L1 Voiries: ON all night (weekend — mode 1)
4. Watch L3 Deco: ON at sunset, OFF at 23:00, stays OFF until sunrise (no rallumage)
5. Force button toggles work
6. MQTT Explorer shows updated JSON with mode/off/on
7. Publish config via MQTT, verify it's applied and saved

---

## Summary of Changes

| Component | Before | After |
|-----------|--------|-------|
| Data model | `Plage[4]` + `bool[7][4]` | `NuitConfig nuits[7]` (mode/ext/ral) |
| Exceptions | Single dates, always ON | Date ranges with 3 modes |
| Core logic | `autorisation AND nuit` | Night-index + mode switch |
| MQTT status | `slot[]`, `exception` | `night_index`, `mode`, `off`/`on` |
| MQTT config | `plages[]`, `planning[][]` | `nights[]` with `mode`/`off`/`on` |
| Flash | 4 plage ints + 1 uint32 bitmap | 3 ints per night (mode/ext/ral) |
| LCD detail | Plage matrix display | Mode label + ext/ral hours |
| Version | 2.1.0 | 3.0.0 |
