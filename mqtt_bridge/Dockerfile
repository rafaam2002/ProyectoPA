FROM python:3.9-slim

WORKDIR /app

# Instalar dependencias de sistema para Snappy
RUN apt-get update && apt-get install -y libsnappy-dev && rm -rf /var/lib/apt/lists/*

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY bridge.py .
CMD ["python", "-u", "bridge.py"]
