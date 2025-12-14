#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <U8x8lib.h>
#include "time.h"
#include <vector>

// ==========================================
// 1. CONFIGURACIÓN DE USUARIO
// ==========================================
const char* ssid = "DIGIFIBRA-5HQ3";
const char* password = "s2UHCFeDcG"; // <--- NO OLVIDES PONER LA CONTRASEÑA DE CASA
const char* mqtt_server = "192.168.1.146";
const int mqtt_port = 1883;
const char* mqtt_topic = "sensores/clima";

const char* ntpServer = "pool.ntp.org";

#define I2C_SDA 4
#define I2C_SCL 15
#define HIH_ADDRESS 0x27

// ==========================================
// 2. OBJETOS Y VARIABLES GLOBALES
// ==========================================
WiFiClient espClient;
PubSubClient client(espClient);
U8X8_SSD1306_128X64_NONAME_HW_I2C display(/*rst*/ 16, /*scl*/ 15, /*sda*/ 4);

struct DatoClima {
  float temp;
  float hum;
  time_t timestamp;
};
std::vector<DatoClima> bufferOffline;
const int MAX_BUFFER = 2000;

unsigned long lastMeasure = 0;

// ==========================================
// 3. FUNCIONES AUXILIARES (INTOCABLES PERO ROBUSTAS)
// ==========================================

bool leerHIH(float& t, float& h) {
  Wire.beginTransmission(HIH_ADDRESS);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;

  delay(50);

  Wire.requestFrom(HIH_ADDRESS, 4);
  if (Wire.available() < 4) return false;

  byte b1 = Wire.read();
  byte b2 = Wire.read();
  byte b3 = Wire.read();
  byte b4 = Wire.read();

  byte status = (b1 >> 6) & 0x03;
  if (status == 3) return false;

  unsigned int rawHum = ((b1 & 0x3F) << 8) | b2;
  unsigned int rawTemp = (b3 << 6) | (b4 >> 2);

  h = rawHum * 6.10e-3;
  t = (rawTemp * 1.007e-2) - 40.0;

  return true;
}

void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  display.clearDisplay(); // Limpiar pantalla para mensajes claros
  display.drawString(0, 0, "Conectando WiFi");
  display.drawString(0, 1, ssid);
  
  Serial.print("Conectando a: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA); // Asegurar modo estación
  WiFi.disconnect();   // Desconectar limpio
  delay(100);
  WiFi.begin(ssid, password);

  int i = 0;
  while (WiFi.status() != WL_CONNECTED && i < 20) { // 20 intentos de 500ms = 10s
    delay(500);
    Serial.print(".");
    char buf[16];
    sprintf(buf, "Intento %d/20", i+1);
    display.drawString(0, 2, buf);
    i++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    display.drawString(0, 3, "Conectado!");
    display.drawString(0, 4, WiFi.localIP().toString().c_str());
    Serial.println("\nWiFi Conectado!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    delay(2000); // Para poder leer la IP
  } else {
    display.drawString(0, 3, "Fallo WiFi");
    char bufErr[16];
    sprintf(bufErr, "ErrCode: %d", WiFi.status());
    display.drawString(0, 4, bufErr);
    Serial.print("\nFallo conexion. Code: ");
    Serial.println(WiFi.status());
    delay(2000);
  }
}

void reconectarMQTT() {
  if (!espClient.connected()) {
    if (WiFi.status() == WL_CONNECTED) {
      // ID aleatorio para evitar conflictos
      String clientId = "ESP32_HIH_" + String(random(0xffff), HEX);
      if (client.connect(clientId.c_str())) {
        display.drawString(0, 1, "MQTT OK     ");
        // Re-suscripción si fuera necesario iría aquí
      } else {
        char bufErr[16];
        sprintf(bufErr, "MQTT Err:%d", client.state());
        display.drawString(0, 1, bufErr);
      }
    }
  }
}

// ==========================================
// 4. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);

  display.begin();
  display.setFont(u8x8_font_chroma48medium8_r);
  display.clearDisplay();
  display.drawString(0, 0, "ARRANCANDO...");

  // ESCÁNER DE REDES
  Serial.println("Escaneando redes WiFi...");
  int n = WiFi.scanNetworks();
  Serial.println("Escaneo terminado");
  if (n == 0) {
      Serial.println("NO se encontraron redes");
  } else {
      Serial.print(n);
      Serial.println(" redes encontradas:");
      for (int i = 0; i < n; ++i) {
          // Imprimimos SSID y la intensidad de señal (RSSI)
          Serial.print(i + 1);
          Serial.print(": ");
          Serial.print(WiFi.SSID(i));
          Serial.print(" (");
          Serial.print(WiFi.RSSI(i));
          Serial.println(")");
          delay(10);
      }
  }

  conectarWiFi();

  // Configuración de hora más agresiva para España/Europa
  configTime(3600, 3600, ntpServer);
  display.drawString(0, 2, "Sync Hora...");

  client.setServer(mqtt_server, mqtt_port);
}

// ==========================================
// 5. LOOP PRINCIPAL
// ==========================================
void loop() {
  conectarWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) reconectarMQTT();
    client.loop();
  }

  time_t now;
  time(&now);

  // Validación de hora (Año > 2023)
  bool horaEsValida = (now > 1700000000);

  // Debug rápido de hora en Serial por si acaso
  if (millis() % 10000 == 0) {
    if (horaEsValida) Serial.printf("Hora OK: %lu\n", (unsigned long)now);
    else Serial.println("Hora NO sincronizada (1970)");
  }

  if (millis() - lastMeasure > 5000) {
    lastMeasure = millis();

    float temp = 0;
    float hum = 0;
    bool lecturaOk = leerHIH(temp, hum);

    display.clearLine(4);
    display.clearLine(5);
    display.clearLine(6);
    display.clearLine(7);

    if (lecturaOk) {
      char buf[20];
      snprintf(buf, sizeof(buf), "T: %.1f H: %.0f", temp, hum);
      display.drawString(0, 4, buf);

      // ARREGLO 1: Visualizar estado NTP en pantalla
      if (horaEsValida) {
        snprintf(buf, sizeof(buf), "TS: %lu", (unsigned long)now);
        display.drawString(0, 5, buf);
      } else {
        display.drawString(0, 5, "TS: --NTP--");  // Aviso visual
      }

      // --- LÓGICA DE ENVÍO BLINDADA ---

      if (WiFi.status() == WL_CONNECTED && client.connected()) {

        // A) VACIAR BUFFER (Solo si tenemos hora válida, para no enviar basura histórica)
        if (horaEsValida && !bufferOffline.empty()) {
          display.drawString(0, 6, "Subiendo Buff...");

          auto it = bufferOffline.begin();
          while (it != bufferOffline.end()) {
            char jsonHist[200];
            
            // >>> CAMBIO 1: Eliminado "tipo":"hist" para unificar la gráfica <<<
            snprintf(jsonHist, sizeof(jsonHist),
                     "{\"temp\":%.2f, \"hum\":%.2f, \"ts\":%lu}",
                     it->temp, it->hum, (unsigned long)it->timestamp);

            if (client.publish(mqtt_topic, jsonHist)) {
              it = bufferOffline.erase(it);
              delay(50);
            } else {
              break;
            }
          }
        }
        display.clearLine(6);

        // B) ENVIAR DATO ACTUAL (LIVE) - ¡¡LA CLAVE!!
        char jsonLive[200];

        if (horaEsValida) {
          // Opción Ideal: Enviamos con timestamp
          // >>> CAMBIO 2: Eliminado "tipo":"live" <<<
          snprintf(jsonLive, sizeof(jsonLive),
                   "{\"temp\":%.2f, \"hum\":%.2f, \"ts\":%lu}",
                   temp, hum, (unsigned long)now);
        } else {
          // ARREGLO 3: PLAN DE EMERGENCIA
          // Si no hay hora NTP, enviamos SIN "ts".
          // >>> CAMBIO 3: Eliminado "tipo" también aquí por consistencia <<<
          snprintf(jsonLive, sizeof(jsonLive),
                   "{\"temp\":%.2f, \"hum\":%.2f}",
                   temp, hum);
          Serial.println("WARN: Enviando sin TS (Fallo NTP)");
        }

        client.publish(mqtt_topic, jsonLive);
        display.drawString(0, 7, ">> ENVIADO OK");
        Serial.print("TX: ");
        Serial.println(jsonLive);

      }
      // CASO OFFLINE (Solo guardar si tenemos hora fiable)
      else {
        if (horaEsValida) {
          if (bufferOffline.size() < MAX_BUFFER) {
            DatoClima dato = { temp, hum, now };
            bufferOffline.push_back(dato);

            char bufInfo[20];
            snprintf(bufInfo, sizeof(bufInfo), "Buff: %d", bufferOffline.size());
            display.drawString(0, 6, "OFFLINE");
            display.drawString(0, 7, bufInfo);
          } else {
            bufferOffline.erase(bufferOffline.begin());
            DatoClima dato = { temp, hum, now };
            bufferOffline.push_back(dato);
            display.drawString(0, 7, "Buff FULL");
          }
        } else {
          // Sin WiFi y sin Hora... estamos jodidos, solo mostramos en pantalla
          display.drawString(0, 6, "NO WIFI/NO NTP");
        }
      }

    } else {
      display.drawString(0, 4, "Err Sensor HIH");
      display.drawString(0, 5, "Check Cables");
    }
  }
}