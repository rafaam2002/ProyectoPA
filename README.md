# Proyecto Final: Monitorización IoT con ESP32

Este proyecto implementa un sistema completo de monitorización IoT ("Full Stack IoT"). Captura datos de temperatura y humedad mediante un ESP32 y los visualiza en tiempo real usando un stack de contenedores Docker.

## 🚀 Arquitectura

*   **ESP32**: Lee sensores y envía datos vía MQTT.
*   **Mosquitto**: Broker MQTT que recibe los mensajes.
*   **Telegraf**: Agente que recoge datos de MQTT y los inserta en la base de datos.
*   **Mimir**: Base de datos de series temporales (compatible con Prometheus).
*   **Grafana**: Interfaz web para visualizar los datos.

## 🛠️ Despliegue con Docker

Todos los servicios de backend se ejecutan mediante Docker Compose.

### 1. Iniciar el sistema
Para levantar todos los servicios, ejecuta en la raíz del proyecto:
```bash
sudo docker compose up -d
```

### 2. Verificar estado
Para ver si todo está corriendo correctamente:
```bash
sudo docker compose ps
```

### 3. Parar el sistema
Para detener y eliminar los contenedores (los datos se conservan):
```bash
sudo docker compose down
```

## 🌐 Servicios y Puertos

Una vez arrancado el sistema, puedes acceder a los siguientes servicios:

| Servicio | URL / Puerto | Descripción | Credenciales (Default) |
| :--- | :--- | :--- | :--- |
| **Grafana** | [http://localhost:3000](http://localhost:3000) | Panel de control visual | `admin` / `admin` |
| **Mimir** | Puerto `9009` | Base de datos (API) | - |
| **Mosquitto**| Puerto `1883` | Broker MQTT (TCP) | - |

## 📦 Configuración ESP32

El código del microcontrolador está en la carpeta `main/`.
Para compilar:
1.  Crear `main/secrets.h` con tus credenciales WiFi (ver ejemplo en código).
2.  Subir usando Arduino IDE o `arduino-cli`.
