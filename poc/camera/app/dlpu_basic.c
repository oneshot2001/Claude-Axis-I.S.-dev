/**
 * dlpu_basic.c
 *
 * Simple time-division based DLPU coordination for AXION POC
 * Prevents multiple cameras from accessing DLPU simultaneously
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <syslog.h>
#include "dlpu_basic.h"

#define LOG(fmt, args...) { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG_ERR(fmt, args...) { syslog(LOG_ERR, fmt, ## args); fprintf(stderr, fmt, ## args); }

#define SLOT_DURATION_MS 200  // Each camera gets 200ms slot
#define CYCLE_DURATION_MS 1000  // Full cycle is 1 second (supports 5 cameras)
#define MAX_CAMERAS 5

DlpuContext* Dlpu_Init(const char* camera_id, int camera_index) {
    if (!camera_id || camera_index < 0 || camera_index >= MAX_CAMERAS) {
        LOG_ERR("Invalid DLPU init parameters: camera_id=%s index=%d\n",
                camera_id ? camera_id : "NULL", camera_index);
        return NULL;
    }

    DlpuContext* ctx = (DlpuContext*)calloc(1, sizeof(DlpuContext));
    if (!ctx) {
        LOG_ERR("Failed to allocate DLPU context\n");
        return NULL;
    }

    strncpy(ctx->camera_id, camera_id, sizeof(ctx->camera_id) - 1);
    ctx->camera_index = camera_index;
    ctx->slot_offset_ms = camera_index * SLOT_DURATION_MS;

    LOG("DLPU initialized: Camera=%s Index=%d SlotOffset=%dms\n",
        camera_id, camera_index, ctx->slot_offset_ms);

    return ctx;
}

int Dlpu_Wait_For_Slot(DlpuContext* ctx) {
    if (!ctx) {
        LOG_ERR("Invalid DLPU context\n");
        return 0;
    }

    struct timeval start, now;
    gettimeofday(&start, NULL);

    // Calculate current position in cycle
    long current_ms = (start.tv_sec * 1000) + (start.tv_usec / 1000);
    long slot_phase = current_ms % CYCLE_DURATION_MS;  // Position in 1-second cycle
    long target_slot = ctx->slot_offset_ms;

    long wait_ms = 0;

    if (slot_phase < target_slot) {
        // Our slot is coming up in this cycle
        wait_ms = target_slot - slot_phase;
    } else if (slot_phase >= target_slot + SLOT_DURATION_MS) {
        // We missed our slot, wait for next cycle
        wait_ms = (CYCLE_DURATION_MS - slot_phase) + target_slot;
    }
    // else: we're in our slot now (slot_phase is between target_slot and target_slot + SLOT_DURATION_MS)

    if (wait_ms > 0) {
        usleep(wait_ms * 1000);
    }

    gettimeofday(&now, NULL);
    int actual_wait_ms = (int)(((now.tv_sec - start.tv_sec) * 1000) +
                               ((now.tv_usec - start.tv_usec) / 1000));

    ctx->total_waits++;
    ctx->total_wait_ms += actual_wait_ms;

    return 1;
}

void Dlpu_Release_Slot(DlpuContext* ctx) {
    // No-op for time-division approach
    // In a mutex-based approach, this would release a lock
    (void)ctx;  // Suppress unused parameter warning
}

int Dlpu_Get_Avg_Wait(DlpuContext* ctx) {
    if (!ctx || ctx->total_waits == 0) return 0;
    return ctx->total_wait_ms / ctx->total_waits;
}

void Dlpu_Cleanup(DlpuContext* ctx) {
    if (!ctx) return;

    LOG("DLPU cleanup: Camera=%s Waits=%d AvgWait=%dms\n",
        ctx->camera_id, ctx->total_waits, Dlpu_Get_Avg_Wait(ctx));

    free(ctx);
}
