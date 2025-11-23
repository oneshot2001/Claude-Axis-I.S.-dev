# Building AXION POC as Native Binary (AXIS OS 12)

## Overview

This ACAP is built as a **native binary** for maximum compatibility with AXIS OS 12. The application runs directly on the camera hardware without Docker containerization.

## Prerequisites

- **Docker Desktop** (for accessing ACAP Native SDK compilation environment)
- **ACAP Native SDK 12.2.0** (accessed via Docker image)
- **AXIS OS 11 or 12** camera (native binaries work on both)

**Important:** Docker is only used for the build environment. The final application runs natively on the camera without Docker.

## Quick Build

```bash
cd /Users/matthewvisher/Documents/AI\ Planning\ and\ Projects/Make\ ACAP\ Claude/poc/camera
./build.sh
```

This will:
1. Use ACAP Native SDK Docker container for compilation
2. Compile all C source files into native binary
3. Package as `.eap` file
4. Place `.eap` in camera directory root

## Build Process Details

### What Happens During Build

```bash
./build.sh [ARCH] [SDK_VERSION]

# Default values:
# ARCH = aarch64 (ARTPEC-9 chip)
# SDK_VERSION = 12.2.0
```

**Steps:**
1. **Clean:** Remove old build artifacts
2. **Compile:** Use SDK container to compile C code
3. **Package:** Create `.eap` with embedded settings
4. **Verify:** Check package size and compatibility

### Manual Build Steps

If you need more control:

```bash
# 1. Enter SDK container
docker run --rm -it \
    -v "$(pwd)":/opt/app \
    -w /opt/app/app \
    axisecp/acap-native-sdk:12.2.0-aarch64-ubuntu24.04 \
    bash

# 2. Inside container, source environment
. /opt/axis/acapsdk/environment-setup*

# 3. Build ACAP with settings files
acap-build . -a settings/settings.json -a settings/mqtt.json

# 4. Exit container
exit

# 5. Package is now in app/*.eap - move to root
mv app/*.eap .
```

## Build Output

**Expected package size:** ~5-10MB (native binary)

**Compare to Docker-based ACAP:** ~100MB (includes container runtime)

**Package contents:**
- Native ARM64 binary (`axion_poc`)
- Settings files (`settings.json`, `mqtt.json`)
- Web UI configuration
- Manifest metadata

## Deployment

### Using deploy.sh (Recommended)

```bash
cd /Users/matthewvisher/Documents/AI\ Planning\ and\ Projects/Make\ ACAP\ Claude/poc
./deploy.sh <camera-ip> <username> <password>
```

### Manual Deployment

1. **Via Web UI:**
   - Open camera web interface
   - Navigate to Settings > Apps
   - Click "Add app" (or "Install")
   - Upload `.eap` file
   - Start application

2. **Via SSH/SCP:**
   ```bash
   scp axion_poc_1_0_0_aarch64.eap root@<camera-ip>:/tmp/
   ssh root@<camera-ip>
   eap-install /tmp/axion_poc_1_0_0_aarch64.eap
   ```

## Verification

After deployment, check that application started:

```bash
# SSH into camera
ssh root@<camera-ip>

# Check if application is running
systemctl status axion_poc

# View application logs
journalctl -u axion_poc -f

# Or check syslog
tail -f /var/log/syslog | grep axion_poc
```

## Differences from Docker-based ACAP

| Aspect | Docker-based | Native Binary |
|--------|--------------|---------------|
| **Package Size** | ~100MB | ~5-10MB |
| **Memory Overhead** | +50MB (container runtime) | Minimal |
| **Startup Time** | 3-5 seconds | ~1 second |
| **OS 11 Compatible** | Yes | Yes |
| **OS 12 Compatible** | No (deprecated) | Yes |
| **Debugging** | Container shell access | Direct logs only |
| **Build Process** | Multi-stage Docker | SDK compilation |

## Troubleshooting

### Build Fails: "Docker not running"

**Solution:** Start Docker Desktop

### Build Fails: "SDK image not found"

**Solution:** Pull SDK image manually:
```bash
docker pull axisecp/acap-native-sdk:12.2.0-aarch64-ubuntu24.04
```

### Build Succeeds but No .eap File

**Check:**
1. Look in `app/*.eap` - may not have moved
2. Check build output for compilation errors
3. Ensure all source files compile (no missing dependencies)

### Application Won't Start on Camera

**Check:**
1. Camera OS version: `ssh root@<camera-ip> "cat /etc/os-release"`
2. Application logs: `journalctl -u axion_poc`
3. Memory constraints: `free -h`
4. Larod service: `systemctl status larod`

### Memory Issues

Native binary should use ~200-300MB total:
- Application: ~50MB
- VDO stream: ~100MB
- Larod model: ~50MB
- Inference buffers: ~50MB

If higher, check for memory leaks in VDO buffer management.

## Development Workflow

### Edit-Compile-Test Cycle

```bash
# 1. Edit source code in app/*.c or app/*.h
vim app/main.c

# 2. Build
./build.sh

# 3. Deploy
cd ..
./deploy.sh <camera-ip> <user> <pass>

# 4. Monitor logs
ssh root@<camera-ip> "journalctl -u axion_poc -f"
```

### Cleaning Build Artifacts

```bash
# Clean build files
rm -f app/*.o app/*.eap app/axion_poc
rm -f *.eap

# Or run make clean
cd app
make clean
```

## Source Code

All source code is in `app/` directory:

**Core Application:**
- `main.c` - Application entry point and main loop
- `vdo_handler.c/h` - Video stream management
- `larod_handler.c/h` - AI inference with Larod
- `metadata.c/h` - Detection metadata handling
- `dlpu_basic.c/h` - DLPU coordination (shared memory)

**Utilities:**
- `ACAP.c/h` - ACAP SDK wrappers
- `MQTT.c/h` - MQTT client for publishing
- `CERTS.c/h` - SSL certificate handling
- `cJSON.c/h` - JSON parsing library

**Build Configuration:**
- `Makefile` - Compilation rules
- `manifest.json` - ACAP package metadata
- `settings/settings.json` - Application settings
- `settings/mqtt.json` - MQTT configuration

**Note:** The source code is identical for Docker and native builds. Only the build process and packaging differ.

## Performance Expectations

**Inference Performance:**
- YOLOv5n model: ~30-50 FPS on ARTPEC-9
- VDO stream: 416x416 @ 10 FPS (configurable)
- MQTT publish latency: ~10-50ms

**Memory Usage:**
- Baseline: ~250MB total
- Peak (during inference): ~300MB

**Startup:**
- Application start: ~1 second
- VDO stream ready: +0.5 seconds
- Larod model load: +1 second
- Total ready time: ~2-3 seconds

## OS 12 Compatibility

This native binary build is fully compatible with AXIS OS 12:

**What Changed in OS 12:**
- Docker container support removed
- Applications must be native binaries
- Same ACAP SDK APIs (VDO, Larod, etc.)

**Our Implementation:**
- Compiles as native ARM64 binary
- No container runtime needed
- Direct hardware access
- Lower memory footprint
- Faster startup

**Backwards Compatible:**
- Also works on AXIS OS 11
- No special handling needed
- Same deployment process

## Additional Resources

- **ACAP SDK Documentation:** https://axiscommunications.github.io/acap-documentation/
- **Larod API:** https://axiscommunications.github.io/acap-documentation/api/native-sdk-api.html#larod
- **VDO API:** https://axiscommunications.github.io/acap-documentation/api/native-sdk-api.html#vdo
- **AXIS Developer Portal:** https://www.axis.com/developer-community

## Support

For issues specific to AXION POC:
- Check `POC_README.md` for architecture overview
- See `NATIVE_BUILD_MIGRATION.md` for migration details
- Review `IMPLEMENTATION_COMPLETE.md` for implementation notes

For ACAP SDK issues:
- AXIS Developer Community forums
- GitHub issues on acap-native-sdk repository
