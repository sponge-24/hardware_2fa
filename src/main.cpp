/*
 * ESP32 TOTP Generator
 * 
 * This device generates Time-based One-Time Passwords (TOTP) and displays them
 * on an OLED screen. It includes WiFi connectivity for time synchronization
 * and a web interface for configuration.
 * 
 * Hardware Requirements:
 * - ESP32 Board
 * - SSD1306 OLED Display (128x32)
 * - RTC DS1302 Module
 * - Push Button
 * - Status LED
 */

#include <WiFi.h>           // WiFi connectivity
#include <Wire.h>           // I2C communication
#include <NTPClient.h>      // Network Time Protocol client
#include <TOTP.h>           // Time-based One-Time Password generation
#include <WebServer.h>      // Web server for configuration interface
#include <Adafruit_GFX.h>       // Graphics library
#include <Adafruit_SSD1306.h>   // OLED display driver
#include <EEPROM.h>             // Persistent storage
#include <ThreeWire.h>          // RTC communication
#include <RtcDS1302.h>          // RTC module driver


// RTC module pin definitions
#define CLOCK_PIN 16     // CLK/SCL pin for RTC
#define DATA_PIN 17     // DAT/SDA pin for RTC
#define RST_PIN 18     // RST/CE pin for RTC

// Initialize RTC module
ThreeWire myWire(DATA_PIN, CLOCK_PIN, RST_PIN);
RtcDS1302<ThreeWire> Rtc(myWire);


#define PUSH_BUTTON 19 // Configuration mode trigger button

// OLED display configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_ADDRESS 0x3C   // I2C address of the display

// Initialize OLED display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// EEPROM configuration for storing settings
#define EEPROM_SIZE 1024
#define SECRET_KEY_ADDRESS 0        // Address for storing TOTP secret key
#define ACCOUNT_NAME_ADDRESS 128    // Address for storing account name
#define SSID_ADDRESS 256            // Address for storing WiFi SSID
#define PASSWORD_ADDRESS 384        // Address for storing WiFi password

// Maximum length constraints for configuration strings
#define MAX_SECRET_KEY_LENGTH 128
#define MAX_ACCOUNT_NAME_LENGTH 64
#define MAX_SSID_LENGTH 32
#define MAX_PASSWORD_LENGTH 64


#define STATUS_LED 23  // Built-in LED for status indication

// Configuration storage
char ssid[MAX_SSID_LENGTH] = "";
char password[MAX_PASSWORD_LENGTH] = "";
char base32Key[MAX_SECRET_KEY_LENGTH] = "";
char accountName[MAX_ACCOUNT_NAME_LENGTH] = "";

// Initialize network time client and web server
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
WebServer server(80);

// TOTP generation variables
uint8_t hmacKey[50];    // Storage for decoded HMAC key
TOTP totp = TOTP(hmacKey, sizeof(hmacKey));
String totpCode;

/**
 * Decodes a base32 encoded string into a byte array
 * @param base32 Input base32 string
 * @param output Output byte array
 * @return Number of decoded bytes
 */

int base32_decode(const char *base32, uint8_t *output) {
    const char *base32Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    int buffer = 0;
    int bitsLeft = 0;
    int outputIndex = 0;

    // Process each character in the input string
    for (size_t i = 0; base32[i] != '\0'; i++) {
        char c = base32[i];
        if (c == '=') break;    // Stop at padding

        // Find the value of the current character in base32 alphabet
        int value = strchr(base32Chars, c) - base32Chars;
        if (value < 0) continue; // Skip invalid characters

        // Add 5 bits to buffer
        buffer <<= 5;
        buffer |= value;
        bitsLeft += 5;

        // Extract complete bytes from buffer
        if (bitsLeft >= 8) {
            output[outputIndex++] = (buffer >> (bitsLeft - 8)) & 0xFF;
            bitsLeft -= 8;
        }
    }
    return outputIndex; // Return the number of decoded bytes
}

/**
 * Formats a raw key into proper base32 format
 * Removes spaces and converts to uppercase
 * @param rawKey Input key string
 * @param formattedKey Output formatted key
 */

void formatBase32Key(const char* rawKey, char* formattedKey) {
    size_t len = strlen(rawKey);
    size_t j = 0; 
    for (size_t i = 0; i < len; i++) {
        char c = rawKey[i];
        // Check if the character is alphanumeric
        if (isalnum(c)) {
            formattedKey[j++] = toupper(c); // Add to formattedKey as uppercase
        }
    }
    formattedKey[j] = '\0'; // Null-terminate the string
}

/**
 * Saves current configuration to EEPROM
 * Stores TOTP secret, account name, and WiFi credentials
 */
void saveConfig() {
    EEPROM.put(SECRET_KEY_ADDRESS, base32Key);
    EEPROM.put(ACCOUNT_NAME_ADDRESS, accountName);
    EEPROM.put(SSID_ADDRESS, ssid);
    EEPROM.put(PASSWORD_ADDRESS, password);
    EEPROM.commit();
}

/**
 * Loads configuration from EEPROM
 * Includes validation of loaded values
 */
void loadConfig() {
    EEPROM.get(SECRET_KEY_ADDRESS, base32Key);
    EEPROM.get(ACCOUNT_NAME_ADDRESS, accountName);
    EEPROM.get(SSID_ADDRESS, ssid);
    EEPROM.get(PASSWORD_ADDRESS, password);
    
    // Validate loaded values
    if (base32Key[0] == 0xFF) base32Key[0] = '\0';
    if (accountName[0] == 0xFF) accountName[0] = '\0';
    if (ssid[0] == 0xFF) ssid[0] = '\0';
    if (password[0] == 0xFF) password[0] = '\0';
}

/**
 * Handles the root webpage request
 * Serves the configuration interface if accessed via AP
 * Implements basic security by checking client IP
 */
void handleRoot() {

    // Only allow connections via access points
    IPAddress clientIP = server.client().remoteIP();
    if (clientIP[0] == 192 && clientIP[1] == 168 && clientIP[2] == 4){
        // Generate the HTML configuration page
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

/**
 * Handles configuration updates from the web interface
 * Updates settings and saves to EEPROM if changes are detected
 * Restarts the device after configuration changes
 */
void handleSetConfig() {

    bool configChanged = false;
    
    // Update account name if provided
    if (server.hasArg("accountname")) {
        strncpy(accountName, server.arg("accountname").c_str(), MAX_ACCOUNT_NAME_LENGTH - 1);
        accountName[MAX_ACCOUNT_NAME_LENGTH - 1] = '\0';
        configChanged = true;
    }

    // Update secret key if provided
    if (server.hasArg("secretkey")) {
        String newKey = server.arg("secretkey");
        if (newKey != String(base32Key)) {
            strncpy(base32Key, newKey.c_str(), MAX_SECRET_KEY_LENGTH - 1);
            base32Key[MAX_SECRET_KEY_LENGTH - 1] = '\0';
            configChanged = true;
        }
    }

    // Update WiFi SSID if provided
    if (server.hasArg("ssid")) {
        String newSSID = server.arg("ssid");
        if (newSSID != String(ssid)) {
            strncpy(ssid, newSSID.c_str(), MAX_SSID_LENGTH - 1);
            ssid[MAX_SSID_LENGTH - 1] = '\0';
            configChanged = true;
        }
    }

    // Update WiFi password if provided
    if (server.hasArg("password")) {
        String newPassword = server.arg("password");
        if (newPassword != String(password)) {
            strncpy(password, newPassword.c_str(), MAX_PASSWORD_LENGTH - 1);
            password[MAX_PASSWORD_LENGTH - 1] = '\0';
            configChanged = true;
        }
    }

    // Save changes and restart if necessary
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


/**
 * Initial setup function
 * Initializes hardware components and establishes connections
 */
void setup() {

    // Initialize serial communication
    Serial.begin(115200);
    while (!Serial);
    Serial.println("TOTP Generator Starting...");

    // Initialize hardware pins
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);
    pinMode(PUSH_BUTTON, INPUT_PULLUP);

    // Initialize EEPROM and load configuration
    EEPROM.begin(EEPROM_SIZE);
    loadConfig();

    // Initialize OLED display
    if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;);
    }
    display.clearDisplay();
    display.display();
    
    // Check button state for AP mode
    if (digitalRead(PUSH_BUTTON) == LOW) {
        // Enable Access Point mode
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP("TOTP_Config_AP", "password");
        Serial.print("Access Point IP: ");
        Serial.println(WiFi.softAPIP());
    } else {
        // Disable Access Point mode
        WiFi.mode(WIFI_STA);
        Serial.println("Access Point disabled");
    }

    // Initialize time services
    timeClient.begin();
    Rtc.Begin();

     // Connect to WiFi
    WiFi.begin(ssid, password);
    unsigned long startAttemptTime = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
        delay(1000);
        Serial.println("Attempting to connect to WiFi...");
        digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));  // Blink LED while connecting
    }

    // Update RTC if WiFi connected
    if (WiFi.status() == WL_CONNECTED) {

        digitalWrite(STATUS_LED, HIGH);
        Serial.println("Connected to WiFi");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());

        timeClient.update();
        RtcDateTime timeToSet;
        timeToSet.InitWithEpoch32Time(timeClient.getEpochTime());
        Rtc.SetDateTime(timeToSet);

    }

    // Configure web server
    server.on("/", handleRoot);
    server.on("/setconfig", HTTP_POST, handleSetConfig);
    server.begin();
    Serial.println("Web server started");

    // Initialize TOTP generator
    char formattedKey[MAX_SECRET_KEY_LENGTH];
    formatBase32Key(base32Key, formattedKey);
    size_t hmacKeyLen = base32_decode(formattedKey, hmacKey);
    totp = TOTP(hmacKey, hmacKeyLen);
}

/**
 * Draws a vertical progress bar for TOTP time remaining
 * @param x X coordinate of the bar
 * @param y Y coordinate of the bar (bottom)
 * @param width Width of the bar
 * @param height Height of the bar
 * @param progress Progress value (0.0 to 1.0)
 */
void drawWaterLevel(int x, int y, int width, int height, float progress) {
    // Calculate filled height based on progress (inverted for countdown effect)
    int filledHeight = (height * progress);
    
    // Draw the outer rectangle frame
    display.drawRect(x, y - height, width, height, SSD1306_WHITE);
    
    // Fill the rectangle
    display.fillRect(x, y - filledHeight, width, filledHeight, SSD1306_WHITE);
}


/**
 * Main loop function
 * Handles:
 * - Web server requests
 * - Push button monitoring
 * - TOTP code generation
 * - Display updates
 * - Progress bar animation
 */
void loop() {

    // Handle any pending web server requests
    server.handleClient();

    // Monitor push button for AP mode toggle
    if (digitalRead(PUSH_BUTTON) == LOW) {
        // Toggle Access Point mode when button is pressed
        if (WiFi.getMode() == WIFI_AP_STA) {
            // Disable Access Point mode
            WiFi.mode(WIFI_STA);
            Serial.println("Access Point disabled");
        } else {
            // Enable Access Point mode
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAP("TOTP_Config_AP", "password");
            Serial.print("Access Point IP: ");
            Serial.println(WiFi.softAPIP());
        }
    }

    // Get current time from RTC
    RtcDateTime now = Rtc.GetDateTime();

    // Generate new TOTP code
    String newCode = totp.getCode(now.Epoch32Time());
    
    // Calculate remaining time for current TOTP code
    unsigned long currentEpochTime =now.Epoch32Time();
    int remainingTime = 30 - (currentEpochTime % 30);  // TOTP changes every 30 seconds

    // Update and log new TOTP code if changed
    if (totpCode != newCode) {
        totpCode = newCode;
        Serial.print("TOTP code for ");
        Serial.print(accountName);
        Serial.print(": ");
        Serial.println(totpCode);
    }

    // Update OLED display
    display.clearDisplay();

    // Display account name
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0); 
    display.print(accountName);

     // Add AP mode indicator if active
    if (WiFi.getMode() == WIFI_AP_STA) {
        display.setCursor(112, 0);
        display.print("AP"); 
    }
    display.println();

    // Display TOTP code in larger font
    display.setCursor(0, 16); 
    display.setTextSize(2);  
    display.println(totpCode);

    // Draw countdown progress bar
    drawWaterLevel(96, 28, 8, 20, remainingTime / 30.0f);

    display.display();
    
    // Short delay for smoother animation
    delay(300);  
}