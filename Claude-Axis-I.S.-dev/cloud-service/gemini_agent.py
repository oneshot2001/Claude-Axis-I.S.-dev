"""
Gemini Vision Agent
Analyzes camera scenes using Google Gemini API with visual context
"""
import asyncio
import logging
import time
import base64
from typing import Dict, Any, List, Optional
import google.generativeai as genai
from ai_agent_base import AIAgentBase
from scene_memory import scene_memory
from database import db, redis
from config import settings

logger = logging.getLogger(__name__)


class GeminiAgent(AIAgentBase):
    """Google Gemini API integration for scene analysis"""

    def __init__(self):
        # Configure Gemini API
        genai.configure(api_key=settings.gemini_api_key)
        self.model = genai.GenerativeModel(settings.gemini_model)

        self.analyses_count = 0
        self.total_input_chars = 0  # Gemini doesn't expose tokens directly
        self.semaphore = asyncio.Semaphore(settings.max_concurrent_analyses)

    async def analyze_scene(self, camera_id: str, trigger_metadata: Dict[str, Any],
                           event_id: int) -> Optional[Dict[str, Any]]:
        """
        Analyze a scene using Gemini Vision API

        Args:
            camera_id: Camera identifier
            trigger_metadata: Metadata that triggered the analysis
            event_id: Database event ID

        Returns:
            Analysis result with summary
        """
        async with self.semaphore:  # Limit concurrent API calls
            start_time = time.time()

            try:
                # Get recent frames from scene memory
                frames = await scene_memory.get_recent_frames(
                    camera_id,
                    limit=5,
                    with_images=True
                )

                if not frames:
                    logger.warning(f"No frames available for analysis: {camera_id}")
                    return None

                # Get scene context
                context = await scene_memory.get_context_for_analysis(camera_id)

                # Build prompt
                prompt = self._build_analysis_prompt(camera_id, trigger_metadata, context)

                # Build content parts (text + images)
                content_parts = [prompt]

                # Add images (up to 5 most recent)
                # Gemini expects raw image data, not base64 in multimodal
                for frame in frames[-5:]:
                    if frame.get('image_base64'):
                        # Decode base64 to bytes for Gemini
                        image_bytes = base64.b64decode(frame['image_base64'])
                        content_parts.append({
                            'mime_type': 'image/jpeg',
                            'data': image_bytes
                        })

                logger.info(f"Analyzing scene: {camera_id} with {len(frames)} frames")

                # Call Gemini API (async via run_in_executor since Gemini SDK is sync)
                loop = asyncio.get_event_loop()
                response = await loop.run_in_executor(
                    None,
                    lambda: self.model.generate_content(
                        content_parts,
                        generation_config=genai.types.GenerationConfig(
                            max_output_tokens=settings.claude_max_tokens,  # Reuse setting
                            temperature=0.4
                        )
                    )
                )

                # Extract summary
                summary = response.text if response.text else "No response"

                # Calculate duration
                duration_ms = int((time.time() - start_time) * 1000)

                # Update stats (Gemini doesn't expose token counts in same way)
                self.analyses_count += 1
                self.total_input_chars += len(prompt)

                logger.info(f"Analysis complete: {camera_id} in {duration_ms}ms")

                # Store analysis in database
                analysis_id = await db.store_analysis(
                    camera_id=camera_id,
                    event_id=event_id,
                    summary=summary,
                    full_response={
                        'model': settings.gemini_model,
                        'finish_reason': response.candidates[0].finish_reason.name if response.candidates else None,
                        'safety_ratings': [
                            {
                                'category': rating.category.name,
                                'probability': rating.probability.name
                            }
                            for rating in response.candidates[0].safety_ratings
                        ] if response.candidates else [],
                        'content': summary
                    },
                    frames_analyzed=len(frames),
                    duration_ms=duration_ms
                )

                logger.info(f"Analysis stored: ID={analysis_id}")

                return {
                    'analysis_id': analysis_id,
                    'summary': summary,
                    'frames_analyzed': len(frames),
                    'duration_ms': duration_ms,
                    'tokens': None,  # Gemini doesn't expose this easily
                    'provider': 'gemini',
                    'model': settings.gemini_model
                }

            except Exception as e:
                logger.error(f"Gemini analysis failed: {camera_id} - {str(e)}")
                return None

    def _build_analysis_prompt(self, camera_id: str, trigger_metadata: Dict[str, Any],
                               context: Dict[str, Any]) -> str:
        """Build analysis prompt for Gemini"""
        detections = trigger_metadata.get('detections', [])
        motion_score = trigger_metadata.get('motion_score', 0)

        # Format detections
        detection_summary = []
        for det in detections[:10]:  # Limit to top 10
            detection_summary.append(
                f"- {self._get_class_name(det.get('class_id', 0))}: "
                f"{det.get('confidence', 0):.2f} confidence"
            )

        prompt = f"""You are analyzing surveillance camera footage from {camera_id}.

**Current Scene Trigger:**
- Motion Score: {motion_score:.2f}
- Objects Detected: {len(detections)}
{chr(10).join(detection_summary) if detection_summary else "- None"}

**Scene Context (last {context['frames_available']} frames):**
- Time Span: {context.get('time_span_seconds', 0):.1f} seconds
- Total Objects: {context.get('total_objects_detected', 0)}
- Average Motion: {context.get('average_motion_score', 0):.2f}
- Frames with Visual Data: {context.get('frames_with_images', 0)}

**Your Task:**
Provide a concise executive summary (2-3 sentences) of what's happening in this scene. Focus on:
1. What activity or event is occurring
2. Any notable objects or people
3. Whether this appears significant or routine
4. Any potential security concerns

Be specific and actionable. If nothing significant is happening, state that clearly.
"""
        return prompt

    def _get_class_name(self, class_id: int) -> str:
        """Get COCO class name from ID"""
        coco_classes = {
            0: 'person', 1: 'bicycle', 2: 'car', 3: 'motorcycle', 4: 'airplane',
            5: 'bus', 6: 'train', 7: 'truck', 8: 'boat', 9: 'traffic light',
            10: 'fire hydrant', 11: 'stop sign', 12: 'parking meter', 13: 'bench',
            14: 'bird', 15: 'cat', 16: 'dog', 17: 'horse', 18: 'sheep', 19: 'cow',
            20: 'elephant', 21: 'bear', 22: 'zebra', 23: 'giraffe', 24: 'backpack',
            25: 'umbrella', 26: 'handbag', 27: 'tie', 28: 'suitcase', 29: 'frisbee',
            30: 'skis', 31: 'snowboard', 32: 'sports ball', 33: 'kite', 34: 'baseball bat',
            35: 'baseball glove', 36: 'skateboard', 37: 'surfboard', 38: 'tennis racket',
            39: 'bottle', 40: 'wine glass', 41: 'cup', 42: 'fork', 43: 'knife',
            44: 'spoon', 45: 'bowl', 46: 'banana', 47: 'apple', 48: 'sandwich',
            49: 'orange', 50: 'broccoli', 51: 'carrot', 52: 'hot dog', 53: 'pizza',
            54: 'donut', 55: 'cake', 56: 'chair', 57: 'couch', 58: 'potted plant',
            59: 'bed', 60: 'dining table', 61: 'toilet', 62: 'tv', 63: 'laptop',
            64: 'mouse', 65: 'remote', 66: 'keyboard', 67: 'cell phone', 68: 'microwave',
            69: 'oven', 70: 'toaster', 71: 'sink', 72: 'refrigerator', 73: 'book',
            74: 'clock', 75: 'vase', 76: 'scissors', 77: 'teddy bear', 78: 'hair drier',
            79: 'toothbrush'
        }
        return coco_classes.get(class_id, f'class_{class_id}')

    def get_stats(self) -> Dict[str, Any]:
        """Get agent statistics"""
        return {
            'provider': 'gemini',
            'model': settings.gemini_model,
            'analyses_count': self.analyses_count,
            'total_input_chars': self.total_input_chars,
            'average_input_chars': self.total_input_chars // self.analyses_count if self.analyses_count > 0 else 0,
            'max_concurrent': settings.max_concurrent_analyses
        }

    def get_provider_name(self) -> str:
        """Get the AI provider name"""
        return "gemini"

    def get_model_name(self) -> str:
        """Get the model name being used"""
        return settings.gemini_model


# Global instance
gemini_agent = GeminiAgent()
