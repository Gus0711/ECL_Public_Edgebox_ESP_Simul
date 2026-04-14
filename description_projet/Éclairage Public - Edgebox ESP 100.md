## Synthèse projet — Fareneit / Dumortier

---

## 1. Objectif

Piloter **3 lignes d'éclairage public** via contacteurs, avec :

- Calcul astronomique du lever/coucher du soleil
- Planning hebdomadaire configurable par ligne (3 modes par nuit)
- Exceptions calendaires par plage de dates (jours fériés, événements, vacances)
- Forçage terrain avec temporisation auto-retour (90 min)
- Supervision et configuration à distance via MQTT → Niagara 4
- Mise à jour firmware OTA (Over-The-Air)

---

## 2. Hardware

### Contrôleur : EdgeBox-ESP-100 (Seeed Studio)

| Caractéristique | Détail |
| --- | --- |
| CPU | ESP32-S3 (512KB + 8MB RAM, 16MB Flash) |
| Connectivité | WiFi 2.4GHz, BLE 5.0, **4G LTE (SIMCom A7670G)** |
| Bus terrain | RS485 isolé, CAN bus isolé, Ethernet 100M (W5500) |
| Sorties digitales | 6 × DO isolées (24V, 1A) |
| Entrées digitales | 4 × DI isolées (24V) |
| Entrées analogiques | 4 × AI isolées (4-20mA / 0-10V, ADC 16 bits SGM58031) |
| Sorties analogiques | 2 × AO isolées (0-5V, PWM+LPF) |
| RTC | PCF8563 (I2C, 0x51) — maintien heure hors tension |
| Alimentation | 10.8 – 36V DC |
| Montage | Rail DIN 35mm |
| Boîtier | Aluminium, IP20, -20°C à +60°C |

### Affectation des I/O

| Fonction | Type | Borne EdgeBox | GPIO |
| --- | --- | --- | --- |
| Contacteur Ligne 1 | DO | DO0 | GPIO40 |
| Contacteur Ligne 2 | DO | DO1 | GPIO39 |
| Contacteur Ligne 3 | DO | DO2 | GPIO38 |
| Shunt/Forçage L1 | DI | DI0 | GPIO4 |
| Shunt/Forçage L2 | DI | DI1 | GPIO5 |
| Shunt/Forçage L3 | DI | DI2 | GPIO6 |
| Bouton page LCD | DI | DI3 | GPIO7 |

**I/O disponibles restantes** : DO3-DO5 (3 DO), AI0-AI3 (4 AI), AO0-AO1 (2 AO), RS485, CAN, Ethernet.

### Connectivité terrain

- **SIM 4G** : SIM M2M avec APN privé (Matooma ou équivalent)
- **Tunnel opérateur** : IPsec entre l'opérateur M2M et le serveur OVH
- **Fallback** : MQTT over TLS (port 8883) en direct si pas d'APN privé

---

## 3. Architecture réseau

```
EdgeBox-ESP-100          Opérateur M2M           Serveur OVH (Windows)
┌──────────────┐         ┌───────────┐           ┌──────────────────┐
│ ESP32-S3     │  4G LTE │           │  IPsec    │ Niagara 4        │
│ A7670G       │────────→│ APN Privé │──────────→│ Broker MQTT      │
│ SIM M2M      │         │ IP fixe   │           │ Serveur OTA      │
└──────────────┘         └───────────┘           │ OpenVPN (existant)│
                                                  └──────────────────┘
                                                          │
                                                    OpenVPN (existant)
                                                          │
                                                  Automates Niagara/ECLYPSE
                                                  (réseau GTB existant)
```

**Coexistence** : le réseau OpenVPN existant (Niagara ↔ automates ECLYPSE) reste inchangé. Les EdgeBox arrivent par un sous-réseau séparé via le tunnel IPsec de l'opérateur M2M.

---

## 4. Analyse fonctionnelle

### 4.1 Logique de base — Modèle par nuit

Chaque nuit (dimanche→lundi, lundi→mardi, ...) a un **mode** par ligne :

| Mode | Label | Comportement |
| --- | --- | --- |
| 0 | Éteint | Pas d'allumage, même s'il fait nuit |
| 1 | Toute la nuit | Allumé du coucher au lever, aucune coupure |
| 2 | Horaires programmés | Allumé au coucher, éteint à `extinction`, rallumé à `rallumage`, éteint au lever |

Si mode 2 sans rallumage (`rallumage = 00:00`) : extinction définitive jusqu'au lever.

Le forçage terrain (DI) ou distant (MQTT cmd) a priorité absolue sur le mode.

### 4.2 Calcul astronomique

- Algorithme NOAA simplifié (formule trigonométrique, précision ±2 min)
- Entrées : latitude, longitude, date courante
- Sorties : heure de lever, heure de coucher (en heure locale)
- **Gestion heure d'été EU exacte** : dernier dimanche de mars (2h→3h) et dernier dimanche d'octobre (3h→2h), algorithme de Tomohiko Sakamoto
- Offsets configurables : `offset_coucher` (ex: -15 min) et `offset_lever` (ex: +15 min), appliqués globalement aux 3 lignes

### 4.3 Planning hebdomadaire

Chaque ligne possède **7 configurations de nuit** indépendantes (une par nuit de la semaine).

**Index des nuits** :

| Index | Nuit |
| --- | --- |
| 0 | Dimanche soir → Lundi matin |
| 1 | Lundi soir → Mardi matin |
| 2 | Mardi soir → Mercredi matin |
| 3 | Mercredi soir → Jeudi matin |
| 4 | Jeudi soir → Vendredi matin |
| 5 | Vendredi soir → Samedi matin |
| 6 | Samedi soir → Dimanche matin |

**Exemple config L1 — Voiries** :

| Nuit | Mode | Extinction | Rallumage |
| --- | --- | --- | --- |
| Dim→Lun | 1 (toute la nuit) | — | — |
| Lun→Mar | 2 (horaires) | 23:30 | 05:30 |
| Mar→Mer | 2 (horaires) | 23:30 | 05:30 |
| ... | ... | ... | ... |
| Sam→Dim | 1 (toute la nuit) | — | — |

Résultat pour L1 un lundi : allumé au coucher (~17h), éteint à 23h30, rallumé à 05h30, éteint au lever (~8h).

### 4.4 Exceptions calendaires

**Plages de dates** qui overrident le planning hebdo pour les 3 lignes. Chaque exception a un mode (0/1/2), comme les nuits du planning.

| Plage | Mode | Description |
| --- | --- | --- |
| 24/12 → 25/12 | 1 (toute la nuit) | Noël |
| 14/07 → 14/07 | 1 (toute la nuit) | Fête nationale |
| 01/01 → 01/01 | 1 (toute la nuit) | Nouvel an |
| 26/12 → 02/01 | 0 (éteint) | Vacances — économies |
| 21/06 → 21/06 | 2 (horaires) | Fête musique — ext 01:00, ral 05:00 |

Les exceptions gèrent le chevauchement d'année (26/12 → 02/01). Maximum 10 exceptions. Liste configurable via MQTT.

### 4.5 Forçage terrain (shunt)

- Entrées DI sur le coffret (contact sec, bouton poussoir ou clé)
- **Toggle** : appui = bascule ON↔OFF
- **Temporisation 90 minutes** : le forçage s'annule automatiquement (identique au Stop Delay Distech)
- Deuxième appui = annulation immédiate du forçage
- Indicateur visuel du temps restant sur le LCD et en MQTT

### 4.6 Horloge et synchronisation

| Source | Usage |
| --- | --- |
| NTP (`pool.ntp.org`) | Synchronisation via 4G, timezone `CET-1CEST,M3.5.0/2,M10.5.0/3` |
| RTC PCF8563 | Maintien heure en cas de coupure réseau/tension |
| Stratégie | Boot → lire RTC → synchro NTP dès connexion → recaler RTC toutes les 6h |

Dérive RTC sans NTP : ~2 sec/jour → négligeable pour l'éclairage public.

---

## 5. Communication MQTT

### 5.1 Topics

| Direction | Topic | Fréquence | Description |
| --- | --- | --- | --- |
| PUB | `eclairage/{site}/status` | 2s | États complets (JSON) |
| SUB | `eclairage/{site}/cmd` | On demand | Forçages manuels distants |
| SUB | `eclairage/{site}/config` | On demand | Planning complet (stocké en flash) |

### 5.2 Payload STATUS (publié toutes les 2s)

```json
{
  "ts": "2025-12-15T23:32:00",
  "night": true,
  "sun": { "rise": "08:12", "set": "16:45" },
  "dst": true,
  "night_index": 1,
  "fw": "3.0.0",
  "lines": [
    { "id": "L1", "name": "Voiries",  "mode": 2, "auth": true,  "state": "ON",  "force": false, "off": "23:30", "on": "05:30" },
    { "id": "L2", "name": "Parkings", "mode": 2, "auth": false, "state": "OFF", "force": false, "off": "23:00", "on": "06:00" },
    { "id": "L3", "name": "Deco",     "mode": 1, "auth": true,  "state": "ON",  "force": true, "force_remaining": 45 }
  ]
}
```

- `night_index` : index de la nuit en cours (0=dim→lun, 1=lun→mar, ...)
- `mode` : mode de la nuit (0/1/2)
- `off`/`on` : heures d'extinction/rallumage (uniquement si mode 2)

### 5.3 Payload CMD (forçage distant)

```json
{ "line": 1, "force": true, "state": true }
{ "line": 1, "force": false }
```

### 5.4 Payload CONFIG (planning complet)

```json
{
  "lat": 49.2583, "lon": 3.52,
  "offset_set": -15, "offset_rise": 15,
  "lines": [
    {
      "name": "Voiries",
      "nights": [
        { "mode": 1 },
        { "mode": 2, "off": "23:30", "on": "05:30" },
        { "mode": 2, "off": "23:30", "on": "05:30" },
        { "mode": 2, "off": "23:30", "on": "05:30" },
        { "mode": 2, "off": "23:30", "on": "05:30" },
        { "mode": 2, "off": "23:30", "on": "05:30" },
        { "mode": 1 }
      ]
    }
  ],
  "exceptions": [
    { "from": "24/12", "to": "25/12", "mode": 1 },
    { "from": "14/07", "to": "14/07", "mode": 1 },
    { "from": "01/01", "to": "01/01", "mode": 1 }
  ]
}
```

- `nights[]` : 7 entrées (index 0=dim→lun ... 6=sam→dim)
- `mode` : 0=éteint, 1=toute la nuit, 2=horaires
- `off`/`on` : heures "HH:MM" (mode 2 uniquement). Si `on` absent = pas de rallumage.
- `exceptions` : plages de dates avec `from`/`to` au format "JJ/MM"

Réception → application immédiate → sauvegarde en flash (Preferences ESP32) → persiste au reboot.

---

## 6. Mise à jour OTA

- L'ESP32 vérifie périodiquement (toutes les 24h) la disponibilité d'un nouveau firmware sur un serveur HTTPS
- URL : `https://ota.datagtb.com/edgebox/{site}/firmware.bin` (Cloudflare Workers + R2)
- Vérification de version (numéro de build dans le firmware)
- Double partition OTA : rollback automatique si le nouveau firmware ne boot pas
- Aucun accès direct au device nécessaire

---

## 7. Supervision Niagara 4

Le broker MQTT (Mosquitto sur le serveur OVH) fait le pont entre les EdgeBox et Niagara 4 :

- **Niagara MQTT Driver** : subscribe aux topics `eclairage/+/status` → points BACnet/Niagara
- **Niagara → EdgeBox** : publish sur `eclairage/{site}/config` et `eclairage/{site}/cmd`
- **Graphiques** : historisation des heures ON/OFF, consommation estimée, exceptions
- **Alarmes** : perte de communication, forçage actif > 90 min (ne devrait pas arriver), contacteur en défaut

---

## 8. Comparaison avec la solution Distech existante

| Fonction | Distech ECLYPSE | EdgeBox-ESP-100 |
| --- | --- | --- |
| Calcul lever/coucher | Lookup 20 points + N° jour année | Formule NOAA trigonométrique |
| Heure d'été | `DaylightSavingStatus` device | Calcul algorithmique EU exact |
| Planning | Bloc `Schedule` natif BACnet | 3 modes × 7 nuits × 3 lignes (MQTT/flash) |
| Logique | `GreaterOrEqual` + `And` + `Or` | Mode nuit (éteint/toute nuit/horaires) |
| Forçage terrain | Toggle + Rising Edge + Stop Delay 90min | Toggle + Tempo 90min (identique) |
| Supervision | BACnet/IP natif | MQTT → Niagara (MQTT Driver) |
| Mise à jour | EC-NetAX / Niagara direct | OTA HTTPS |
| Coût hardware | ~800-1200€ (ECLYPSE + licence) | ~150€ (EdgeBox-ESP-100) |
| Connectivité | Ethernet (nécessite réseau sur site) | 4G LTE intégré (autonome) |

---

## 9. Chiffrage indicatif par site

| Poste | Coût estimé |
| --- | --- |
| EdgeBox-ESP-100 | ~150€ |
| SIM M2M (Matooma, 2 ans) | ~5€/mois = 120€ |
| Antenne 4G SMA | ~15€ |
| Alimentation 24V DIN | ~30€ |
| Coffret + câblage | ~100€ |
| **Total matériel par site** | **~415€** |
| Licence logicielle | 0€ (firmware propriétaire Fareneit) |

---

## 10. Livrables

- [x]  Simulation Wokwi fonctionnelle (validation logique v3.0.0)
- [x]  MQTT réel (PubSubClient + ArduinoJson, testé sur test.mosquitto.org)
- [x]  NTP Europe/Paris + support RTC PCF8563 (production)
- [x]  OTA HTTPS (vérification toutes les 24h)
- [x]  Dual build wokwi/edgebox avec #ifdef
- [x]  Refactoring modèle par nuit (3 modes) + exceptions par plage de dates
- [ ]  Test sur hardware EdgeBox-ESP-100 réel
- [ ]  Serveur OTA (Cloudflare Workers + R2)
- [ ]  Configuration broker MQTT production (Mosquitto sur OVH)
- [ ]  Intégration Niagara 4 (MQTT Driver + points + graphiques)
- [ ]  Documentation de déploiement terrain
- [ ]  Procédure de commissioning par site