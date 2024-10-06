#include <Wire.h>
#include <Adafruit_PN532.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <SD.h>

// Pins for the PN532
#define SDA_PIN 2
#define SCL_PIN 3
Adafruit_PN532 nfc(SDA_PIN, SCL_PIN);

// LCD setup
LiquidCrystal_I2C lcd(0x27, 16, 2); // Change the address according to your LCD

// SD card setup
const int chipSelect = 10; // Change as needed

// Passive buzzer setup
const int buzzerPin = 8; // Pin for the buzzer

// LED setup
const int redLEDPin = 4;  // Pin for the red LED
const int greenLEDPin = 5; // Pin for the green LED

// User and tool UIDs
uint8_t user1[4] = {0x69, 0x18, 0x09, 0x9F}; // User 1 UID
uint8_t user2[4] = {0x19, 0x73, 0x5A, 0x9F}; // User 2 UID
uint8_t tool1[4] = {0xC9, 0xFC, 0x20, 0x83}; // Tool 1 UID
uint8_t tool2[4] = {0xB5, 0xD7, 0x79, 0xB9}; // Tool 2 UID
uint8_t tool3[4] = {0x59, 0x7D, 0x0C, 0x9F}; // Tool 3 UID
uint8_t tool4[4] = {0x79, 0x5B, 0x72, 0x9F}; // Tool 4 UID

// Tool Status: 0 = available, 1 = signed out
bool toolStatus[4] = {false, false, false, false}; // Initialize all tools as available

// Current logged in user
uint8_t *currentUserUID = NULL;
const char* currentUserName = ""; // Keep track of the current user's name

// CSV filename
const char* logFileName = "tool_log.csv";

// Maximum usage time
const unsigned long maxUsageTime = 3600000; // 1 hour in milliseconds
unsigned long signOutTimes[4] = {0, 0, 0, 0}; // Track sign out times for tools

// Function declarations
bool isUser(uint8_t *uid, const char*& userName);
bool isTool(uint8_t *uid);
void displayMessage(const char *message);
void displayInstructions(const char *instruction);
void logEvent(const String& event);
void logEvent(const String& event, const String& type, const String& tool, const String& user);
void logEventToCSV(const String& type, const String& tool, const String& user);
void checkForOverdueTools();
void playHappySoundSignIn();
void playHappySoundToolSignOut();
void playHappySoundToolSignIn();
void playSadSound();
void playSDCardErrorSound();
void flashRedLED(int duration);
void flashGreenLED(int duration);

void setup() {
    Serial.begin(115200);
    nfc.begin();
    lcd.begin(16, 2); // Specify columns and rows
    lcd.backlight();

    // Initialize pins for LEDs
    pinMode(redLEDPin, OUTPUT);
    pinMode(greenLEDPin, OUTPUT);

    if (!SD.begin(chipSelect)) {
        playSDCardErrorSound();
        displayMessage("SD Card Error");
        while (true); // Stop here if SD card initialization fails
    }

    // Check if the CSV file exists, if not, create it and add headers
    if (!SD.exists(logFileName)) {
        File logFile = SD.open(logFileName, FILE_WRITE);
        if (logFile) {
            logFile.println("Timestamp,Event Type,Tool,User");
            logFile.close();
        }
    }

    // Display welcome message
    displayMessage("Tool Tracking Sys");
    delay(2000);
    lcd.clear();
    displayInstructions("Sign In"); // Initial instruction
}

void loop() {
    // Check for overdue tools
    checkForOverdueTools();

    // Check if a tag is available
    uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0, 0}; // Buffer for UID
    uint8_t uidLength; // Length of UID

    // Try to read a tag
    uidLength = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);

    // Check if we have a valid UID
    if (uidLength > 0) {
        const char* toolName = ""; // Name of the tool being checked out
        // Check if the UID belongs to a user
        if (isUser(uid, currentUserName)) {
            currentUserUID = uid; // Set current user
            displayMessage("User Signed In");
            playHappySoundSignIn(); // Play sign-in sound
            flashGreenLED(500); // Flash green LED
            logEvent("User: " + String(currentUserName)); // Log the user sign in
            logEventToCSV("Sign In", "", String(currentUserName)); // Log to CSV
            delay(2000);
            lcd.clear();
            displayInstructions("Scan Tool"); // Change instruction
        } 
        // If the user is already signed in
        else if (currentUserUID != NULL) {
            // Check if the UID belongs to a tool
            if (isTool(uid)) {
                // Determine tool name
                if (memcmp(uid, tool1, 4) == 0) {
                    toolName = "Tool 1";
                } else if (memcmp(uid, tool2, 4) == 0) {
                    toolName = "Tool 2";
                } else if (memcmp(uid, tool3, 4) == 0) {
                    toolName = "Tool 3";
                } else if (memcmp(uid, tool4, 4) == 0) {
                    toolName = "Tool 4";
                }

                // Check tool index
                int toolIndex = (uid[0] == tool1[0]) ? 0 :
                                (uid[0] == tool2[0]) ? 1 :
                                (uid[0] == tool3[0]) ? 2 :
                                (uid[0] == tool4[0]) ? 3 : -1;

                unsigned long currentTime = millis(); // Current time for overdue checks

                if (!toolStatus[toolIndex]) { // Tool is available
                    toolStatus[toolIndex] = true; // Mark as signed out
                    signOutTimes[toolIndex] = currentTime; // Track sign out time
                    displayMessage("Tool Signed Out");
                    playHappySoundToolSignOut(); // Play tool sign-out sound
                    flashRedLED(500); // Flash red LED
                    logEvent(String(currentUserName), "Signed Out", String(toolName), ""); 
                    logEventToCSV("Signed Out", String(toolName), String(currentUserName)); // Log to CSV
                } else { // Tool is already signed out
                    toolStatus[toolIndex] = false; // Mark as available
                    displayMessage("Tool Signed In");
                    playHappySoundToolSignIn(); // Play tool sign-in sound
                    flashGreenLED(500); // Flash green LED
                    logEvent("Tool: " + String(toolName), "Signed In", "", String(currentUserName)); 
                    logEventToCSV("Signed In", String(toolName), String(currentUserName)); // Log to CSV
                }

                delay(2000);
                lcd.clear();
                displayInstructions("Scan Tool"); // Remain in scan tool mode
            } else {
                displayMessage("Invalid Tool");
                delay(2000);
                lcd.clear();
                displayInstructions("Scan Tool"); // Remain in scan tool mode
            }
        } 
        // If it's an invalid user
        else {
            displayMessage("Invalid User");
            flashRedLED(500);  // Flash red LED when invalid user detected
            playSadSound();    // Play the sad sound for invalid user
            delay(2000);
            lcd.clear();
            displayInstructions("Sign In"); // Change instruction back to sign in
        }
    }
}

// Check for overdue tools and notify if any are found
void checkForOverdueTools() {
    unsigned long currentTime = millis();
    for (int i = 0; i < 4; i++) {
        if (toolStatus[i] && (currentTime - signOutTimes[i] > maxUsageTime)) {
            // Tool is overdue
            String overdueTool = "Tool " + String(i + 1);
            displayMessage(overdueTool.c_str()); // Fixed the conversion error
            flashRedLED(1000); // Flash red LED for overdue indication
            playSadSound(); // Play sad sound for overdue indication
            delay(2000); // Keep message for a while
        }
    }
}

// Buzzer sounds for different events
// Buzzer sounds for different events
void playHappySoundSignIn() {
    tone(buzzerPin, 1046, 200);
    delay(100);
    tone(buzzerPin, 1568, 200);
    delay(100);
    tone(buzzerPin, 2092, 200);
    delay(250);
    noTone(buzzerPin);
}

void playHappySoundToolSignOut() {
    tone(buzzerPin, 2092, 200);
    delay(100);
    tone(buzzerPin, 1046, 200);
    delay(250);
    noTone(buzzerPin);
}

void playHappySoundToolSignIn() {
    tone(buzzerPin, 1046, 200);
    delay(100);
    tone(buzzerPin, 2092, 200);
    delay(100);
    noTone(buzzerPin);
}

void playSadSound() {
    tone(buzzerPin, 233, 150); // Play first tone for 150ms
    delay(150);
    tone(buzzerPin, 220, 150); // Play second tone for 150ms
    delay(150);
    noTone(buzzerPin);
}

void playSDCardErrorSound() {
    tone(buzzerPin, 330, 200); // Play tone for 200ms
    delay(200);
    tone(buzzerPin, 233, 200); // Play tone for 200ms
    delay(200);
    tone(buzzerPin, 220, 200); // Play tone for 200ms
    delay(250);
    noTone(buzzerPin);
}

// Display message on the LCD
void displayMessage(const char *message) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(message);
}

// Display instructions on the LCD
void displayInstructions(const char *instruction) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(instruction);
}

// Check if the UID belongs to a user
bool isUser(uint8_t *uid, const char*& userName) {
    if (memcmp(uid, user1, 4) == 0) {
        userName = "User 1";
        return true;
    } else if (memcmp(uid, user2, 4) == 0) {
        userName = "User 2";
        return true;
    }
    return false;
}

// Check if the UID belongs to a tool
bool isTool(uint8_t *uid) {
    return memcmp(uid, tool1, 4) == 0 ||
           memcmp(uid, tool2, 4) == 0 ||
           memcmp(uid, tool3, 4) == 0 ||
           memcmp(uid, tool4, 4) == 0;
}

// Log events to the console
void logEvent(const String& event) {
    Serial.println(event);
}

// Log events with additional information
void logEvent(const String& event, const String& type, const String& tool, const String& user) {
    Serial.print(event + " ");
    Serial.print(type + " ");
    Serial.print(tool + " ");
    Serial.println(user);
}

// Log events to CSV file
void logEventToCSV(const String& type, const String& tool, const String& user) {
    File logFile = SD.open(logFileName, FILE_WRITE);
    if (logFile) {
        unsigned long timestamp = millis();
        logFile.print(timestamp);
        logFile.print(",");
        logFile.print(type);
        logFile.print(",");
        logFile.print(tool);
        logFile.print(",");
        logFile.println(user);
        logFile.close();
    }
}

// Flash red LED
void flashRedLED(int duration) {
    digitalWrite(redLEDPin, HIGH);
    delay(duration);
    digitalWrite(redLEDPin, LOW);
}

// Flash green LED
void flashGreenLED(int duration) {
    digitalWrite(greenLEDPin, HIGH);
    delay(duration);
    digitalWrite(greenLEDPin, LOW);
}
