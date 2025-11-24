# AI Model Selection Guide

**Axis I.S. Platform - Multi-Model Support**

The Axis I.S. cloud service now supports multiple AI providers and models for scene analysis. Choose the right model based on your quality, speed, and cost requirements.

---

## Supported Models

### Claude (Anthropic)

**Claude Sonnet 4.5** - Highest Quality
- Model: `claude-3-5-sonnet-20241022`
- Best for: Maximum analysis quality, complex scenes
- Speed: Medium (2-5 seconds per analysis)
- Cost: $3.00 / 1M input tokens, $15.00 / 1M output tokens
- Token estimate: ~500-1000 tokens per analysis

**Claude Haiku 4.5** - Fast & Affordable
- Model: `claude-3-5-haiku-20241022`
- Best for: Cost-sensitive deployments, high volume
- Speed: Fast (1-3 seconds per analysis)
- Cost: $0.80 / 1M input tokens, $4.00 / 1M output tokens
- Token estimate: ~500-1000 tokens per analysis

### Gemini (Google)

**Gemini 2.0 Flash** - Maximum Cost Savings
- Model: `gemini-2.0-flash-exp`
- Best for: Highest volume, lowest cost
- Speed: Very Fast (<1 second per analysis)
- Cost: $0.10 / 1M input tokens, $0.40 / 1M output tokens
- Note: Experimental model, may have lower quality than production models

**Gemini 1.5 Pro** - Balanced Quality/Cost
- Model: `gemini-1.5-pro`
- Best for: Balance between quality and cost
- Speed: Medium (2-4 seconds per analysis)
- Cost: $1.25 / 1M input tokens, $5.00 / 1M output tokens

---

## Cost Comparison

### Monthly Cost Estimates (3 Cameras)

**Scenario: Conservative** (1 analysis/camera/hour)
- 2,160 analyses/month
- ~1,080,000 tokens/month

| Model | Input Cost | Output Cost | Total/Month |
|-------|------------|-------------|-------------|
| Claude Sonnet 4.5 | $1.62 | $8.10 | **$9.72** |
| Claude Haiku 4.5 | $0.43 | $2.16 | **$2.59** |
| Gemini 2.0 Flash | $0.05 | $0.22 | **$0.27** |
| Gemini 1.5 Pro | $0.68 | $2.70 | **$3.38** |

**Scenario: Active** (10 analyses/camera/hour)
- 21,600 analyses/month
- ~10,800,000 tokens/month

| Model | Input Cost | Output Cost | Total/Month |
|-------|------------|-------------|-------------|
| Claude Sonnet 4.5 | $16.20 | $81.00 | **$97.20** |
| Claude Haiku 4.5 | $4.32 | $21.60 | **$25.92** |
| Gemini 2.0 Flash | $0.54 | $2.16 | **$2.70** |
| Gemini 1.5 Pro | $6.75 | $27.00 | **$33.75** |

**Scenario: Very Active** (50 analyses/camera/hour)
- 108,000 analyses/month
- ~54,000,000 tokens/month

| Model | Input Cost | Output Cost | Total/Month |
|-------|------------|-------------|-------------|
| Claude Sonnet 4.5 | $81.00 | $405.00 | **$486.00** |
| Claude Haiku 4.5 | $21.60 | $108.00 | **$129.60** |
| Gemini 2.0 Flash | $2.70 | $10.80 | **$13.50** |
| Gemini 1.5 Pro | $33.75 | $135.00 | **$168.75** |

### Cost Savings vs. Claude Sonnet 4.5

| Model | Conservative | Active | Very Active |
|-------|--------------|--------|-------------|
| Claude Haiku 4.5 | **73% savings** | **73% savings** | **73% savings** |
| Gemini 2.0 Flash | **97% savings** | **97% savings** | **97% savings** |
| Gemini 1.5 Pro | **65% savings** | **65% savings** | **65% savings** |

---

## Configuration

### Option 1: Claude Sonnet (Highest Quality)

```env
AI_PROVIDER=claude
ANTHROPIC_API_KEY=sk-ant-api-your-key-here
CLAUDE_MODEL=claude-3-5-sonnet-20241022
```

### Option 2: Claude Haiku (Fast & Cheap)

```env
AI_PROVIDER=claude
ANTHROPIC_API_KEY=sk-ant-api-your-key-here
CLAUDE_MODEL=claude-3-5-haiku-20241022
```

### Option 3: Gemini Flash (Maximum Savings)

```env
AI_PROVIDER=gemini
GEMINI_API_KEY=AIza-your-key-here
GEMINI_MODEL=gemini-2.0-flash-exp
```

### Option 4: Gemini Pro (Balanced)

```env
AI_PROVIDER=gemini
GEMINI_API_KEY=AIza-your-key-here
GEMINI_MODEL=gemini-1.5-pro
```

---

## Performance Comparison

### Response Quality

| Model | Scene Understanding | Detail Level | Accuracy | Best Use Case |
|-------|---------------------|--------------|----------|---------------|
| Claude Sonnet 4.5 | Excellent | Very High | Highest | Security-critical, complex scenes |
| Claude Haiku 4.5 | Good | Medium-High | High | General surveillance, routine monitoring |
| Gemini 2.0 Flash | Good | Medium | Good | High-volume, cost-sensitive deployments |
| Gemini 1.5 Pro | Very Good | High | Very High | Balance between quality and cost |

### Response Speed

| Model | Average Latency | Max Concurrent | Throughput |
|-------|----------------|----------------|------------|
| Claude Sonnet 4.5 | 2-5s | 5 | Medium |
| Claude Haiku 4.5 | 1-3s | 5 | High |
| Gemini 2.0 Flash | <1s | 5 | Very High |
| Gemini 1.5 Pro | 2-4s | 5 | Medium-High |

---

## Selection Decision Tree

### 1. What's your priority?

**Quality & Accuracy** → Claude Sonnet 4.5
- Security-critical applications
- Complex scene analysis
- Maximum detail required

**Speed & Volume** → Gemini 2.0 Flash
- High-traffic locations
- Real-time requirements
- Budget constraints

**Balanced** → Claude Haiku 4.5 or Gemini 1.5 Pro
- General surveillance
- Good quality at reasonable cost
- Most common use case

### 2. What's your budget?

**Premium** ($50-500/month) → Claude Sonnet 4.5
**Mid-Range** ($25-100/month) → Claude Haiku 4.5 or Gemini 1.5 Pro
**Budget** ($5-25/month) → Gemini 2.0 Flash

### 3. What's your analysis frequency?

**Low** (<5/hour/camera) → Any model works well
**Medium** (5-20/hour/camera) → Claude Haiku or Gemini
**High** (>20/hour/camera) → Gemini 2.0 Flash strongly recommended

---

## API Key Setup

### Claude (Anthropic)

1. Sign up at https://console.anthropic.com/
2. Navigate to API Keys
3. Create new key
4. Copy key (starts with `sk-ant-api-...`)
5. Add to `.env`: `ANTHROPIC_API_KEY=sk-ant-api-...`

**Pricing:** https://www.anthropic.com/pricing

### Gemini (Google)

1. Go to https://aistudio.google.com/app/apikey
2. Create API key
3. Copy key (starts with `AIza...`)
4. Add to `.env`: `GEMINI_API_KEY=AIza...`

**Pricing:** https://ai.google.dev/pricing

---

## Switching Models

### Runtime (No Restart Required)

Models can be switched by updating environment variables and restarting the cloud service:

```bash
# Edit configuration
vi .env

# Change AI_PROVIDER or MODEL settings
# Example: Switch from Claude to Gemini
AI_PROVIDER=gemini
GEMINI_API_KEY=AIza-your-key-here

# Restart service
docker-compose restart cloud-service
```

### Testing Different Models

```bash
# Test Claude Sonnet
AI_PROVIDER=claude CLAUDE_MODEL=claude-3-5-sonnet-20241022 docker-compose up cloud-service

# Test Claude Haiku
AI_PROVIDER=claude CLAUDE_MODEL=claude-3-5-haiku-20241022 docker-compose up cloud-service

# Test Gemini Flash
AI_PROVIDER=gemini GEMINI_MODEL=gemini-2.0-flash-exp docker-compose up cloud-service
```

---

## Monitoring Model Performance

### Check Current Provider

```bash
curl http://localhost:8000/stats | jq '.ai_agent'
```

Returns:
```json
{
  "provider": "claude",
  "model": "claude-3-5-sonnet-20241022",
  "analyses_count": 150,
  "total_tokens": 75000,
  "average_tokens": 500
}
```

### Track Costs

```bash
# Get analysis statistics
curl http://localhost:8000/stats | jq '{
  provider: .ai_agent.provider,
  model: .ai_agent.model,
  total_analyses: .ai_agent.analyses_count,
  total_tokens: .ai_agent.total_tokens,
  avg_tokens: .ai_agent.average_tokens
}'

# Calculate estimated cost (Claude Sonnet example)
# total_tokens * $0.003 / 1000 = cost
```

---

## Recommendations by Use Case

### 🏢 Corporate Office
- **Traffic:** Low-Medium
- **Recommended:** Claude Haiku 4.5
- **Why:** Good balance of quality and cost for routine monitoring

### 🏪 Retail Store
- **Traffic:** Medium-High
- **Recommended:** Gemini 2.0 Flash
- **Why:** High customer traffic requires frequent analysis at low cost

### 🏦 Bank / High Security
- **Traffic:** Medium
- **Recommended:** Claude Sonnet 4.5
- **Why:** Security-critical application requires highest quality

### 🅿️ Parking Lot
- **Traffic:** High
- **Recommended:** Gemini 2.0 Flash or Claude Haiku 4.5
- **Why:** High volume, routine monitoring, cost-sensitive

### 🏭 Manufacturing
- **Traffic:** Low
- **Recommended:** Claude Sonnet 4.5 or Gemini 1.5 Pro
- **Why:** Safety-critical, but infrequent events

---

## Quality Examples

### Example Scene: Delivery Truck Arrival

**Claude Sonnet 4.5:**
> A white delivery truck has reversed into loading bay 3. Two workers are unloading boxes using a forklift. The driver is visible in the cab completing paperwork. Activity appears routine for afternoon deliveries. Motion level is moderate (0.72) consistent with typical loading operations. No security concerns detected.

**Claude Haiku 4.5:**
> Delivery truck in loading bay with two workers unloading boxes. Forklift in use. Routine delivery activity, no security concerns. Motion level normal (0.72).

**Gemini 2.0 Flash:**
> Truck in loading bay, workers unloading with forklift. Normal delivery activity detected. Motion score 0.72.

**Gemini 1.5 Pro:**
> A delivery truck is backed into the loading dock with two workers unloading cargo using a forklift. Driver appears to be in the vehicle. This appears to be routine delivery activity with no apparent security issues. Motion level is moderate.

---

## Migration Guide

### From Single Model to Multi-Model

**Before (Claude Only):**
```env
ANTHROPIC_API_KEY=sk-ant-api-...
CLAUDE_MODEL=claude-3-5-sonnet-20241022
```

**After (Multi-Model Support):**
```env
# Choose provider
AI_PROVIDER=claude  # or "gemini"

# Claude settings (if AI_PROVIDER=claude)
ANTHROPIC_API_KEY=sk-ant-api-...
CLAUDE_MODEL=claude-3-5-sonnet-20241022  # or haiku

# Gemini settings (if AI_PROVIDER=gemini)
GEMINI_API_KEY=AIza-...
GEMINI_MODEL=gemini-2.0-flash-exp  # or 1.5-pro
```

**No code changes required!** Just update `.env` and restart.

---

## Troubleshooting

### Error: "ANTHROPIC_API_KEY is required"
- Check AI_PROVIDER is set to "claude"
- Verify ANTHROPIC_API_KEY is set in .env
- Restart cloud service

### Error: "GEMINI_API_KEY is required"
- Check AI_PROVIDER is set to "gemini"
- Verify GEMINI_API_KEY is set in .env
- Restart cloud service

### Low quality summaries
- Try Claude Sonnet 4.5 instead of Haiku
- Check if frames are being received (view scene memory)
- Verify trigger thresholds are appropriate

### High costs
- Switch to Claude Haiku or Gemini Flash
- Increase FRAME_REQUEST_COOLDOWN
- Raise MOTION_THRESHOLD to reduce triggers

---

## Future Models

The modular architecture supports adding new models easily:

**Coming Soon:**
- OpenAI GPT-4 Vision
- Azure OpenAI
- AWS Bedrock (Claude via AWS)
- Custom models

To add a new provider, implement `AIAgentBase` interface and update `ai_factory.py`.

---

## Summary

| Model | When to Use | Monthly Cost (Active) |
|-------|-------------|----------------------|
| **Claude Sonnet 4.5** | Maximum quality, security-critical | $97 |
| **Claude Haiku 4.5** | Balanced quality/cost, recommended | $26 |
| **Gemini 2.0 Flash** | Maximum volume, minimum cost | $3 |
| **Gemini 1.5 Pro** | Quality alternative to Claude | $34 |

**Default Recommendation:** Start with **Claude Haiku 4.5** for best balance of quality, speed, and cost.

---

For questions or support, see [cloud-service/README.md](cloud-service/README.md)
