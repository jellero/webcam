#include <Arduino.h>
#include <SPI.h>
#include <Arducam_Mega.h>
#include "config.h"

// Arducam official Pico example uses CS on GP17.
Arducam_Mega camera(CAMERA_CS_PIN);

static constexpr size_t CAMERA_READ_CHUNK = 240; // Arducam readBuff() must stay <255
static constexpr size_t SOCKET_CHUNK = 720;      // SIM7020 CSODSEND supports up to 768 bytes
static uint8_t socketBuffer[SOCKET_CHUNK];

static void modemFlushInput()
{
    while (Serial1.available()) {
        Serial1.read();
    }
}

static bool modemReadUntil(const char *needle, uint32_t timeoutMs, String *captured = nullptr)
{
    String response;
    response.reserve(1024);

    const uint32_t started = millis();
    while (millis() - started < timeoutMs) {
        while (Serial1.available()) {
            const char c = (char)Serial1.read();
            if (response.length() < 2048) {
                response += c;
            }

            if (response.indexOf(needle) >= 0) {
                if (captured) *captured = response;
                return true;
            }

            if (response.indexOf("ERROR") >= 0) {
                if (captured) *captured = response;
                return false;
            }
        }
        delay(1);
    }

    if (captured) *captured = response;
    return false;
}

static bool modemCommand(const String &command,
                         const char *expected = "OK",
                         uint32_t timeoutMs = MODEM_COMMAND_TIMEOUT_MS,
                         String *captured = nullptr)
{
    modemFlushInput();
    Serial1.print(command);
    Serial1.print("\r\n");
    return modemReadUntil(expected, timeoutMs, captured);
}

static bool modemRegistered()
{
    String response;
    if (!modemCommand("AT+CGREG?", "OK", MODEM_COMMAND_TIMEOUT_MS, &response)) {
        return false;
    }

    // 1 = registered home, 5 = registered roaming.
    return response.indexOf(",1") >= 0 || response.indexOf(",5") >= 0;
}

static bool modemEnsureNetwork()
{
    if (!modemCommand("AT")) return false;
    modemCommand("ATE0");

    if (strlen(APN) > 0) {
        String cmd = "AT+CGDCONT=1,\"IP\",\"";
        cmd += APN;
        cmd += "\"";
        if (!modemCommand(cmd)) return false;
    }

    // Harmless if RF is already fully enabled.
    modemCommand("AT+CFUN=1", "OK", 10000);

    const uint32_t started = millis();
    while (millis() - started < MODEM_NETWORK_TIMEOUT_MS) {
        if (modemRegistered()) {
            modemCommand("AT+CGATT=1", "OK", 30000);
            // Many SIM7020 networks auto-activate CID 1. If not, try explicitly.
            modemCommand("AT+CGACT=1,1", "OK", 30000);
            return true;
        }
        delay(2000);
    }

    return false;
}

static bool modemResolveHost(const char *host, String &ip)
{
    // If an IPv4 literal was configured, avoid DNS entirely.
    bool onlyDigitsAndDots = true;
    for (const char *p = host; *p; ++p) {
        if ((*p < '0' || *p > '9') && *p != '.') {
            onlyDigitsAndDots = false;
            break;
        }
    }
    if (onlyDigitsAndDots) {
        ip = host;
        return true;
    }

    modemFlushInput();
    Serial1.print("AT+CDNSGIP=\"");
    Serial1.print(host);
    Serial1.print("\"\r\n");

    String response;
    if (!modemReadUntil("+CDNSGIP:", MODEM_DNS_TIMEOUT_MS, &response)) {
        return false;
    }

    // The URC may still be arriving after the first marker.
    const uint32_t extraStarted = millis();
    while (millis() - extraStarted < 1500) {
        while (Serial1.available()) {
            response += (char)Serial1.read();
        }
        if (response.indexOf("\r\n") >= 0 && response.indexOf("+CDNSGIP: 1") >= 0) break;
        delay(1);
    }

    if (response.indexOf("+CDNSGIP: 1") < 0) return false;

    // Response form: +CDNSGIP: 1,"host","ip"[,"ip2"]
    int lastQuote = response.lastIndexOf('"');
    if (lastQuote < 0) return false;
    int previousQuote = response.lastIndexOf('"', lastQuote - 1);
    if (previousQuote < 0) return false;

    ip = response.substring(previousQuote + 1, lastQuote);
    return ip.length() > 0;
}

static int modemOpenTcp(const String &ip, uint16_t port)
{
    String response;
    if (!modemCommand("AT+CSOC=1,1,1", "OK", MODEM_SOCKET_TIMEOUT_MS, &response)) {
        return -1;
    }

    int marker = response.indexOf("+CSOC:");
    if (marker < 0) return -1;
    marker += 6;
    while (marker < (int)response.length() && response[marker] == ' ') marker++;
    const int socketId = response.substring(marker).toInt();

    String cmd = "AT+CSOCON=";
    cmd += socketId;
    cmd += ',';
    cmd += port;
    cmd += ",\"";
    cmd += ip;
    cmd += "\"";

    if (!modemCommand(cmd, "OK", MODEM_SOCKET_TIMEOUT_MS)) {
        modemCommand(String("AT+CSOCL=") + socketId);
        return -1;
    }

    modemCommand("AT+CSOSENDFLAG=0");
    return socketId;
}

static void modemCloseSocket(int socketId)
{
    if (socketId < 0) return;
    modemCommand(String("AT+CSOCL=") + socketId, "OK", 10000);
}

static bool modemSendSocket(int socketId, const uint8_t *data, size_t length)
{
    if (length == 0 || length > 768) return false;

    modemFlushInput();
    Serial1.print("AT+CSODSEND=");
    Serial1.print(socketId);
    Serial1.print(',');
    Serial1.print(length);
    Serial1.print("\r\n");

    if (!modemReadUntil(">", 10000)) return false;

    const size_t written = Serial1.write(data, length);
    Serial1.flush();
    if (written != length) return false;

    return modemReadUntil("DATA ACCEPT:", MODEM_SEND_TIMEOUT_MS);
}

static bool modemSendSocket(int socketId, const String &text)
{
    return modemSendSocket(socketId,
                           reinterpret_cast<const uint8_t *>(text.c_str()),
                           text.length());
}

static bool uploadCurrentJpeg()
{
    const uint32_t jpegLength = camera.getTotalLength();
    if (jpegLength == 0) {
        Serial.println("Camera returned an empty JPEG");
        return false;
    }

    Serial.print("JPEG bytes: ");
    Serial.println(jpegLength);

    if (!modemEnsureNetwork()) {
        Serial.println("SIM7020 network registration failed");
        return false;
    }

    String ip;
    if (!modemResolveHost(SERVER_HOST, ip)) {
        Serial.println("DNS resolution failed");
        return false;
    }

    Serial.print("Server IP: ");
    Serial.println(ip);

    const int socketId = modemOpenTcp(ip, SERVER_PORT);
    if (socketId < 0) {
        Serial.println("TCP connection failed");
        return false;
    }

    String header;
    header.reserve(384);
    header += "POST ";
    header += SERVER_PATH;
    header += " HTTP/1.1\r\nHost: ";
    header += SERVER_HOST;
    header += "\r\nContent-Type: image/jpeg\r\nContent-Length: ";
    header += jpegLength;
    header += "\r\nConnection: close\r\n";
    if (strlen(DEVICE_TOKEN) > 0) {
        header += "X-Device-Token: ";
        header += DEVICE_TOKEN;
        header += "\r\n";
    }
    header += "\r\n";

    if (!modemSendSocket(socketId, header)) {
        Serial.println("HTTP header send failed");
        modemCloseSocket(socketId);
        return false;
    }

    uint32_t remaining = jpegLength;
    uint32_t sent = 0;

    while (remaining > 0) {
        size_t buffered = 0;

        while (buffered < SOCKET_CHUNK && remaining > 0) {
            const size_t room = SOCKET_CHUNK - buffered;
            uint8_t wanted = (uint8_t)min((size_t)CAMERA_READ_CHUNK, room);
            wanted = (uint8_t)min((uint32_t)wanted, remaining);

            const uint8_t got = camera.readBuff(socketBuffer + buffered, wanted);
            if (got == 0) {
                Serial.println("Unexpected end of camera FIFO");
                modemCloseSocket(socketId);
                return false;
            }

            buffered += got;
            remaining -= got;
        }

        if (!modemSendSocket(socketId, socketBuffer, buffered)) {
            Serial.println("JPEG socket send failed");
            modemCloseSocket(socketId);
            return false;
        }

        sent += buffered;
        Serial.print("Uploaded ");
        Serial.print(sent);
        Serial.print('/');
        Serial.println(jpegLength);
    }

    modemCloseSocket(socketId);
    return sent == jpegLength;
}

static bool takeAndUploadPhoto()
{
    camera.lowPowerOff();
    delay(CAMERA_WARMUP_MS);

    // Auto exposure / gain / white balance are preferable for an outdoor webcam.
    camera.setAutoExposure(0);
    camera.setAutoISOSensitive(0);
    camera.setAutoWhiteBalance(0);
    camera.setImageQuality(CAMERA_JPEG_QUALITY);

    Serial.println("Taking photo...");
    if (camera.takePicture(CAMERA_IMAGE_MODE, CAM_IMAGE_PIX_FMT_JPG) != CAM_ERR_SUCCESS) {
        Serial.println("Camera capture failed");
        camera.lowPowerOn();
        return false;
    }

    const bool ok = uploadCurrentJpeg();
    camera.lowPowerOn();
    return ok;
}

void setup()
{
    Serial.begin(115200);
    delay(1500);
    Serial.println("webcam-pico boot");

    // Earle Philhower Arduino-Pico core supports remapping Serial1 this way.
    Serial1.setTX(MODEM_TX_PIN);
    Serial1.setRX(MODEM_RX_PIN);
    Serial1.begin(MODEM_BAUD);

    SPI.begin();
    if (camera.begin() != CAM_ERR_SUCCESS) {
        Serial.println("Arducam Mega init failed");
        while (true) delay(1000);
    }

    camera.setImageQuality(CAMERA_JPEG_QUALITY);
    camera.lowPowerOn();

    Serial.println("Ready");
}

void loop()
{
    const uint32_t cycleStarted = millis();

    const bool ok = takeAndUploadPhoto();
    Serial.println(ok ? "Upload completed" : "Upload failed");

    const uint32_t elapsed = millis() - cycleStarted;
    if (elapsed < SHOT_INTERVAL_MS) {
        const uint32_t waitMs = SHOT_INTERVAL_MS - elapsed;
        Serial.print("Next shot in ms: ");
        Serial.println(waitMs);
        delay(waitMs);
    } else {
        // If NB-IoT upload takes longer than 6 minutes, do not queue frames.
        // Start the next capture immediately.
        Serial.println("Cycle exceeded interval; starting next shot immediately");
    }
}
