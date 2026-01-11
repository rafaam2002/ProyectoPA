# Proyecto Final: Sistema IoT Resiliente con ESP32, MQTT y Mimir

Este documento detalla la arquitectura, tecnologías y funcionalidades del sistema de monitorización ambiental desarrollado. El proyecto se centra en la **resilencia de datos** (no perder mediciones) y la **visualización histórica precisa** utilizando un stack moderno de contenedores.

## 🚀 Tecnologías Clave

*   **ESP32 (C++/Arduino)**: Microcontrolador con lógica avanzada de buffering offline.
*   **MQTT (Mosquitto)**: Protocolo ligero para transmisión de mensajería (Broker).
*   **Python Bridge (Custom)**: Servicio intermedio desarrollado a medida para ingesta de datos. Reemplaza a Telegraf para garantizar el manejo correcto de timestamps históricos (Backfilling).
*   **Prometheus Remote Write (Protobuf)**: Protocolo utilizado por el Bridge para enviar métricas eficientes.
*   **Mimir (Grafana Labs)**: Base de datos de series temporales escalable y compatible con Prometheus.
*   **Grafana**: Plataforma de visualización para dashboards y alertas.
*   **Docker & Docker Compose**: Orquestación de contenedores para un despliegue replicable.

## 🏛️ Arquitectura del Sistema

1.  **Sensorización (Edge)**:
    *   El ESP32 lee temperatura y humedad (Sensor HIH) cada segundo.
    *   Sincroniza la hora vía **NTP** (necesario para marcar hitos de tiempo reales).
    *   **Buffer Circular**: Si pierde conexión WiFi o MQTT, guarda los datos en RAM (`std::vector`).
    *   **Lógica Anti-Race Condition**: Al reconectar, espera 30 segundos estables antes de volcar el buffer para asegurar que el backend está listo.

2.  **Transmisión**:
    *   Protocolo MQTT sobre TCP/IP.
    *   Topic: `sensores/clima`.
    *   Payload JSON: `{"temp": 25.5, "hum": 40.2, "ts": 1700000000}`.

3.  **Ingesta (Backend)**:
    *   **Service: mqtt-bridge**: Script Python optimizado.
    *   Escucha MQTT y decodifica el JSON.
    *   Transforma los datos a **Protobuf** (formato binario de Prometheus).
    *   Envía los datos a Mimir vía HTTP POST (`/api/v1/push`).
    *   *Ventaja*: Permite inyectar datos con timestamps pasados (lo que fallaba con Telegraf).

4.  **Almacenamiento y Visualización**:
    *   **Mimir**: Recibe y almacena métricas con alta compresión. Soporta ingesta desordenada (out-of-order).
    *   **Grafana**: Consulta Mimir usando PromQL (`clima_temp`, `clima_hum`) y grafica los resultados.

## 🛠️ Instrucciones de Despliegue

### 1. Backend (Servidor)
Ejecutar en la raíz del proyecto (requiere Docker):
```bash
sudo docker compose up -d --build
```
*   **Grafana**: [http://localhost:3000](http://localhost:3000) (Usuario/Pass: `admin` / `admin`)
*   **Logs Bridge**: `sudo docker logs mqtt-bridge -f`

### 2. ESP32 (Dispositivo)
1.  Abrir `main/main.ino` con Arduino IDE o VS Code (PlatformIO).
2.  Renombrar y configurar `main/secrets.h` con tu WiFi.
3.  Subir el código.

## 🧪 Pruebas de Resiliencia (Demo)

El sistema soporta desconexiones de red sin perder datos ("Huecos" en la gráfica):

1.  **Corte**: Desconecta el contenedor de MQTT (`docker stop mosquitto`) o apaga el router WiFi.
2.  **Acumulación**: El ESP32 mostrará en pantalla "OFFLINE Buff: X".
3.  **Reconexión**: Restaura el servicio (`docker start mosquitto`).
4.  **Recuperación**: El ESP32 esperará 30s ("Wait: 30s") y luego enviará todos los datos guardados a alta velocidad.
5.  **Resultado**: En Grafana, la línea aparecerá continua, rellenando el tiempo que estuvo desconectado.

---
*Máster Universitario en Ingeniería de Telecomunicación*
*Autor: Rafa*
