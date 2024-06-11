#include "uff.h"

#ifdef USE_FAT_LIBFAT
#include <stdio.h>
#include <string.h>

#ifndef __gamecube__
#define __gamecube__ // fix ide issue
#endif
#include <fat.h>

#include "sd.h"

static char *__current_file_path = NULL;

static char *abs_path(char *path) {
    static char path_buf[255];
    strcpy(path_buf, "sd:/");
    strcat(path_buf, path);
    return path_buf;
}

FRESULT uf_mount(FATFS* fs) {
    iprintf("Device ptr = %08x\n", (u32)get_current_device());
    if (!fatMountSimple("sd", get_current_device())) {
        return FR_DISK_ERR;
    }

    return FR_OK;
}

FRESULT uf_open(const char* path) {
    __current_file_path = abs_path((char*)path);
    FILE *file = fopen(__current_file_path, "r");
    if (file == NULL) {
        __current_file_path = NULL;
        return FR_NOT_OPENED;   
    }

    fclose(file);
    return FR_OK;
}

FRESULT uf_read(void* buff, UINT btr, UINT* br) {
    FILE *file = fopen(__current_file_path, "r");
    size_t n = fread(buff, 1, btr, file);
    *br = n;

    fclose(file);
    return FR_OK;
}

FRESULT uf_write(const void* buff, UINT btw, UINT* bw) {
    return FR_DISK_ERR;
}

FRESULT uf_lseek(DWORD ofs) {
    return FR_DISK_ERR;
}

DWORD uf_size() {
    FILE *file = fopen(__current_file_path, "r");
    fseek(file, 0, SEEK_END);
    long size = ftell(file);

    fclose(file);
    return size;
}

FRESULT uf_unmount() {
    __current_file_path = NULL;
    return FR_OK;
}

#endif

#ifdef USE_FAT_FATFS
#include "fatfs/ff.h"
#include "ffshim.h"

#include "sd.h"

ATTRIBUTE_ALIGN(32) static FIL __current_file = {};

FRESULT uf_mount(FATFS* fs) {
    iface = get_current_device();
	return f_mount(fs, "", 1);
}

FRESULT uf_open(const char* path) {
    if (__current_file.flag == 0x00) f_close(&__current_file);
    return f_open(&__current_file, path, FA_READ);
}

FRESULT uf_read(void* buff, UINT btr, UINT* br) {
    return f_read(&__current_file, buff, btr, br);
}

FRESULT uf_write(const void* buff, UINT btw, UINT* bw) {
    return FR_DISK_ERR;
}

FRESULT uf_lseek(DWORD ofs) {
    return FR_DISK_ERR;
}

DWORD uf_size() {
    return f_size(&__current_file);
}

FRESULT uf_unmount() {
    if (__current_file.flag == 0x00) f_close(&__current_file);
    return f_unmount("");
}

#endif

#ifdef USE_FAT_LIBFLIPPY

#include <string.h>

#include "gc_dvd.h"
#include "print.h"

static u32 __file_offset;
static u32 __file_size;
__attribute__((aligned(32))) static u8 __file_buffer[4096];
__attribute__((aligned(32))) static file_status_t __current_file;

FRESULT uf_mount(FATFS* fs) {
	return FR_OK;
}

FRESULT uf_open(const char* path) {
    __file_offset = 0;
    int ret = dvd_custom_open(IPC_DEVICE_SD, (char*)path, FILE_ENTRY_TYPE_FILE, IPC_FILE_FLAG_DISABLECACHE | IPC_FILE_FLAG_DISABLEFASTSEEK);
    iprintf("dvd_custom_open ret: %08x\n", ret);
    if (ret != 0) return FR_NOT_OPENED;

    dvd_custom_status(IPC_DEVICE_SD, &__current_file);
    __file_size = (u32)__builtin_bswap64(*(u64*)(&__current_file.fsize));
    iprintf("dvd_custom_status res: %08x\n", __current_file.result);

    iprintf("dvd_custom_status size (fixed): %08x\n", __file_size);
    iprintf("dvd_custom_status size (direct): %08x\n", (u32)__current_file.fsize);

    if (__current_file.result != 0) return FR_NOT_OPENED;
    return ret;
}

FRESULT uf_read(void* buff, UINT btr, UINT* br) {
    // read 4k at a time and copy to buff
    u32 btr_left = btr;
    u32 btr_read = 0;
    while (btr_left > 0) {
        u32 read_size = btr_left > 4096 ? 4096 : btr_left;
        u32 padded_read_size = (read_size + 31) & ~31;

        dvd_read(__file_buffer, padded_read_size, __file_offset);
        memcpy((u8*)buff + btr_read, __file_buffer, read_size);
        btr_read += read_size;
        btr_left -= read_size;
    }

    __file_offset += btr;
    return FR_OK;
}

FRESULT uf_write(const void* buff, UINT btw, UINT* bw) {
    return FR_DISK_ERR;
}

FRESULT uf_lseek(DWORD ofs) {
    return FR_DISK_ERR;
}

DWORD uf_size() {
    return __file_size;
}

FRESULT uf_unmount() {
    return FR_OK;
}

#endif
