# AXION POC - Cloud-Side MQTT Subscriber

This directory contains the cloud-side components for the AXION POC.

## Components

- **mqtt_subscriber.py** - Python script that subscribes to camera metadata
- **docker-compose.yml** - Orchestrates Mosquitto broker + subscriber
- **Dockerfile** - Containerizes the Python subscriber
- **mosquitto.conf** - MQTT broker configuration
- **requirements.txt** - Python dependencies

## Quick Start

### Option 1: Docker Compose (Recommended)

```bash
# Start Mosquitto broker + subscriber
docker-compose up -d

# View logs
docker-compose logs -f subscriber

# Stop services
docker-compose down
```

### Option 2: Local Python

```bash
# Install dependencies
pip install -r requirements.txt

# Run subscriber (assumes MQTT broker at localhost:1883)
python mqtt_subscriber.py

# Or specify broker address
python mqtt_subscriber.py --broker 192.168.1.50 --port 1883

# Enable debug output
python mqtt_subscriber.py --debug
```

## Configuration

### Mosquitto Broker

Edit `mosquitto.conf` to customize:
- Port (default: 1883 for MQTT, 9001 for WebSockets)
- Authentication (currently disabled for POC)
- Logging level
- Message retention

### Subscriber

Command-line arguments:
```
--broker <address>  MQTT broker address (default: localhost)
--port <number>     MQTT broker port (default: 1883)
--debug             Enable debug output
```

## Monitoring

The subscriber displays:
- Real-time message statistics per camera
- Actual FPS achieved
- Average inference latency
- Object detection counts
- Motion scores
- Sequence gaps (dropped frames)

Example output:
```
[axis-camera-001] Msg= 100 FPS= 9.8 Inf= 48ms Avg= 47.2ms Obj= 2 Mot=0.35 Gaps= 0
```

## Success Criteria

The subscriber validates these POC objectives:
- ✓ FPS ≥ 5 per camera
- ✓ Average inference < 200ms
- ✓ No sequence gaps (no dropped frames)
- ✓ MQTT messages received successfully

## MQTT Topics

The subscriber listens to:
- `axion/camera/+/metadata` - Frame metadata (10 FPS)
- `axion/camera/+/status` - Camera online/offline status

## Troubleshooting

### No messages received

1. Check MQTT broker is running:
   ```bash
   docker-compose ps
   ```

2. Verify camera can reach broker:
   ```bash
   # On camera
   ping <broker-ip>
   ```

3. Check camera settings (settings/mqtt.json):
   ```json
   {
     "broker": "<your-broker-ip>",
     "port": 1883
   }
   ```

### Sequence gaps

Indicates dropped frames. Possible causes:
- Network congestion
- Camera overload
- DLPU contention (multiple cameras)

Check camera logs for errors.

### High inference latency

If avg inference > 100ms:
- Verify YOLOv5n INT8 model is loaded (not FP32)
- Check DLPU is available
- Review camera CPU/memory usage

## Next Steps

After validating message reception:
1. Run for 24 hours to check stability
2. Add multiple cameras to test DLPU coordination
3. Monitor memory usage trends
4. Implement retention/archival if needed
