"""
Cloud Service Configuration
"""
import os
from pydantic_settings import BaseSettings
from typing import Optional


class Settings(BaseSettings):
    """Application settings from environment variables"""

    # Application
    app_name: str = "Axis I.S. Cloud Service"
    app_version: str = "1.0.0"
    debug: bool = False

    # MQTT
    mqtt_broker: str = "localhost"
    mqtt_port: int = 1883
    mqtt_username: Optional[str] = None
    mqtt_password: Optional[str] = None
    mqtt_keepalive: int = 60
    mqtt_reconnect_delay: int = 5

    # AI Provider Selection
    ai_provider: str = "claude"  # Options: "claude", "gemini"

    # Claude API Settings
    anthropic_api_key: Optional[str] = None  # Required if ai_provider="claude"
    claude_model: str = "claude-3-5-sonnet-20241022"  # or "claude-3-5-haiku-20241022"
    claude_max_tokens: int = 500
    claude_timeout: int = 30

    # Gemini API Settings
    gemini_api_key: Optional[str] = None  # Required if ai_provider="gemini"
    gemini_model: str = "gemini-3-pro"  # or "gemini-2.0-flash-exp", "gemini-1.5-pro"
    gemini_timeout: int = 30

    # Database
    database_url: str = "postgresql://postgres:postgres@localhost:5432/axis_is"
    database_pool_size: int = 20
    database_max_overflow: int = 10

    # Redis
    redis_url: str = "redis://localhost:6379"
    redis_db: int = 0
    redis_max_connections: int = 50

    # Scene Memory
    scene_memory_frames: int = 30  # Keep last 30 frames per camera
    scene_memory_ttl: int = 600     # 10 minutes TTL

    # Frame Request
    frame_request_cooldown: int = 60  # Minimum seconds between requests per camera
    frame_request_enabled: bool = True

    # Triggers for AI Analysis
    motion_threshold: float = 0.7          # Request frame if motion > 0.7
    vehicle_confidence_threshold: float = 0.5  # Request frame if vehicle detected
    scene_change_enabled: bool = True      # Request frame on scene hash change

    # Performance
    max_concurrent_analyses: int = 5  # Max concurrent AI API calls

    class Config:
        env_file = ".env"
        case_sensitive = False


# Global settings instance
settings = Settings()
