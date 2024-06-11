#include <gctypes.h>

#include "config.h"

#ifdef USE_FAT_PFF
#define PATH_FIX(var) { for (char *p = var; *p; ++p) *p = (*p >= 'a' && *p <= 'z') ? *p - 0x20 : *p; }
#include "pff/pff.h"

#define uf_mount pf_mount
#define uf_open pf_open
#define uf_read pf_read
#define uf_write pf_write
#define uf_lseek pf_lseek
#define uf_size pf_size
#define uf_unmount() FR_OK
#endif

#ifdef USE_FAT_FATFS
#include "fatfs/ff.h"
#endif

#if defined(USE_FAT_LIBFAT) || defined(USE_FAT_LIBFLIPPY)
typedef u32 UINT;
typedef u32 DWORD;
typedef u32 FATFS;

typedef enum {
	FR_OK = 0,			/* 0 */
	FR_DISK_ERR,		/* 1 */
	FR_NOT_READY,		/* 2 */
	FR_NO_FILE,			/* 3 */
	FR_NOT_OPENED,		/* 4 */
	FR_NOT_ENABLED,		/* 5 */
	FR_NO_FILESYSTEM	/* 6 */
} FRESULT;
#endif

#if defined(USE_FAT_FATFS) || defined(USE_FAT_LIBFAT) || defined(USE_FAT_LIBFLIPPY)
#define PATH_FIX(var)

#define	FA_READ				0x01
#define	FA_WRITE			0x02
#define	FA_OPEN_EXISTING	0x00
#define	FA_CREATE_NEW		0x04
#define	FA_CREATE_ALWAYS	0x08
#define	FA_OPEN_ALWAYS		0x10
#define	FA_OPEN_APPEND		0x30

FRESULT uf_mount (FATFS* fs);								/* Mount/Unmount a logical drive */
FRESULT uf_open (const char* path);							/* Open a file */
FRESULT uf_read (void* buff, UINT btr, UINT* br);			/* Read data from the open file */
FRESULT uf_write (const void* buff, UINT btw, UINT* bw);	/* Write data to the open file */
FRESULT uf_lseek (DWORD ofs);								/* Move file pointer of the open file */
DWORD   uf_size ();											/* Get size from the open file */
FRESULT uf_unmount ();										/* Mount/Unmount a logical drive */

#endif
