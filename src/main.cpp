#include <WiFi.h>
#include <Wire.h>
#include <NTPClient.h>
#include <TOTP.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

// OLED display width and height
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32

// I2C address of the OLED display (usually 0x3C)
#define OLED_ADDRESS 0x3C

// Create an instance of the display object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// EEPROM size
#define EEPROM_SIZE 1024

// EEPROM addresses for storing configuration
#define EEPROM_SIZE 1024
#define SECRET_KEY_ADDRESS 0
#define ACCOUNT_NAME_ADDRESS 128
#define SSID_ADDRESS 256
#define PASSWORD_ADDRESS 384

// Maximum lengths for configuration strings
#define MAX_SECRET_KEY_LENGTH 128
#define MAX_ACCOUNT_NAME_LENGTH 64
#define MAX_SSID_LENGTH 32
#define MAX_PASSWORD_LENGTH 64

// LED pin for status indication
#define STATUS_LED 2  // Built-in LED on most ESP32 boards

char ssid[MAX_SSID_LENGTH] = "";
char password[MAX_PASSWORD_LENGTH] = "";
char base32Key[MAX_SECRET_KEY_LENGTH] = "";
char accountName[MAX_ACCOUNT_NAME_LENGTH] = "";

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
    EEPROM.put(ACCOUNT_NAME_ADDRESS, accountName);
    EEPROM.put(SSID_ADDRESS, ssid);
    EEPROM.put(PASSWORD_ADDRESS, password);
    EEPROM.commit();
}

void loadConfig() {
    EEPROM.get(SECRET_KEY_ADDRESS, base32Key);
    EEPROM.get(ACCOUNT_NAME_ADDRESS, accountName);
    EEPROM.get(SSID_ADDRESS, ssid);
    EEPROM.get(PASSWORD_ADDRESS, password);
    
    // Check if the loaded values are valid
    if (base32Key[0] == 0xFF) base32Key[0] = '\0';
    if (accountName[0] == 0xFF) accountName[0] = '\0';
    if (ssid[0] == 0xFF) ssid[0] = '\0';
    if (password[0] == 0xFF) password[0] = '\0';
}

void handleRoot() {
    IPAddress clientIP = server.client().remoteIP();
    if (clientIP[0] == 192 && clientIP[1] == 168 && clientIP[2] == 4){
    String html = "<!DOCTYPE html>";
    html += "<html lang='en'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>TOTP Configuration</title>";
    html += "<style>";
    html += "body { font-family: 'Courier New', Courier, monospace; background-color: #1d1f21; color: #e0e0e0; margin: 0; display: flex; justify-content: center; align-items: center; height: 100vh; }";
    html += ".container { background-color: #282c34; padding: 30px; border-radius: 12px; box-shadow: 0 0 20px rgba(0, 255, 153, 0.4); max-width: 400px; width: 100%; border: 1px solid #00ff99; }";
    html += "h1 { font-size: 1.8rem; color: #00ff99; text-align: center; margin-bottom: 20px; }";
    html += "label { display: block; margin: 10px 0 5px; color: #00ffcc; font-weight: bold; }";
    html += "input[type='text'], input[type='password'] { background-color: #1c1e22; border: 1px solid #00ffcc; color: #00ffcc; padding: 10px; margin: 5px 0 15px; border-radius: 6px; width: 100%; font-size: 1rem; box-sizing: border-box; }";
    html += "input[type='submit'] { background-color: #00ff99; color: black; padding: 12px; border: none; border-radius: 6px; width: 100%; font-size: 1rem; cursor: pointer; transition: background-color 0.3s ease; }";
    html += "input[type='submit']:hover { background-color: #00cc77; }";
    html += "</style></head><body>";
    html += "<div class='container'><h1>TOTP Configuration</h1>";
    html += "<form action='/setconfig' method='post'>";
    html += "<label for='accountname'>Account Name:</label>";
    html += "<input type='text' id='accountname' name='accountname' value='" + String(accountName) + "' placeholder='your account name'>";
    html += "<label for='secretkey'>Secret Key:</label>";
    html += "<input type='text' id='secretkey' name='secretkey' value='" + String(base32Key) + "' placeholder='your secret key'>";
    html += "<label for='ssid'>WiFi SSID:</label>";
    html += "<input type='text' id='ssid' name='ssid' value='" + String(ssid) + "' placeholder='Enter ssid '>";
    html += "<label for='password'>WiFi Password:</label>";
    html += "<input type='password' id='password' name='password' value='" + String(password) + "' placeholder='Enter password' required>";
    html += "<input type='submit' value='Update Configuration'>";
    html += "</form></div></body></html>";
    server.send(200, "text/html", html);
    }
    else{
      server.send(403, "text/plain", "Access forbidden: not connected via Access Point.");
    }
}


void handleSetConfig() {

    bool configChanged = false;
    
    if (server.hasArg("accountname")) {
        strncpy(accountName, server.arg("accountname").c_str(), MAX_ACCOUNT_NAME_LENGTH - 1);
        accountName[MAX_ACCOUNT_NAME_LENGTH - 1] = '\0';
        configChanged = true;
    }

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
    } 
    else {
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

    // Initialize EEPROM and load config
    EEPROM.begin(EEPROM_SIZE);
    loadConfig();

    // Initialize OLED display
    if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;);
    }
    display.clearDisplay();
    display.display();
    

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
        
    } else {
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

    // Initial TOTP setup
    char formattedKey[MAX_SECRET_KEY_LENGTH];
    formatBase32Key(base32Key, formattedKey);
    size_t hmacKeyLen = base32_decode(formattedKey, hmacKey);
    totp = TOTP(hmacKey, hmacKeyLen);
}

void drawProgressCircle(int x, int y, int radius, float progress) {
    int segments = 100;  // Number of segments to fill the circle
    int filledSegments = progress * segments;  // Proportional to progress

    for (int i = 0; i < filledSegments; i++) {
        float angleStart = 2 * PI * i / segments;
        float angleEnd = 2 * PI * (i + 1) / segments;

        int x1 = x + radius * cos(angleStart);
        int y1 = y + radius * sin(angleStart);
        int x2 = x + radius * cos(angleEnd);
        int y2 = y + radius * sin(angleEnd);

        display.drawLine(x, y, x1, y1, SSD1306_WHITE);  // Draw lines to form the circle
        display.drawLine(x, y, x2, y2, SSD1306_WHITE);  // Another segment for smoothness
    }
}

void loop() {
    server.handleClient();

    // Update the time and TOTP code
    timeClient.update();
    String newCode = totp.getCode(timeClient.getEpochTime());
    
    // Compute time left for the current TOTP code
    unsigned long currentEpochTime = timeClient.getEpochTime();
    int remainingTime = 30 - (currentEpochTime % 30);  // TOTP typically changes every 30 seconds

    if (totpCode != newCode) {
        totpCode = newCode;
        Serial.print("TOTP code for ");
        Serial.print(accountName);
        Serial.print(": ");
        Serial.println(totpCode);
    }

    // Clear and update OLED display
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);  // First line for account name
    display.println(accountName);
    display.setCursor(0, 16);  // Second line for TOTP code
    display.setTextSize(2);    // Bigger font for the code
    display.println(totpCode);

    // Draw the timeout progress circle (move left by adjusting x, e.g., 100 to 90)
    drawProgressCircle(96, 20, 7, remainingTime / 30.0f);  // Position: (x, y), radius: 10, progress from 0 to 1

    display.display();
  
    delay(100);  // Short delay for smoother animation
}