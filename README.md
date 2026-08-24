# Webcam Pico LTE

Firmware iniziale per una webcam autonoma a basso consumo basata su:

- Raspberry Pi Pico / RP2040
- Arducam Mega 3 MP B0434 (SPI, JPEG onboard)
- modem SIMCom SIM7020 / SIM7020G (NB-IoT)
- alimentazione 5 V da batteria/pannello

## Obiettivo

Una foto JPEG ogni ~6 minuti, caricata direttamente al server senza tenere l'intera immagine nella RAM della Pico.

La Arducam genera il JPEG internamente; la Pico lo legge a blocchi SPI e lo inoltra al SIM7020 tramite socket TCP. Il modem usa `AT+CSODSEND`, che consente invii binari a blocchi fino a 768 byte.

## Stato

Questa è una **prima versione hardware-oriented** da provare sul banco. Sono già implementati:

- inizializzazione Arducam Mega
- JPEG 2048x1536 (`CAM_IMAGE_MODE_QXGA`)
- qualità JPEG selezionabile
- lettura JPEG a blocchi (nessun buffer immagine completo in RAM)
- registrazione NB-IoT
- DNS tramite `AT+CDNSGIP`
- socket TCP SIM7020
- upload HTTP `POST` con `Content-Length`
- intervallo di 6 minuti
- low-power mode della Arducam fra gli scatti

Da verificare sull'hardware reale:

- APN della SIM
- pin UART effettivi della board SIM7020
- eventuale pin PWRKEY / enable del modem
- endpoint HTTP definitivo
- comportamento PSM del modem con l'operatore
- assorbimenti reali

## Configurazione

1. Copia `config.example.h` in `config.h`.
2. Imposta `APN`, `SERVER_HOST`, `SERVER_PORT`, `SERVER_PATH` ed eventuale token.
3. Installa la libreria ufficiale [ArduCAM/Arducam_Mega](https://github.com/ArduCAM/Arducam_Mega).
4. Compila `webcam_pico.ino` con un core Arduino compatibile Raspberry Pi Pico.

### Collegamento Arducam Mega

Il firmware parte dalla stessa assegnazione usata dall'esempio ufficiale Arducam per Pico:

- CS: GP17
- SPI: bus SPI standard della Pico
- VCC/GND secondo il modulo

I pin sono modificabili in `config.h`.

### Upload

Il server deve accettare un body JPEG puro:

```http
POST /webcam/upload HTTP/1.1
Host: example.com
Content-Type: image/jpeg
Content-Length: ...
X-Device-Token: ...

<jpeg binary>
```

Per ora il trasporto è **HTTP/TCP**, non HTTPS. Prima del deployment pubblico conviene aggiungere TLS lato SIM7020 oppure terminare la connessione su un endpoint privato/VPN.

## Frequenza

Default: `360000 ms` = 6 minuti fra l'inizio di uno scatto e il successivo.

## Nota energetica

Il vantaggio della soluzione è che la Pico non elabora il frame: il JPEG viene creato dalla camera. Fra gli scatti la camera viene posta in low-power mode. La gestione PSM/power-gating del SIM7020 verrà rifinita dopo la prova con la board reale, perché dipende anche dal breakout/modulo e dall'operatore.
