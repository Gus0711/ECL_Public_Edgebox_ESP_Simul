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

// Forward declarations
struct NuitConfig;
struct LigneConfig;
struct ExceptionConfig;
void gererBoutons();
void afficherPage0();
void afficherLigne(int li);
void afficherPage2();
void afficherPageReseau();
void sauvegarderConfig();
extern const char* JOURS[];

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <math.h>

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPUpdate.h>
#ifdef EDGEBOX
#include <pcf8563.h>
#endif

LiquidCrystal_I2C lcd(0x27, 20, 4);
Preferences prefs;

// ── MQTT Configuration ──────────────────────────────────────
#ifdef WOKWI
const char* MQTT_SERVER = "test.mosquitto.org";
#else
const char* MQTT_SERVER = "mqtt.datagtb.com";  // broker Mosquitto OVH
#endif
const int   MQTT_PORT   = 1883;

char topicStatus[64];
char topicCmd[64];
char topicConfig[64];

// ── OTA Configuration ───────────────────────────────────────
const char* FW_VERSION = "3.0.0";
char OTA_URL[128];
const unsigned long INTERVAL_OTA = 24UL * 3600UL * 1000UL;  // 24h
unsigned long dernierCheckOTA = 0;

WiFiClient espClient;
PubSubClient mqtt(espClient);
bool connecte = false;  // true si connecte au reseau (WiFi ou 4G)

#ifdef EDGEBOX
PCF8563_Class rtc;
unsigned long dernierRecalRTC = 0;
const unsigned long INTERVAL_RECAL_RTC = 6UL * 3600UL * 1000UL;  // 6h
#endif

// ════════════════════════════════════════════════════════════
//  CONFIGURATION — MODIFIABLE PAR MQTT OU EN DUR ICI
// ════════════════════════════════════════════════════════════

// ── Identifiant site (pour topics MQTT) ─────────────────────
const char* SITE_ID = "reims_parc_leo";

// ── Coordonnées GPS ─────────────────────────────────────────
float cfg_latitude  = 49.2583;   // Reims
float cfg_longitude =  3.5200;

// ── Offset allumage/extinction (minutes) ────────────────────
// Appliqué au coucher/lever pour toutes les lignes
int cfg_offset_coucher = -15;  // Allumage 15min AVANT coucher
int cfg_offset_lever   =  15;  // Extinction 15min APRÈS lever

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

#ifdef WOKWI
// ════════════════════════════════════════════════════════════
//  DATE/HEURE SIMULEE (Wokwi)
// ════════════════════════════════════════════════════════════
int annee  = 2025;
int mois   = 12;
int jour   = 15;     // 15 décembre
int heure  = 15;
int minute = 30;
int seconde = 0;
int jourSemaine = 1; // 0=dim, 1=lun
#else
// ════════════════════════════════════════════════════════════
//  DATE/HEURE REELLE (production)
// ════════════════════════════════════════════════════════════
int annee, mois, jour, heure, minute, seconde;
int jourSemaine;
#endif

// ════════════════════════════════════════════════════════════
//  GPIO
// ════════════════════════════════════════════════════════════

const int PIN_LED[3]  = { 40, 39, 38 };  // DO0, DO1, DO2
const int PIN_BTN[3]  = { 5, 6, 7 };     // DI0, DI1, DI2
#ifdef WOKWI
const int PIN_BTN_PAGE = 8;              // DI3 Wokwi
const int PIN_POT     = 4;               // AI0 — potentiometre acceleration temps
#else
const int PIN_BTN_PAGE = 7;              // DI3 EdgeBox production
#endif

// ════════════════════════════════════════════════════════════
//  ÉTAT DES LIGNES
// ════════════════════════════════════════════════════════════

struct LigneEtat {
  bool etat;           // état réel du contacteur
  bool autorise;       // autorisation planning courante
  bool nuit;           // il fait nuit (commun mais stocké ici pour MQTT)
  bool force_manuel;   // forçage actif
  bool force_etat;     // état forcé
  bool btn_prec;       // anti-rebond bouton
  unsigned long force_debut_ms;  // timestamp début forçage (millis)
  int force_restant_min;         // minutes restantes (pour affichage)
};

// Durée de temporisation forçage (minutes) — comme le Stop Delay 90min Distech
const int TEMPO_FORCAGE_MIN = 90;

LigneEtat lignes[3] = {
  { false, false, false, false, false, true, 0, 0 },
  { false, false, false, false, false, true, 0, 0 },
  { false, false, false, false, false, true, 0, 0 },
};

// Noms des lignes accessibles via lignesCfg[i].nom

// ════════════════════════════════════════════════════════════
//  VARIABLES GLOBALES
// ════════════════════════════════════════════════════════════

int leverMin   = 0;    // lever en minutes depuis minuit
int coucherMin = 0;    // coucher en minutes depuis minuit
bool estNuit   = false;
#ifdef WOKWI
float vitesseTemps = 1.0;
#else
const float vitesseTemps = 1.0;  // pas d'acceleration en production
#endif

unsigned long dernierTick = 0;
unsigned long dernierAffichage = 0;
unsigned long dernierMQTT = 0;
int pageAffichage = 0;
bool btnPagePrec = true;  // anti-rebond bouton page

// ════════════════════════════════════════════════════════════
//  CALCUL ASTRONOMIQUE
// ════════════════════════════════════════════════════════════

int jourDeLAnnee(int j, int m, int a) {
  int t[] = {0,31,59,90,120,151,181,212,243,273,304,334};
  int n = t[m-1] + j;
  if (m > 2 && (a%4==0 && (a%100!=0 || a%400==0))) n++;
  return n;
}

// ── Calcul exact heure d'été (règle UE) ─────────────────────
// Heure d'été = dernier dimanche de mars 2h UTC → dernier dimanche d'octobre 3h UTC
// Algorithme : trouver le dernier dimanche d'un mois donné
// Formule de Zeller simplifiée pour le jour de la semaine

int jourDeLaSemaine(int j, int m, int a) {
  // Retourne 0=dimanche, 1=lundi, ... 6=samedi
  // Algorithme de Tomohiko Sakamoto
  int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) a--;
  return (a + a/4 - a/100 + a/400 + t[m-1] + j) % 7;
}

int dernierDimanche(int mois_cible, int annee_cible) {
  // Retourne le jour (1-31) du dernier dimanche du mois donné
  int joursMax[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (annee_cible%4==0 && (annee_cible%100!=0 || annee_cible%400==0)) joursMax[1] = 29;
  int dernier = joursMax[mois_cible - 1];
  int dow = jourDeLaSemaine(dernier, mois_cible, annee_cible);  // jour de la semaine du dernier jour
  // Reculer jusqu'au dimanche (dow=0)
  return dernier - dow;
}

bool estHeureDEte(int j, int m, int a) {
  // Règle UE : heure d'été du dernier dimanche de mars au dernier dimanche d'octobre
  if (m < 3 || m > 10) return false;   // jan-fév, nov-déc → hiver
  if (m > 3 && m < 10) return true;    // avr-sept → été

  int dimMars = dernierDimanche(3, a);
  int dimOct  = dernierDimanche(10, a);

  if (m == 3) return (j >= dimMars);    // mars : été à partir du dernier dimanche
  // m == 10
  return (j < dimOct);                  // octobre : été AVANT le dernier dimanche
}

void calculerSoleil(int j, int m, int a, float lat, float lon,
                    int &lev, int &cou) {
  float rad = M_PI / 180.0;
  int N = jourDeLAnnee(j, m, a);

  float decl = -23.45 * cos(rad * 360.0 / 365.0 * (N + 10));
  float B = rad * 360.0 / 365.0 * (N - 81);
  float eot = 9.87*sin(2*B) - 7.53*cos(B) - 1.5*sin(B);

  float latR = lat * rad;
  float decR = decl * rad;
  float cosW = (cos(90.833*rad) - sin(latR)*sin(decR)) / (cos(latR)*cos(decR));

  if (cosW > 1.0)  { lev = -1; cou = -1; return; }  // nuit polaire
  if (cosW < -1.0) { lev = 0;  cou = 1440; return; } // jour polaire

  float omega = acos(cosW) / rad;
  float midi = 720.0 - 4.0*lon - eot;

  // Fuseau France : UTC+1 hiver, UTC+2 été
  // Règle EU exacte via estHeureDEte()
  int fuseau = estHeureDEte(j, m, a) ? 120 : 60;

  lev = (int)(midi - omega*4.0 + fuseau + 0.5);
  cou = (int)(midi + omega*4.0 + fuseau + 0.5);

  if (lev < 0) lev += 1440;
  if (cou > 1440) cou -= 1440;
}

// ════════════════════════════════════════════════════════════
//  GESTION DU TEMPS
// ════════════════════════════════════════════════════════════

int minutesDuJour() { return heure * 60 + minute; }

#ifdef WOKWI
void avancerTemps(float dtReel) {
  seconde += (int)(dtReel * vitesseTemps);
  while (seconde >= 60) { seconde -= 60; minute++; }
  while (minute >= 60)  { minute -= 60;  heure++; }
  while (heure >= 24) {
    heure -= 24;
    jour++;
    jourSemaine = (jourSemaine + 1) % 7;
    int jmax[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (annee%4==0) jmax[1] = 29;
    if (jour > jmax[mois-1]) {
      jour = 1; mois++;
      if (mois > 12) { mois = 1; annee++; }
    }
    calculerSoleil(jour, mois, annee, cfg_latitude, cfg_longitude, leverMin, coucherMin);
  }
}
#endif // WOKWI

// ════════════════════════════════════════════════════════════
//  LOGIQUE ÉCLAIRAGE — MODELE PAR NUIT
// ════════════════════════════════════════════════════════════

// Verifier si une date est dans une plage (gere le chevauchement d'annee)
bool dateEstDansPlage(int j, int m, const ExceptionConfig &exc) {
  int d = m * 100 + j;
  int from = exc.from_mois * 100 + exc.from_jour;
  int to = exc.to_mois * 100 + exc.to_jour;
  if (from <= to) {
    return (d >= from && d <= to);
  } else {
    return (d >= from || d <= to);  // chevauchement annee (26/12→02/01)
  }
}

// Config de nuit applicable (exception prioritaire sur planning hebdo)
NuitConfig getNuitConfig(int ligne, int indexNuit, int j, int m) {
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
  return lignesCfg[ligne].nuits[indexNuit];
}

int indexNuitActuelle = 0;

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

  // Index de la nuit (quel soir a commence cette nuit)
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
      lignes[i].autorise = true;
      digitalWrite(PIN_LED[i], lignes[i].etat ? HIGH : LOW);
      continue;
    }

    if (!estNuit) {
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
          if (ext > finNuit) {
            dansCreneauOff = (now >= ext || now < finNuit);
          } else {
            dansCreneauOff = (now >= ext);
          }
        } else {
          if (ext > ral) {  // ext=23:00, ral=06:00 → chevauche minuit
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

// ════════════════════════════════════════════════════════════
//  BOUTONS (toggle forçage)
// ════════════════════════════════════════════════════════════

void gererBoutons() {
  unsigned long now = millis();

  for (int i = 0; i < 3; i++) {
    bool btn = (digitalRead(PIN_BTN[i]) == LOW);

    // Toggle au front montant
    if (btn && !lignes[i].btn_prec) {
      if (lignes[i].force_manuel) {
        // Déjà en forçage → annuler
        lignes[i].force_manuel = false;
      } else {
        // Activer forçage + démarrer tempo
        lignes[i].force_manuel = true;
        lignes[i].force_etat = !lignes[i].etat;
        lignes[i].force_debut_ms = now;
      }
    }
    lignes[i].btn_prec = btn;

    // Temporisation : auto-annulation après TEMPO_FORCAGE_MIN minutes
    // On utilise le temps réel (millis) pas le temps simulé, pour que
    // la tempo soit testable même en accéléré.
    // En production (vrai EdgeBox) : vitesseTemps = 1, donc c'est identique.
    if (lignes[i].force_manuel) {
      unsigned long elapsed_ms = now - lignes[i].force_debut_ms;
      // En simulation, la tempo est accélérée par vitesseTemps
      float elapsed_min = (elapsed_ms / 60000.0) * vitesseTemps;
      lignes[i].force_restant_min = TEMPO_FORCAGE_MIN - (int)elapsed_min;

      if (lignes[i].force_restant_min <= 0) {
        lignes[i].force_manuel = false;
        lignes[i].force_restant_min = 0;
        Serial.print("[TEMPO] Forcage L"); Serial.print(i+1);
        Serial.println(" expire apres 90min");
      }
    }
  }
}

// ════════════════════════════════════════════════════════════
//  RESEAU — WiFi (Wokwi) / 4G (production)
// ════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════
//  NTP — SYNCHRONISATION HEURE
// ════════════════════════════════════════════════════════════

void syncNTP() {
  if (!connecte) return;
  Serial.println("[NTP] Synchronisation...");
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
    jourSemaine = timeinfo.tm_wday;  // 0=dimanche

    calculerSoleil(jour, mois, annee, cfg_latitude, cfg_longitude, leverMin, coucherMin);

    Serial.print("[NTP] OK: ");
    Serial.print(jour); Serial.print("/"); Serial.print(mois); Serial.print("/"); Serial.print(annee);
    Serial.print(" "); Serial.print(heure); Serial.print(":"); Serial.print(minute);
    Serial.print(" ("); Serial.print(JOURS[jourSemaine]); Serial.println(")");
  } else {
    Serial.println("[NTP] ECHEC — heure non synchronisee");
  }
}

#ifdef EDGEBOX
void lireRTC() {
  RTC_Date dt = rtc.getDateTime();
  annee = dt.year;
  mois = dt.month;
  jour = dt.day;
  heure = dt.hour;
  minute = dt.minute;
  seconde = dt.second;
  jourSemaine = jourDeLaSemaine(jour, mois, annee);  // calculer car PCF8563 ne le stocke pas de maniere fiable
  calculerSoleil(jour, mois, annee, cfg_latitude, cfg_longitude, leverMin, coucherMin);
  Serial.print("[RTC] Lu: ");
  Serial.print(jour); Serial.print("/"); Serial.print(mois); Serial.print("/"); Serial.print(annee);
  Serial.print(" "); Serial.print(heure); Serial.print(":"); Serial.println(minute);
}

void ecrireRTC() {
  rtc.setDateTime(annee, mois, jour, heure, minute, seconde);
  Serial.println("[RTC] Recalibree depuis NTP");
}
#endif

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

    if (jour != ancienJour) {
      calculerSoleil(jour, mois, annee, cfg_latitude, cfg_longitude, leverMin, coucherMin);
    }
  }
}
#endif

// ════════════════════════════════════════════════════════════
//  MQTT — PUBLICATION & RECEPTION
// ════════════════════════════════════════════════════════════

void construireTopics() {
  snprintf(topicStatus, sizeof(topicStatus), "eclairage/%s/status", SITE_ID);
  snprintf(topicCmd,    sizeof(topicCmd),    "eclairage/%s/cmd",    SITE_ID);
  snprintf(topicConfig, sizeof(topicConfig), "eclairage/%s/config", SITE_ID);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
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
    // Planning complet depuis Niagara (format nuits)
    if (doc["lat"].is<float>()) cfg_latitude = doc["lat"];
    if (doc["lon"].is<float>()) cfg_longitude = doc["lon"];
    if (doc["offset_set"].is<int>()) cfg_offset_coucher = doc["offset_set"];
    if (doc["offset_rise"].is<int>()) cfg_offset_lever = doc["offset_rise"];

    JsonArray jLines = doc["lines"];
    for (int l = 0; l < 3 && l < (int)jLines.size(); l++) {
      JsonObject jl = jLines[l];
      JsonArray jNights = jl["nights"];
      for (int n = 0; n < 7 && n < (int)jNights.size(); n++) {
        JsonObject jn = jNights[n];
        lignesCfg[l].nuits[n].mode = jn["mode"] | 0;
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
      const char* fromStr = je["from"];
      const char* toStr = je["to"];
      if (fromStr && toStr) {
        sscanf(fromStr, "%d/%d", &exceptions[i].from_jour, &exceptions[i].from_mois);
        sscanf(toStr, "%d/%d", &exceptions[i].to_jour, &exceptions[i].to_mois);
        exceptions[i].mode = je["mode"] | 1;
        const char* eStr = je["off"];
        if (eStr) { sscanf(eStr, "%d:%d", &exceptions[i].ext_h, &exceptions[i].ext_m); }
        else { exceptions[i].ext_h = 0; exceptions[i].ext_m = 0; }
        const char* rStr = je["on"];
        if (rStr) { sscanf(rStr, "%d:%d", &exceptions[i].ral_h, &exceptions[i].ral_m); }
        else { exceptions[i].ral_h = 0; exceptions[i].ral_m = 0; }
        nbExceptions++;
      }
    }

    calculerSoleil(jour, mois, annee, cfg_latitude, cfg_longitude, leverMin, coucherMin);
    sauvegarderConfig();
    Serial.println("[MQTT] Config appliquee et sauvegardee");
  }
}

void connecterMQTT() {
  if (!connecte) return;
  if (mqtt.connected()) return;

  Serial.print("[MQTT] Connexion a "); Serial.print(MQTT_SERVER); Serial.print("...");

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

void publierStatus() {
  JsonDocument doc;
  char ts[24];
  snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02d",
           annee, mois, jour, heure, minute, seconde);
  doc["ts"] = ts;
  doc["night"] = estNuit;

  JsonObject sun = doc["sun"].to<JsonObject>();
  char rise[6], set_str[6];
  snprintf(rise, sizeof(rise), "%02d:%02d", leverMin/60, leverMin%60);
  snprintf(set_str, sizeof(set_str), "%02d:%02d", coucherMin/60, coucherMin%60);
  sun["rise"] = rise;
  sun["set"] = set_str;

  doc["dst"] = estHeureDEte(jour, mois, annee);

  doc["night_index"] = indexNuitActuelle;
  doc["fw"] = FW_VERSION;

  JsonArray jLines = doc["lines"].to<JsonArray>();
  for (int i = 0; i < 3; i++) {
    JsonObject jl = jLines.add<JsonObject>();
    char id[4];
    snprintf(id, sizeof(id), "L%d", i+1);
    jl["id"] = id;
    jl["name"] = lignesCfg[i].nom;
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
  }

  char buffer[512];
  serializeJson(doc, buffer, sizeof(buffer));

  if (mqtt.connected()) {
    bool ok = mqtt.publish(topicStatus, buffer);
    if (!ok) Serial.println("[MQTT] ERREUR publish status");
  }
  Serial.println(buffer);
}

// ════════════════════════════════════════════════════════════
//  OTA — MISE A JOUR FIRMWARE
// ════════════════════════════════════════════════════════════

void verifierOTA() {
  if (!connecte) return;

  Serial.print("[OTA] Verification v");
  Serial.print(FW_VERSION);
  Serial.print(" sur ");
  Serial.println(OTA_URL);

  WiFiClient otaClient;
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  t_httpUpdate_return ret = httpUpdate.update(otaClient, OTA_URL, FW_VERSION);

  switch (ret) {
    case HTTP_UPDATE_OK:
      Serial.println("[OTA] Mise a jour OK — redemarrage...");
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

// ════════════════════════════════════════════════════════════
//  FLASH — SAUVEGARDE CONFIG (Preferences ESP32)
// ════════════════════════════════════════════════════════════

void sauvegarderConfig() {
  prefs.begin("eclairage", false);
  prefs.putFloat("lat", cfg_latitude);
  prefs.putFloat("lon", cfg_longitude);
  prefs.putInt("off_cou", cfg_offset_coucher);
  prefs.putInt("off_lev", cfg_offset_lever);

  for (int l = 0; l < 3; l++) {
    for (int n = 0; n < 7; n++) {
      char key[10];
      snprintf(key, sizeof(key), "m%d_%d", l, n);
      prefs.putInt(key, lignesCfg[l].nuits[n].mode);
      snprintf(key, sizeof(key), "e%d_%d", l, n);
      prefs.putInt(key, lignesCfg[l].nuits[n].ext_h * 60 + lignesCfg[l].nuits[n].ext_m);
      snprintf(key, sizeof(key), "r%d_%d", l, n);
      prefs.putInt(key, lignesCfg[l].nuits[n].ral_h * 60 + lignesCfg[l].nuits[n].ral_m);
    }
  }

  prefs.putInt("nbExc", nbExceptions);
  for (int i = 0; i < nbExceptions; i++) {
    char key[10];
    snprintf(key, sizeof(key), "xf%d", i);
    prefs.putInt(key, exceptions[i].from_mois * 100 + exceptions[i].from_jour);
    snprintf(key, sizeof(key), "xt%d", i);
    prefs.putInt(key, exceptions[i].to_mois * 100 + exceptions[i].to_jour);
    snprintf(key, sizeof(key), "xm%d", i);
    prefs.putInt(key, exceptions[i].mode);
    snprintf(key, sizeof(key), "xe%d", i);
    prefs.putInt(key, exceptions[i].ext_h * 60 + exceptions[i].ext_m);
    snprintf(key, sizeof(key), "xr%d", i);
    prefs.putInt(key, exceptions[i].ral_h * 60 + exceptions[i].ral_m);
  }

  prefs.end();
  Serial.println("[FLASH] Config sauvegardee");
}

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

// ════════════════════════════════════════════════════════════
//  AFFICHAGE LCD
// ════════════════════════════════════════════════════════════

void lcdHM(int h, int m) {
  if (h<10) lcd.print("0"); lcd.print(h);
  lcd.print(":"); if (m<10) lcd.print("0"); lcd.print(m);
}

const char* JOURS[] = {"Dim","Lun","Mar","Mer","Jeu","Ven","Sam"};

void afficherPage0() {
  // Page principale
  lcd.setCursor(0, 0);
  if (jour<10) lcd.print("0"); lcd.print(jour);
  lcd.print("/"); if (mois<10) lcd.print("0"); lcd.print(mois);
  lcd.print(" "); lcd.print(JOURS[jourSemaine]);
  lcd.print(" "); lcdHM(heure, minute);
  lcd.print(":"); if (seconde<10) lcd.print("0"); lcd.print(seconde);
  lcd.setCursor(19, 0);
  lcd.print(mqtt.connected() ? "M" : " ");

  lcd.setCursor(0, 1);
  lcd.print("Lev "); lcdHM(leverMin/60, leverMin%60);
  lcd.print(" Cou "); lcdHM(coucherMin/60, coucherMin%60);

  lcd.setCursor(0, 2);
  lcd.print("Nuit="); lcd.print(estNuit ? "OUI" : "NON");
  lcd.print(" N:");
  lcd.print(JOURS[indexNuitActuelle]);
  lcd.print(">");
  lcd.print(JOURS[(indexNuitActuelle+1)%7]);
  lcd.print(" ");

  lcd.setCursor(0, 3);
  for (int i = 0; i < 3; i++) {
    lcd.print("L"); lcd.print(i+1); lcd.print("=");
    lcd.print(lignes[i].etat ? "ON " : "OFF");
    if (lignes[i].force_manuel) {
      lcd.print(lignes[i].force_restant_min);
      lcd.print("m");
    } else {
      lcd.print("  ");
    }
  }
}

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

void afficherPage2() {
  // Infos système
  lcd.setCursor(0, 0);
  lcd.print(SITE_ID);
  lcd.print(" v");
  lcd.print(FW_VERSION);

  lcd.setCursor(0, 1);
  lcd.print("GPS ");
  lcd.print(cfg_latitude, 2);
  lcd.print(" ");
  lcd.print(cfg_longitude, 2);

  lcd.setCursor(0, 2);
  lcd.print("Off: cou=");
  lcd.print(cfg_offset_coucher);
  lcd.print(" lev=+");
  lcd.print(cfg_offset_lever);

  lcd.setCursor(0, 3);
  bool dst = estHeureDEte(jour, mois, annee);
  lcd.print(dst ? "UTC+2 ETE " : "UTC+1 HIV ");
#ifdef WOKWI
  lcd.print("x");
  lcd.print((int)vitesseTemps);
#endif
  lcd.print(connecte ? " NET" : " ---");
  lcd.print(mqtt.connected() ? " MQTT" : "     ");
  lcd.print("   ");
}

void afficherPageReseau() {
  // Page reseau/MQTT
  lcd.setCursor(0, 0);
#ifdef WOKWI
  lcd.print("WiFi: ");
  if (WiFi.status() == WL_CONNECTED) {
    lcd.print(WiFi.localIP());
  } else {
    lcd.print("DECONNECTE      ");
  }
#else
  lcd.print("4G: ");
  lcd.print(connecte ? "CONNECTE" : "DECONNECTE");
  lcd.print("        ");
#endif

  lcd.setCursor(0, 1);
  lcd.print("MQTT: ");
  lcd.print(mqtt.connected() ? "CONNECTE" : "DECONNECTE");
  lcd.print("      ");

  lcd.setCursor(0, 2);
  lcd.print("Broker: ");
  lcd.print(MQTT_SERVER);

  lcd.setCursor(0, 3);
  lcd.print("Pub: ");
  lcd.print(topicStatus);
}

// ════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("=============================================");
  Serial.println("  EdgeBox-ESP-100 — Eclairage public v2");
  Serial.println("  Autorisation + Nuit = Allume");
  Serial.println("=============================================");

  // Charger config flash
  chargerConfig();

  // I2C LCD
  Wire.begin(11, 12);
  lcd.init();
  lcd.backlight();

  // Splash
  lcd.setCursor(2, 0);
  lcd.print("EDGEBOX-ESP-100");
  lcd.setCursor(3, 1);
  lcd.print("FARENEIT  GTB");
  lcd.setCursor(1, 2);
  lcd.print("Eclairage public");
  lcd.setCursor(0, 3);
  lcd.print("Auth+Nuit = Allume");
  delay(2000);
  lcd.clear();

#ifdef WOKWI
  // ADC pour potentiometre
  analogReadResolution(12);
#endif

  // Sorties
  for (int i = 0; i < 3; i++) {
    pinMode(PIN_LED[i], OUTPUT);
    digitalWrite(PIN_LED[i], LOW);
  }

  // Entrées boutons
  for (int i = 0; i < 3; i++) {
    pinMode(PIN_BTN[i], INPUT_PULLUP);
  }
  pinMode(PIN_BTN_PAGE, INPUT_PULLUP);

  // Calcul initial lever/coucher
  calculerSoleil(jour, mois, annee, cfg_latitude, cfg_longitude, leverMin, coucherMin);

  Serial.print("Lever: "); Serial.print(leverMin/60); Serial.print(":"); Serial.println(leverMin%60);
  Serial.print("Coucher: "); Serial.print(coucherMin/60); Serial.print(":"); Serial.println(coucherMin%60);

  // RTC au boot (production) — heure disponible immediatement
#ifdef EDGEBOX
  rtc.begin();
  lireRTC();
#endif

  // Connexion reseau
#ifdef WOKWI
  connecterWiFi();
#endif

  // NTP (en production, synchro heure reelle)
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

  // MQTT
  construireTopics();
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(1024);

  // OTA URL
  snprintf(OTA_URL, sizeof(OTA_URL), "https://ota.datagtb.com/edgebox/%s/firmware.bin", SITE_ID);

  Serial.println();
}

// ════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════

void loop() {
  unsigned long now = millis();

#ifdef WOKWI
  // 1. VITESSE DU TEMPS (simulation)
  vitesseTemps = 1.0 + (analogRead(PIN_POT) / 4095.0) * 499.0;

  // 2. AVANCER LE TEMPS (simulation)
  if (now - dernierTick >= 100) {
    float dt = (now - dernierTick) / 1000.0;
    dernierTick = now;
    avancerTemps(dt);
  }
#else
  // 2. LIRE L'HEURE REELLE (production)
  if (now - dernierTick >= 1000) {
    dernierTick = now;
    mettreAJourHeure();
  }
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
#endif

  // 3. BOUTONS
  gererBoutons();

  // 4. LOGIQUE ÉCLAIRAGE
  calculerEtats();

  // 5. BOUTON PAGE LCD (DI3)
  {
    bool btnPage = (digitalRead(PIN_BTN_PAGE) == LOW);
    if (btnPage && !btnPagePrec) {
      pageAffichage = (pageAffichage + 1) % 6;
      lcd.clear();
    }
    btnPagePrec = btnPage;
  }

  // 6. AFFICHAGE LCD (toutes les 300ms)
  if (now - dernierAffichage >= 300) {
    dernierAffichage = now;
    switch (pageAffichage) {
      case 0: afficherPage0(); break;
      case 1: afficherLigne(0); break;  // L1
      case 2: afficherLigne(1); break;  // L2
      case 3: afficherLigne(2); break;  // L3
      case 4: afficherPage2(); break;       // Système
      case 5: afficherPageReseau(); break; // Reseau/MQTT
    }
  }

  // 7. MQTT — reconnexion + publish
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

  // 8. OTA (toutes les 24h)
  if (connecte && (now - dernierCheckOTA >= INTERVAL_OTA || dernierCheckOTA == 0)) {
    dernierCheckOTA = now;
    verifierOTA();
  }

  delay(50);
}
