/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Ryan */
/* GLOAM: witch house and melodic dubstep for the M-VAVE FM-1, on fm1-base. The app's half of the contract. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "GLOAM"
#define APP_SPLASH_RGB RGB(150, 120, 255)
#define APP_BLOCK 128u                   /* frames per render: the sequencer runs in it, sample-exact */
#define APP_CANVAS_PX (240u * 200u)
#define APP_TOUCH
#define APP_BTDEV
#define APP_BLE_AT_BOOT (!gloam_ble_off())
int gloam_ble_off(void);

/* the flash: the vocal bank (the resource region, written by the installer) at the data area's start; the four
 * project slots of 10 sectors (38,400 B) after it and in the extra window, clear of the store at 0xDC000 */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_NOBJ };
#define APP_STORE_MAP {PLAT_GLOB, 0xDC000u, 0xDE000u}
#define APP_RES_BASE 0x97000u
#define APP_RES_SECTORS 48u              /* 0x97000 .. 0xC7000: 196 KiB, about 12 s of voice at 16 kHz */
#define SLOT_SECTS 10u
#define APP_SLOT_BASES {0xC7000u, 0xD1000u, 0xE5000u, 0xEF000u}

/* backup (fm1-base needs it for the resource region's transfer): the settings */
#define APP_BACKUP_OBJS {OBJ_SETTINGS}
#define APP_BACKUP_MAGIC 0x314D4C47u
#define APP_BACKUP_VERSION 1u
#define APP_BACKUP_WHAT "replaces the settings"
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
