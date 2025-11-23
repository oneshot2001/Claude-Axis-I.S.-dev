# Axis I.S. POC - Proof of Concept

**Version:** 1.0.0
**Status:** Ready for Testing
**Target:** Axis Camera with ARTPEC-9 SoC
**Build Type:** Native Binary (AXIS OS 12 Compatible)

---

## Overview

This is a working Proof of Concept for the Axis I.S. edge-cloud AI surveillance platform. It validates the core technical assumptions:

1. **VDO Streaming** - Capture frames at 416x416 @ 10 FPS
2. **ML Inference** - Run YOLOv5n INT8 on DLPU hardware
3. **Metadata Extraction** - Extract scene hashes, motion, object counts
4. **MQTT Publishing** - Stream metadata at 10 FPS
5. **DLPU Coordination** - Prevent conflicts across multiple cameras
6. **Performance** - Achieve 5-10 FPS with <200ms latency

**OS Compatibility:**
- AXIS OS 12 (native binary - recommended)
- AXIS OS 11 (backwards compatible)

## Quick Start

### Prerequisites

- Docker Desktop (for building)
- Axis camera with ARTPEC-9 (P3245-LVE, Q1656-LE, etc.)
- Camera has network access to MQTT broker
- YOLOv5n INT8 model (see camera/models/README.md)

### 1. Build the ACAP

```bash
cd poc/camera
./build.sh
```

This creates `axis_is_poc_1_0_0_aarch64.eap` (~5-10MB native binary)

**Note:** The build process uses ACAP Native SDK to compile a native binary, not a Docker container. This ensures compatibility with AXIS OS 12. See `camera/BUILDING_NATIVE.md` for detailed build documentation.

### 2. Copy Template Files

```bash
# Copy MQTT framework files from make_acap template
cp ../../make_acap/mqtt/app/ACAP.c app/
cp ../../make_acap/mqtt/app/ACAP.h app/
cp ../../make_acap/mqtt/app/MQTT.c app/
cp ../../make_acap/mqtt/app/MQTT.h app/
cp ../../make_acap/mqtt/app/CERTS.c app/
cp ../../make_acap/mqtt/app/CERTS.h app/
cp ../../make_acap/mqtt/app/cJSON.c app/
cp ../../make_acap/mqtt/app/cJSON.h app/

# Rebuild after copying
./build.sh
```

### 3. Download Model

```bash
cd camera/models
# Follow instructions in README.md to download YOLOv5n INT8
wget <model-url>
mv yolov5n_416_416_int8.tflite ./
cd ../..
```

### 4. Deploy to Camera

```bash
cd poc
./deploy.sh 192.168.1.101 root mypassword
```

### 5. Start Cloud Subscriber

```bash
cd poc/cloud

# Option A: Docker (includes Mosquitto broker)
docker-compose up

# Option B: Local Python (requires separate MQTT broker)
pip install -r requirements.txt
python mqtt_subscriber.py --broker 192.168.1.50
```

### 6. Verify Operation

Monitor the cloud subscriber output:
```
[axis-camera-001] Msg= 100 FPS= 9.8 Inf= 48ms Avg= 47.2ms Obj= 2 Mot=0.35 Gaps= 0
```

Check HTTP endpoints:
```bash
curl http://192.168.1.101/local/axis_is_poc/status | jq
curl http://192.168.1.101/local/axis_is_poc/metadata | jq
```

View camera logs:
```bash
ssh root@192.168.1.101
tail -f /var/log/messages | grep axis_is_poc
```

## Project Structure

```
poc/
├── camera/                 # Camera-side ACAP
│   ├── app/               # C source code
│   │   ├── main.c         # Main application
│   │   ├── vdo_handler.*  # VDO streaming
│   │   ├── larod_handler.* # ML inference
│   │   ├── metadata.*     # Metadata extraction
│   │   ├── dlpu_basic.*   # DLPU coordination
│   │   ├── ACAP.*/MQTT.*/etc. # Framework (copy from make_acap)
│   │   ├── Makefile       # Build configuration
│   │   ├── manifest.json  # ACAP manifest (native mode)
│   │   └── settings/      # Configuration files
│   ├── models/            # ML models
│   ├── Dockerfile.old     # Old Docker build (backup)
│   ├── build.sh           # Native build script
│   └── BUILDING_NATIVE.md # Build documentation
│
├── cloud/                 # Cloud-side components
│   ├── mqtt_subscriber.py # Python subscriber
│   ├── docker-compose.yml # Mosquitto + subscriber
│   └── requirements.txt   # Python dependencies
│
├── deploy.sh              # Deployment script
├── NATIVE_BUILD_MIGRATION.md  # Native build migration guide
└── docs/                  # Documentation
    ├── POC_README.md (this file)
    ├── POC_TESTING_GUIDE.md
    └── POC_SUCCESS_CRITERIA.md
```

## Configuration

### Camera Settings (app/settings/settings.json)

```json
{
  "axion": {
    "camera_id": "axis-camera-001",
    "target_fps": 10,
    "confidence_threshold": 0.25,
    "publish_enabled": true
  }
}
```

### MQTT Settings (app/settings/mqtt.json)

```json
{
  "broker": "192.168.1.50",
  "port": 1883,
  "clientId": "axion-camera-001"
}
```

Update these before building/deploying.

## Performance Targets

| Metric | Target | Measured |
|--------|--------|----------|
| Inference FPS | ≥5 FPS | TBD |
| Inference Latency | <100ms | TBD |
| Total Latency | <200ms | TBD |
| Memory Usage | <300MB | TBD |
| MQTT Publish Rate | 10/sec | TBD |
| DLPU Wait Time | <50ms | TBD |

Record actual measurements in `POC_RESULTS_TEMPLATE.md`.

## Troubleshooting

### Build fails
- Ensure Docker is running (needed for SDK access)
- Check Makefile has all source files listed
- Verify SDK version matches your camera
- See `camera/BUILDING_NATIVE.md` troubleshooting section
- Pull SDK image: `docker pull axisecp/acap-native-sdk:12.2.0-aarch64-ubuntu24.04`

### Deploy fails
- Check camera IP is correct
- Verify username/password
- Test with `ping <camera-ip>`
- Check camera has enough storage space

### No MQTT messages
- Verify broker IP in mqtt.json
- Check broker is running: `docker-compose ps`
- Test connectivity: `ping <broker-ip>` from camera
- Check camera logs for MQTT errors

### Low FPS
- Verify YOLOv5n INT8 model is loaded (not FP32)
- Check multiple cameras aren't conflicting
- Review DLPU logs for contention
- Reduce image resolution if needed

### Inference errors
- Verify model file exists: `/usr/local/packages/axis_is_poc/models/yolov5n_int8.tflite`
- Check model permissions
- Review larod logs
- Ensure DLPU is available on your camera model

## Multi-Camera Setup

To test with multiple cameras:

1. Build ACAP once
2. Update settings.json for each camera (different camera_id)
3. Update dlpu_basic.c camera_index (0, 1, 2, etc.)
4. Deploy to each camera
5. All cameras must point to same MQTT broker

Example for 3 cameras:
- Camera 1: camera_index=0, slot_offset=0ms
- Camera 2: camera_index=1, slot_offset=200ms
- Camera 3: camera_index=2, slot_offset=400ms

This prevents DLPU conflicts via time-division.

## Native Binary Benefits

This POC uses native compilation for AXIS OS 12 compatibility:

**Advantages:**
- **Smaller Package:** ~5-10MB vs ~100MB (Docker-based)
- **Lower Memory:** ~250MB vs ~300MB (no container overhead)
- **Faster Startup:** ~1 second vs 3-5 seconds
- **OS 12 Ready:** Works on latest AXIS cameras
- **Backwards Compatible:** Also works on AXIS OS 11

**Migration Notes:**
- See `NATIVE_BUILD_MIGRATION.md` for technical details
- Source code unchanged - only build process modified
- All functionality identical to Docker-based approach

## Next Steps

After validating POC:

1. **24-Hour Stability Test** - Check for memory leaks, crashes
2. **Multi-Camera Test** - Deploy to 3-5 cameras simultaneously
3. **Performance Optimization** - Tune for your specific use case
4. **Feature Expansion** - Add bandwidth control, Claude integration
5. **Production Hardening** - Add error recovery, logging, monitoring

See `POC_TESTING_GUIDE.md` for detailed test procedures.

## Support

For issues or questions:
1. Check `POC_TESTING_GUIDE.md`
2. Check `camera/BUILDING_NATIVE.md` for build issues
3. Check `NATIVE_BUILD_MIGRATION.md` for migration details
4. Review camera syslog output
5. Enable debug in cloud subscriber: `--debug`
6. Check Axis I.S._REPOS_ANALYSIS.md for code patterns

## License

See LICENSE file in repository root.

---

**POC Created:** November 23, 2025
**Last Updated:** November 23, 2025
**Maintainer:** Axis I.S. Development Team
