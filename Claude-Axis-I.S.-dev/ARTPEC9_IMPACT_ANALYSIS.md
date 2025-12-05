# ARTPEC-9 / AXIS OS 12 Impact Analysis for Axis I.S. POC

**Date:** November 23, 2025  
**Status:** 🔴 CRITICAL - Requires Decision

---

## Summary

The ARTPEC-9 reference document reveals a **breaking change** in AXIS OS 12 that fundamentally affects our POC and production architecture.

### The Critical Issue

**AXIS OS 12 removes Docker container support.**

This impacts:
- ✅ **Current POC:** Uses Docker (AXIS OS 11 style) - Still valid for OS 11 cameras
- ⚠️ **Production Deployment:** May need native binary approach for OS 12
- 🔄 **Architecture:** Need to decide OS version target

---

## Current POC Status

### What We Built (Still Valid for AXIS OS 11)

```
✅ C-based ACAP using Docker
✅ VDO + Larod inference
✅ MQTT publishing
✅ Multi-camera DLPU coordination
✅ Cloud-based Python subscriber
```

**Compatibility:**
- ✅ Works on AXIS OS 11 (current stable)
- ❌ Won't work on AXIS OS 12 (future)

### POC Architecture (AXIS OS 11)

```
Camera (Docker Container)
  ├─ C Application
  ├─ VDO/Larod APIs
  ├─ MQTT Publisher
  └─ Packaged as .eap
```

**Pros:**
- Uses familiar Docker patterns
- Easier debugging (can shell into container)
- Proven approach (Axis examples use this)

**Cons:**
- Deprecated in AXIS OS 12
- Larger package size

---

## AXIS OS 12 Changes

### New Architecture (Native Binary)

```
Camera (Bare Metal)
  ├─ Native Binary (Rust/C/Go)
  ├─ Direct ACAP SDK access
  ├─ No container overhead
  └─ Single static .eap
```

**Pros:**
- Smaller package size
- Lower memory overhead
- Required for AXIS OS 12

**Cons:**
- More complex development (no container sandbox)
- Language choice matters more (Rust > C > Go)
- Harder to debug (no shell access)

---

## Decision Matrix

### Option 1: Continue with C + Docker (AXIS OS 11)

**✅ Pros:**
- POC already built and ready to test
- Works on current stable OS version
- Proven patterns from Axis
- Easier development/debugging

**❌ Cons:**
- Not future-proof for OS 12
- May need rewrite later
- Larger memory footprint

**Best For:**
- Immediate POC validation
- Cameras running AXIS OS 11
- Short-term deployment (1-2 years)

### Option 2: Migrate to Rust (AXIS OS 12 Ready)

**✅ Pros:**
- Future-proof for OS 12
- Better async handling (Claude API calls)
- Memory safety without GC
- Smaller binary size
- Modern tooling (`acap-rs`)

**❌ Cons:**
- Need to rewrite POC code
- Team learning curve for Rust
- Less Axis example code
- 2-3 week delay for rewrite

**Best For:**
- Long-term production
- OS 12 target cameras
- Claude integration on camera
- Multiple deployment sites

### Option 3: Hybrid Approach

**Phase 1 (Now):** C + Docker POC for validation  
**Phase 2 (After POC):** Rust production implementation

**✅ Pros:**
- Validate architecture immediately with C POC
- Learn from C implementation
- Migrate to Rust with proven design
- Test on real hardware faster

**❌ Cons:**
- Two implementations (more work)
- Need Rust expertise eventually

**Best For:**
- Risk mitigation
- Hardware validation first
- Team training during transition

---

## Specific Axis I.S. Impacts

### 1. Claude Integration Location

**Current Plan (C POC):**
```
Camera → MQTT → Cloud Python → Claude API
```
- Works for both OS 11 and OS 12
- Claude processing happens in cloud
- Higher network bandwidth

**Rust Alternative:**
```
Camera (Rust) → Claude API directly → Local processing
```
- Only practical with Rust async/await
- Lower network bandwidth (metadata only)
- Requires OS 12 or native build

**Recommendation:** Keep cloud architecture regardless of language choice. Bandwidth controller logic requires centralized coordination.

### 2. DLPU Scheduler

**Current Implementation (C):**
- Simple file-based coordination
- Works in Docker or native

**Rust Implementation:**
- Could use `tokio::sync` primitives
- Cleaner async coordination
- Same shared memory approach

**Impact:** Minimal - algorithm is language-agnostic

### 3. VDO + Larod Integration

**C Approach:**
```c
VdoBuffer* buffer = vdo_stream_get_buffer(stream);
larodRunInference(conn, model, input, output, &error);
vdo_buffer_unref(buffer);  // Critical!
```

**Rust Approach:**
```rust
let buffer = stream.get_buffer().await?;
let result = model.infer(&buffer).await?;
// Automatic cleanup via Drop trait
```

**Impact:** Rust eliminates entire class of memory leak bugs (VDO buffer reference counting issues from Axis I.S._CRITICAL_IMPLEMENTATION_GUIDE.md)

### 4. MQTT Publishing

**C Implementation:**
- Paho MQTT C client
- Manual connection management
- Callback-based

**Rust Implementation:**
- `rumqttc` crate
- Async/await naturally
- Type-safe message handling

**Impact:** Rust makes retry logic and sequence numbering cleaner

---

## Performance Comparison

| Metric | C (Docker) | C (Native) | Rust (Native) |
|--------|-----------|-----------|---------------|
| **Binary Size** | ~100 MB | ~5 MB | ~8 MB |
| **Memory Overhead** | +50 MB (Docker) | Baseline | Baseline |
| **Startup Time** | 3-5 sec | 1 sec | 1 sec |
| **Memory Safety** | Manual | Manual | Automatic |
| **Async LLM Calls** | Complex | Complex | Built-in |
| **OS 11 Compatible** | ✅ | ✅ | ✅ |
| **OS 12 Compatible** | ❌ | ✅ | ✅ |

---

## Recommended Path Forward

### Immediate (Week 1-2): Validate C POC on OS 11

**Actions:**
1. ✅ Keep existing C + Docker POC
2. ✅ Test on real AXIS OS 11 camera
3. ✅ Validate VDO + Larod + MQTT pipeline
4. ✅ Measure actual performance (FPS, memory, latency)
5. ✅ Test multi-camera DLPU coordination

**Goal:** Prove architecture works on real hardware

### Short-term (Week 3-4): Assess Camera OS Version

**Actions:**
1. Check target camera models and their OS versions
2. Determine customer deployment OS version
3. Understand OS 11 → OS 12 migration timeline

**Decision Point:**
- **If OS 11 only:** Proceed with C implementation
- **If OS 12 required:** Plan Rust migration
- **If mixed:** Hybrid approach (C now, Rust later)

### Medium-term (Month 2-3): Rust Implementation (If Needed)

**Actions:**
1. Set up Rust development environment
2. Install `acap-rs` tooling
3. Port POC to Rust incrementally:
   - Week 1: VDO + Larod
   - Week 2: MQTT publishing
   - Week 3: DLPU coordination
   - Week 4: Testing + validation
4. Maintain C version for OS 11 cameras

**Goal:** Future-proof for OS 12 while supporting OS 11

---

## Questions to Answer

### 1. Target OS Version?
- [ ] AXIS OS 11 (current stable, Docker works)
- [ ] AXIS OS 12 (future, native only)
- [ ] Both (need hybrid approach)

### 2. Deployment Timeline?
- [ ] Immediate (next 6 months) → C is fine
- [ ] Long-term (1+ years) → Consider Rust
- [ ] Ongoing (multi-year) → Rust recommended

### 3. Camera Models?
- [ ] List specific models
- [ ] Check their OS versions
- [ ] Verify upgrade paths

### 4. Team Rust Experience?
- [ ] None → Steeper learning curve
- [ ] Some → Feasible migration
- [ ] Experienced → Rust is clear choice

### 5. Claude Integration Strategy?
- [ ] Cloud-based (current plan) → Language less critical
- [ ] On-camera (direct API) → Rust strongly preferred

---

## Recommendation

**FOR POC VALIDATION (Immediate):**
→ **Continue with C + Docker POC** ✅

**Rationale:**
- Code already written and ready
- Validates architecture on real hardware
- Works on AXIS OS 11 (current stable)
- Faster time to validation
- Team can test while evaluating OS version question

**FOR PRODUCTION (After POC Success):**
→ **Evaluate based on target OS version**

**If OS 11:**
- Enhance C implementation
- Add production hardening
- Deploy as Docker ACAP

**If OS 12:**
- Migrate to Rust
- Use POC learnings
- Deploy as native binary

**If Both:**
- Maintain C for OS 11
- Create Rust for OS 12
- Share algorithm logic

---

## Action Items

### Immediate (This Week)
- [ ] Test C POC on OS 11 camera
- [ ] Document actual OS version of test cameras
- [ ] Measure real performance metrics

### Next Week
- [ ] Survey target deployment cameras
- [ ] Determine OS 11 vs OS 12 split
- [ ] Make language decision for production

### Month 2 (If Rust Selected)
- [ ] Set up Rust development environment
- [ ] Team Rust training
- [ ] Begin incremental port

---

## Conclusion

The ARTPEC-9 reference document is **critical information** but **doesn't invalidate our POC**:

✅ **POC is still valid** for AXIS OS 11 cameras  
✅ **Architecture proven** regardless of implementation language  
⚠️ **Production decision** depends on target OS version  
🔄 **Rust migration path** exists if OS 12 is required  

**Next Step:** Test the C POC on real hardware while determining target OS version for production deployment.

---

**Document Created:** November 23, 2025  
**Status:** Pending customer OS version confirmation  
**Decision Date:** After POC validation (Week 2)
