# ARTPEC-9 and AXIS OS 12: Modern ACAP Development with Rust

**Source:** Architecture guidance for ARTPEC-9 SoC with LLM integration  
**Date Added:** November 23, 2025  
**Critical:** AXIS OS 12 removes Docker container support

---

## Executive Summary

If architecting an ACAP for ARTPEC-9 to integrate LLMs today, **Rust** is the superior choice.

### Key Changes in AXIS OS 12

**⚠️ BREAKING CHANGE: No Docker Containers**
- AXIS OS 12 deprecates Docker support
- Applications must be **native static binaries**
- No Python/Node.js runtimes in containers
- Single `.eap` package with embedded binary

---

## The Winner: Rust

Rust is the only modern language that creates **native, zero-dependency binaries** (required by AXIS OS 12) while offering **async I/O** capabilities for slow LLM APIs without freezing the camera's video pipeline.

### 1. The "No-Container" Reality of AXIS OS 12

The deprecation of Docker in AXIS OS 12 is the single biggest constraint. You can no longer ship a Python runtime or a Node.js environment in a container.

**Rust's Advantage:**
- Rust compiles to a **single, static binary**
- No runtime installation needed on camera
- Compile for `aarch64-unknown-linux-gnu`
- Package into `.eap` file
- Runs natively on ARTPEC-9 with full hardware access

**Tooling:**
- **`acap-rs`** project abstracts complex Makefiles
- **`cargo-acap-sdk`** enables standard `cargo build` workflows
- Generates Axis-compatible packages automatically

### 2. Handling LLM Latency (Async/Await)

LLM calls are network-bound and slow (500ms to several seconds latency).

**The Problem in C/C++:**
- Requires complex callback hell
- Manual thread management (`libcurl` or `pthreads`)
- Blocking main thread drops frames

**Rust's Solution:**
- `async` / `await` syntax (via `tokio` runtime)
- Fire off request to Claude/Gemini
- "await" the response
- Runtime automatically yields to video event loop
- Camera doesn't drop frames or miss motion events

**Library Support:**
- **`reqwest` crate:** High-level HTTP client
  - Handles HTTPS
  - JSON serialization
  - Authentication headers (API keys)
  - Secure and ergonomic (vs verbose C code)

### 3. Memory Safety Without Garbage Collection

ARTPEC-9 devices: ~2 GB RAM shared with OS, video encoder, analytics.

**Rust's Advantage:**
- Memory safety (prevents segfaults common in C/C++)
- **No Garbage Collector (GC)**
- Ownership model ensures deterministic cleanup
- Consistent performance

**The GC Risk:**
- Go/Java use GC
- GC periodically pauses execution
- On 30fps video, GC pause causes jitter or frame drops
- Rust avoids this completely

---

## Runner Up: Go (Golang)

Viable alternative if team is proficient and wants rapid development over raw optimization.

**Why it works:**
- Compiles to single static binary
- Compatible with container-less AXIS OS 12
- **`goxis` library** provides Axis API bindings (events, video, parameters)

**The Drawbacks:**
1. **Binary Size:** Go binaries larger than Rust/C++ (heavy runtime bundled)
2. **Limited Flash Storage:** Camera flash space is constrained
3. **GC Overhead:** Non-zero risk for real-time video applications

---

## Niche Choice: WebAssembly (Wasm)

Only recommend if you **must** use TypeScript/JavaScript syntax for business logic.

**The "TypeScript" Workaround:**
- Write logic in TypeScript
- Compile to Wasm via AssemblyScript
- Developer-friendly experience

**The Hurdle:**
- Docker is gone, cannot deploy "Wasm container"
- Need "Host" application (C or Rust) that bundles WasmEdge runtime
- Host compiles and includes runtime inside native application

**Why it's harder:**
- Reintroduces "glue code" complexity
- Not just writing Wasm; writing C app that *loads* Wasm
- Only worth it for dynamic over-the-air logic updates
- Too complex for single fixed-function application

---

## Recommended Stack for ARTPEC-9 + LLM Integration

### Core Technology

**Language:** Rust (2021 edition)  
**SDK:** ACAP Native SDK (via `acap-rs` bindings)  
**HTTP Client:** `reqwest` (with `rustls` for SSL)  
**Async Runtime:** `tokio`  
**JSON Parsing:** `serde_json` (for LLM responses)

### Why This Stack

1. **Native:** Fits AXIS OS 12 "bare metal" requirement
2. **Safe:** Prevents crashes common in C/C++
3. **Modern:** Makes Claude/Gemini API calls trivial with `reqwest`
4. **Async:** Handles LLM latency without blocking video pipeline
5. **Zero-overhead:** No GC pauses, deterministic performance

---

## Comparison Matrix

| Feature | C/C++ | Rust | Go | Wasm |
|---------|-------|------|-----|------|
| **Native Binary** | ✅ | ✅ | ✅ | ⚠️ Needs host |
| **Memory Safety** | ❌ Manual | ✅ Ownership | ⚠️ GC | ✅ Sandboxed |
| **Async/Await** | ❌ Manual threads | ✅ Built-in | ✅ Built-in | ⚠️ Limited |
| **Binary Size** | Small | Small | Large | Small |
| **Runtime Overhead** | None | None | GC pauses | Host overhead |
| **Learning Curve** | Medium | Steep | Easy | Medium |
| **LLM API Libraries** | Manual | `reqwest` | `net/http` | Limited |
| **ACAP Tooling** | Native SDK | `acap-rs` | `goxis` | Custom |
| **AXIS OS 12 Ready** | ✅ | ✅ | ✅ | ⚠️ Complex |

---

## Migration Path from C to Rust

### Current C ACAP Pattern
```c
// C code with manual memory management
VdoBuffer* buffer = vdo_stream_get_buffer(stream);
process_frame(buffer);
vdo_buffer_unref(buffer);  // Easy to forget!
```

### Rust ACAP Pattern
```rust
// Rust with automatic cleanup
let buffer = stream.get_buffer()?;
process_frame(&buffer);
// buffer automatically dropped, no memory leak possible
```

### Async LLM Call (C vs Rust)

**C (Complex):**
```c
// Manual thread management
pthread_t thread;
pthread_create(&thread, NULL, call_llm, data);
// ... complex synchronization ...
pthread_join(thread, &result);
```

**Rust (Simple):**
```rust
// Built-in async/await
let response = reqwest::Client::new()
    .post("https://api.anthropic.com/v1/messages")
    .header("x-api-key", api_key)
    .json(&request)
    .send()
    .await?
    .json::<ClaudeResponse>()
    .await?;
```

---

## ACAP-RS Example Structure

```rust
use acap_rs::prelude::*;
use tokio::runtime::Runtime;
use reqwest::Client;

#[tokio::main]
async fn main() -> Result<()> {
    // Initialize ACAP SDK
    let acap = Acap::new("axis-is")?;
    
    // VDO stream setup
    let stream = acap.vdo()
        .width(416)
        .height(416)
        .fps(10)
        .build()?;
    
    // Larod model
    let model = acap.larod()
        .load_model("models/yolov5n.tflite")?;
    
    // HTTP client for LLM
    let client = Client::new();
    
    // Main loop
    loop {
        let frame = stream.get_frame().await?;
        let result = model.infer(&frame).await?;
        
        // Non-blocking LLM call
        if result.significant() {
            tokio::spawn(async move {
                let analysis = call_claude(&client, &result).await;
                handle_response(analysis).await;
            });
        }
    }
}
```

---

## Critical Considerations for Axis I.S.

### 1. Our POC Uses Docker (AXIS OS 11 Style)
- **Impact:** Won't work on AXIS OS 12
- **Solution:** Need to compile as native binary

### 2. We Wrote C Code
- **Impact:** Complex async handling for Claude API
- **Solution:** Consider Rust rewrite or C++ with coroutines

### 3. Target OS Version Matters
- **AXIS OS 11:** Docker-based ACAPs work
- **AXIS OS 12:** Only native binaries work
- **Decision:** Which OS version are we targeting?

### 4. Claude Integration
- **Current Plan:** Python cloud service
- **AXIS OS 12:** Could put Claude calls directly in camera
- **Trade-off:** Camera compute vs network bandwidth

---

## Recommendations for Axis I.S.

### Immediate (POC)
- Continue with C POC for AXIS OS 11 cameras
- Validates core architecture (VDO + Larod + MQTT)
- Tests on available hardware

### Short-term (Production)
- **If targeting AXIS OS 11:** Current C approach is fine
- **If targeting AXIS OS 12:** Migrate to Rust

### Long-term (Future-proof)
- Build Rust version in parallel
- Use `acap-rs` for cleaner code
- Direct Claude API calls from camera (optional)
- Eliminates cloud MQTT relay layer

---

## Resources

**Rust ACAP Development:**
- https://github.com/AxisCommunications/acap-rs
- https://crates.io/crates/acap-sdk
- ACAP Native SDK documentation (Rust bindings)

**Rust Async:**
- https://tokio.rs (async runtime)
- https://docs.rs/reqwest (HTTP client)

**AXIS OS 12 Documentation:**
- Check Axis developer portal for migration guides
- ACAP SDK 12.x release notes

---

**Document Added:** November 23, 2025  
**Relevance:** Critical for AXIS OS 12 compatibility  
**Action Required:** Determine target OS version for Axis I.S. deployment
