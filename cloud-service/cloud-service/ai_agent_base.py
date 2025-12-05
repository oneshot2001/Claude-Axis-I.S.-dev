"""
AI Agent Abstract Base Class
Defines interface for different AI providers (Claude, Gemini, etc.)
"""
from abc import ABC, abstractmethod
from typing import Dict, Any, Optional


class AIAgentBase(ABC):
    """Abstract base class for AI analysis agents"""

    @abstractmethod
    async def analyze_scene(self, camera_id: str, trigger_metadata: Dict[str, Any],
                           event_id: int) -> Optional[Dict[str, Any]]:
        """
        Analyze a scene using AI vision API

        Args:
            camera_id: Camera identifier
            trigger_metadata: Metadata that triggered the analysis
            event_id: Database event ID

        Returns:
            Analysis result with summary, or None if failed
            {
                'analysis_id': int,
                'summary': str,
                'frames_analyzed': int,
                'duration_ms': int,
                'tokens': int (if applicable),
                'provider': str,
                'model': str
            }
        """
        pass

    @abstractmethod
    def get_stats(self) -> Dict[str, Any]:
        """
        Get agent statistics

        Returns:
            Statistics dict with provider-specific metrics
            {
                'analyses_count': int,
                'total_tokens': int (if applicable),
                'provider': str,
                'model': str,
                ...
            }
        """
        pass

    @abstractmethod
    def get_provider_name(self) -> str:
        """Get the AI provider name (e.g., 'claude', 'gemini')"""
        pass

    @abstractmethod
    def get_model_name(self) -> str:
        """Get the model name being used"""
        pass
