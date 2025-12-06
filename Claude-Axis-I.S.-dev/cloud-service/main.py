"""
Axis I.S. Cloud Service - Main Application
FastAPI application with MQTT integration and Claude analysis
"""
import asyncio
import logging
import sys
from contextlib import asynccontextmanager
from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from typing import Dict, Any, List, Optional

from config import settings
from database import db, redis
from mqtt_handler import mqtt_handler
from ai_factory import get_ai_agent
from scene_memory import scene_memory
from ws_manager import manager

# Configure logging
logging.basicConfig(
    level=logging.DEBUG if settings.debug else logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[
        logging.StreamHandler(sys.stdout),
        logging.FileHandler('cloud-service.log')
    ]
)
logger = logging.getLogger(__name__)


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Application lifespan manager"""
    # Startup
    logger.info(f"Starting {settings.app_name} v{settings.app_version}")

    try:
        # Connect to databases
        await db.connect()
        await redis.connect()

        # Connect to MQTT and start processing
        await mqtt_handler.connect()

        # Start MQTT processing loop in background
        mqtt_task = asyncio.create_task(mqtt_handler.start())

        logger.info("All services started successfully")

        yield

    finally:
        # Shutdown
        logger.info("Shutting down services...")

        # Stop MQTT handler
        await mqtt_handler.stop()
        if 'mqtt_task' in locals():
            mqtt_task.cancel()
            try:
                await mqtt_task
            except asyncio.CancelledError:
                pass

        # Disconnect from services
        await mqtt_handler.disconnect()
        await redis.disconnect()
        await db.disconnect()

        logger.info("Shutdown complete")


# Create FastAPI application
app = FastAPI(
    title=settings.app_name,
    version=settings.app_version,
    lifespan=lifespan
)

# Add CORS middleware for ACAP UI access
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # In production, restrict to camera IPs
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


# Pydantic models for chat
class ChatMessage(BaseModel):
    role: str
    content: str


class ChatRequest(BaseModel):
    camera_id: str
    question: str
    context: Optional[Dict[str, Any]] = None
    conversation_history: Optional[List[ChatMessage]] = None


# ============================================================================
# API Endpoints
# ============================================================================

@app.get("/")
async def root():
    """Root endpoint"""
    return {
        "app": settings.app_name,
        "version": settings.app_version,
        "status": "running"
    }


@app.get("/health")
async def health():
    """Health check endpoint"""
    try:
        # Check database
        async with db.pool.acquire() as conn:
            await conn.fetchval("SELECT 1")

        # Check Redis
        await redis.redis.ping()

        ai_agent = get_ai_agent()

        return {
            "status": "healthy",
            "database": "connected",
            "redis": "connected",
            "mqtt": "connected" if mqtt_handler.running else "disconnected",
            "ai_provider": ai_agent.get_provider_name(),
            "ai_model": ai_agent.get_model_name()
        }
    except Exception as e:
        logger.error(f"Health check failed: {str(e)}")
        raise HTTPException(status_code=503, detail=f"Service unhealthy: {str(e)}")


@app.get("/stats")
async def stats():
    """Get service statistics"""
    try:
        redis_stats = await redis.get_stats()

        return {
            "mqtt": mqtt_handler.get_stats(),
            "ai_agent": get_ai_agent().get_stats(),
            "scene_memory": scene_memory.get_stats(),
            "redis": redis_stats
        }
    except Exception as e:
        logger.error(f"Stats error: {str(e)}")
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/cameras")
async def list_cameras():
    """List all active cameras"""
    try:
        # Get all cameras from Redis (cameras that have sent status recently)
        pattern = "camera:*:state"
        keys = []
        async for key in redis.redis.scan_iter(match=pattern):
            keys.append(key.decode())

        cameras = []
        for key in keys:
            camera_id = key.split(':')[1]
            state = await redis.get_camera_state(camera_id)
            if state:
                cameras.append({
                    "camera_id": camera_id,
                    "state": state
                })

        return {
            "count": len(cameras),
            "cameras": cameras
        }
    except Exception as e:
        logger.error(f"List cameras error: {str(e)}")
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/cameras/{camera_id}/analyses")
async def get_camera_analyses(camera_id: str, limit: int = 10):
    """Get recent Claude analyses for a camera"""
    try:
        analyses = await db.get_recent_analyses(camera_id, limit)

        return {
            "camera_id": camera_id,
            "count": len(analyses),
            "analyses": analyses
        }
    except Exception as e:
        logger.error(f"Get analyses error: {str(e)}")
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/cameras/{camera_id}/scene-memory")
async def get_scene_memory(camera_id: str, limit: int = 30):
    """Get scene memory for a camera"""
    try:
        frames = await scene_memory.get_recent_frames(camera_id, limit=limit, with_images=False)
        context = await scene_memory.get_context_for_analysis(camera_id)

        return {
            "camera_id": camera_id,
            "frames": frames,
            "context": context
        }
    except Exception as e:
        logger.error(f"Get scene memory error: {str(e)}")
        raise HTTPException(status_code=500, detail=str(e))


@app.post("/cameras/{camera_id}/request-frame")
async def manual_frame_request(camera_id: str, reason: str = "manual_request"):
    """Manually request a frame from a camera"""
    try:
        # Check if camera exists
        state = await redis.get_camera_state(camera_id)
        if not state:
            raise HTTPException(status_code=404, detail=f"Camera {camera_id} not found")

        # Check cooldown
        can_request = await redis.check_request_cooldown(camera_id)
        if not can_request:
            raise HTTPException(status_code=429, detail="Rate limit: too soon since last request")

        # Create dummy event ID (since this is manual)
        event_id = 0
        trigger_metadata = {
            "timestamp_us": 0,
            "manual_request": True
        }

        # Send request
        await mqtt_handler.request_frame(camera_id, reason, event_id, trigger_metadata)

        return {
            "camera_id": camera_id,
            "status": "requested",
            "reason": reason
        }
    except HTTPException:
        raise
    except Exception as e:
        logger.error(f"Manual frame request error: {str(e)}")
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/config")
async def get_config():
    """Get current configuration (sanitized)"""
    return {
        "app_name": settings.app_name,
        "app_version": settings.app_version,
        "mqtt_broker": settings.mqtt_broker,
        "mqtt_port": settings.mqtt_port,
        "claude_model": settings.claude_model,
        "scene_memory_frames": settings.scene_memory_frames,
        "frame_request_cooldown": settings.frame_request_cooldown,
        "motion_threshold": settings.motion_threshold,
        "vehicle_confidence_threshold": settings.vehicle_confidence_threshold,
        "max_concurrent_analyses": settings.max_concurrent_analyses
    }


@app.post("/chat")
async def chat(request: ChatRequest):
    """
    Natural language chat interface for scene analysis.
    Allows users to ask questions about camera detections in plain English.
    """
    from anthropic import AsyncAnthropic

    try:
        ai_agent = get_ai_agent()
        logger.info(f"Chat request from {request.camera_id}: {request.question[:100]}...")

        # Get recent scene context from scene memory
        context = await scene_memory.get_context_for_analysis(request.camera_id)
        frames = await scene_memory.get_recent_frames(request.camera_id, limit=3, with_images=True)

        # Build the prompt with context
        context_info = request.context or {}
        detections = context_info.get('detections', [])

        # Format detection info
        detection_summary = []
        for det in detections[:20]:
            class_names = [
                'person', 'bicycle', 'car', 'motorcycle', 'airplane', 'bus', 'train', 'truck', 'boat',
                'traffic light', 'fire hydrant', 'stop sign', 'parking meter', 'bench', 'bird', 'cat',
                'dog', 'horse', 'sheep', 'cow', 'elephant', 'bear', 'zebra', 'giraffe', 'backpack',
                'umbrella', 'handbag', 'tie', 'suitcase', 'frisbee', 'skis', 'snowboard', 'sports ball',
                'kite', 'baseball bat', 'baseball glove', 'skateboard', 'surfboard', 'tennis racket',
                'bottle', 'wine glass', 'cup', 'fork', 'knife', 'spoon', 'bowl', 'banana', 'apple',
                'sandwich', 'orange', 'broccoli', 'carrot', 'hot dog', 'pizza', 'donut', 'cake', 'chair',
                'couch', 'potted plant', 'bed', 'dining table', 'toilet', 'tv', 'laptop', 'mouse',
                'remote', 'keyboard', 'cell phone', 'microwave', 'oven', 'toaster', 'sink', 'refrigerator',
                'book', 'clock', 'vase', 'scissors', 'teddy bear', 'hair drier', 'toothbrush'
            ]
            class_id = det.get('class_id', 0)
            label = class_names[class_id] if 0 <= class_id < len(class_names) else f'object_{class_id}'
            conf = det.get('confidence', 0)
            if conf and 0 < conf <= 1:
                detection_summary.append(f"- {label}: {conf*100:.0f}% confidence")

        system_prompt = f"""You are an AI assistant analyzing a surveillance camera feed from camera "{request.camera_id}".

**Current Scene Information:**
- Camera ID: {request.camera_id}
- Timestamp: {context_info.get('timestamp', 'unknown')}
- Motion Score: {context_info.get('metadata', {}).get('motion_score', 0):.2f}
- Objects in Frame: {len(detections)}
{chr(10).join(detection_summary[:10]) if detection_summary else '- No valid detections above threshold'}

**Scene Memory (Historical Context):**
- Frames Available: {context.get('frames_available', 0)}
- Time Span: {context.get('time_span_seconds', 0):.1f} seconds
- Total Objects Detected: {context.get('total_objects_detected', 0)}
- Average Motion: {context.get('average_motion_score', 0):.2f}

You are having a conversation with a security operator who can ask natural language questions.
Answer questions about:
- What objects/people are currently visible
- Counting specific object types
- Describing the scene
- Assessing activity levels or suspicious behavior
- Tracking how long objects have been present (based on scene memory)

Be concise, helpful, and specific. If you cannot determine something, say so clearly."""

        # Build conversation messages
        messages = []

        # Add conversation history
        if request.conversation_history:
            for msg in request.conversation_history[-6:]:  # Last 3 exchanges
                messages.append({
                    "role": msg.role,
                    "content": msg.content
                })

        # Add current question with images if available
        current_content = []

        # Add images from recent frames
        for frame in frames[-2:]:  # Up to 2 most recent images
            if frame.get('image_base64'):
                current_content.append({
                    "type": "image",
                    "source": {
                        "type": "base64",
                        "media_type": "image/jpeg",
                        "data": frame['image_base64']
                    }
                })

        current_content.append({
            "type": "text",
            "text": request.question
        })

        messages.append({
            "role": "user",
            "content": current_content if len(current_content) > 1 else request.question
        })

        # Call Claude API
        client = AsyncAnthropic(api_key=settings.anthropic_api_key)
        response = await client.messages.create(
            model=settings.claude_model,
            max_tokens=1024,
            system=system_prompt,
            messages=messages,
            timeout=30
        )

        response_text = response.content[0].text if response.content else "I couldn't generate a response."

        logger.info(f"Chat response generated ({response.usage.output_tokens} tokens)")

        return {
            "response": response_text,
            "camera_id": request.camera_id,
            "tokens_used": response.usage.input_tokens + response.usage.output_tokens,
            "model": response.model,
            "provider": ai_agent.get_provider_name()
        }

    except Exception as e:
        logger.error(f"Chat error: {str(e)}")
        raise HTTPException(status_code=500, detail=f"Chat processing failed: {str(e)}")


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    try:
        while True:
            data = await websocket.receive_text()
            # Handle incoming messages (if any)
            # await manager.broadcast(f"Client says: {data}")
    except WebSocketDisconnect:
        manager.disconnect(websocket)


# ============================================================================
# Main entry point
# ============================================================================

if __name__ == "__main__":
    import uvicorn

    logger.info(f"Starting {settings.app_name} v{settings.app_version}")
    logger.info(f"Debug mode: {settings.debug}")

    uvicorn.run(
        "main:app",
        host="0.0.0.0",
        port=8000,
        reload=settings.debug,
        log_level="debug" if settings.debug else "info"
    )
