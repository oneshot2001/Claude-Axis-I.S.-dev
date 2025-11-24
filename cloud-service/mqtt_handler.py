"""
MQTT Handler
Subscribes to camera topics and coordinates analysis pipeline
"""
import asyncio
import json
import logging
import uuid
from typing import Dict, Any
from aiomqtt import Client, Message
from scene_memory import scene_memory
from ai_factory import get_ai_agent
from database import db, redis
from config import settings

logger = logging.getLogger(__name__)


class MQTTHandler:
    """MQTT client for camera communication"""

    def __init__(self):
        self.client: Client = None
        self.running = False
        self.messages_received = 0
        self.frame_requests_sent = 0
        self.analyses_triggered = 0

    async def connect(self):
        """Connect to MQTT broker"""
        logger.info(f"Connecting to MQTT broker: {settings.mqtt_broker}:{settings.mqtt_port}")

        self.client = Client(
            hostname=settings.mqtt_broker,
            port=settings.mqtt_port,
            username=settings.mqtt_username,
            password=settings.mqtt_password,
            keepalive=settings.mqtt_keepalive,
            identifier=f"cloud-service-{uuid.uuid4().hex[:8]}"
        )

        await self.client.__aenter__()
        logger.info("MQTT connected")

        # Subscribe to all camera topics
        await self.subscribe_to_cameras()

    async def disconnect(self):
        """Disconnect from MQTT broker"""
        if self.client:
            await self.client.__aexit__(None, None, None)
            logger.info("MQTT disconnected")

    async def subscribe_to_cameras(self):
        """Subscribe to camera topics"""
        topics = [
            "axis-is/camera/+/metadata",    # Metadata stream
            "axis-is/camera/+/frame",       # Frame responses
            "axis-is/camera/+/status",      # Health status
            "axis-is/camera/+/event",       # Significant events
            "axis-is/camera/+/alert"        # Critical alerts
        ]

        for topic in topics:
            await self.client.subscribe(topic)
            logger.info(f"Subscribed to: {topic}")

    async def start(self):
        """Start MQTT message processing loop"""
        self.running = True
        logger.info("Starting MQTT message loop")

        try:
            async for message in self.client.messages:
                if not self.running:
                    break

                self.messages_received += 1
                asyncio.create_task(self.handle_message(message))

        except asyncio.CancelledError:
            logger.info("MQTT loop cancelled")
        except Exception as e:
            logger.error(f"MQTT loop error: {str(e)}")
            raise

    async def stop(self):
        """Stop MQTT processing"""
        self.running = False
        logger.info("Stopping MQTT handler")

    async def handle_message(self, message: Message):
        """Route MQTT message to appropriate handler"""
        try:
            topic = str(message.topic)
            payload = message.payload.decode('utf-8')

            # Extract camera ID from topic
            parts = topic.split('/')
            if len(parts) < 3:
                logger.warning(f"Invalid topic format: {topic}")
                return

            camera_id = parts[2]
            topic_type = parts[3] if len(parts) > 3 else None

            # Route to handler
            if topic_type == 'metadata':
                await self.handle_metadata(camera_id, payload)
            elif topic_type == 'frame':
                await self.handle_frame(camera_id, payload)
            elif topic_type == 'status':
                await self.handle_status(camera_id, payload)
            elif topic_type == 'event':
                await self.handle_event(camera_id, payload)
            elif topic_type == 'alert':
                await self.handle_alert(camera_id, payload)
            else:
                logger.warning(f"Unknown topic type: {topic}")

        except Exception as e:
            logger.error(f"Error handling message: {str(e)}")

    async def handle_metadata(self, camera_id: str, payload: str):
        """Handle metadata stream from camera"""
        try:
            metadata = json.loads(payload)

            # Add to scene memory
            await scene_memory.add_metadata(camera_id, metadata)

            # Store in database
            event_id = await db.store_event(camera_id, metadata)

            # Check if we should request a frame for analysis
            should_request, reason = await scene_memory.should_request_frame(camera_id, metadata)

            if should_request:
                logger.info(f"Triggering frame request for {camera_id}: {reason}")
                await self.request_frame(camera_id, reason, event_id, metadata)

        except json.JSONDecodeError as e:
            logger.error(f"Invalid JSON in metadata: {str(e)}")
        except Exception as e:
            logger.error(f"Error handling metadata: {str(e)}")

    async def handle_frame(self, camera_id: str, payload: str):
        """Handle frame image from camera"""
        try:
            frame_data = json.loads(payload)

            request_id = frame_data.get('request_id')
            timestamp_us = frame_data.get('timestamp_us')
            image_base64 = frame_data.get('image_base64')

            if not all([request_id, timestamp_us, image_base64]):
                logger.warning(f"Incomplete frame data from {camera_id}")
                return

            logger.info(f"Received frame: {camera_id} @ {timestamp_us} "
                       f"(size: {len(image_base64)} bytes)")

            # Add frame to scene memory
            await scene_memory.add_frame_image(camera_id, request_id, timestamp_us, image_base64)

            # Get the event ID from Redis (stored when request was sent)
            event_id_key = f"frame_request:{request_id}:event_id"
            event_id_bytes = await redis.redis.get(event_id_key)
            event_id = int(event_id_bytes.decode()) if event_id_bytes else None

            # Get trigger metadata
            metadata_key = f"frame_request:{request_id}:metadata"
            metadata_bytes = await redis.redis.get(metadata_key)
            trigger_metadata = json.loads(metadata_bytes.decode()) if metadata_bytes else {}

            if event_id:
                # Trigger Claude analysis
                logger.info(f"Triggering Claude analysis for {camera_id}")
                self.analyses_triggered += 1

                # Run analysis in background
                asyncio.create_task(
                    get_ai_agent().analyze_scene(camera_id, trigger_metadata, event_id)
                )

                # Clean up request metadata
                await redis.redis.delete(event_id_key, metadata_key)
            else:
                logger.warning(f"No event ID found for frame request: {request_id}")

        except json.JSONDecodeError as e:
            logger.error(f"Invalid JSON in frame: {str(e)}")
        except Exception as e:
            logger.error(f"Error handling frame: {str(e)}")

    async def handle_status(self, camera_id: str, payload: str):
        """Handle camera status updates"""
        try:
            status = json.loads(payload)
            logger.debug(f"Camera status: {camera_id} - {status.get('state', 'unknown')}")

            # Update camera state in Redis
            await redis.set_camera_state(camera_id, status, ttl=120)

        except json.JSONDecodeError as e:
            logger.error(f"Invalid JSON in status: {str(e)}")
        except Exception as e:
            logger.error(f"Error handling status: {str(e)}")

    async def handle_event(self, camera_id: str, payload: str):
        """Handle significant events from camera"""
        try:
            event = json.loads(payload)
            logger.info(f"Camera event: {camera_id} - {event.get('type', 'unknown')}")

            # Could trigger immediate analysis for high-priority events
            # For now, just log

        except json.JSONDecodeError as e:
            logger.error(f"Invalid JSON in event: {str(e)}")
        except Exception as e:
            logger.error(f"Error handling event: {str(e)}")

    async def handle_alert(self, camera_id: str, payload: str):
        """Handle critical alerts from camera"""
        try:
            alert = json.loads(payload)
            logger.warning(f"Camera ALERT: {camera_id} - {alert.get('message', 'unknown')}")

            # Store alert in database
            # TODO: Implement alert notification system

        except json.JSONDecodeError as e:
            logger.error(f"Invalid JSON in alert: {str(e)}")
        except Exception as e:
            logger.error(f"Error handling alert: {str(e)}")

    async def request_frame(self, camera_id: str, reason: str, event_id: int,
                           trigger_metadata: Dict[str, Any]):
        """Send frame request to camera"""
        try:
            # Generate unique request ID
            request_id = str(uuid.uuid4())

            # Build request message
            request = {
                "request_id": request_id,
                "reason": reason,
                "timestamp": trigger_metadata.get('timestamp_us')
            }

            # Store event ID and metadata for later (when frame arrives)
            await redis.redis.setex(
                f"frame_request:{request_id}:event_id",
                300,  # 5 minute TTL
                str(event_id)
            )
            await redis.redis.setex(
                f"frame_request:{request_id}:metadata",
                300,
                json.dumps(trigger_metadata)
            )

            # Publish request
            topic = f"axis-is/camera/{camera_id}/frame_request"
            await self.client.publish(
                topic,
                payload=json.dumps(request),
                qos=1
            )

            # Set cooldown
            await redis.set_request_cooldown(camera_id, settings.frame_request_cooldown)

            self.frame_requests_sent += 1
            logger.info(f"Frame requested: {camera_id} (id={request_id}, reason={reason})")

        except Exception as e:
            logger.error(f"Error requesting frame: {str(e)}")

    def get_stats(self) -> Dict[str, Any]:
        """Get handler statistics"""
        return {
            'messages_received': self.messages_received,
            'frame_requests_sent': self.frame_requests_sent,
            'analyses_triggered': self.analyses_triggered,
            'running': self.running
        }


# Global instance
mqtt_handler = MQTTHandler()
