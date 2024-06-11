#include <gctypes.h>
#include <ogc/gx.h>
#include <ogc/cache.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>

#include "logo.h"
#include "sd.h"

#include "pngu/pngu.h"
#include "halt.h"

u8 *load_logo_texture(char *path, void *dst) {
    // int width = GAMECUBE_LOGO_WIDTH;
    // int height = GAMECUBE_LOGO_HEIGHT;
    // int format = GX_TF_I8;

    char fixed_path[128];
    if (path[0] != '/') {
        strcpy(fixed_path, "/");
        strcat(fixed_path, path);
    } else {
        strcpy(fixed_path, path);
    }

    int logo_size = get_file_size(fixed_path);
    iprintf("logo image size = %d\n", logo_size);

    void *logo_buffer;
    if (load_file_dynamic(fixed_path, &logo_buffer) != SD_OK) {
        prog_halt("Could not load logo file\n");
        return NULL;
    }

    PNGUPROP imgProp;
    IMGCTX ctx = PNGU_SelectImageFromBuffer(logo_buffer);
    int res = PNGU_GetImageProperties(ctx, &imgProp);
    iprintf("parsed image res = %d, size = %ux%u\n", res, imgProp.imgWidth, imgProp.imgHeight);

    if (imgProp.imgWidth != GAMECUBE_LOGO_WIDTH || imgProp.imgHeight != GAMECUBE_LOGO_HEIGHT) {
        prog_halt("The logo image is not the correct size (352x40px)\n");
        return NULL;
    }

    int width = 0;
    int height = 0;
    u8 *image = PNGU_DecodeTo4x4RGBA8(ctx, imgProp.imgWidth, imgProp.imgHeight, &width, &height, dst);

    PNGU_ReleaseImageContext(ctx);
    free(logo_buffer);

    return (u8*)image;
}
