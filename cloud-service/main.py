"""
Axis I.S. Cloud Service - Main Application
FastAPI application with MQTT integration and Claude analysis
"""
import asyncio
import logging
import sys
from contextlib import asynccontextmanager
from fastapi import FastAPI, HTTPException
from fastapi.responses import JSONResponse
from typing import Dict, Any

from config import settings
from database import db, redis
from mqtt_handler import mqtt_handler
from ai_factory import get_ai_agent
from scene_memory import scene_memory

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

        return {
            "status": "healthy",
            "database": "connected",
            "redis": "connected",
            "mqtt": "connected" if mqtt_handler.running else "disconnected"
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
