import paho.mqtt.client as mqtt
import requests
import json
import time
import os
import struct
import snappy

# ==========================================
# MANUAL PROTOBUF SERIALIZER (Mini-Prometheus)
# ==========================================
# Avoids 'google.protobuf' dependency hell.
# Implements only what we need for Remote Write.

def append_varint(buffer, value):
    """Appends a varint-encoded integer to the buffer."""
    while value > 127:
        buffer.append((value & 0x7F) | 0x80)
        value >>= 7
    buffer.append(value)

def encode_string(field_number, value):
    """Encodes a string field."""
    utf8 = value.encode('utf-8')
    header = bytearray()
    # Tag: (field_number << 3) | 2 (Length Delimited)
    append_varint(header, (field_number << 3) | 2)
    append_varint(header, len(utf8))
    return header + utf8

def encode_double(field_number, value):
    """Encodes a double field (64-bit float)."""
    header = bytearray()
    # Tag: (field_number << 3) | 1 (64-bit)
    append_varint(header, (field_number << 3) | 1)
    # Little-endian double
    return header + struct.pack('<d', value)

def encode_int64(field_number, value):
    """Encodes an int64 field (varint)."""
    header = bytearray()
    # Tag: (field_number << 3) | 0 (Varint)
    append_varint(header, (field_number << 3) | 0)
    append_varint(header, value)
    return header

def encode_label(name, value):
    """Encodes a Label message."""
    # Label: 1:name (string), 2:value (string)
    payload = encode_string(1, name) + encode_string(2, value)
    
    header = bytearray()
    # Tag for Label is usually embedded in TimeSeries.labels (field 1)
    # We return the payload prefixed by its length-delimited tag?
    # No, this function returns the raw bytes OF the Label message.
    # The caller must wrap it in a Length-Delimited tag.
    return payload

def encode_sample(value, timestamp_ms):
    """Encodes a Sample message."""
    # Sample: 1:value (double), 2:timestamp (int64)
    payload = encode_double(1, value) + encode_int64(2, int(timestamp_ms))
    return payload

def encode_timeseries(labels_dict, value, timestamp_ms):
    """Encodes a TimeSeries message."""
    # TimeSeries: 1:repeated Label, 2:repeated Sample
    payload = bytearray()
    
    # Encode Labels (Field 1)
    for k, v in labels_dict.items():
        label_bytes = encode_label(k, v)
        # Wrap in Tag (1) + Length
        append_varint(payload, (1 << 3) | 2)
        append_varint(payload, len(label_bytes))
        payload.extend(label_bytes)
    
    # Encode Sample (Field 2)
    sample_bytes = encode_sample(value, timestamp_ms)
    # Wrap in Tag (2) + Length
    append_varint(payload, (2 << 3) | 2)
    append_varint(payload, len(sample_bytes))
    payload.extend(sample_bytes)
    
    return payload

def create_write_request(metrics):
    """
    Creates a WriteRequest.
    metrics: list of (labels_dict, value, timestamp_ms)
    """
    # WriteRequest: 1:repeated TimeSeries, 2:metadata (optional)
    payload = bytearray()
    
    for labels, val, ts in metrics:
        ts_bytes = encode_timeseries(labels, val, ts)
        # Wrap in Tag (1) + Length
        append_varint(payload, (1 << 3) | 2)
        append_varint(payload, len(ts_bytes))
        payload.extend(ts_bytes)
        
    return bytes(payload)

# ==========================================
# MAIN BRIDGE LOGIC
# ==========================================

# Configuración
MQTT_BROKER = os.getenv("MQTT_BROKER", "mosquitto")
MQTT_PORT = int(os.getenv("MQTT_PORT", 1883))
MQTT_TOPIC = os.getenv("MQTT_TOPIC", "sensores/clima")
MIMIR_URL = os.getenv("MIMIR_URL", "http://mimir:9009/api/v1/push")

print(f"Iniciando Bridge MQTT -> Mimir (Manual Proto)...")
print(f"MQTT: {MQTT_BROKER}:{MQTT_PORT} [{MQTT_TOPIC}]")

def send_to_mimir_manual(temp, hum, ts_ms):
    try:
        # Preparar métricas
        metrics = []
        
        # 1. Temperatura
        metrics.append((
            {"__name__": "clima_temp", "job": "mqtt_bridge", "sensor": "ESP32"},
            temp,
            ts_ms
        ))
        
        # 2. Humedad
        metrics.append((
            {"__name__": "clima_hum", "job": "mqtt_bridge", "sensor": "ESP32"},
            hum,
            ts_ms
        ))

        # Serializar
        proto_data = create_write_request(metrics)
        
        # Comprimir
        compressed = snappy.compress(proto_data)

        headers = {
            "Content-Encoding": "snappy",
            "Content-Type": "application/x-protobuf",
            "X-Prometheus-Remote-Write-Version": "0.1.0",
            "X-Scope-OrgID": "demo"
        }

        response = requests.post(MIMIR_URL, data=compressed, headers=headers)
        
        if response.status_code >= 200 and response.status_code < 300:
            print(f"OK: T={temp} H={hum} TS={ts_ms}")
        else:
            print(f"ERROR Mimir [{response.status_code}]: {response.text}")

    except Exception as e:
        print(f"Error enviando a Mimir: {e}")

def on_connect(client, userdata, flags, rc):
    print(f"Conectado a MQTT con código: {rc}")
    client.subscribe(MQTT_TOPIC)

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode('utf-8')
        data = json.loads(payload)
        
        temp = float(data.get('temp', 0))
        hum = float(data.get('hum', 0))
        ts_sec = int(data.get('ts', time.time()))
        ts_ms = ts_sec * 1000 
        
        # DEBUG TIMESTAMP
        now_ms = int(time.time() * 1000)
        diff_ms = now_ms - ts_ms
        print(f"DEBUG: RX TS={ts_ms} NOW={now_ms} DIFF={diff_ms}ms ({diff_ms/1000}s hold)")
        
        send_to_mimir_manual(temp, hum, ts_ms)

    except Exception as e:
        print(f"Error procesando mensaje: {e}")

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

while True:
    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        client.loop_forever()
    except Exception as e:
        print(f"Error de conexión MQTT: {e}. Reintentando en 5s...")
        time.sleep(5)
