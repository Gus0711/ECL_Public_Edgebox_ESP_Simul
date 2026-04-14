# MQTT Config — Exemples de publish

Broker : `test.mosquitto.org:1883`
Topic : `eclairage/reims_parc_leo/config`

---

## Config complete (3 lignes + exceptions)

```json
{
  "lat": 49.2583,
  "lon": 3.52,
  "offset_set": -15,
  "offset_rise": 15,
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
    },
    {
      "name": "Parkings",
      "nights": [
        { "mode": 2, "off": "23:00", "on": "06:00" },
        { "mode": 2, "off": "23:00", "on": "06:00" },
        { "mode": 2, "off": "23:00", "on": "06:00" },
        { "mode": 2, "off": "23:00", "on": "06:00" },
        { "mode": 2, "off": "23:00", "on": "06:00" },
        { "mode": 2, "off": "23:00", "on": "06:00" },
        { "mode": 2, "off": "23:00", "on": "06:00" }
      ]
    },
    {
      "name": "Deco",
      "nights": [
        { "mode": 0 },
        { "mode": 2, "off": "23:00" },
        { "mode": 2, "off": "23:00" },
        { "mode": 2, "off": "23:00" },
        { "mode": 2, "off": "23:00" },
        { "mode": 2, "off": "23:00" },
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

---

## Ordre des nuits dans `nights[]`

| Index | Nuit |
|-------|------|
| 0 | Dimanche soir → Lundi matin |
| 1 | Lundi soir → Mardi matin |
| 2 | Mardi soir → Mercredi matin |
| 3 | Mercredi soir → Jeudi matin |
| 4 | Jeudi soir → Vendredi matin |
| 5 | Vendredi soir → Samedi matin |
| 6 | Samedi soir → Dimanche matin |

---

## Les 3 modes

### Mode 0 — Eteint toute la nuit

```json
{ "mode": 0 }
```

Pas d'allumage meme s'il fait nuit. Utile pour les economies d'energie ou les periodes sans besoin.

### Mode 1 — Allume toute la nuit

```json
{ "mode": 1 }
```

Allume du coucher au lever, aucune coupure. Pour les week-ends, jours feries, zones sensibles.

### Mode 2 — Horaires programmes

Avec rallumage matin :
```json
{ "mode": 2, "off": "23:30", "on": "05:30" }
```

Cycle : coucher → ON → 23:30 OFF → 05:30 ON → lever OFF

Sans rallumage (extinction definitive) :
```json
{ "mode": 2, "off": "23:00" }
```

Cycle : coucher → ON → 23:00 OFF → reste OFF jusqu'au lever

---

## Exemples de configs partielles

### Changer seulement les offsets

```json
{
  "offset_set": -10,
  "offset_rise": 20
}
```

### Changer seulement les coordonnees GPS

```json
{
  "lat": 48.8566,
  "lon": 2.3522
}
```

### Changer le planning d'une seule ligne (L1)

```json
{
  "lines": [
    {
      "nights": [
        { "mode": 1 },
        { "mode": 2, "off": "22:00", "on": "06:00" },
        { "mode": 2, "off": "22:00", "on": "06:00" },
        { "mode": 2, "off": "22:00", "on": "06:00" },
        { "mode": 2, "off": "22:00", "on": "06:00" },
        { "mode": 2, "off": "22:00", "on": "06:00" },
        { "mode": 1 }
      ]
    }
  ]
}
```

Note : seule la ligne index 0 (L1) est modifiee. L2 et L3 gardent leur config.

---

## Exemples d'exceptions

### Noel — allume toute la nuit du 24 au 25

```json
{
  "exceptions": [
    { "from": "24/12", "to": "25/12", "mode": 1 }
  ]
}
```

### 14 juillet — horaires speciaux

```json
{
  "exceptions": [
    { "from": "14/07", "to": "14/07", "mode": 2, "off": "01:00", "on": "05:00" }
  ]
}
```

### Vacances — eteint pour economies

```json
{
  "exceptions": [
    { "from": "26/12", "to": "02/01", "mode": 0 }
  ]
}
```

### Fete de la musique + 14 juillet + Noel

```json
{
  "exceptions": [
    { "from": "21/06", "to": "21/06", "mode": 1 },
    { "from": "14/07", "to": "14/07", "mode": 1 },
    { "from": "24/12", "to": "25/12", "mode": 1 },
    { "from": "01/01", "to": "01/01", "mode": 1 }
  ]
}
```

---

## Forcage distant (topic `cmd`)

Topic : `eclairage/reims_parc_leo/cmd`

### Forcer L1 ON

```json
{ "line": 1, "force": true, "state": true }
```

### Forcer L2 OFF

```json
{ "line": 2, "force": true, "state": false }
```

### Annuler forcage L1

```json
{ "line": 1, "force": false }
```

Note : le forcage expire automatiquement apres 90 minutes.
