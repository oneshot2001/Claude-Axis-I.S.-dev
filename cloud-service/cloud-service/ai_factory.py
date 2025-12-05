"""
AI Agent Factory
Selects and creates the appropriate AI agent based on configuration
"""
import logging
from typing import Optional
from ai_agent_base import AIAgentBase
from config import settings

logger = logging.getLogger(__name__)

# Global agent instance
_ai_agent: Optional[AIAgentBase] = None


def create_ai_agent() -> AIAgentBase:
    """
    Create AI agent based on configuration

    Returns:
        AI agent instance (Claude or Gemini)

    Raises:
        ValueError: If unknown provider specified
        RuntimeError: If required API keys are missing
    """
    provider = settings.ai_provider.lower()

    logger.info(f"Creating AI agent: provider={provider}")

    if provider == "claude":
        # Validate Claude API key
        if not settings.anthropic_api_key:
            raise RuntimeError("ANTHROPIC_API_KEY is required for Claude provider")

        from claude_agent import ClaudeAgent
        agent = ClaudeAgent()
        logger.info(f"Claude agent created: model={settings.claude_model}")
        return agent

    elif provider == "gemini":
        # Validate Gemini API key
        if not settings.gemini_api_key:
            raise RuntimeError("GEMINI_API_KEY is required for Gemini provider")

        from gemini_agent import GeminiAgent
        agent = GeminiAgent()
        logger.info(f"Gemini agent created: model={settings.gemini_model}")
        return agent

    else:
        raise ValueError(
            f"Unknown AI provider: {provider}. "
            f"Supported providers: claude, gemini"
        )


def get_ai_agent() -> AIAgentBase:
    """
    Get the global AI agent instance (singleton pattern)

    Returns:
        AI agent instance

    Raises:
        RuntimeError: If agent not initialized
    """
    global _ai_agent

    if _ai_agent is None:
        _ai_agent = create_ai_agent()

    return _ai_agent


def reset_ai_agent():
    """Reset the global AI agent (useful for testing or config changes)"""
    global _ai_agent
    _ai_agent = None
    logger.info("AI agent reset")
