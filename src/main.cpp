#include <WiFi.h>
#include <NTPClient.h>
#include <TOTP.h>

const char* ssid = "your-ssid";
const char* password = "your-password";

// Replace this with your actual 80-bit base32 key (in string format)
const char* base32Key = "3mv5 uezm tmip rfvj mdlh wllx 4rmb tqir"; // Example key, replace with your own

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

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

// Prepare to store the hmac key
uint8_t hmacKey[50]; // 80 bits = 10 bytes

TOTP totp = TOTP(hmacKey, sizeof(hmacKey));
String totpCode;

void setup() {
    Serial.begin(9600);
    while (!Serial);

    Serial.println("TOTP demo\n");

    // Connect to the WiFi network
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Establishing connection to WiFi...");
    }

    Serial.print("Connected to WiFi with IP: ");
    Serial.println(WiFi.localIP());
    Serial.println();

    char formattedKey[50];

    formatBase32Key(base32Key, formattedKey);
    Serial.println(formattedKey);
    // Decode the base32 key into a byte array
    size_t hmacKeyLen = base32_decode(formattedKey, hmacKey);

    // Start the NTP client
    timeClient.begin();
    Serial.println("NTP client started\n");
}

void loop() {
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