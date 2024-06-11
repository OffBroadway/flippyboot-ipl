/**
 * Wii64/Cube64 - gc_dvd.h
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
#ifndef GC_DVD_H
#define GC_DVD_H

#include <stdint.h>
#include "ipc.h"

#define DVD_READ_NORMAL 0xA8000000

typedef struct __attribute__((packed, aligned(32)))
{
	uint32_t magic;
	uint8_t show_video;
	uint8_t show_progress_bar;
	uint16_t current_progress;
	char status_text[64];
	char status_sub[64];
	uint8_t padding[120];
} firmware_status_blob_t;

typedef struct __attribute__((__packed__)) {
    uint8_t major;
    uint8_t minor;
    uint16_t build;
} flippy_version_parts_t;

typedef struct __attribute__((__packed__)) {
	u16 rev_level;
	u16 dev_code;
	u32 rel_date;
	u32 fd_flags;
	flippy_version_parts_t fw_ver;
	u8  pad1[16];
} dvd_info;

int dvd_get_status(firmware_status_blob_t*dst);
void dvd_bootloader_boot();
void dvd_bootloader_update();
void dvd_bootloader_noupdate();
int dvd_cover_status();

int dvd_custom_open(ipc_device_type_t device, char *path, uint8_t type, uint8_t flags);
int dvd_threaded_custom_open(ipc_device_type_t device, char *path, uint8_t type, uint8_t flags);
int dvd_custom_unlink(ipc_device_type_t device, char *path);
int dvd_custom_readdir(ipc_device_type_t device, file_entry_t *target);
int dvd_custom_status(ipc_device_type_t device, file_status_t *target);
void dvd_custom_set_active(ipc_device_type_t device);
void dvd_custom_bypass();
void dvd_custom_close(ipc_device_type_t device);

void dvd_break();
void dvd_motor_off();
dvd_info *dvd_inquiry();
unsigned int dvd_get_error(void);
int dvd_read(void* dst, unsigned int len, uint64_t offset);
int dvd_threaded_read(void* dst, unsigned int len, uint64_t offset);
int dvd_read_id();
int dvd_transaction_wait();

struct pvd_s
{
	char id[8];
	char system_id[32];
	char volume_id[32];
	char zero[8];
	unsigned long total_sector_le, total_sect_be;
	char zero2[32];
	unsigned long volume_set_size, volume_seq_nr;
	unsigned short sector_size_le, sector_size_be;
	unsigned long path_table_len_le, path_table_len_be;
	unsigned long path_table_le, path_table_2nd_le;
	unsigned long path_table_be, path_table_2nd_be;
	unsigned char root_direntry[34];
	char volume_set_id[128], publisher_id[128], data_preparer_id[128], application_id[128];
	char copyright_file_id[37], abstract_file_id[37], bibliographical_file_id[37];
	// some additional dates, but we don't care for them :)
}  __attribute__((packed));

#endif

