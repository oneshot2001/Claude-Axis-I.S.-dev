/**
 * vdo_handler.h
 *
 * VDO (Video Data Object) stream handler for AXION POC
 * Provides zero-copy frame capture from Axis camera sensor
 */

#ifndef VDO_HANDLER_H
#define VDO_HANDLER_H

#include <glib.h>
#include "vdo-types.h"
#include "vdo-frame.h"
#include "vdo-stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/* VDO context structure */
typedef struct {
    VdoStream* stream;
    unsigned int width;
    unsigned int height;
    unsigned int fps;
    unsigned int frames_captured;
    unsigned int frames_dropped;
} VdoContext;

/**
 * Initialize VDO stream
 * @param width Target frame width
 * @param height Target frame height
 * @param fps Target frames per second
 * @return VdoContext pointer on success, NULL on failure
 */
VdoContext* Vdo_Init(unsigned int width, unsigned int height, unsigned int fps);

/**
 * Get next frame from VDO stream
 * @param ctx VDO context
 * @return VdoBuffer pointer on success, NULL on failure
 *
 * IMPORTANT: Caller must call Vdo_Release_Frame() when done with buffer
 */
VdoBuffer* Vdo_Get_Frame(VdoContext* ctx);

/**
 * Release frame buffer back to VDO
 * @param ctx VDO context
 * @param buffer VdoBuffer to release
 */
void Vdo_Release_Frame(VdoContext* ctx, VdoBuffer* buffer);

/**
 * Cleanup VDO resources
 * @param ctx VDO context
 */
void Vdo_Cleanup(VdoContext* ctx);

#ifdef __cplusplus
}
#endif

#endif /* VDO_HANDLER_H */
