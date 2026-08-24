#pragma once

// ---- Camera ----
#define CAMERA_CS_PIN 17
#define CAMERA_WARMUP_MS 1200

// Arducam Mega 3 MP B0434: 2048x1536 JPEG.
#define CAMERA_IMAGE_MODE CAM_IMAGE_MODE_QXGA
#define CAMERA_JPEG_QUALITY HIGH_QUALITY

// ---- SIM7020 UART ----
#define MODEM_BAUD 115200
#define MODEM_TX_PIN 4   // Pico TX -> modem RX
#define MODEM_RX_PIN 5   // Pico RX <- modem TX

// APN. Lascia stringa vuota se la SIM si configura automaticamente.
#define APN ""

// ---- Server ----
// Prima versione: HTTP/TCP, non HTTPS.
#define SERVER_HOST "example.com"
#define SERVER_PORT 80
#define SERVER_PATH "/webcam/upload"

// Header opzionale per autenticare il dispositivo.
// Lascia vuoto per non inviare X-Device-Token.
#define DEVICE_TOKEN ""

// 6 minuti fra l'inizio di uno scatto e il successivo.
#define SHOT_INTERVAL_MS (6UL * 60UL * 1000UL)

// Timeouts NB-IoT: volutamente larghi.
#define MODEM_COMMAND_TIMEOUT_MS 5000UL
#define MODEM_NETWORK_TIMEOUT_MS 180000UL
#define MODEM_DNS_TIMEOUT_MS 60000UL
#define MODEM_SOCKET_TIMEOUT_MS 30000UL
#define MODEM_SEND_TIMEOUT_MS 30000UL
