/**
 * dlpu_basic.h
 *
 * Simple DLPU coordinator for AXION POC
 * Uses time-division multiplexing to prevent concurrent DLPU access
 */

#ifndef DLPU_BASIC_H
#define DLPU_BASIC_H

#ifdef __cplusplus
extern "C" {
#endif

/* DLPU coordination context */
typedef struct {
    int camera_index;
    int slot_offset_ms;
    char camera_id[64];
    int total_waits;
    int total_wait_ms;
} DlpuContext;

/**
 * Initialize DLPU coordinator
 * @param camera_id Camera identifier
 * @param camera_index Camera index (0-4 for up to 5 cameras)
 * @return DlpuContext pointer on success, NULL on failure
 */
DlpuContext* Dlpu_Init(const char* camera_id, int camera_index);

/**
 * Wait for DLPU time slot
 * Blocks until it's this camera's turn to use DLPU
 * @param ctx DLPU context
 * @return 1 on success, 0 on timeout/failure
 */
int Dlpu_Wait_For_Slot(DlpuContext* ctx);

/**
 * Release DLPU slot
 * @param ctx DLPU context
 */
void Dlpu_Release_Slot(DlpuContext* ctx);

/**
 * Get average wait time
 * @param ctx DLPU context
 * @return Average wait time in milliseconds
 */
int Dlpu_Get_Avg_Wait(DlpuContext* ctx);

/**
 * Cleanup DLPU coordinator
 * @param ctx DLPU context
 */
void Dlpu_Cleanup(DlpuContext* ctx);

#ifdef __cplusplus
}
#endif

#endif /* DLPU_BASIC_H */
