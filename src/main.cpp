#include <WiFi.h>
#include <NTPClient.h>
#include <TOTP.h>
#include <WebServer.h>
#include <EEPROM.h>

const char* ssid = "Heartstopper";
const char* password = "Charlie@69";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
WebServer server(80);

// EEPROM size
#define EEPROM_SIZE 512

// EEPROM address to store the secret key
#define SECRET_KEY_ADDRESS 0

// Maximum length of the secret key
#define MAX_SECRET_KEY_LENGTH 128

char base32Key[MAX_SECRET_KEY_LENGTH] = "";

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

void saveSecretKey(const char* key) {
    strncpy(base32Key, key, MAX_SECRET_KEY_LENGTH - 1);
    base32Key[MAX_SECRET_KEY_LENGTH - 1] = '\0';
    EEPROM.put(SECRET_KEY_ADDRESS, base32Key);
    EEPROM.commit();
}

void loadSecretKey() {
    EEPROM.get(SECRET_KEY_ADDRESS, base32Key);
    if (base32Key[0] == 0xFF) {
        base32Key[0] = '\0';  // No key stored yet
    }
}

void handleRoot() {
    String html = "<html><body>";
    html += "<h1>TOTP Generator</h1>";
    html += "<form action='/setkey' method='post'>";
    html += "Secret Key: <input type='text' name='secretkey' value='" + String(base32Key) + "'><br>";
    html += "<input type='submit' value='Set Key'>";
    html += "</form></body></html>";
    server.send(200, "text/html", html);
}

void handleSetKey() {
    if (server.hasArg("secretkey")) {
        String newKey = server.arg("secretkey");
        saveSecretKey(newKey.c_str());
        server.send(200, "text/plain", "Key updated successfully");
    } else {
        server.send(400, "text/plain", "Missing secret key");
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial);
    Serial.println("TOTP demo\n");

    // Initialize EEPROM
    EEPROM.begin(EEPROM_SIZE);

    // Load the secret key from EEPROM
    loadSecretKey();

    // Connect to the WiFi network
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Establishing connection to WiFi...");
    }
    Serial.print("Connected to WiFi with IP: ");
    Serial.println(WiFi.localIP());
    Serial.println();

    // Start the NTP client
    timeClient.begin();
    Serial.println("NTP client started\n");

    // Set up web server routes
    server.on("/", handleRoot);
    server.on("/setkey", HTTP_POST, handleSetKey);

    // Start the web server
    server.begin();
    Serial.println("Web server started");

    // Initial TOTP setup
    char formattedKey[MAX_SECRET_KEY_LENGTH];
    formatBase32Key(base32Key, formattedKey);
    Serial.println(formattedKey);
    size_t hmacKeyLen = base32_decode(formattedKey, hmacKey);
    totp = TOTP(hmacKey, hmacKeyLen);
}

void loop() {
    server.handleClient();

    // Update the time
    timeClient.update();

    // Generate the TOTP code and, if different from the previous one, print to screen
    String newCode = totp.getCode(timeClient.getEpochTime());
    if (totpCode != newCode) {
        totpCode = newCode;
        Serial.print("TOTP code: ");
        Serial.println(newCode);
    }

    delay(1000); // Add a delay to avoid flooding the output
}