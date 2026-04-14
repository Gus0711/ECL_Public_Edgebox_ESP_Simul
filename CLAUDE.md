# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Public street lighting controller firmware for ESP32-S3 (EdgeBox-ESP-100) by Fareneit (groupe Dumortier). Built with PlatformIO and the Arduino framework. Currently a working prototype on Wokwi; transitioning to production hardware. The project is written in French — variable names, comments, and domain terms are all in French.

The complete project specification is in `description_projet/Éclairage Public - Edgebox ESP 100.md` (hardware, network architecture, MQTT payloads, OTA, functional analysis).

## Build & Upload

```bash
# Build
pio run

# Upload to device
pio run --target upload

# Serial monitor (115200 baud)
pio device monitor

# Clean build
pio run --target clean
```

Simulation runs via the Wokwi VS Code extension using `diagram.json` and `wokwi.toml`.

## Architecture

Single-file firmware in `src/main.cpp`. No custom libraries in `lib/` or `include/`.

### Core Logic: `autorisation AND nuit = allumé`

A lighting line turns ON only when **both** conditions are met:
1. The current time slot (plage) is authorized for today's day-of-week in the line's planning
2. It is nighttime (calculated from astronomical sunset/sunrise + configurable offsets)

Manual override (forçage) bypasses this logic with a 90-minute auto-expiry timer.

### Three Independent Lighting Lines

Each line (`LigneConfig`) has its own:
- 4 time slot boundaries (plages) — not shared across lines
- 7×4 weekly authorization matrix (`planning[jour][plage]`)

Lines: L1=Voiries, L2=Parkings, L3=Décoratif.

### Key Subsystems

- **Astronomical calculation** (`calculerSoleil`): Sunrise/sunset from GPS coords with EU DST rules and equation-of-time correction. France timezone (UTC+1/+2).
- **Simulated clock** (`avancerTemps`): Time acceleration controlled by potentiometer (1x–500x) for testing. On real hardware, would use RTC/NTP.
- **Exception dates**: Specific dates (holidays) override all planning to ON for all lines.
- **Flash persistence** (`Preferences`): Planning and GPS config saved/loaded via ESP32 NVS under namespace `"eclairage"`.
- **MQTT (stub)**: JSON status published to Serial every 2s. Topic structure `eclairage/{site}/status|cmd|config` is defined in comments but not connected (no WiFi on Wokwi).
- **LCD display**: 20×4 I2C LCD with 3 rotating pages (status, line detail, system info), 4s per page.

### GPIO Mapping — Wokwi vs Production

The Wokwi simulation uses different GPIO for DI inputs than the real EdgeBox-ESP-100:

| Function | EdgeBox Production | Wokwi Simulation |
|----------|-------------------|------------------|
| DO0–DO2 (contactors L1–L3) | GPIO 40, 39, 38 | GPIO 40, 39, 38 (same) |
| DI0 (force L1) | GPIO 4 | GPIO 5 |
| DI1 (force L2) | GPIO 5 | GPIO 6 |
| DI2 (force L3) | GPIO 6 | GPIO 7 |
| AI0 (time speed pot) | — | GPIO 4 |
| I2C SDA/SCL (LCD) | 11, 12 | 11, 12 (same) |
| RTC PCF8563 | I2C 0x51 | — (not in Wokwi) |

Use `#ifdef` or build flags to handle the divergence when adding production support.

## Dependencies

- `marcoschwartz/LiquidCrystal_I2C@^1.1.4` (managed by PlatformIO)
- ESP32 `Preferences.h` (built-in NVS)

Planned production dependencies (not yet integrated): PubSubClient (MQTT), NTP client, PCF8563 RTC driver, HTTPUpdate (OTA).

## Production Target Architecture

- **Connectivity**: 4G LTE via SIMCom A7670G module (SIM M2M, APN privé Matooma) → IPsec tunnel → serveur OVH
- **MQTT broker**: Mosquitto on OVH server, bridged to Niagara 4 (MQTT Driver)
- **NTP**: `pool.ntp.org` via 4G, timezone `CET-1CEST,M3.5.0/2,M10.5.0/3`
- **RTC**: PCF8563 at I2C 0x51 — boot reads RTC, NTP recalibrates RTC every 6h
- **OTA**: HTTPS pull from `ota.datagtb.com/edgebox/{site}/firmware.bin` every 24h, dual-partition with rollback
- **Supervision**: Niagara 4 subscribes `eclairage/+/status`, publishes on `cmd` and `config` topics

## Critical Rules

- **Never break the core logic**: `autorisation AND nuit = allumé` is the validated business rule. All additions (MQTT, NTP, OTA) are peripheral to this.
- **Keep Wokwi compatibility**: The simulation must remain functional for testing. Use `#ifdef WOKWI` or PlatformIO build environments to separate simulation-specific code from production code.

## Language & Naming

All code uses French naming: `estNuit`, `plageActuelle`, `jourSemaine`, `leverMin`/`coucherMin`, `gererBoutons`, `calculerEtats`, etc. Maintain this convention.
