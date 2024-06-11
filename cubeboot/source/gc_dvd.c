/**
 * Wii64/Cube64 - gc_dvd.c
 * Copyright (C) 2007, 2008, 2009, 2010 emu_kidid
 * 
 * DVD Reading support for GC/Wii
 *
 * Wii64 homepage: http://www.emulatemii.com
 * email address: emukidid@gmail.com
 *
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
**/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ogc/dvd.h>
#include <malloc.h>
#include <string.h>
#include <gccore.h>
#include <unistd.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/machine/processor.h>
#include "gc_dvd.h"

#include "print.h"

#define DVD_DI_MODE (1 << 2)
#define DVD_DI_DMA (1 << 1)
#define DVD_DI_START (1 << 0)

volatile unsigned long* dvd = (volatile unsigned long*)0xCC006000;
static GCN_ALIGNED(file_entry_t) entry;

#define DVD_FLIPPY_STATUS_BLOB 0xB4000000

int dvd_get_status(firmware_status_blob_t* dst)
{
    u32 status_size = sizeof(firmware_status_blob_t);

    dvd[0] = 0x2E;
    dvd[1] = 0;
    dvd[2] = DVD_FLIPPY_STATUS_BLOB;
    dvd[3] = 0;
    dvd[4] = 0;
    dvd[5] = (u32)dst & 0x1FFFFFFF;
    dvd[6] = status_size;
    dvd[7] = (DVD_DI_DMA|DVD_DI_START); // enable reading!
    DCInvalidateRange(dst, status_size);
    while (dvd[7] & 1);

    dst->status_text[63] = 0;
    dst->status_sub[63] = 0;
    if (dvd[0] & 0x4)
        return 1;
    return 0;
}

int dvd_cover_status() {
    return (dvd[1] & 1); // 0: cover closed, 1: cover opened
}

int dvd_transaction_wait() {
    return 0;
}

void dvd_custom_bypass()
{
    dvd[0] = 0x2E;
    dvd[1] = 0;
    dvd[2] = 0xDC000000;
    dvd[3] = 0;
    dvd[4] = 0;
    dvd[5] = 0;
    dvd[6] = 0;
    dvd[7] = 1; // IMM
    while (dvd[7] & 1)
        ;
}

#define DVD_FLIPPY_FILEAPI_BASE 0xB5000000

int dvd_custom_open(ipc_device_type_t device, char *path, uint8_t type, uint8_t flags)
{
    strncpy(entry.name, path, 256);
    entry.name[255] = 0;
    entry.type = type;
    entry.flags = flags;

    iprintf("SD Opening: %s\n", entry.name);

    DCFlushRange(&entry, sizeof(file_entry_t));

    dvd[0] = 0x2E;
    dvd[1] = 0;
    dvd[2] = DVD_FLIPPY_FILEAPI_BASE | IPC_FILE_OPEN | device;
    dvd[3] = 0;
    dvd[4] = sizeof(file_entry_t);
    dvd[5] = (u32)&entry & 0x1FFFFFFF;
    dvd[6] = sizeof(file_entry_t);
    dvd[7] = (DVD_DI_MODE|DVD_DI_DMA|DVD_DI_START);
    while (dvd[7] & 1) {
        if (ticks_to_millisecs(gettime()) % 1000 == 0)
            iprintf("Still reading: %u/%u\n", (u32)dvd[6], sizeof(file_entry_t));
    }

    if (dvd[0] & 0x4)
        return 1;
    return 0;
}

int dvd_threaded_custom_open(ipc_device_type_t device, char *path, uint8_t type, uint8_t flags)
{
    strncpy(entry.name, path, 256);
    entry.name[255] = 0;
    entry.type = type;
    entry.flags = flags;

    iprintf("SD Opening: %s\n", entry.name);

    DCFlushRange(&entry, sizeof(file_entry_t));

    dvd[0] = 0x2E;
    dvd[1] = 0;
    dvd[2] = DVD_FLIPPY_FILEAPI_BASE | IPC_FILE_OPEN | device;
    dvd[3] = 0;
    dvd[4] = sizeof(file_entry_t);
    dvd[5] = (u32)&entry & 0x1FFFFFFF;
    dvd[6] = sizeof(file_entry_t);
    dvd[7] = (DVD_DI_MODE|DVD_DI_DMA|DVD_DI_START);
    while (dvd[7] & 1) LWP_YieldThread();

    if (dvd[0] & 0x4)
        return 1;
    return 0;
}

int dvd_custom_unlink(ipc_device_type_t device, char *path)
{
    strcpy(entry.name, path);

    iprintf("DVD Unlinking: %s\n", entry.name);

    DCFlushRange(&entry, sizeof(file_entry_t));

    dvd[0] = 0x2E;
    dvd[1] = 0;
    dvd[2] = DVD_FLIPPY_FILEAPI_BASE | IPC_FILE_UNLINK | device;
    dvd[3] = 0;
    dvd[4] = sizeof(file_entry_t);
    dvd[5] = (u32)&entry & 0x1FFFFFFF;
    dvd[6] = sizeof(file_entry_t);
    dvd[7] = (DVD_DI_MODE | DVD_DI_DMA | DVD_DI_START);
    while (dvd[7] & 1)
    {
        if (ticks_to_millisecs(gettime()) % 1000 == 0)
            iprintf("Still reading: %u/%u\n", (u32)dvd[6], sizeof(file_entry_t));
    }

    if (dvd[0] & 0x4)
        return 1;
    return 0;
}

int dvd_custom_readdir(ipc_device_type_t device, file_entry_t *target)
{
    dvd[0] = 0x2E;
    dvd[1] = 0;
    dvd[2] = DVD_FLIPPY_FILEAPI_BASE | IPC_FILE_READDIR | device;
    dvd[3] = 0;
    dvd[4] = 0;
    dvd[5] = (u32)&entry & 0x1FFFFFFF;
    dvd[6] = sizeof(file_entry_t);
    dvd[7] = 3; // enable reading!
    DCInvalidateRange((u8 *)&entry, sizeof(file_entry_t));
    while (dvd[7] & 1)
        ;

    if (dvd[0] & 0x4) 
        return 1;

    memcpy(target, &entry, sizeof(file_entry_t));
    return 0;
}

int dvd_custom_status(ipc_device_type_t device, file_status_t *target)
{
    static GCN_ALIGNED(file_status_t) dst;

    dvd[0] = 0x2E;
    dvd[1] = 0;
    dvd[2] = DVD_FLIPPY_FILEAPI_BASE | IPC_READ_STATUS | device;
    dvd[3] = 0;
    dvd[4] = 0;
    dvd[5] = (u32)&dst & 0x1FFFFFFF;
    dvd[6] = sizeof(file_status_t);
    dvd[7] = 3; // enable reading!
    DCInvalidateRange((u8*)&dst, sizeof(file_status_t));

    while (dvd[7] & 1)
        ;

    memcpy(target, &dst, sizeof(file_status_t));

    if (dvd[0] & 0x4)
        return 1;

    return 0;
}

void dvd_custom_close(ipc_device_type_t device)
{
    dvd[0] = 0x2E;
    dvd[1] = 0;
    dvd[2] = DVD_FLIPPY_FILEAPI_BASE | IPC_FILE_CLOSE | device;
    dvd[3] = 0;
    dvd[4] = 0;
    dvd[5] = 0;
    dvd[6] = 0;
    dvd[7] = 1; // IMM
    while (dvd[7] & 1)
        ;
}

void dvd_custom_set_active(ipc_device_type_t device)
{
    dvd[0] = 0x2E;
    dvd[1] = 0;
    dvd[2] = DVD_FLIPPY_FILEAPI_BASE | IPC_SET_ACTIVE | device;
    dvd[3] = 0;
    dvd[4] = 0;
    dvd[5] = 0;
    dvd[6] = 0;
    dvd[7] = 1; // IMM
    while (dvd[7] & 1)
        ;
}

int dvd_read_id()
{
    dvd[0] = 0x2E;
    dvd[1] = 0;
    dvd[2] = 0xA8000040;
    dvd[3] = 0;
    dvd[4] = 0x20;
    dvd[5] = 0;
    dvd[6] = 0x20;
    dvd[7] = 3; // enable reading!
    while (dvd[7] & 1)
        ;
    if (dvd[0] & 0x4)
        return 1;
    return 0;
}

unsigned int dvd_get_error(void)
{
    dvd[2] = 0xE0000000;
    dvd[8] = 0;
    dvd[7] = 1; // IMM
    while (dvd[7] & 1);
    return dvd[8];
}

void dvd_motor_off()
{
    dvd[0] = 0x2E;
    dvd[1] = 0;
    dvd[2] = 0xe3000000;
    dvd[3] = 0;
    dvd[4] = 0;
    dvd[5] = 0;
    dvd[6] = 0;
    dvd[7] = 1; // IMM
    while (dvd[7] & 1);
}

void dvd_bootloader_boot()
{
    dvd[0] = 0x2E;
    dvd[1] = 0;
    dvd[2] = 0x12000000;
    dvd[3] = 0xabadbeef;
    dvd[4] = 0xcafe6969;
    dvd[5] = 0;
    dvd[6] = 0;
    dvd[7] = 1; // IMM
    while (dvd[7] & 1)
        ;
}

void dvd_bootloader_update()
{
    dvd[0] = 0x2E;
    dvd[1] = 0;
    dvd[2] = 0x12000000;
    dvd[3] = 0xabadbeef;
    dvd[4] = 0xdabfed69;
    dvd[5] = 0;
    dvd[6] = 0;
    dvd[7] = 1; // IMM
    while (dvd[7] & 1)
        ;
}

void dvd_bootloader_noupdate()
{
    dvd[0] = 0x2E;
    dvd[1] = 0;
    dvd[2] = 0x12000000;
    dvd[3] = 0xabadbeef;
    dvd[4] = 0xdecaf420;
    dvd[5] = 0;
    dvd[6] = 0;
    dvd[7] = 1; // IMM
    while (dvd[7] & 1)
        ;
}

static GCN_ALIGNED(dvd_info) info = {0};
dvd_info *dvd_inquiry()
{
    dvd[0] = 0x2E;
    dvd[1] = 0;
    dvd[2] = 0x12000000;
    dvd[3] = 0;
    dvd[4] = 0;
    dvd[5] = ((u32)&info) & 0x1FFFFFFF;
    dvd[6] = sizeof(dvd_info);
    dvd[7] = 3; // enable reading!
    DCInvalidateRange((u8*)&info, sizeof(dvd_info));
    while (dvd[7] & 1);

    return &info;
}

void dvd_break() {
    dvd[0] = 1;
    // while((dvd[0] & 1) == 1);
}

/* 
dvd_read(void* dst, unsigned int len, uint64_t offset)
    Read Raw, needs to be on sector boundaries
    Has 8,796,093,020,160 byte limit (8 TeraBytes)
    Synchronous function.
        return -1 if offset is out of range
*/
int dvd_read(void* dst, unsigned int len, uint64_t offset)
{
    if(offset>>2 > 0xFFFFFFFF)
        return -1;
        
    if ((((int)dst) & 0xC0000000) == 0x80000000) // cached?
    {
        dvd[0] = 0x2E;
    }
    dvd[1] = 0;
    dvd[2] = DVD_READ_NORMAL;
    dvd[3] = offset >> 2;
    dvd[4] = len;
    dvd[5] = (u32)dst & 0x1FFFFFFF;
    dvd[6] = len;
    dvd[7] = 3; // enable reading!
    DCInvalidateRange(dst, len);
    while (dvd[7] & 1);

    if (dvd[0] & 0x4)
        return 1;
    return 0;
}

int dvd_threaded_read(void* dst, unsigned int len, uint64_t offset)
{
    if(offset>>2 > 0xFFFFFFFF)
        return -1;
        
    if ((((int)dst) & 0xC0000000) == 0x80000000) // cached?
    {
        dvd[0] = 0x2E;
    }
    dvd[1] = 0;
    dvd[2] = DVD_READ_NORMAL;
    dvd[3] = offset >> 2;
    dvd[4] = len;
    dvd[5] = (u32)dst & 0x1FFFFFFF;
    dvd[6] = len;
    dvd[7] = 3; // enable reading!
    DCInvalidateRange(dst, len);
    while (dvd[7] & 1) LWP_YieldThread();

    if (dvd[0] & 0x4)
        return 1;
    return 0;
}
