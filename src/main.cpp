#include <WiFi.h>
#include <NTPClient.h>
#include <TOTP.h>
#include <WebServer.h>
#include <EEPROM.h>

// EEPROM size
#define EEPROM_SIZE 512

// EEPROM addresses for storing configuration
#define SECRET_KEY_ADDRESS 0
#define SSID_ADDRESS 128
#define PASSWORD_ADDRESS 192

// Maximum lengths for configuration strings
#define MAX_SECRET_KEY_LENGTH 128
#define MAX_SSID_LENGTH 32
#define MAX_PASSWORD_LENGTH 64

// LED pin for status indication
#define STATUS_LED 2  // Built-in LED on most ESP32 boards

char ssid[MAX_SSID_LENGTH] = "";
char password[MAX_PASSWORD_LENGTH] = "";
char base32Key[MAX_SECRET_KEY_LENGTH] = "";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
WebServer server(80);

// Prepare to store the hmac key
uint8_t hmacKey[50]; // 80 bits = 10 bytes
TOTP totp = TOTP(hmacKey, sizeof(hmacKey));
String totpCode;

// Function to decode a base32 string into a byte array
int base32_decode(const char *base32, uint8_t *output) {
    const char *base32Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    int buffer = 0;
    int bitsLeft = 0;
    int outputIndex = 0;
    for (size_t i = 0; base32[i] != '\0'; i++) {
        char c = base32[i];
        if (c == '=') break; // Padding character
        int value = strchr(base32Chars, c) - base32Chars;
        if (value < 0) continue; // Ignore invalid characters
        buffer <<= 5;
        buffer |= value;
        bitsLeft += 5;
        if (bitsLeft >= 8) {
            output[outputIndex++] = (buffer >> (bitsLeft - 8)) & 0xFF;
            bitsLeft -= 8;
        }
    }
    return outputIndex; // Return the number of decoded bytes
}

void formatBase32Key(const char* rawKey, char* formattedKey) {
    size_t len = strlen(rawKey);
    size_t j = 0; // Index for the formatted key
    for (size_t i = 0; i < len; i++) {
        char c = rawKey[i];
        // Check if the character is alphanumeric
        if (isalnum(c)) {
            formattedKey[j++] = toupper(c); // Add to formattedKey as uppercase
        }
    }
    formattedKey[j] = '\0'; // Null-terminate the string
}

void saveConfig() {
    EEPROM.put(SECRET_KEY_ADDRESS, base32Key);
    EEPROM.put(SSID_ADDRESS, ssid);
    EEPROM.put(PASSWORD_ADDRESS, password);
    EEPROM.commit();
}

void loadConfig() {
    EEPROM.get(SECRET_KEY_ADDRESS, base32Key);
    EEPROM.get(SSID_ADDRESS, ssid);
    EEPROM.get(PASSWORD_ADDRESS, password);
    
    // Check if the loaded values are valid
    if (base32Key[0] == 0xFF) base32Key[0] = '\0';
    if (ssid[0] == 0xFF) ssid[0] = '\0';
    if (password[0] == 0xFF) password[0] = '\0';
}

void handleRoot() {
    String html = "<html><body>";
    html += "<h1>TOTP Configuration</h1>";
    html += "<form action='/setconfig' method='post'>";
    html += "Secret Key: <input type='text' name='secretkey' value='" + String(base32Key) + "'><br>";
    html += "WiFi SSID: <input type='text' name='ssid' value='" + String(ssid) + "'><br>";
    html += "WiFi Password: <input type='password' name='password' value='" + String(password) + "'><br>";
    html += "<input type='submit' value='Update Configuration'>";
    html += "</form></body></html>";
    server.send(200, "text/html", html);
}

void handleSetConfig() {
    bool configChanged = false;

    if (server.hasArg("secretkey")) {
        String newKey = server.arg("secretkey");
        if (newKey != String(base32Key)) {
            strncpy(base32Key, newKey.c_str(), MAX_SECRET_KEY_LENGTH - 1);
            base32Key[MAX_SECRET_KEY_LENGTH - 1] = '\0';
            configChanged = true;
        }
    }

    if (server.hasArg("ssid")) {
        String newSSID = server.arg("ssid");
        if (newSSID != String(ssid)) {
            strncpy(ssid, newSSID.c_str(), MAX_SSID_LENGTH - 1);
            ssid[MAX_SSID_LENGTH - 1] = '\0';
            configChanged = true;
        }
    }

    if (server.hasArg("password")) {
        String newPassword = server.arg("password");
        if (newPassword != String(password)) {
            strncpy(password, newPassword.c_str(), MAX_PASSWORD_LENGTH - 1);
            password[MAX_PASSWORD_LENGTH - 1] = '\0';
            configChanged = true;
        }
    }

    if (configChanged) {
        saveConfig();
        server.send(200, "text/plain", "Configuration updated successfully. Restarting...");
        delay(1000);
        ESP.restart();
    } else {
        server.send(200, "text/plain", "No changes in configuration");
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial);
    Serial.println("TOTP Generator Starting...");

    // Initialize LED
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);  // Turn off LED initially

    // Initialize EEPROM and load config (unchanged)
    EEPROM.begin(EEPROM_SIZE);
    loadConfig();

    // Set up Access Point
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("TOTP_Config_AP", "password");
    Serial.print("Access Point IP: ");
    Serial.println(WiFi.softAPIP());

    // Try to connect to saved WiFi
    WiFi.begin(ssid, password);
    unsigned long startAttemptTime = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
        delay(1000);
        Serial.println("Attempting to connect to WiFi...");
        digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));  // Blink LED while connecting
    }

    if (WiFi.status() == WL_CONNECTED) {
        digitalWrite(STATUS_LED, HIGH);  // Solid LED when connected
        Serial.println("Connected to WiFi");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());

    } 
    else {
        Serial.println("Failed to connect to saved WiFi. Using AP mode only.");
        // Blink LED rapidly in AP-only mode
        for (int i = 0; i < 10; i++) {
            digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
            delay(100);
        }
    }

    // Start NTP client
    timeClient.begin();

    // Set up web server routes
    server.on("/", handleRoot);
    server.on("/setconfig", HTTP_POST, handleSetConfig);
    server.begin();
    Serial.println("Web server started");

    // Initial TOTP setup (unchanged)
    char formattedKey[MAX_SECRET_KEY_LENGTH];
    formatBase32Key(base32Key, formattedKey);
    size_t hmacKeyLen = base32_decode(formattedKey, hmacKey);
    totp = TOTP(hmacKey, hmacKeyLen);
}

void loop() {
    server.handleClient();

    // Update the time and TOTP code (unchanged)
    timeClient.update();
    String newCode = totp.getCode(timeClient.getEpochTime());
    if (totpCode != newCode) {
        totpCode = newCode;
        Serial.print("TOTP code: ");
        Serial.println(newCode);
    }

    delay(1000);
}