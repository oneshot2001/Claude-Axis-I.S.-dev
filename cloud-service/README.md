# Axis I.S. Cloud Service

Cloud-based AI analysis service for Axis I.S. camera surveillance platform.

## Architecture

```
Camera → MQTT (metadata + frames) → Cloud Service → Claude Vision API → PostgreSQL/Redis
                                          ↓
                                    Executive Summaries
```

## Features

- **MQTT Integration**: Subscribes to camera metadata streams (10 FPS)
- **Scene Memory**: Maintains last 30 frames per camera in Redis
- **Intelligent Frame Requests**: Triggers on motion, vehicles, scene changes
- **Claude Vision Analysis**: Analyzes scenes with visual context
- **PostgreSQL Storage**: Events, analyses, alerts
- **Redis Cache**: Real-time state, scene memory, rate limiting
- **REST API**: Query analyses, trigger manual requests

## Prerequisites

- Docker & Docker Compose
- Anthropic API key for Claude
- Running MQTT broker (or use included Mosquitto)

## Quick Start

### 1. Configure Environment

```bash
cd cloud-service
cp .env.example .env
vi .env  # Set ANTHROPIC_API_KEY and other settings
```

### 2. Start Services

```bash
docker-compose up -d
```

This starts:
- Mosquitto MQTT broker (port 1883)
- PostgreSQL database (port 5432)
- Redis cache (port 6379)
- Cloud service API (port 8000)

### 3. Verify Deployment

```bash
# Check service health
curl http://localhost:8000/health

# View statistics
curl http://localhost:8000/stats

# List active cameras
curl http://localhost:8000/cameras
```

### 4. View Logs

```bash
# All services
docker-compose logs -f

# Cloud service only
docker-compose logs -f cloud-service

# MQTT broker
docker-compose logs -f mosquitto
```

## Configuration

Edit `.env` file:

### MQTT Settings
```env
MQTT_BROKER=mosquitto
MQTT_PORT=1883
```

### Claude API
```env
ANTHROPIC_API_KEY=sk-ant-api-your-key-here
CLAUDE_MODEL=claude-3-5-sonnet-20241022
CLAUDE_MAX_TOKENS=500
```

### Analysis Triggers
```env
MOTION_THRESHOLD=0.7              # Request frame if motion > 0.7
VEHICLE_CONFIDENCE_THRESHOLD=0.5  # Request frame if vehicle detected
SCENE_CHANGE_ENABLED=true         # Request frame on scene change
FRAME_REQUEST_COOLDOWN=60         # Min seconds between requests per camera
```

### Performance
```env
MAX_CONCURRENT_ANALYSES=5         # Max parallel Claude API calls
SCENE_MEMORY_FRAMES=30           # Keep last N frames per camera
```

## API Endpoints

### Health & Status

**GET /health**
```bash
curl http://localhost:8000/health
```

**GET /stats**
```bash
curl http://localhost:8000/stats
```

### Cameras

**GET /cameras**
```bash
curl http://localhost:8000/cameras
```

**GET /cameras/{camera_id}/analyses**
```bash
curl http://localhost:8000/cameras/axis-camera-001/analyses?limit=10
```

**GET /cameras/{camera_id}/scene-memory**
```bash
curl http://localhost:8000/cameras/axis-camera-001/scene-memory
```

**POST /cameras/{camera_id}/request-frame**
```bash
curl -X POST http://localhost:8000/cameras/axis-camera-001/request-frame?reason=manual
```

### Configuration

**GET /config**
```bash
curl http://localhost:8000/config
```

## How It Works

### 1. Metadata Stream Processing

Cameras publish metadata at 10 FPS:
```
Topic: axis-is/camera/{camera_id}/metadata
Payload: {
  "timestamp_us": 1700000000000000,
  "motion_score": 0.65,
  "object_count": 3,
  "detections": [...]
}
```

Cloud service:
1. Stores metadata in PostgreSQL
2. Adds to scene memory (Redis)
3. Checks trigger conditions

### 2. Frame Request Trigger

Triggers when:
- Motion score > 0.7
- Vehicle detected (class 2, 5, 7) with confidence > 0.5
- Scene hash changes

If triggered:
1. Publishes frame request to camera
2. Sets cooldown (60s)
3. Waits for frame response

### 3. Frame Reception

Camera publishes frame:
```
Topic: axis-is/camera/{camera_id}/frame
Payload: {
  "request_id": "uuid",
  "timestamp_us": 1700000000000000,
  "image_base64": "..."
}
```

Cloud service:
1. Adds frame to scene memory
2. Retrieves last 5 frames with images
3. Triggers Claude analysis

### 4. Claude Analysis

Service:
1. Builds context from metadata + frames
2. Calls Claude Vision API
3. Receives executive summary
4. Stores in PostgreSQL
5. Can trigger alerts

Example summary:
> "A sedan is entering the parking lot from the north entrance. Motion detected in loading bay area with two people carrying boxes. Activity appears routine for business hours. No security concerns."

## Database Schema

### PostgreSQL Tables

**camera_events** - Metadata stream storage
- Partitioned by timestamp
- Indexes on (camera_id, timestamp)
- 30-day retention

**claude_analyses** - AI analysis results
- Links to trigger events
- Full Claude response (JSONB)
- Performance metrics

**alerts** - Security notifications
- Severity levels
- Acknowledgment workflow

### Redis Keys

```
camera:{id}:state              # Current camera state
camera:{id}:scene_memory       # Last 30 frames (sorted set)
camera:{id}:last_request       # Rate limiting
frame_request:{uuid}:event_id  # Request tracking
frame_request:{uuid}:metadata  # Request context
```

## Monitoring

### View Service Stats
```bash
curl http://localhost:8000/stats | jq
```

Returns:
```json
{
  "mqtt": {
    "messages_received": 36000,
    "frame_requests_sent": 15,
    "analyses_triggered": 15
  },
  "claude": {
    "analyses_count": 15,
    "total_tokens": 12500,
    "average_tokens": 833
  },
  "scene_memory": {
    "cameras": 3,
    "total_frames_processed": 108000
  },
  "redis": {
    "connected_clients": 2,
    "used_memory_human": "12.5M",
    "total_keys": 45
  }
}
```

### Database Queries

```bash
# Connect to PostgreSQL
docker exec -it axis-is-postgres psql -U postgres -d axis_is

# Recent analyses
SELECT camera_id, summary, created_at
FROM claude_analyses
ORDER BY created_at DESC
LIMIT 10;

# Events by camera
SELECT camera_id, COUNT(*)
FROM camera_events
GROUP BY camera_id;
```

### Redis Inspection

```bash
# Connect to Redis
docker exec -it axis-is-redis redis-cli

# List all keys
KEYS *

# View camera state
HGETALL camera:axis-camera-001:state

# View scene memory
ZRANGE camera:axis-camera-001:scene_memory 0 -1
```

## Development

### Run Locally (without Docker)

```bash
# Install dependencies
pip install -r requirements.txt

# Set environment variables
export ANTHROPIC_API_KEY=sk-ant-api-...
export MQTT_BROKER=localhost
export DATABASE_URL=postgresql://postgres:postgres@localhost:5432/axis_is
export REDIS_URL=redis://localhost:6379

# Run application
python main.py
```

### Run Tests

```bash
# TODO: Add pytest tests
pytest tests/
```

## Troubleshooting

### Service won't start

```bash
# Check logs
docker-compose logs cloud-service

# Common issues:
# 1. Missing ANTHROPIC_API_KEY
# 2. PostgreSQL not ready (wait 10s and retry)
# 3. MQTT broker not reachable
```

### No analyses being triggered

```bash
# Check if cameras are connected
curl http://localhost:8000/cameras

# Check trigger thresholds
curl http://localhost:8000/config

# Lower motion threshold for testing
# In .env: MOTION_THRESHOLD=0.3
docker-compose restart cloud-service
```

### High memory usage

```bash
# Reduce scene memory
# In .env: SCENE_MEMORY_FRAMES=10
docker-compose restart cloud-service

# Check Redis memory
docker exec -it axis-is-redis redis-cli INFO memory
```

## Production Deployment

### Security Checklist

- [ ] Set strong PostgreSQL password
- [ ] Enable MQTT authentication
- [ ] Use TLS for MQTT (port 8883)
- [ ] Add API authentication (JWT)
- [ ] Enable firewall rules
- [ ] Rotate Claude API keys
- [ ] Set up log monitoring
- [ ] Configure backup for PostgreSQL

### Scaling

- **Horizontal**: Run multiple cloud-service instances
- **MQTT**: Use clustered Mosquitto broker
- **PostgreSQL**: Enable replication
- **Redis**: Use Redis Cluster

### Monitoring

- Add Prometheus metrics endpoint
- Set up Grafana dashboards
- Configure alerting (PagerDuty, Slack)

## License

See main project LICENSE file.

---

**Built for Axis I.S. Camera Surveillance Platform**
**Powered by Claude Vision API**
