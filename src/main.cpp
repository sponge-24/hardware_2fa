#include <WiFi.h>
#include <NTPClient.h>
#include <TOTP.h>

const char* ssid = "Heartstopper";
const char* password = "Charlie@69";

// Replace this with your actual 80-bit base32 key (in string format)
const char* base32Key = "JBSWY3DPEHPK3PXP"; // Example key, replace with your own

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

// Prepare to store the hmac key
uint8_t hmacKey[10]; // 80 bits = 10 bytes

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

    // Decode the base32 key into a byte array
    size_t hmacKeyLen = base32_decode(base32Key, hmacKey);

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