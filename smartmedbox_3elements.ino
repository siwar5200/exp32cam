// ============================================================
//  Smart Med Box — VERSION SIMPLIFIÉE (3 éléments)
//  ESP32-WROOM-32D
//
//  Éléments utilisés :
//    1. Capteur IR E18-D80NK  → GPIO14
//    2. ESP32-CAM OV2640      → UART2 (GPIO17 TX, GPIO16 RX)
//    3. ESP32-WROOM-32D       → ce programme
//
//  FIX : timeout 10s si ESP32-CAM ne répond pas
//        → système se remet en attente automatiquement
// ============================================================

// ─── Broches ────────────────────────────────────────────────
#define IR_PIN      14    // E18-D80NK signal OUT  (LOW = présence)
#define CAM_TX_PIN  17    // GPIO17 → U0R ESP32-CAM
#define CAM_RX_PIN  16    // GPIO16 ← U0T ESP32-CAM
#define CAM_BAUD    115200

// ─── Timing ─────────────────────────────────────────────────
#define DELAI_MIN_MS    5000   // 5 s min entre deux captures
#define TIMEOUT_CAM_MS 10000  // 10 s max attente réponse CAM

// ─── Variables ──────────────────────────────────────────────
unsigned long derniereCaptureMs = 0;
unsigned long debutCaptureMs    = 0;
String bufferCam = "";
bool captureEnCours = false;

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial2.begin(CAM_BAUD, SERIAL_8N1, CAM_RX_PIN, CAM_TX_PIN);

  pinMode(IR_PIN, INPUT_PULLUP);

  Serial.println("==================================");
  Serial.println("  Smart Med Box - 3 elements");
  Serial.println("  IR + ESP32 + ESP32-CAM");
  Serial.println("==================================");
  Serial.println("En attente de presence...");
  Serial.println("----------------------------------");
}

void loop() {

  // ── 1. Lire réponse ESP32-CAM ─────────────────────────────
  while (Serial2.available()) {
    char c = (char)Serial2.read();
    if (c == '\n') {
      bufferCam.trim();
      if (bufferCam.length() > 0) {
        traiterReponseCam(bufferCam);
      }
      bufferCam = "";
    } else if (c != '\r') {
      bufferCam += c;
    }
  }

  // ── 2. Timeout : si ESP32-CAM ne répond pas en 10s ────────
  if (captureEnCours) {
    if (millis() - debutCaptureMs > TIMEOUT_CAM_MS) {
      Serial.println("----------------------------------");
      Serial.println("TIMEOUT: ESP32-CAM ne repond pas");
      Serial.println("Retour en attente...");
      Serial.println("----------------------------------");
      captureEnCours = false;
    }
  }

  
  if (!captureEnCours) {
    bool presence = (digitalRead(IR_PIN) == LOW);
    unsigned long maintenant = millis();

    if (presence && (maintenant - derniereCaptureMs > DELAI_MIN_MS)) {
      Serial.println();
      Serial.println("==================================");
      Serial.println("IR: presence detectee");
      envoyerCapture();
      derniereCaptureMs = maintenant;
    }
  }

  delay(10);
}

void envoyerCapture() {
  captureEnCours  = true;
  debutCaptureMs  = millis();
  Serial.println("UART: envoi commande CAPTURE -> ESP32-CAM");
  Serial2.println("CAPTURE");
}

// ============================================================
void traiterReponseCam(const String& ligne) {

  Serial.print("CAM: ");
  Serial.println(ligne);

  if (ligne == "DEBUT CAPTURE") {
    Serial.println(">>> Photo en cours...");
  }
  else if (ligne == "FLASH ALLUME") {
    Serial.println(">>> Flash allume");
  }
  else if (ligne == "PRISE PHOTO") {
    Serial.println(">>> Obturateur declenche");
  }
  else if (ligne == "ENVOI AU SERVEUR...") {
    Serial.println(">>> Envoi image au serveur Python...");
  }
  else if (ligne == "RESULTAT : OK") {
    Serial.println("----------------------------------");
    Serial.println("RESULTAT: VISAGE RECONNU - acces autorise");
    Serial.println("----------------------------------");
    captureEnCours = false;
  }
  else if (ligne == "RESULTAT : ACCES REFUSE") {
    Serial.println("----------------------------------");
    Serial.println("RESULTAT: VISAGE NON RECONNU - acces refuse");
    Serial.println("----------------------------------");
    captureEnCours = false;
  }
  else if (ligne == "RESULTAT : AUCUN VISAGE") {
    Serial.println("----------------------------------");
    Serial.println("RESULTAT: AUCUN VISAGE DETECTE");
    Serial.println("----------------------------------");
    captureEnCours = false;
  }
  else if (ligne.startsWith("RESULTAT : ERREUR")) {
    Serial.println("----------------------------------");
    Serial.println("RESULTAT: ERREUR - " + ligne);
    Serial.println("----------------------------------");
    captureEnCours = false;
  }
  else if (ligne == "FIN CAPTURE") {
    Serial.println(">>> Cycle termine");
    Serial.println("En attente de presence...");
    Serial.println("----------------------------------");
    captureEnCours = false;
  }
  else if (ligne.startsWith("PATIENT RECONNU")) {
    Serial.println(">>> " + ligne);
  }
  else if (ligne.startsWith("DISTANCE")) {
    Serial.println(">>> " + ligne);
  }
  else if (ligne.startsWith("TAILLE PHOTO")) {
    Serial.println(">>> " + ligne);
  }
  else if (ligne.startsWith("ERREUR HTTP")) {
    Serial.println(">>> Probleme reseau: " + ligne);
  }
}
