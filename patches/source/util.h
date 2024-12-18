#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <ogc/gx.h>

#include "GRRLIB_pixel.h"
#include "dolphin_os.h"
#include "../source/reloc.h"

void C_MTXIdentity(Mtx mtx);
void C_MTXCopy(Mtx src, Mtx dst);

extern void DCFlushRange(void *addr, u32 nBytes);

typedef union rgb_color {
    u32 color;
    struct {
        u8 r, g, b, a;
    } parts;
} rgb_color;

typedef struct hsv_color {
    u32 color;
    struct {
        u8 h, s, v, a;
    } parts;
} hsv_color;

rgb_color hsv2rgb(hsv_color hsv);
hsv_color rgb2hsv(rgb_color rgb);

inline void copy_color10(rgb_color src, GXColorS10* dst) {
    dst->r = src.parts.r;
    dst->g = src.parts.g;
    dst->b = src.parts.b;
    dst->a = src.parts.a;
}

inline void copy_color(rgb_color src, GXColor* dst) {
    dst->r = src.parts.r;
    dst->g = src.parts.g;
    dst->b = src.parts.b;
    dst->a = src.parts.a;
}

inline void copy_gx_color(GXColor* src, GXColor* dst) {
    dst->r = src->r;
    dst->g = src->g;
    dst->b = src->b;
    dst->a = src->a;
}

#define DUMP_COLOR(x) dump_color(#x, x);

inline void dump_color(char *line, GXColorS10 *input) {
    GXColorS10 cs10_temp;
    rgb_color rgb_temp;
    u32 hsl_temp;

#ifndef DEBUG
    (void)hsl_temp;
#endif

    if (input == NULL) {
        OSReport("COLOR: %s = NULL\n", line);
        return;
    }

    cs10_temp = *input;
    rgb_temp.parts.r = cs10_temp.r;
    rgb_temp.parts.g = cs10_temp.g;
    rgb_temp.parts.b = cs10_temp.b;
    rgb_temp.parts.a = cs10_temp.a;
    hsl_temp = GRRLIB_RGBToHSL(rgb_temp.color);
    OSReport("COLOR: %s = %02x%02x%02x = (%d, %d, %d)\n", line, cs10_temp.r, cs10_temp.g, cs10_temp.b, H(hsl_temp), S(hsl_temp), L(hsl_temp));
}

#define DUMP_SMALL_COLOR(x) dump_small_color(#x, x);

inline void dump_small_color(char *line, GXColor *input) {
    if (input == NULL) {
        OSReport("COLOR: %s = NULL\n", line);
        return;
    }

    GXColor cs_temp = *input;
    (void)cs_temp;

    OSReport("COLOR: %s = %02x%02x%02x\n", line, cs_temp.r, cs_temp.g, cs_temp.b);
}

inline void override_texture(tex_data *tex) {
    u16 wd = tex->width;
    u16 ht = tex->height;
    void *img_ptr = (void*)((u8*)tex + tex->offset);
    OSReport("TEX: %dx%d[%d] @ %p\n", wd, ht, tex->format, img_ptr);
    for (int i = 0; i < wd*ht; i++) // only works on I8 format. Change to u32 for RGBA8
        ((u8*)img_ptr)[i] = 0xFF;
}

// inline double u64toDouble(u64 val) {
//     //this is necessary to make gcc not try to use soft float.
//     u32 hi = val >> 32LL;
//     u32 lo = val & 0xFFFFFFFFLL;
//     return (double)lo + (double)(hi * 4294967296.0);
// }

// inline double ticksToSecs(u64 ticks) {
//     return u64toDouble(ticks) / (__OSBusClock / 4);
// }

// // count time taken
// u64 before_ticks = __OSGetSystemTime();

// ... do some work

// double time_delta = ticksToSecs(__OSGetSystemTime() - before_ticks);
// OSReport("TIME DELTA: %f\n", time_delta * 1000.0);

inline void decode_tex_data(u32 *img_data, u32 *texture_data, u32 texture_width, u32 texture_height) {
    /* decode GX_TF_RGBA8 format (4x4 pixels paired titles) */
    u16 *ar = (u16*)(texture_data);
    u16 *gb = (u16*)((u32*)texture_data + 32);
    u32 *dst1 = (u32*)(img_data);
    u32 *dst2 = dst1 + texture_width;
    u32 *dst3 = dst2 + texture_width;
    u32 *dst4 = dst3 + texture_width;
    u32 i,h,w,pixel;

    for (h=0; h<texture_height; h+=4) {
        for (w=0; w<texture_width; w+=4) {
            /* line N (4 pixels) */
            for (i=0; i<4; i++) {
                pixel = ((*ar & 0xff) << 24) | (*gb << 8) | ((*ar & 0xff00) >> 8);
                *dst1++ = pixel;
                ar++;
                gb++;
            }

            /* line N + 1 (4 pixels) */
            for (i=0; i<4; i++) {
                pixel = ((*ar & 0xff) << 24) | (*gb << 8) | ((*ar & 0xff00) >> 8);
                *dst2++ = pixel;
                ar++;
                gb++;
            }

            /* line N + 2 (4 pixels) */
            for (i=0; i<4; i++) {
                pixel = ((*ar & 0xff) << 24) | (*gb << 8) | ((*ar & 0xff00) >> 8);
                *dst3++ = pixel;
                ar++;
                gb++;
            }

            /* line N + 3 (4 pixels) */
            for (i=0; i<4; i++) {
                pixel = ((*ar & 0xff) << 24) | (*gb << 8) | ((*ar & 0xff00) >> 8);
                *dst4++ = pixel;
                ar++;
                gb++;
            }

            /* next paired tiles */
            ar += 16;
            gb += 16;
        }

        /* next 4 lines */
        dst1 = dst4;
        dst2 = dst1 + texture_width;
        dst3 = dst2 + texture_width;
        dst4 = dst3 + texture_width;
    }
}

// credit to https://github.com/ArminTamzarian/metaphrasis
inline void encode_tex_data(void* img_data, void* texture_data, uint16_t buffer_width, uint16_t buffer_height) {
    uint32_t buffer_size = buffer_width * buffer_height;
	uint32_t *src = (uint32_t *)img_data;
	uint8_t *dst = (uint8_t *)texture_data;

	for(uint16_t y = 0; y < buffer_height; y += 4) {
		for(uint16_t x = 0; x < buffer_width; x += 8) {
			for(uint16_t rows = 0; rows < 4; rows++) {
				*dst++ = src[((y + rows) * buffer_width) + (x + 0)] & 0xff;
				*dst++ = src[((y + rows) * buffer_width) + (x + 1)] & 0xff;
				*dst++ = src[((y + rows) * buffer_width) + (x + 2)] & 0xff;
				*dst++ = src[((y + rows) * buffer_width) + (x + 3)] & 0xff;
				*dst++ = src[((y + rows) * buffer_width) + (x + 4)] & 0xff;
				*dst++ = src[((y + rows) * buffer_width) + (x + 5)] & 0xff;
				*dst++ = src[((y + rows) * buffer_width) + (x + 6)] & 0xff;
				*dst++ = src[((y + rows) * buffer_width) + (x + 7)] & 0xff;
			}
		}
	}

	DCFlushRange(texture_data, buffer_size);

	return;
}

// credit to https://github.com/ArminTamzarian/metaphrasis
inline void Metaphrasis_convertBufferToRGBA8(void* rgbaBuffer, void* dataBufferRGBA8, uint16_t bufferWidth, uint16_t bufferHeight) {
	uint32_t bufferSize = (bufferWidth * bufferHeight) << 2;
	memset(dataBufferRGBA8, 0x00, bufferSize);

	uint8_t *src = (uint8_t *)rgbaBuffer;
	uint8_t *dst = (uint8_t *)dataBufferRGBA8;

	for(uint16_t block = 0; block < bufferHeight; block += 4) {
		for(uint16_t i = 0; i < bufferWidth; i += 4) {
            for (uint32_t c = 0; c < 4; c++) {
				uint32_t blockWid = (((block + c) * bufferWidth) + i) << 2 ;

				*dst++ = src[blockWid + 3];  // ar = 0
				*dst++ = src[blockWid + 0];
				*dst++ = src[blockWid + 7];  // ar = 1
				*dst++ = src[blockWid + 4];
				*dst++ = src[blockWid + 11]; // ar = 2
				*dst++ = src[blockWid + 8];
				*dst++ = src[blockWid + 15]; // ar = 3
				*dst++ = src[blockWid + 12];
            }
            for (uint32_t c = 0; c < 4; c++) {
				uint32_t blockWid = (((block + c) * bufferWidth) + i ) << 2 ;

				*dst++ = src[blockWid + 1];  // gb = 0
				*dst++ = src[blockWid + 2];
				*dst++ = src[blockWid + 5];  // gb = 1
				*dst++ = src[blockWid + 6];
				*dst++ = src[blockWid + 9];  // gb = 2
				*dst++ = src[blockWid + 10];
				*dst++ = src[blockWid + 13]; // gb = 3
				*dst++ = src[blockWid + 14];
            }
		}
	}
	DCFlushRange(dataBufferRGBA8, bufferSize);

	return;
}

// from Nintendont
static inline void iwrite32(u32 addr, u32 x) {
	asm volatile("stw %0,0(%1) ; eieio" : : "r"(x), "b"(0x40000000 | addr));
}

void sync_before_read(void *p, u32 len);
void sync_after_write(const void *p, u32 len);
