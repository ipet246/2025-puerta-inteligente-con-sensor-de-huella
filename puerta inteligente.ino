/* CONEXIONES (Updated)
// Fingerprint Sensor to ESP32
Sensor VCC   -> ESP32 5V
Sensor GND   -> ESP32 GND
Sensor TX    -> ESP32 GPIO 16
Sensor RX    -> ESP32 GPIO 17

// Servo 1 (Upright) to ESP32
Servo VCC    -> ESP32 5V
Servo GND    -> ESP32 GND
Servo Signal -> ESP32 GPIO 25

// Servo 2 (Upside-down) to ESP32
Servo VCC    -> ESP32 5V 
Servo GND    -> ESP32 GND
Servo Signal -> ESP32 GPIO 26

// Button 1 (Enroll/Mode) to ESP32
Button 1 Leg 1 -> ESP32 GPIO 4
Button 1 Leg 2 -> ESP32 GND

// Button 2 (Wipe) to ESP32
Button 2 Leg 1 -> ESP32 GPIO 5
Button 2 Leg 2 -> ESP32 GND
*/

#include <Adafruit_Fingerprint.h>
#include "BluetoothSerial.h"
#include <ESP32Servo.h>

// --- NEW: Includes for Wi-Fi and Time ---
#include <WiFi.h>
#include "time.h"

// Check if Bluetooth is enabled in the ESP32 board configuration
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `Tools > ESP32 Bluetooth Settings` and enable it.
#endif

// --- CONFIGURATION ---
// Bluetooth Device Name that will appear on your phone
const char* bt_device_name = "ESP32Puerta";

// --- NEW: Wi-Fi Credentials for NTP ---
const char* ssid = "Estudiantes";
const char* password = "educar_2018";

// --- NEW: NTP Server Configuration ---
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -10800; // Offset for UTC-3 (3 hours * 3600 seconds)
const int   daylightOffset_sec = 0; // Change to 3600 if your region observes DST and it's in effect

// Pin definitions
#define FP_RX 16
#define FP_TX 17
#define SERVO_PIN 25
#define SERVO2_PIN 26  // NEW: Define the pin for the second servo
#define ENROLL_MODE_BUTTON_PIN 4
#define WIPE_BUTTON_PIN 5

// System settings
#define SERVO_LOCKED_POS 0
#define SERVO_UNLOCKED_POS 90

// --- NEW: Define opposite positions for the inverted second servo ---
#define SERVO2_LOCKED_POS SERVO_UNLOCKED_POS // When Servo 1 is locked (0), Servo 2 is at 90
#define SERVO2_UNLOCKED_POS SERVO_LOCKED_POS // When Servo 1 is unlocked (90), Servo 2 is at 0

#define MASTER_ID 1 // The fingerprint ID that can authorize actions
#define LONG_PRESS_TIME 2000 // 2 seconds for a long press

// --- OBJECTS & GLOBAL VARIABLES ---
BluetoothSerial SerialBT;

// Use HardwareSerial for the fingerprint sensor
HardwareSerial fingerSerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerSerial);

// CHANGE: Using the ESP32Servo library for better compatibility and performance.
Servo myServo;
Servo myServo2;  // NEW: Create a second servo object

// State Machine for Enrollment and System Control
enum State {
  STATE_NORMAL,
  STATE_FIRST_RUN,               // For enrolling the very first master
  STATE_AWAITING_MASTER_ENROLL,  // Awaiting master to authorize new enrollment
  STATE_AWAITING_MASTER_WIPE,    // Awaiting master to authorize database wipe
  STATE_AWAITING_MASTER_MODE,    // Awaiting master to authorize mode toggle
  STATE_AWAITING_NEW_FINGER,
  STATE_ENROLLING_1,
  STATE_ENROLLING_2
};
State systemState = STATE_NORMAL;

// System flags and variables
bool allowAnyMode = false; // Flag for "Allow Any" mode
unsigned long doorOpenTime = 0;
const long doorOpenDuration = 3000; // 3 seconds
unsigned long stateChangeTime = 0;
const long stateTimeout = 10000; // 10 seconds to complete an action
int newFingerprintID = -1;

// --- NEW: Automatic ID Assignment ---
int nextEnrollID = 1; // Global variable to track the next ID to use

// --- FIX: Flag to distinguish between master replacement and new user enrollment ---
bool isReplacingMaster = false;

// --- NEW: Flag to track the initial master enrollment ---
bool isFirstRunEnrollment = false;

// Button press tracking
unsigned long enrollButtonPressTime = 0;
bool enrollButtonPressed = false;

// --- FUNCTION PROTOTYPES ---
// This section tells the compiler about all our functions upfront, preventing the 'was not declared' error.
void handleButtons();
void handleStateMachine();
void handleBluetoothCommands();
void checkDoorLockTimer();
void checkForMasterFingerprint(State nextState);
void wipeDatabase();
void promptForNewFinger();
void enrollFingerStep1();
void enrollFingerStep2();
void getFingerprintID();
int getFingerprintIDez();
void unlockDoor();
void lockDoor();
void logEvent(String message);

// --- NEW: Helper function prototypes for time ---
void printLocalTime();
String getFormattedTimestamp();


// --- SETUP ---
void setup() {
  Serial.begin(115200);
  myServo.attach(SERVO_PIN);
  myServo2.attach(SERVO2_PIN);  // NEW: Attach the second servo
  
  // Initialize both servos to their locked positions
  myServo.write(SERVO_LOCKED_POS);
  myServo2.write(SERVO2_LOCKED_POS);  // NEW: Initialize second servo to its locked position (90 deg)
  delay(500);

  // Setup button pins with internal pull-up resistors
  pinMode(ENROLL_MODE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(WIPE_BUTTON_PIN, INPUT_PULLUP);

  // --- NEW: Connect to Wi-Fi for NTP ---
  Serial.printf("Conectado a %s ", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" CONECTADO");

  // --- NEW: Init and get the time ---
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  printLocalTime();
  
  // Initialize Bluetooth
  SerialBT.begin(bt_device_name);
  logEvent("Sistema: Bluetooth Listo! emparejado con '" + String(bt_device_name) + "'");

  // Initialize fingerprint sensor on the correct hardware serial port
  fingerSerial.begin(57600, SERIAL_8N1, FP_TX, FP_RX); // RX, TX
  finger.begin(57600);
  
  if (finger.verifyPassword()) {
    logEvent("Sistema: Sensor de huella inicializado.");
    // Check if database is empty for first-run setup
    finger.getTemplateCount();
    if (finger.templateCount == 0) {
      logEvent("Sistema: No se encontraron huellas. Porfavor registre la huella MASTER.");
      systemState = STATE_FIRST_RUN;
      nextEnrollID = 1; // The next ID will be 1 (for the master)
    } else {
      logEvent("Sistema: " + String(finger.templateCount) + " huellas encontradas. Listo.");
      
      // --- NEW: Find the next available ID ---
      int highestID = 0;
      for (int id = 1; id <= 127; id++) {
        if (finger.loadModel(id) == FINGERPRINT_OK) {
          highestID = id;
        }
      }
      nextEnrollID = highestID + 1;
      logEvent("Sistema: La proxíma huella será guardada como ID #" + String(nextEnrollID));
    }
  } else {
    logEvent("ERROR: Sensor de huella no encontrado!");
    while (1); // Halt
  }
}

// --- MAIN LOOP ---
void loop() {
  handleButtons();
  handleStateMachine();
  handleBluetoothCommands(); // Check for commands from the app
  checkDoorLockTimer();
  delay(50); // Small delay to prevent overwhelming the system
}

// --- BLUETOOTH COMMAND HANDLING ---
void handleBluetoothCommands() {
  if (SerialBT.available()) {
    String command = SerialBT.readStringUntil('\n'); // Read command until newline
    command.trim(); // Remove any whitespace

    logEvent("Comando desde la App recibido: " + command);

    if (command == "UNLOCK") {
      unlockDoor();
    } else if (command == "LOCK") {
      lockDoor();
    } else if (command == "STATUS") {
      logEvent("Estado: El modo actual es " + String(allowAnyMode ? "Permitir a todos" : "Seguro") + ". El estado es " + String(systemState));
    } else if (command == "ENROLL") {
      if (systemState == STATE_NORMAL) {
        logEvent("App: Esperando autorización de MASTER para REGISTRAR HUELLA...");
        systemState = STATE_AWAITING_MASTER_ENROLL;
        stateChangeTime = millis();
      }
    } else if (command == "WIPE") {
      if (systemState == STATE_NORMAL) {
        logEvent("App: Esperando autorización de MASTER para VACIAR BASE DE DATOS...");
        systemState = STATE_AWAITING_MASTER_WIPE;
        stateChangeTime = millis();
      }
    } else if (command == "TOGGLE_MODE") {
      if (systemState == STATE_NORMAL) {
        logEvent("App: Esperando autorización de MASTER para CAMBIAR MODO...");
        systemState = STATE_AWAITING_MASTER_MODE;
        stateChangeTime = millis();
      }
    }
  }
}

// --- CORE LOGIC FUNCTIONS ---

void handleButtons() {
  // --- Recovery Mode ---
  // Check for both buttons pressed simultaneously to force master re-enrollment
  if (digitalRead(ENROLL_MODE_BUTTON_PIN) == LOW && digitalRead(WIPE_BUTTON_PIN) == LOW) {
    delay(50); // Debounce
    if (digitalRead(ENROLL_MODE_BUTTON_PIN) == LOW && digitalRead(WIPE_BUTTON_PIN) == LOW) {
      logEvent("Modo de recuperación: Forzando registro de MASTER (ID #1).");
      // --- FIX: Set flags directly and go to waiting state ---
      isReplacingMaster = true; // Set the flag directly
      isFirstRunEnrollment = false; // Ensure this is false during recovery
      newFingerprintID = 1; // Master is always ID 1
      systemState = STATE_AWAITING_NEW_FINGER; // Wait for the user to place their finger
      stateChangeTime = millis();
      // Wait for both buttons to be released to prevent re-triggering
      while(digitalRead(ENROLL_MODE_BUTTON_PIN) == LOW || digitalRead(WIPE_BUTTON_PIN) == LOW);
      return; // Exit the function after handling recovery
    }
  }

  if (digitalRead(ENROLL_MODE_BUTTON_PIN) == LOW) {
    if (!enrollButtonPressed) {
      enrollButtonPressed = true;
      enrollButtonPressTime = millis();
    }
  } else {
    if (enrollButtonPressed) {
      unsigned long pressDuration = millis() - enrollButtonPressTime;
      if (pressDuration < LONG_PRESS_TIME) {
        if (systemState == STATE_NORMAL) {
          logEvent("Botón: Pulso corto. Esperando autorización de MASTER para REGISTRAR HUELLA...");
          systemState = STATE_AWAITING_MASTER_ENROLL;
          stateChangeTime = millis();
        }
      } else {
        if (systemState == STATE_NORMAL) {
          logEvent("Botón: Pulso largo. Esperando autorización de MASTER para CAMBIAR MODO...");
          systemState = STATE_AWAITING_MASTER_MODE;
          stateChangeTime = millis();
        }
      }
      enrollButtonPressed = false;
    }
  }

  if (digitalRead(WIPE_BUTTON_PIN) == LOW) {
    delay(50);
    if (digitalRead(WIPE_BUTTON_PIN) == LOW) {
      if (systemState == STATE_NORMAL) {
        logEvent("Botón: 'Vaciar' presionado. Esperando autorización de MASTER para VACIAR BASE DE DATOS...");
        systemState = STATE_AWAITING_MASTER_WIPE;
        stateChangeTime = millis();
      }
      while(digitalRead(WIPE_BUTTON_PIN) == LOW);
    }
  }
}

void handleStateMachine() {
  // --- FIX: Exclude enrollment states from the timeout check ---
  // This prevents the system from timing out while you are trying to enroll a master finger.
  if (systemState != STATE_NORMAL && systemState != STATE_FIRST_RUN && 
      systemState != STATE_ENROLLING_1 && systemState != STATE_ENROLLING_2 && 
      millis() - stateChangeTime > stateTimeout) {
    logEvent("La acción expiró. Volviendo al modo normal.");
    systemState = STATE_NORMAL;
  }

  switch (systemState) {
    case STATE_NORMAL:
      getFingerprintID();
      break;
      
    case STATE_FIRST_RUN:
      // --- FIX: Set flags for master replacement and first run ---
      logEvent("Registrando huella MASTER (ID #1).");
      isReplacingMaster = true; // Set the flag
      isFirstRunEnrollment = true; // --- NEW: Set the first run flag ---
      newFingerprintID = 1; // Master is always ID 1
      systemState = STATE_ENROLLING_1;
      stateChangeTime = millis(); // Start the timer for the enrollment state
      break;

    case STATE_AWAITING_MASTER_ENROLL:
      checkForMasterFingerprint(STATE_AWAITING_MASTER_ENROLL);
      break;
    case STATE_AWAITING_MASTER_WIPE:
      checkForMasterFingerprint(STATE_AWAITING_MASTER_WIPE);
      break;
    case STATE_AWAITING_MASTER_MODE:
      checkForMasterFingerprint(STATE_AWAITING_MASTER_MODE);
      break;
    case STATE_AWAITING_NEW_FINGER:
      promptForNewFinger();
      break;
    case STATE_ENROLLING_1:
      enrollFingerStep1();
      break;
    case STATE_ENROLLING_2:
      enrollFingerStep2();
      break;
  }
}

void checkForMasterFingerprint(State nextState) {
  logEvent("Por favor escanee la huella MASTER para proceder.");
  int id = getFingerprintIDez();
  if (id == MASTER_ID) {
    logEvent("Huella MASTER verificada.");
    if (nextState == STATE_AWAITING_MASTER_ENROLL) {
      systemState = STATE_AWAITING_NEW_FINGER;
    } else if (nextState == STATE_AWAITING_MASTER_WIPE) {
      wipeDatabase();
    } else if (nextState == STATE_AWAITING_MASTER_MODE) {
      allowAnyMode = !allowAnyMode;
      logEvent("Modo cambiado. 'Permitir a todos' está " + String(allowAnyMode ? "activado" : "desactivado"));
      systemState = STATE_NORMAL;
    }
    stateChangeTime = millis();
  } else if (id > 0) {
    logEvent("Acceso DENEGADO. No es la huella MASTER.");
    systemState = STATE_NORMAL;
  }
}

void wipeDatabase() {
  logEvent("Eliminando todas las huellas... POR FAVOR ESPERE.");
  finger.emptyDatabase();
  logEvent("Base de datos vaciada. Todas las huellas fueron eliminadas.");
  nextEnrollID = 1; // Reset the counter after a wipe
  systemState = STATE_FIRST_RUN; // Now correctly go to first run state
}

void promptForNewFinger() {
  logEvent("Ponga el dedo NUEVO en el sensor.");
  if (finger.getImage() == FINGERPRINT_OK) {
    // --- FIX: Only set these values if not replacing master ---
    if (!isReplacingMaster) {
      isReplacingMaster = false; // This is a new user, not a master replacement
      newFingerprintID = nextEnrollID; // Assign the next available ID
    }
    systemState = STATE_ENROLLING_1;
    stateChangeTime = millis();
  }
}

void enrollFingerStep1() {
  logEvent("Remueva su dedo...");
  delay(1000);
  if (finger.image2Tz(1) != FINGERPRINT_OK) {
    logEvent("Error convirtiendo imagen. Intente de nuevo.");
    systemState = STATE_AWAITING_NEW_FINGER;
    return;
  }
  logEvent("Ponga el mismo dedo DE NUEVO.");
  systemState = STATE_ENROLLING_2;
  stateChangeTime = millis();
}

void enrollFingerStep2() {
  if (finger.getImage() != FINGERPRINT_OK) return;
  if (finger.image2Tz(2) != FINGERPRINT_OK) {
    logEvent("Error convirtiendo segunda imagen. Intente de nuevo.");
    systemState = STATE_AWAITING_NEW_FINGER;
    return;
  }
  if (finger.createModel() != FINGERPRINT_OK) {
    logEvent("ERROR: Las huellas no coincidieron. Intente de nuevo.");
    systemState = STATE_AWAITING_NEW_FINGER;
    return;
  }

  // --- FIX: Use the pre-assigned ID and update counter correctly ---
  if (newFingerprintID > 127) {
    logEvent("ERROR: Base de datos llena.");
    systemState = STATE_NORMAL;
    return;
  }

  if (finger.storeModel(newFingerprintID) == FINGERPRINT_OK) {
    logEvent("Éxitoso! Huella guardada como ID #" + String(newFingerprintID));
    
    // --- REVISED LOGIC for updating the next ID counter ---
    if (isFirstRunEnrollment) {
      // This was the very first master enrollment, so set the next ID to 2
      nextEnrollID = 2;
      isFirstRunEnrollment = false; // Reset the flag immediately after use
    } else if (!isReplacingMaster) {
      // This was a new user (not a master replacement), so increment the counter
      nextEnrollID++;
    }
    // If isReplacingMaster is true (and it's not the first run), we do nothing to the counter.
    
  } else {
    logEvent("ERROR: guardado de modelo fallido.");
  }
  
  // Reset the replacement flag for safety
  isReplacingMaster = false;
  systemState = STATE_NORMAL;
}

// --- ACCESS CONTROL & HELPERS ---

void getFingerprintID() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return;

  if (allowAnyMode) {
    logEvent("Acceso PERMITIDO (Modo Permitir a todos)");
    unlockDoor();
    return;
  }

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return;

  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    logEvent("Acceso PERMITIDO para Usuario de ID: " + String(finger.fingerID));
    unlockDoor();
  } else if (p == FINGERPRINT_NOTFOUND) {
    logEvent("Acceso DENEGADO - Huella desconocida");
  }
}

int getFingerprintIDez() {
  if (finger.getImage() != FINGERPRINT_OK) return -1;
  if (finger.image2Tz() != FINGERPRINT_OK) return -1;
  if (finger.fingerFastSearch() != FINGERPRINT_OK) return -1;
  return finger.fingerID;
}

void checkDoorLockTimer() {
  if (doorOpenTime > 0 && millis() - doorOpenTime >= doorOpenDuration) {
    lockDoor();
  }
}

void unlockDoor() { 
  myServo.write(SERVO_UNLOCKED_POS);         // Moves to 90 deg
  myServo2.write(SERVO2_UNLOCKED_POS);       // Moves to 0 deg (opposite direction)
  doorOpenTime = millis(); 
  logEvent("Puerta ABIERTA.");
}

void lockDoor() { 
  myServo.write(SERVO_LOCKED_POS);           // Moves to 0 deg
  myServo2.write(SERVO2_LOCKED_POS);         // Moves to 90 deg (opposite direction)
  doorOpenTime = 0; 
  logEvent("Puerta CERRADA.");
}

// --- LOGGING & TIME HELPERS ---

// --- NEW: Helper function to print time to Serial Monitor ---
void printLocalTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("No se pudo obtener el tiempo");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

// --- NEW: Helper function to get a formatted timestamp string ---
String getFormattedTimestamp() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    // If time is not synced, return an empty string
    return ""; 
  }
  
  char buffer[50];
  // Format: YYYY-MM-DD HH:MM:SS
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

/**
 * @brief Logs an event with a real-world timestamp (or uptime as a fallback).
 * @param message The event message to log.
 */
void logEvent(String message) {
  String logEntry;

  // Try to get the real timestamp
  String timestamp = getFormattedTimestamp();

  if (timestamp != "") {
    // If we have a real time, use it
    logEntry = "[" + timestamp + "] " + message;
  } else {
    // Fallback to uptime if time is not synced
    String uptime = String(millis() / 1000);
    logEntry = "[" + uptime + "s] " + message;
  }

  // Send to Serial Monitor for debugging
  Serial.println(logEntry);
  
  // Send to Bluetooth for the App
  SerialBT.println(logEntry);
}