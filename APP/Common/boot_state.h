#ifndef BL_BOOT_STATE_H
#define BL_BOOT_STATE_H

#include <stdint.h>

#define BL_BOOT_STATE_MAGIC      0x424C5354UL
#define BL_BOOT_STATE_VERSION    0x00000002UL

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t active_slot;
    uint32_t confirmed;
    uint32_t checksum;
} bl_boot_state_t;

static inline uint32_t BL_BootState_CalcChecksum(const bl_boot_state_t *s)
{
    return s->magic ^ s->version ^ s->active_slot ^ s->confirmed ^ 0x5A5AA5A5UL;
}

#endif
