# Integration Production — Recap des modifications (2026-04-14)

## Fichiers modifies

### platformio.ini
- Deux environnements : `env:wokwi` (`-DWOKWI`, WiFi Wokwi-GUEST) et `env:edgebox` (`-DEDGEBOX`, lib RTC PCF8563)
- Libs partagees : LiquidCrystal_I2C, PubSubClient, ArduinoJson v7

### wokwi.toml
- Chemin firmware mis a jour vers `.pio/build/wokwi/firmware.bin`

### src/main.cpp
- `#ifdef WOKWI` / `#ifdef EDGEBOX` pour separer simulation et production
- Logique metier inchangee (`autorisation AND nuit = allume`)

## Fonctionnalites ajoutees

| Feature | Wokwi | EdgeBox |
|---------|-------|---------|
| Horloge | Simulee + potentiometre x500 | NTP Europe/Paris + RTC PCF8563 |
| Reseau | WiFi Wokwi-GUEST | WiFi (a remplacer par TinyGSM/A7670G sur vrai hardware) |
| MQTT broker | `broker.hivemq.com:1883` | `mqtt.datagtb.com:1883` |
| MQTT publish | `eclairage/{site}/status` toutes les 2s (JSON ArduinoJson) | idem |
| MQTT subscribe | `eclairage/{site}/cmd` (forcage distant) + `config` (planning complet) | idem |
| OTA | Check au boot (echoue proprement, pas de serveur) | HTTPS `ota.datagtb.com` toutes les 24h |
| RTC | -- | PCF8563 I2C 0x51, lecture au boot, recalibration NTP toutes les 6h |
| Version firmware | v2.1.0 affichee sur LCD + dans JSON MQTT | idem |

## MQTT — Topics et payloads

**Publish** `eclairage/reims_parc_leo/status` (toutes les 2s) :
```json
{
  "ts": "2025-12-15T23:32:00",
  "night": true,
  "sun": {"rise": "08:12", "set": "16:45"},
  "dst": true,
  "slot": [3, 2, 2],
  "exception": false,
  "fw": "2.1.0",
  "lines": [
    {"id": "L1", "name": "Voiries", "auth": true, "state": "ON", "force": false},
    {"id": "L2", "name": "Parkings", "auth": false, "state": "OFF", "force": false},
    {"id": "L3", "name": "Deco", "auth": true, "state": "ON", "force": true, "force_remaining": 45}
  ]
}
```

**Subscribe** `eclairage/reims_parc_leo/cmd` (forcage distant) :
```json
{"line": 1, "force": true, "state": true}
{"line": 1, "force": false}
```

**Subscribe** `eclairage/reims_parc_leo/config` (planning complet, sauvegarde flash auto) :
```json
{
  "lat": 49.2583, "lon": 3.52,
  "offset_set": -15, "offset_rise": 15,
  "lines": [
    {
      "name": "Voiries",
      "plages": [{"h":0,"m":0}, {"h":5,"m":30}, {"h":22,"m":0}, {"h":23,"m":30}],
      "planning": [[true,true,true,true], [false,true,true,false], ...]
    }
  ],
  "exceptions": [{"m":12,"d":24}, {"m":12,"d":25}, {"m":7,"d":14}, {"m":1,"d":1}]
}
```

## LCD — Indicateurs ajoutes

- Page 0 (statut) : "M" en position 19,0 quand MQTT connecte
- Page 4 (systeme) : version firmware, "NET"/"---" (reseau), "MQTT"/"     " (broker)

## Comment tester sur Wokwi

1. `pio run -e wokwi`
2. Lancer la simulation Wokwi dans VS Code
3. Verifier dans le Serial : WiFi connect, MQTT connect a `broker.hivemq.com`, JSON status
4. Avec MQTT Explorer, connecter a `test.mosquitto.org:1883`, subscriber a `eclairage/reims_parc_leo/#`
5. Tester le forcage distant : publier `{"line":1,"force":true,"state":true}` sur `eclairage/reims_parc_leo/cmd`
6. Verifier que L1 passe en forcage ON sur le LCD et les LEDs

## Reste a faire

- [ ] Tester sur Wokwi (WiFi + MQTT + JSON)
- [ ] Remplacer WiFi par TinyGSM/A7670G dans `#ifdef EDGEBOX` quand le hardware est disponible
- [ ] Configurer le broker Mosquitto sur le serveur OVH
- [ ] Deployer le serveur OTA (Cloudflare Workers + R2)
- [ ] Tester sur EdgeBox-ESP-100 reel
