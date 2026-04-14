# EdgeBox-ESP-100 — Eclairage Public

Firmware ESP32-S3 pour le pilotage de **3 lignes d'eclairage public** via contacteurs, developpe par **Fareneit** (groupe Dumortier).

Simulation complete sur **Wokwi** avec MQTT reel, pret pour deploiement sur hardware **EdgeBox-ESP-100** (Seeed Studio).

---

## Logique metier

Chaque nuit (dimanche→lundi, lundi→mardi, ...) a un **mode** par ligne :

| Mode | Comportement |
|------|-------------|
| **0 — Eteint** | Pas d'allumage, meme s'il fait nuit |
| **1 — Toute la nuit** | Allume du coucher au lever, aucune coupure |
| **2 — Horaires** | Allume au coucher, eteint a `extinction`, rallume a `rallumage`, eteint au lever |

**Exemple mode 2** — nuit de lundi a mardi, coucher 17h, lever 8h, extinction 23h30, rallumage 5h30 :

```
Lundi  17:00  →  ON   (coucher + offset)
Lundi  23:30  →  OFF  (extinction programmee)
Mardi  05:30  →  ON   (rallumage programme)
Mardi  08:00  →  OFF  (lever + offset)
```

## Fonctionnalites

- **Calcul astronomique** lever/coucher du soleil (formule NOAA, precision +/-2min)
- **Heure d'ete EU** exacte (dernier dimanche mars/octobre, algorithme Tomohiko Sakamoto)
- **Planning hebdo** par ligne : 7 nuits x 3 modes, configurable a distance via MQTT
- **Exceptions calendaires** par plage de dates (Noel, 14 juillet, vacances...) avec modes configurables
- **Forcage terrain** via entrees digitales (DI) avec temporisation auto-retour 90 min
- **Forcage distant** via MQTT
- **MQTT reel** : publication JSON status toutes les 2s + reception commandes et config
- **Persistance flash** : planning sauvegarde en NVS ESP32, persiste au reboot
- **NTP** : synchronisation heure `Europe/Paris` avec gestion DST automatique
- **RTC PCF8563** : maintien heure hors reseau (production)
- **OTA HTTPS** : verification mise a jour firmware toutes les 24h
- **LCD 20x4** : 6 pages navigables par bouton (statut, detail lignes, systeme, reseau)
- **Dual build** : `env:wokwi` (simulation WiFi) / `env:edgebox` (production 4G)

## Architecture

```
          Wokwi (simulation)                    Production (EdgeBox-ESP-100)
     ┌──────────────────────┐              ┌──────────────────────────┐
     │  WiFi Wokwi-GUEST    │              │  4G LTE (SIMCom A7670G)  │
     │  Horloge simulee     │              │  NTP + RTC PCF8563       │
     │  Potentiometre x500  │              │  OTA HTTPS               │
     └──────────┬───────────┘              └──────────┬───────────────┘
                │                                     │
                └──────────────┬──────────────────────┘
                               │
                    ┌──────────┴──────────┐
                    │   Logique metier    │
                    │   (mode nuit)       │
                    │   3 lignes          │
                    │   Forcage terrain   │
                    │   LCD 20x4          │
                    │   Flash NVS         │
                    └──────────┬──────────┘
                               │
                    ┌──────────┴──────────┐
                    │   MQTT              │
                    │   PUB status (2s)   │
                    │   SUB cmd + config  │
                    └─────────────────────┘
```

## Hardware cible

| Composant | Detail |
|-----------|--------|
| Controleur | EdgeBox-ESP-100 (ESP32-S3, 16MB Flash) |
| Connectivite | 4G LTE integre (SIMCom A7670G) |
| Sorties | 6 x DO isolees 24V — 3 utilisees (contacteurs L1/L2/L3) |
| Entrees | 4 x DI isolees 24V — 4 utilisees (forcage L1/L2/L3 + page LCD) |
| RTC | PCF8563 (I2C 0x51) |
| Affichage | LCD 20x4 I2C |
| Alimentation | 10.8 – 36V DC, rail DIN |
| Cout materiel | ~415 EUR/site (vs ~1000 EUR Distech ECLYPSE) |

## Demarrage rapide

### Pre-requis

- [VS Code](https://code.visualstudio.com/) + [PlatformIO](https://platformio.org/)
- [Extension Wokwi](https://docs.wokwi.com/vscode/getting-started) (licence requise pour la simulation)
- [MQTT Explorer](https://mqtt-explorer.com/) (optionnel, pour visualiser les messages)

### Build et simulation

```bash
# Cloner
git clone https://github.com/Gus0711/ECL_Public_Edgebox_ESP_Simul.git
cd ECL_Public_Edgebox_ESP_Simul

# Build environnement simulation
pio run -e wokwi

# Lancer la simulation
# Dans VS Code : Ctrl+Shift+P → "Wokwi: Start Simulator"
```

<img width="1009" height="670" alt="image" src="https://github.com/user-attachments/assets/629920aa-0eb5-4d1c-a639-2ea72024ff41" />


### Tester le MQTT

1. Connecter MQTT Explorer a `test.mosquitto.org:1883`
2. Observer les messages sur `eclairage/reims_parc_leo/status`
3. Publier une commande de forcage sur `eclairage/reims_parc_leo/cmd` :
   ```json
   {"line": 1, "force": true, "state": true}
   ```

Voir [docs/mqtt-config-examples.md](docs/mqtt-config-examples.md) pour tous les exemples de config.

## MQTT — Topics

| Direction | Topic | Description |
|-----------|-------|-------------|
| PUB | `eclairage/{site}/status` | Etats complets JSON toutes les 2s |
| SUB | `eclairage/{site}/cmd` | Forcage distant (`{"line":1,"force":true,"state":true}`) |
| SUB | `eclairage/{site}/config` | Planning complet (sauvegarde flash auto) |

### Exemple payload status

```json
{
  "ts": "2025-12-15T23:32:00",
  "night": true,
  "sun": { "rise": "08:36", "set": "16:48" },
  "dst": false,
  "night_index": 1,
  "fw": "3.0.0",
  "lines": [
    {
      "id": "L1", "name": "Voiries", "mode": 2,
      "auth": true, "state": "ON", "force": false,
      "off": "23:30", "on": "05:30"
    }
  ]
}
```

## Structure du projet

```
├── src/main.cpp                  # Firmware (fichier unique)
├── diagram.json                  # Schema Wokwi (ESP32-S3 + LCD + boutons + LEDs)
├── platformio.ini                # Config PlatformIO (env:wokwi + env:edgebox)
├── wokwi.toml                    # Config simulation Wokwi
├── CLAUDE.md                     # Instructions pour Claude Code
├── description_projet/           # Synthese complete du projet
└── docs/
    ├── mqtt-config-examples.md   # Exemples de publish MQTT
    └── ...
```

## Simulation Wokwi

Le schema `diagram.json` inclut :

- ESP32-S3 DevKitC-1
- LCD 20x4 I2C
- 3 LEDs (contacteurs L1/L2/L3)
- 3 boutons poussoirs (forcage L1/L2/L3)
- 1 bouton page LCD
- 1 potentiometre (acceleration du temps x1 a x500)

## Licence

Firmware proprietaire Fareneit (groupe Dumortier). Tous droits reserves.

## Auteur

**Gus** — Fareneit GTB (groupe Dumortier)

Developpe en collaboration avec [Claude](https://claude.ai) (Anthropic).
