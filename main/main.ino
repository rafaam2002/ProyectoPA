#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <U8x8lib.h>
#include "time.h"
#include <vector>
#include "secrets.h" // Importar credenciales secretas

// ==========================================
// 1. CONFIGURACIÓN DE USUARIO
// ==========================================
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* mqtt_server = MQTT_SERVER;
const int mqtt_port = MQTT_PORT;
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

// Variables para lógica no bloqueante
unsigned long reconnectTime = 0;
bool esperandoBackend = false;
bool isFirstConnection = true;

// ==========================================
// 3. FUNCIONES AUXILIARES
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

  display.clearDisplay(); 
  display.drawString(0, 0, "Conectando WiFi");
  display.drawString(0, 1, ssid);
  
  Serial.print("Conectando a: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA); 
  WiFi.disconnect();   
  delay(100);
  WiFi.begin(ssid, password);

  int i = 0;
  while (WiFi.status() != WL_CONNECTED && i < 20) { 
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
    delay(2000); 
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
      String clientId = "ESP32_HIH_" + String(random(0xffff), HEX);
      if (client.connect(clientId.c_str())) {
        display.drawString(0, 1, "MQTT OK     ");
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

  Serial.println("Escaneando redes WiFi...");
  int n = WiFi.scanNetworks();
  if (n == 0) {
      Serial.println("NO se encontraron redes");
  } else {
      Serial.print(n);
      Serial.println(" redes encontradas");
  }

  conectarWiFi();

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
    if (!client.connected()) {
       reconectarMQTT();
       // Al reconectar, iniciamos el timer de espera de seguridad
       if (client.connected()) { 
           if (isFirstConnection) {
              // Primera conexión (arranque): No esperamos
              esperandoBackend = false;
              isFirstConnection = false;
              Serial.println("MQTT CONECTADO (Inicio). Sin espera.");
           } else {
              // Reconexión tras caída: Esperamos 30s
              reconnectTime = millis();
              esperandoBackend = true;
              Serial.println("MQTT RECONECTADO. Iniciando espera de 30s...");
           }
       }
    }
    client.loop();
  }

  time_t now;
  time(&now);

  bool horaEsValida = (now > 1700000000);

  if (millis() % 10000 == 0) {
    if (horaEsValida) Serial.printf("Hora OK: %lu\n", (unsigned long)now);
    else Serial.println("Hora NO sincronizada (1970)");
  }

  // BUCLE DE MEDICIÓN CADA 1000ms
  if (millis() - lastMeasure > 1000) {
    lastMeasure = millis();

    float temp = 0;
    float hum = 0;
    bool lecturaOk = leerHIH(temp, hum);

    display.clear(); // Usamos clear() en lugar de clearLine() para evitar residuos
    display.setFont(u8x8_font_chroma48medium8_r);
    
    // Titulo Fijo
    // display.drawString(0,0, "MONITOR CLIMA"); 

    if (lecturaOk) {
      // 1. Mostrar Lectura Actual
      char buf[20];
      snprintf(buf, sizeof(buf), "T:%.1f H:%.0f", temp, hum);
      display.drawString(0, 0, buf);

      if (horaEsValida) {
        snprintf(buf, sizeof(buf), "TS:%lu", (unsigned long)now);
        display.drawString(0, 1, buf);
      } else {
        display.drawString(0, 1, "TS: --NTP--");
      }

      // --- LÓGICA DE CONTROL ---
      
      bool hayConexion = (WiFi.status() == WL_CONNECTED && client.connected());

      if (hayConexion) {
          
          if (esperandoBackend) {
              // *** MODO ESPERA (Safety Wait 30s) ***
              unsigned long elapsed = millis() - reconnectTime;
              long remaining = 30000 - elapsed; // 30s

              if (remaining > 0) {
                  // Aún esperando: SEGUIMOS GUARDANDO EN BUFFER
                  if (horaEsValida) {
                      if (bufferOffline.size() < MAX_BUFFER) {
                          DatoClima dato = { temp, hum, now };
                          bufferOffline.push_back(dato);
                          display.drawString(0, 7, "Save: OK"); // Visual feedback
                      } else {
                          display.drawString(0, 7, "Save: FULL");
                      }
                  } else {
                      display.drawString(0, 7, "Save: NTP ERR"); // Diagnóstico clave
                  }
                  
                  // Mostrar info espera
                  char waitBuf[20];
                  snprintf(waitBuf, sizeof(waitBuf), "Wait: %lds", remaining/1000);
                  display.drawString(0, 4, waitBuf);
                  
                  snprintf(waitBuf, sizeof(waitBuf), "Buff: %d", bufferOffline.size());
                  display.drawString(0, 5, waitBuf);

              } else {
                  // Tiempo cumplido
                  esperandoBackend = false;
                  Serial.println("ESPERA TERMINADA. Vaciando Buffer...");
              }

          } 
          
          if (!esperandoBackend) {
              // *** MODO ONLINE NORMAL ***
              
              // A) VACIAR BUFFER
              if (horaEsValida && !bufferOffline.empty()) {
                  display.drawString(0, 3, "UPLOADING...");
                  Serial.print("SUBIENDO BUFFER. Size: ");
                  Serial.println(bufferOffline.size());
                  
                  // Enviamos bloques de 10 para no bloquear 
                  int enviados = 0;
                  auto it = bufferOffline.begin();
                  while (it != bufferOffline.end() && enviados < 20) {
                      char jsonHist[200];
                      snprintf(jsonHist, sizeof(jsonHist),
                               "{\"temp\":%.2f, \"hum\":%.2f, \"ts\":%lu}",
                               it->temp, it->hum, (unsigned long)it->timestamp);
                      
                      Serial.print("HIST TX: "); Serial.println(jsonHist);

                      if (client.publish(mqtt_topic, jsonHist)) {
                          it = bufferOffline.erase(it);
                          delay(20); 
                          enviados++;
                      } else {
                          Serial.println("FAIL TX HIST");
                          break;
                      }
                  }
                  
                  char buffRest[20];
                  snprintf(buffRest, sizeof(buffRest), "Left: %d", bufferOffline.size());
                  display.drawString(0, 4, buffRest);

              } else {
                  display.clearLine(3);
                  display.clearLine(4);
              }

              // B) ENVIAR DATO LIVE
              char jsonLive[200];
              if (horaEsValida) {
                  snprintf(jsonLive, sizeof(jsonLive),
                           "{\"temp\":%.2f, \"hum\":%.2f, \"ts\":%lu}",
                           temp, hum, (unsigned long)now);
              } else {
                  snprintf(jsonLive, sizeof(jsonLive),
                           "{\"temp\":%.2f, \"hum\":%.2f}",
                           temp, hum);
              }
              client.publish(mqtt_topic, jsonLive);
              Serial.print("LIVE TX: "); Serial.println(jsonLive);
              display.drawString(0, 7, ">> ENVIADO OK");
          }

      } else {
          // *** MODO OFFLINE ***
          if (horaEsValida) {
              if (bufferOffline.size() < MAX_BUFFER) {
                  DatoClima dato = { temp, hum, now };
                  bufferOffline.push_back(dato);
                  Serial.print("OFFLINE. Guardando. Size: ");
                  Serial.println(bufferOffline.size());
              } else {
                  bufferOffline.erase(bufferOffline.begin());
                  DatoClima dato = { temp, hum, now };
                  bufferOffline.push_back(dato);
                  Serial.println("Buff Full (Rotando)");
              }
              char buff[20];
              snprintf(buff, sizeof(buff), "Buff: %d", bufferOffline.size());
              display.drawString(0, 6, "OFFLINE");
              display.drawString(0, 7, buff);
          } else {
              display.drawString(0, 6, "NO HORA/RED");
          }
      }

    } else {
      display.drawString(0, 4, "Err Sensor HIH");
    }
  }
}