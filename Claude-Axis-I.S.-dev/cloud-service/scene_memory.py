"""
Scene Memory Manager
Maintains last 30 frames per camera with metadata correlation
"""
import logging
from typing import Dict, Any, List, Optional
from database import redis
from config import settings

logger = logging.getLogger(__name__)


class SceneMemory:
    """Manages scene memory for each camera"""

    def __init__(self):
        self.frames_per_camera: Dict[str, int] = {}

    async def add_metadata(self, camera_id: str, metadata: Dict[str, Any]):
        """Add metadata event to scene memory"""
        timestamp_us = metadata.get('timestamp_us')
        if not timestamp_us:
            logger.warning(f"Metadata missing timestamp: {camera_id}")
            return

        # Build frame entry
        frame_data = {
            'timestamp_us': timestamp_us,
            'frame_id': metadata.get('sequence'),
            'motion_score': metadata.get('motion_score', 0),
            'object_count': metadata.get('object_count', 0),
            'scene_hash': metadata.get('scene_hash'),
            'detections': metadata.get('detections', []),
            'has_image': False  # Will be updated when frame arrives
        }

        # Store in Redis sorted set
        await redis.add_to_scene_memory(
            camera_id,
            timestamp_us,
            frame_data,
            ttl=settings.scene_memory_ttl
        )

        # Track stats
        if camera_id not in self.frames_per_camera:
            self.frames_per_camera[camera_id] = 0
        self.frames_per_camera[camera_id] += 1

        logger.debug(f"Added metadata to scene memory: {camera_id} @ {timestamp_us}")

    async def add_frame_image(self, camera_id: str, request_id: str,
                             timestamp_us: int, image_base64: str):
        """Update scene memory entry with actual frame image"""
        # Get current scene memory
        frames = await redis.get_scene_memory(camera_id, limit=settings.scene_memory_frames)

        # Find matching frame by timestamp (within 1 second tolerance)
        updated = False
        for frame in frames:
            if abs(frame['timestamp_us'] - timestamp_us) < 1_000_000:  # 1 second
                frame['has_image'] = True
                frame['image_base64'] = image_base64
                frame['request_id'] = request_id
                updated = True

                # Re-add to Redis
                await redis.add_to_scene_memory(
                    camera_id,
                    frame['timestamp_us'],
                    frame,
                    ttl=settings.scene_memory_ttl
                )

                logger.info(f"Updated scene memory with frame image: {camera_id} @ {timestamp_us}")
                break

        if not updated:
            # Frame not found in memory, add it
            frame_data = {
                'timestamp_us': timestamp_us,
                'request_id': request_id,
                'has_image': True,
                'image_base64': image_base64
            }
            await redis.add_to_scene_memory(camera_id, timestamp_us, frame_data,
                                           ttl=settings.scene_memory_ttl)
            logger.info(f"Added new frame to scene memory: {camera_id} @ {timestamp_us}")

    async def get_recent_frames(self, camera_id: str, limit: int = 5,
                               with_images: bool = True) -> List[Dict[str, Any]]:
        """Get recent frames from scene memory"""
        frames = await redis.get_scene_memory(camera_id, limit=settings.scene_memory_frames)

        if with_images:
            # Filter only frames with images
            frames = [f for f in frames if f.get('has_image', False)]

        # Return most recent N
        return frames[-limit:] if limit else frames

    async def get_context_for_analysis(self, camera_id: str) -> Dict[str, Any]:
        """Build context for Claude analysis"""
        frames = await redis.get_scene_memory(camera_id, limit=30)

        if not frames:
            return {
                'camera_id': camera_id,
                'frames_available': 0,
                'summary': 'No frames in memory'
            }

        # Aggregate statistics
        total_objects = sum(f.get('object_count', 0) for f in frames)
        avg_motion = sum(f.get('motion_score', 0) for f in frames) / len(frames) if frames else 0
        frames_with_images = sum(1 for f in frames if f.get('has_image', False))

        # Get unique detection classes
        all_detections = []
        for f in frames:
            all_detections.extend(f.get('detections', []))

        unique_classes = set(d.get('class_id') for d in all_detections if 'class_id' in d)

        # Build context
        context = {
            'camera_id': camera_id,
            'frames_available': len(frames),
            'frames_with_images': frames_with_images,
            'time_span_seconds': (frames[-1]['timestamp_us'] - frames[0]['timestamp_us']) / 1_000_000 if len(frames) > 1 else 0,
            'total_objects_detected': total_objects,
            'average_motion_score': round(avg_motion, 3),
            'unique_object_classes': len(unique_classes),
            'latest_timestamp': frames[-1]['timestamp_us'] if frames else None
        }

        return context

    async def should_request_frame(self, camera_id: str, metadata: Dict[str, Any]) -> tuple[bool, str]:
        """Determine if we should request a frame based on metadata"""
        # Check cooldown
        can_request = await redis.check_request_cooldown(camera_id)
        if not can_request:
            return False, "cooldown"

        # Check if frame requests are enabled
        if not settings.frame_request_enabled:
            return False, "disabled"

        # Trigger conditions
        motion_score = metadata.get('motion_score') or 0
        detections = metadata.get('detections') or []
        scene_hash = metadata.get('scene_hash')

        # High motion trigger
        if motion_score and motion_score > settings.motion_threshold:
            return True, f"high_motion_{motion_score:.2f}"

        # Vehicle detection trigger
        vehicle_classes = [2, 5, 7]  # car, bus, truck in COCO
        for det in detections:
            if (det.get('class_id') in vehicle_classes and
                det.get('confidence', 0) > settings.vehicle_confidence_threshold):
                return True, f"vehicle_detected_{det['class_id']}"

        # Scene change trigger
        if settings.scene_change_enabled and scene_hash:
            # Get last scene hash from camera state
            state = await redis.get_camera_state(camera_id)
            if state and state.get('last_scene_hash'):
                if state['last_scene_hash'] != scene_hash:
                    # Update scene hash
                    await redis.set_camera_state(camera_id, {'last_scene_hash': scene_hash})
                    return True, "scene_change"
            else:
                # First time seeing this camera
                await redis.set_camera_state(camera_id, {'last_scene_hash': scene_hash})

        return False, "no_trigger"

    def get_stats(self) -> Dict[str, Any]:
        """Get scene memory statistics"""
        return {
            'cameras': len(self.frames_per_camera),
            'total_frames_processed': sum(self.frames_per_camera.values()),
            'frames_per_camera': self.frames_per_camera
        }


# Global instance
scene_memory = SceneMemory()
