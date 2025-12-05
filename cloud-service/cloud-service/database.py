"""
Database Layer - PostgreSQL + Redis
"""
import asyncpg
from redis import asyncio as aioredis
from typing import Optional, Dict, Any, List
import json
import logging
from datetime import datetime
from config import settings

logger = logging.getLogger(__name__)


class Database:
    """PostgreSQL database manager"""

    def __init__(self):
        self.pool: Optional[asyncpg.Pool] = None

    async def connect(self):
        """Create database connection pool"""
        logger.info(f"Connecting to PostgreSQL: {settings.database_url}")
        self.pool = await asyncpg.create_pool(
            settings.database_url,
            min_size=5,
            max_size=settings.database_pool_size,
            max_inactive_connection_lifetime=300
        )
        logger.info("PostgreSQL connected")

        # Run migrations
        await self.create_schema()

    async def disconnect(self):
        """Close database connection pool"""
        if self.pool:
            await self.pool.close()
            logger.info("PostgreSQL disconnected")

    async def create_schema(self):
        """Create database schema if not exists"""
        async with self.pool.acquire() as conn:
            # Camera events table (partitioned by day)
            await conn.execute("""
                CREATE TABLE IF NOT EXISTS camera_events (
                    id BIGSERIAL,
                    camera_id VARCHAR(64) NOT NULL,
                    timestamp_us BIGINT NOT NULL,
                    frame_id BIGINT,
                    metadata JSONB NOT NULL,
                    motion_score FLOAT,
                    object_count INT,
                    scene_hash BIGINT,
                    created_at TIMESTAMP DEFAULT NOW(),
                    PRIMARY KEY (id, timestamp_us)
                )
            """)

            # Create indexes
            await conn.execute("""
                CREATE INDEX IF NOT EXISTS idx_camera_events_camera_time
                ON camera_events(camera_id, timestamp_us DESC)
            """)

            await conn.execute("""
                CREATE INDEX IF NOT EXISTS idx_camera_events_motion
                ON camera_events(motion_score) WHERE motion_score > 0.5
            """)

            # Claude analyses table
            await conn.execute("""
                CREATE TABLE IF NOT EXISTS claude_analyses (
                    id BIGSERIAL PRIMARY KEY,
                    camera_id VARCHAR(64) NOT NULL,
                    trigger_event_id BIGINT,
                    timestamp_us BIGINT NOT NULL,
                    summary TEXT NOT NULL,
                    full_response JSONB,
                    frames_analyzed INT DEFAULT 0,
                    analysis_duration_ms INT,
                    created_at TIMESTAMP DEFAULT NOW()
                )
            """)

            await conn.execute("""
                CREATE INDEX IF NOT EXISTS idx_claude_analyses_camera_time
                ON claude_analyses(camera_id, timestamp_us DESC)
            """)

            # Alerts table
            await conn.execute("""
                CREATE TABLE IF NOT EXISTS alerts (
                    id BIGSERIAL PRIMARY KEY,
                    camera_id VARCHAR(64) NOT NULL,
                    analysis_id BIGINT REFERENCES claude_analyses(id),
                    alert_type VARCHAR(64) NOT NULL,
                    severity INT NOT NULL,
                    message TEXT NOT NULL,
                    metadata JSONB,
                    acknowledged BOOLEAN DEFAULT FALSE,
                    created_at TIMESTAMP DEFAULT NOW()
                )
            """)

            logger.info("Database schema created/verified")

    async def store_event(self, camera_id: str, metadata: Dict[str, Any]) -> int:
        """Store camera event"""
        async with self.pool.acquire() as conn:
            row = await conn.fetchrow("""
                INSERT INTO camera_events
                (camera_id, timestamp_us, frame_id, metadata, motion_score, object_count, scene_hash)
                VALUES ($1, $2, $3, $4, $5, $6, $7)
                RETURNING id
            """,
                camera_id,
                metadata.get('timestamp_us'),
                metadata.get('sequence'),
                json.dumps(metadata),
                metadata.get('motion_score'),
                metadata.get('object_count'),
                metadata.get('scene_hash')
            )
            return row['id']

    async def store_analysis(self, camera_id: str, event_id: int, summary: str,
                            full_response: Dict[str, Any], frames_analyzed: int,
                            duration_ms: int) -> int:
        """Store Claude analysis"""
        async with self.pool.acquire() as conn:
            row = await conn.fetchrow("""
                INSERT INTO claude_analyses
                (camera_id, trigger_event_id, timestamp_us, summary, full_response,
                 frames_analyzed, analysis_duration_ms)
                VALUES ($1, $2, $3, $4, $5, $6, $7)
                RETURNING id
            """,
                camera_id,
                event_id,
                int(datetime.utcnow().timestamp() * 1_000_000),
                summary,
                json.dumps(full_response),
                frames_analyzed,
                duration_ms
            )
            return row['id']

    async def get_recent_analyses(self, camera_id: str, limit: int = 10) -> List[Dict[str, Any]]:
        """Get recent Claude analyses for a camera"""
        async with self.pool.acquire() as conn:
            rows = await conn.fetch("""
                SELECT id, timestamp_us, summary, frames_analyzed, created_at
                FROM claude_analyses
                WHERE camera_id = $1
                ORDER BY timestamp_us DESC
                LIMIT $2
            """, camera_id, limit)
            return [dict(row) for row in rows]


class RedisCache:
    """Redis cache manager"""

    def __init__(self):
        self.redis: Optional[aioredis.Redis] = None

    async def connect(self):
        """Connect to Redis"""
        logger.info(f"Connecting to Redis: {settings.redis_url}")
        self.redis = await aioredis.from_url(
            settings.redis_url,
            db=settings.redis_db,
            max_connections=settings.redis_max_connections,
            decode_responses=False  # We'll handle encoding
        )
        logger.info("Redis connected")

    async def disconnect(self):
        """Disconnect from Redis"""
        if self.redis:
            await self.redis.close()
            logger.info("Redis disconnected")

    async def set_camera_state(self, camera_id: str, state: Dict[str, Any], ttl: int = 300):
        """Set camera state"""
        key = f"camera:{camera_id}:state"
        await self.redis.hset(key, mapping={k: json.dumps(v) for k, v in state.items()})
        await self.redis.expire(key, ttl)

    async def get_camera_state(self, camera_id: str) -> Optional[Dict[str, Any]]:
        """Get camera state"""
        key = f"camera:{camera_id}:state"
        data = await self.redis.hgetall(key)
        if not data:
            return None
        return {k.decode(): json.loads(v) for k, v in data.items()}

    async def check_request_cooldown(self, camera_id: str) -> bool:
        """Check if we can request a frame (rate limiting)"""
        key = f"camera:{camera_id}:last_request"
        exists = await self.redis.exists(key)
        return exists == 0

    async def set_request_cooldown(self, camera_id: str, cooldown: int):
        """Set frame request cooldown"""
        key = f"camera:{camera_id}:last_request"
        await self.redis.setex(key, cooldown, "1")

    async def add_to_scene_memory(self, camera_id: str, timestamp_us: int,
                                  frame_data: Dict[str, Any], ttl: int = 600):
        """Add frame to scene memory (sorted set by timestamp)"""
        key = f"camera:{camera_id}:scene_memory"
        await self.redis.zadd(key, {json.dumps(frame_data): timestamp_us})
        await self.redis.expire(key, ttl)

        # Keep only last N frames
        await self.redis.zremrangebyrank(key, 0, -(settings.scene_memory_frames + 1))

    async def get_scene_memory(self, camera_id: str, limit: int = 30) -> List[Dict[str, Any]]:
        """Get recent frames from scene memory"""
        key = f"camera:{camera_id}:scene_memory"
        frames = await self.redis.zrange(key, -limit, -1)
        return [json.loads(f) for f in frames]

    async def get_stats(self) -> Dict[str, Any]:
        """Get Redis stats"""
        info = await self.redis.info()
        return {
            "connected_clients": info.get("connected_clients"),
            "used_memory_human": info.get("used_memory_human"),
            "total_keys": await self.redis.dbsize()
        }


# Global instances
db = Database()
redis = RedisCache()
