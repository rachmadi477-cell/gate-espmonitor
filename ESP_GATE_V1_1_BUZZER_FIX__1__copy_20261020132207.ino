/*
  ESP GATE V1.1 - SANIHARTO
  FIX BUZZER ACTIVE

  LEGAL  : P10 HIJAU + RELAY PINTU + 2x BIB PENDEK
  ILEGAL : P10 MERAH + RELAY OFF + 1x BIB PANJANG 5 DETIK

  Gate TIDAK mengecek kapasitas toilet.
  Gate hanya meminta keputusan LEGAL / ILEGAL dari server.

  Hardware:
  RC522   : SS=5, RST=22, SCK=18, MISO=19, MOSI=23
  Relay  : IN1=26 (PINTU), IN2=27, IN3=32, IN4=33
  Buzzer : GPIO25 (ACTIVE BUZZER ON/OFF)
*/

#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>

// =====================================================
// WIFI / SERVER
// =====================================================
const char* ssid     = "SEED-5";
const char* password = "transformer7";
const char* SERVER_IP = "192.168.11.177";
const char* API_GATE = "/personal_produksi/api_gate.php";

#define GATE_ID "GATE-TOILET-01"

// =====================================================
// RC522
// =====================================================
#define SS_PIN   5
#define RST_PIN  22
#define SCK_PIN  18
#define MISO_PIN 19
#define MOSI_PIN 23

MFRC522 rfid(SS_PIN, RST_PIN);

// =====================================================
// RELAY 4 CHANNEL
// =====================================================
#define RELAY_PINTU 26
#define RELAY_2     27
#define RELAY_3     32
#define RELAY_4     33

// Umumnya modul relay 4CH seperti ini aktif LOW.
#define RELAY_ACTIVE_LOW true

// =====================================================
// BUZZER ACTIVE
// =====================================================
#define BUZZER_PIN 25
#define BUZZER_ON  HIGH
#define BUZZER_OFF LOW

// =====================================================
// P10 SEMENTARA
// Belum langsung ke HUB12.
// =====================================================
#define P10_HIJAU 4
#define P10_MERAH 16

// =====================================================
// TIMER
// =====================================================
const unsigned long DOOR_OPEN_MS = 3000;
const unsigned long BIB_SHORT_MS = 120;
const unsigned long BIB_LONG_MS  = 5000;
const unsigned long SCAN_COOLDOWN = 1500;

unsigned long lastScan = 0;
unsigned long doorOffAt = 0;
unsigned long longBibOffAt = 0;

bool doorActive = false;
bool longBibActive = false;

// =====================================================
// RELAY
// =====================================================
void relayWrite(int pin, bool on) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(pin, on ? LOW : HIGH);
  } else {
    digitalWrite(pin, on ? HIGH : LOW);
  }
}

void semuaRelayOff() {
  relayWrite(RELAY_PINTU, false);
  relayWrite(RELAY_2, false);
  relayWrite(RELAY_3, false);
  relayWrite(RELAY_4, false);
}

// =====================================================
// P10
// =====================================================
void p10Off() {
  digitalWrite(P10_HIJAU, LOW);
  digitalWrite(P10_MERAH, LOW);
}

void p10Hijau() {
  digitalWrite(P10_MERAH, LOW);
  digitalWrite(P10_HIJAU, HIGH);
}

void p10Merah() {
  digitalWrite(P10_HIJAU, LOW);
  digitalWrite(P10_MERAH, HIGH);
}

// =====================================================
// BUZZER ACTIVE - ON/OFF LANGSUNG
// =====================================================
void buzzerOFF() {
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
}

void bibPendek() {
  digitalWrite(BUZZER_PIN, BUZZER_ON);
  delay(BIB_SHORT_MS);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
}

void bibLegal() {
  bibPendek();
  delay(120);
  bibPendek();
}

void mulaiBibIlegal() {
  digitalWrite(BUZZER_PIN, BUZZER_ON);
  longBibActive = true;
  longBibOffAt = millis() + BIB_LONG_MS;
}

// =====================================================
// PINTU
// =====================================================
void bukaPintu() {
  relayWrite(RELAY_PINTU, true);
  doorActive = true;
  doorOffAt = millis() + DOOR_OPEN_MS;
  Serial.println("PINTU OPEN 3 DETIK");
}

// =====================================================
// UID
// =====================================================
String uidToString() {
  String s = "";

  for (byte i = 0; i < rfid.uid.size; i++) {
    if (i > 0) s += " ";
    if (rfid.uid.uidByte[i] < 0x10) s += "0";
    s += String(rfid.uid.uidByte[i], HEX);
  }

  s.toUpperCase();
  return s;
}

String enc(String s) {
  s.replace(" ", "%20");
  return s;
}

// =====================================================
// HASIL LEGAL
// =====================================================
void aksesLegal() {
  Serial.println("================================");
  Serial.println("ACCESS LEGAL");
  Serial.println("================================");

  p10Hijau();

  // 2x bib pendek
  bibLegal();

  // buka pintu
  bukaPintu();

  // indikator hijau tampil sebentar
  delay(1000);
  p10Off();
}

// =====================================================
// HASIL ILEGAL
// =====================================================
void aksesIlegal() {
  Serial.println("================================");
  Serial.println("ACCESS ILEGAL");
  Serial.println("================================");

  // merah
  p10Merah();

  // pastikan pintu tidak terbuka
  relayWrite(RELAY_PINTU, false);
  doorActive = false;

  // 1x bib panjang 5 detik
  mulaiBibIlegal();
}

// =====================================================
// SERVER
// =====================================================
void cekServer(String uid) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WIFI PUTUS - AKSES DITOLAK");
    aksesIlegal();
    return;
  }

  HTTPClient http;

  String url = "http://" + String(SERVER_IP) + String(API_GATE);
  url += "?uid=" + enc(uid);
  url += "&gate_id=" + enc(GATE_ID);

  Serial.println();
  Serial.println("REQUEST SERVER:");
  Serial.println(url);

  http.begin(url);
  http.setTimeout(5000);

  int code = http.GET();

  if (code <= 0) {
    Serial.print("HTTP ERROR: ");
    Serial.println(code);
    aksesIlegal();
    http.end();
    return;
  }

  String payload = http.getString();

  Serial.print("HTTP CODE: ");
  Serial.println(code);
  Serial.println("RESPONSE:");
  Serial.println(payload);

  // Tahap awal: mencari legal=true pada response server.
  // API final nanti dapat mengembalikan JSON lengkap.
  String u = payload;
  u.toUpperCase();

  bool legal = false;

  if (u.indexOf("\"LEGAL\":TRUE") >= 0) legal = true;
  if (u.indexOf("\"LEGAL\": TRUE") >= 0) legal = true;
  if (u.indexOf("LEGAL=TRUE") >= 0) legal = true;

  if (legal) {
    aksesLegal();
  } else {
    aksesIlegal();
  }

  http.end();
}

// =====================================================
// WIFI
// =====================================================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("CONNECT WIFI");

  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WIFI CONNECTED");
    Serial.print("IP GATE: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WIFI GAGAL");
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  // Relay
  pinMode(RELAY_PINTU, OUTPUT);
  pinMode(RELAY_2, OUTPUT);
  pinMode(RELAY_3, OUTPUT);
  pinMode(RELAY_4, OUTPUT);

  // Buzzer active
  pinMode(BUZZER_PIN, OUTPUT);
  buzzerOFF();

  // P10 sementara
  pinMode(P10_HIJAU, OUTPUT);
  pinMode(P10_MERAH, OUTPUT);
  p10Off();

  // Relay semua OFF
  semuaRelayOff();

  // RC522
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  rfid.PCD_Init();
  delay(50);

  Serial.println();
  Serial.println("================================");
  Serial.println("       ESP GATE V1.1");
  Serial.println("       BUZZER ACTIVE FIX");
  Serial.println("================================");
  Serial.print("GATE ID: ");
  Serial.println(GATE_ID);

  connectWiFi();

  Serial.println("GATE SIAP - TAP RFID");
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  unsigned long now = millis();

  // ---------------------------------------------------
  // Pintu OFF setelah 3 detik
  // ---------------------------------------------------
  if (doorActive && (long)(now - doorOffAt) >= 0) {
    relayWrite(RELAY_PINTU, false);
    doorActive = false;
    Serial.println("PINTU CLOSE");
  }

  // ---------------------------------------------------
  // Buzzer ilegal OFF setelah 5 detik
  // ---------------------------------------------------
  if (longBibActive && (long)(now - longBibOffAt) >= 0) {
    buzzerOFF();
    longBibActive = false;

    Serial.println("BIB PANJANG 5 DETIK SELESAI");

    delay(300);
    p10Off();
  }

  // Jangan menerima scan baru selama bib ilegal 5 detik
  if (longBibActive) {
    return;
  }

  // ---------------------------------------------------
  // RFID
  // ---------------------------------------------------
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  if (millis() - lastScan < SCAN_COOLDOWN) {
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  lastScan = millis();

  String uid = uidToString();

  Serial.println();
  Serial.println("RFID TERBACA");
  Serial.print("UID: ");
  Serial.println(uid);

  cekServer(uid);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  delay(300);
}
