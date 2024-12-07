

// The concept here is that we have a thread which can be started and performs 3 actions in order
// 1. List all of the games in target_dir and sort them by name (only including certain file extensions)
// 2. Enumerate all of the games and evaluate if they are valid by checking their headers
// 3. Load assets for each approved path and store them in the auxiliarly data RAM using DMA

// Note that the thread is protected by a mutex
// Also note that the cube animation will return WAIT until step 3. starts running

// Given that assets are stored in "external memory" we need to be able to load them from the ARAM
// We will use a thread to handle queue requeust and loading the assets in the background

// This is how data is stored for file entries...

#include <gctypes.h>

#include "pmalloc/pmalloc.h"
#include "picolibc.h"
#include "reloc.h"
#include "attr.h"

#include "dolphin_os.h"
#include "dolphin_arq.h"
#include "dolphin_dvd.h"
#include "flippy_sync.h"
#include "dvd_threaded.h"

#include "ok_png.h"
#include "metaphrasis.h"

#include "games.h"
#include "grid.h"
#include "menu.h"
#include "time.h"

#define PRELOAD_LINE_COUNT 2
#define ASSETS_PER_LINE 8
#define ASSETS_PER_PAGE (ASSETS_PER_LINE * DRAW_TOTAL_ROWS)
#define ASSETS_INITIAL_COUNT (ASSETS_PER_PAGE + (PRELOAD_LINE_COUNT * ASSETS_PER_LINE)) // assuming we start at the top
#define ASSET_BUFFER_COUNT 128

// Globals
int number_of_lines = 4;
int game_backing_count = 0;

static OSMutex game_enum_mutex_obj;
OSMutex *game_enum_mutex = &game_enum_mutex_obj;

char game_enum_path[128] = {0};
bool game_enum_running = false;

// TODO: use a log2 malloc copy strategy for this
__attribute_data_lowmem__ static gm_path_entry_t __gm_early_path_list[2000];
__attribute_data_lowmem__ static gm_path_entry_t *__gm_sorted_path_list[2000];

static gm_file_entry_t *gm_entry_backing[2000];
static u32 gm_entry_count = 0;

gm_file_entry_t *gm_get_game_entry(int index) {
    if (index >= gm_entry_count) return NULL;
    return gm_entry_backing[index];
}

__attribute_aligned_data_lowmem__ static gm_icon_buf_t gm_icon_pool[ASSET_BUFFER_COUNT] = {};
__attribute_aligned_data_lowmem__ static gm_banner_buf_t gm_banner_pool[ASSET_BUFFER_COUNT] = {};

static inline gm_icon_buf_t *gm_get_icon_buf() {
    for (int i = 0; i < ASSET_BUFFER_COUNT; i++) {
        if (gm_icon_pool[i].used == 0) {
            OSReport("Allocating icon buffer\n");
            gm_icon_pool[i].used = 1;
            return &gm_icon_pool[i];
        }
    }

    return NULL;
}

static inline void gm_free_icon_buf(gm_icon_buf_t *buf) {
    if (buf == NULL) return;
    buf->used = 0;
}

static int gm_count_icon_buf() {
    int count = 0;
    for (int i = 0; i < ASSET_BUFFER_COUNT; i++) {
        if (gm_icon_pool[i].used == 1) count++;
    }

    return count;
}

static inline gm_banner_buf_t *gm_get_banner_buf() {
    for (int i = 0; i < ASSET_BUFFER_COUNT; i++) {
        if (gm_banner_pool[i].used == 0) {
            gm_banner_pool[i].used = 1;
            return &gm_banner_pool[i];
        }
    }

    return NULL;
}

static inline void gm_free_banner_buf(gm_banner_buf_t *buf) {
    if (buf == NULL) return;
    buf->used = 0;
}

static int gm_count_banner_buf() {
    int count = 0;
    for (int i = 0; i < ASSET_BUFFER_COUNT; i++) {
        if (gm_banner_pool[i].used == 1) count++;
    }

    return count;
}

static int gm_count_pending_free() {
    int count = 0;
    for (int i = 0; i < gm_entry_count; i++) {
        gm_file_entry_t *entry = gm_entry_backing[i];
        count += entry->asset.icon.schedule_free;
        count += entry->asset.banner.schedule_free;
    }

    return count;
}

// TODO: swap the icon/banner callback with req->owner checks
static void arq_icon_callback_setup(u32 arq_request_ptr) {
    // OSReport("CALLBACK arq_icon_callback_setup\n");
    ARQRequest *req = (ARQRequest*)arq_request_ptr;
    gm_icon_t *icon = (gm_icon_t*)req;

    icon->state = GM_LOAD_STATE_LOADED;
    DCFlushRange(icon, sizeof(gm_icon_t));
}

static void arq_icon_callback_unload(u32 arq_request_ptr) {
    // OSReport("CALLBACK arq_icon_callback_unload\n");
    ARQRequest *req = (ARQRequest*)arq_request_ptr;
    gm_icon_t *icon = (gm_icon_t*)req;

    icon->state = GM_LOAD_STATE_UNLOADED;
    gm_free_icon_buf(icon->buf);
    DCFlushRange(icon, sizeof(gm_icon_t));
}

static void arq_icon_callback_load(u32 arq_request_ptr) {
    // OSReport("CALLBACK arq_icon_callback_load\n");
    ARQRequest *req = (ARQRequest*)arq_request_ptr;
    gm_icon_t *icon = (gm_icon_t*)req;

    icon->state = GM_LOAD_STATE_LOADED;
    DCFlushRange(icon, sizeof(gm_icon_t));
}

static void arq_banner_callback_setup(u32 arq_request_ptr) {
    // OSReport("CALLBACK arq_banner_callback_setup\n");
    ARQRequest *req = (ARQRequest*)arq_request_ptr;
    gm_banner_t *banner = (gm_banner_t*)req;

    banner->state = GM_LOAD_STATE_LOADED;
    DCFlushRange(banner, sizeof(gm_banner_t));
}

static void arq_banner_callback_unload(u32 arq_request_ptr) {
    // OSReport("CALLBACK arq_banner_callback_unload\n");
    ARQRequest *req = (ARQRequest*)arq_request_ptr;
    gm_banner_t *banner = (gm_banner_t*)req;

    banner->state = GM_LOAD_STATE_UNLOADED;
    gm_free_banner_buf(banner->buf);
    DCFlushRange(banner, sizeof(gm_banner_t));
}

static void arq_banner_callback_load(u32 arq_request_ptr) {
    // OSReport("CALLBACK arq_banner_callback_load\n");
    ARQRequest *req = (ARQRequest*)arq_request_ptr;
    gm_banner_t *banner = (gm_banner_t*)req;

    banner->state = GM_LOAD_STATE_LOADED;
    DCFlushRange(banner, sizeof(gm_banner_t));
}

// asset offload helpers
void gm_icon_setup(gm_icon_t *icon, u32 aram_offset) {
    OSReport("Setting up icon\n");
    icon->aram_offset = aram_offset;
    icon->state = GM_LOAD_STATE_SETUP;

    ARQRequest *req = &icon->req;
    u32 owner = make_type('I', 'X', 'X', 'S');
    u32 type = ARAM_DIR_MRAM_TO_ARAM;
    u32 priority = ARQ_PRIORITY_LOW;
    u32 source = (u32)icon->buf->data;
    u32 dest = aram_offset;
    u32 length = ICON_PIXELDATA_LEN;

    dolphin_ARQPostRequest(req, owner, type, priority, source, dest, length, &arq_icon_callback_setup);
}

void gm_icon_setup_unload(gm_icon_t *icon, u32 aram_offset) {
    icon->aram_offset = aram_offset;
    icon->state = GM_LOAD_STATE_UNLOADING;

    ARQRequest *req = &icon->req;
    u32 owner = make_type('I', 'C', 'O', 'U');
    u32 type = ARAM_DIR_MRAM_TO_ARAM;
    u32 priority = ARQ_PRIORITY_LOW;
    u32 source = (u32)icon->buf->data;
    u32 dest = aram_offset;
    u32 length = ICON_PIXELDATA_LEN;

    dolphin_ARQPostRequest(req, owner, type, priority, source, dest, length, &arq_icon_callback_unload);
}

void gm_icon_load(gm_icon_t *icon) {
    // OSReport("Loading icon %d\n", icon->state);
    if (icon->state == GM_LOAD_STATE_SETUP) {
        OSReport("ERROR: banner is still in setup\n");
    }
    if (icon->state == GM_LOAD_STATE_UNLOADING) {
        OSReport("ERROR: banner is still in unload\n");
    }
    if (icon->state == GM_LOAD_STATE_NONE || icon->state == GM_LOAD_STATE_LOADED) return;
    icon->state = GM_LOAD_STATE_LOADING;

    gm_icon_buf_t *icon_ptr = gm_get_icon_buf();
    if (icon_ptr == NULL) {
        OSReport("ERROR: could not allocate memory\n");
        return;
    }
    icon->buf = icon_ptr;
    DCFlushRange(icon, sizeof(gm_icon_t));

    ARQRequest *req = &icon->req;
    u32 owner = make_type('I', 'C', 'O', 'L');
    u32 type = ARAM_DIR_ARAM_TO_MRAM;
    u32 priority = ARQ_PRIORITY_LOW;
    u32 source = icon->aram_offset;
    u32 dest = (u32)icon->buf->data;
    u32 length = ICON_PIXELDATA_LEN;

    dolphin_ARQPostRequest(req, owner, type, priority, source, dest, length, &arq_icon_callback_load);
}

void gm_icon_free(gm_icon_t *icon) {
    if (icon->state == GM_LOAD_STATE_NONE) return;
    
    if (icon->state == GM_LOAD_STATE_LOADING || icon->state == GM_LOAD_STATE_SETUP) {
        OSReport("ERROR: icon is still loading\n");
        icon->schedule_free = true;
        return;
    }

    icon->state = GM_LOAD_STATE_UNLOADING;
    // memset(icon->buf->data, 0, ICON_PIXELDATA_LEN); // test only
    gm_free_icon_buf(icon->buf);
    icon->buf = NULL;
    icon->state = GM_LOAD_STATE_UNLOADED;
}

void gm_banner_setup(gm_banner_t *banner, u32 aram_offset) {
    // OSReport("Setting up banner\n");
    banner->aram_offset = aram_offset;
    banner->state = GM_LOAD_STATE_SETUP;

    ARQRequest *req = &banner->req;
    u32 owner = make_type('B', 'X', 'X', 'S');
    u32 type = ARAM_DIR_MRAM_TO_ARAM;
    u32 priority = ARQ_PRIORITY_LOW;
    u32 source = (u32)banner->buf->data;
    u32 dest = aram_offset;
    u32 length = BNR_PIXELDATA_LEN;

    dolphin_ARQPostRequest(req, owner, type, priority, source, dest, length, &arq_banner_callback_setup);
}

void gm_banner_setup_unload(gm_banner_t *banner, u32 aram_offset) {
    banner->aram_offset = aram_offset;
    banner->state = GM_LOAD_STATE_UNLOADING;

    ARQRequest *req = &banner->req;
    u32 owner = make_type('I', 'X', 'X', 'U');
    u32 type = ARAM_DIR_MRAM_TO_ARAM;
    u32 priority = ARQ_PRIORITY_LOW;
    u32 source = (u32)banner->buf->data;
    u32 dest = aram_offset;
    u32 length = BNR_PIXELDATA_LEN;

    dolphin_ARQPostRequest(req, owner, type, priority, source, dest, length, &arq_banner_callback_unload);
}

void gm_banner_load(gm_banner_t *banner) {
    // OSReport("Loading banner %d\n", banner->state);
    if (banner->state == GM_LOAD_STATE_SETUP) {
        OSReport("ERROR: banner is still in setup\n");
    }
    if (banner->state == GM_LOAD_STATE_UNLOADING) {
        OSReport("ERROR: banner is still in unload\n");
    }
    if (banner->state == GM_LOAD_STATE_NONE || banner->state == GM_LOAD_STATE_LOADED) return;
    banner->state = GM_LOAD_STATE_LOADING;

    gm_banner_buf_t *banner_ptr = gm_get_banner_buf();
    if (banner_ptr == NULL) {
        OSReport("ERROR: could not allocate memory\n");
        return;
    }
    banner->buf = banner_ptr;
    DCFlushRange(banner, sizeof(gm_banner_t));

    ARQRequest *req = &banner->req;
    u32 owner = make_type('I', 'X', 'X', 'L');
    u32 type = ARAM_DIR_ARAM_TO_MRAM;
    u32 priority = ARQ_PRIORITY_LOW;
    u32 source = banner->aram_offset;
    u32 dest = (u32)banner->buf->data;
    u32 length = BNR_PIXELDATA_LEN;

    dolphin_ARQPostRequest(req, owner, type, priority, source, dest, length, &arq_banner_callback_load);
}

void gm_banner_free(gm_banner_t *banner) {
    if (banner->state == GM_LOAD_STATE_NONE || banner->state == GM_LOAD_STATE_UNLOADING) {
        if (banner->state == GM_LOAD_STATE_UNLOADING) OSReport("ERROR: banner is unloading??\n");
        return;
    }
    
    if (banner->state == GM_LOAD_STATE_LOADING || banner->state == GM_LOAD_STATE_SETUP) {
        OSReport("ERROR: banner is still loading\n");
        banner->schedule_free = true;
        return;
    }

    banner->state = GM_LOAD_STATE_UNLOADING;
    // memset(banner->buf->data, 0, BNR_PIXELDATA_LEN); // test only
    gm_free_banner_buf(banner->buf);
    banner->buf = NULL;
    banner->state = GM_LOAD_STATE_UNLOADED;
}

// HEAP
pmalloc_t pmblock;
pmalloc_t *pm = &pmblock;
__attribute_aligned_data_lowmem__ static u8 gm_heap_buffer[2 * 1024 * 1024];

// MACROS
#define gm_malloc(x) pmalloc_memalign(pm, x, 32);
#define gm_free(x) pmalloc_freealign(pm, x);

void gm_init_heap() {
    OSReport("Initializing heap [%x]\n", sizeof(gm_heap_buffer));
    
    // Initialise our pmalloc
	pmalloc_init(pm);
	pmalloc_addblock(pm, &gm_heap_buffer[0], sizeof(gm_heap_buffer));
}

// png
static void *ok_gm_alloc(void *user_data, size_t size) {
    (void)user_data;
    return pmalloc_malloc(pm, size);
}

static void ok_gm_free(void *user_data, void *memory) {
    (void)user_data;
    pmalloc_free(pm, memory);
}

const ok_png_allocator OK_PNG_GM_ALLOCATOR = {
    .alloc = ok_gm_alloc,
    .free = ok_gm_free,
    .image_alloc = NULL,
};

typedef struct {
    uint8_t *data;
    size_t size;
    size_t position;
} ok_mem_state_t;
static ok_mem_state_t ok_mem_state;

static size_t ok_mem_read(void *user_data, uint8_t *buffer, size_t length) {
    (void)user_data;
    ok_mem_state_t *state = &ok_mem_state;
    if (state->position + length > state->size) {
        length = state->size - state->position;  // Adjust length to prevent overflow
    }
    memcpy(buffer, state->data + state->position, length);
    state->position += length;
    return length;
}

static bool ok_mem_seek(void *user_data, long count) {
    (void)user_data;
    ok_mem_state_t *state = &ok_mem_state;
    if (state->position + count > state->size || state->position + count < 0) {
        return false;  // Out of bounds
    }
    state->position += count;
    return true;
}

static const ok_png_input OK_PNG_MEM_INPUT = {
    .read = ok_mem_read,
    .seek = ok_mem_seek,
};

ok_png gm_png_decode(void *file_buf, size_t file_size) {
    ok_mem_state.data = file_buf;
    ok_mem_state.size = file_size;
    ok_mem_state.position = 0;

    ok_png png = ok_png_read_from_input(OK_PNG_COLOR_FORMAT_RGBA, OK_PNG_MEM_INPUT, file_buf, OK_PNG_GM_ALLOCATOR, NULL);
    return png;
}

// HELPERS
static char *valid_game_exts[] = {".gcm", ".iso", ".fdi"};
static char *valid_prog_exts[] = {".dol", ".dol+cli"}; // TODO: add .elf
static gm_file_type_t gm_get_file_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (ext == NULL) return GM_FILE_TYPE_UNKNOWN;

    for (int i = 0; i < sizeof(valid_game_exts) / sizeof(char*); i++) {
        if (strcasecmp(ext, valid_game_exts[i]) == 0) return GM_FILE_TYPE_GAME;
    }

    for (int i = 0; i < sizeof(valid_prog_exts) / sizeof(char*); i++) {
        if (strcasecmp(ext, valid_prog_exts[i]) == 0) return GM_FILE_TYPE_PROGRAM;
    }

    return GM_FILE_TYPE_UNKNOWN;
}

static void ipl_panic() {
    // TODO: set XFB
    OSReport("PANIC: unknown\n");
    while(1);
}

int gm_cmp_path_entry(const void* ptr_a, const void* ptr_b){
    const gm_path_entry_t *obj_a = *(gm_path_entry_t**)ptr_a;
    const gm_path_entry_t *obj_b = *(gm_path_entry_t**)ptr_b;

    return strcasecmp(obj_a->path, obj_b->path);
}

// DEFS
typedef struct {
    int num_paths;
} gm_list_info;

// 1. func named gm_list_files returns {pointer to path array, number of paths}
// 2. func named gm_check_headers returns {pointer to valid path array, number of valid paths}
// 3. func named gm_load_assets returns {void}

// Before the next go a dealloc function is called to free all of the memory before the thread is started again

gm_list_info gm_list_files(const char *target_dir) {
    // List all of the games in target_dir and sort them by name (only including certain file extensions)
    OSReport("Listing files in %s\n", target_dir);
    u64 start_time = gettime();

    int res = dvd_custom_open(target_dir, FILE_ENTRY_TYPE_DIR, 0);
    if (res != 0) {
        OSReport("PANIC: SD Card could not be opened\n");
        while(1);
    }

    // TODO: if the SD card is not inserted this would be a good place to bail out 
    file_status_t *status = dvd_custom_status();
    if (status->result != 0) {
        OSReport("PANIC: SD Card could not be opened\n");
        while(1);
    }

    uint8_t dir_fd = status->fd;
    OSReport("found readdir fd=%u\n", dir_fd);

    // now list everything
    static GCN_ALIGNED(file_entry_t) ent;
    int path_entry_count = 0;
    char file_full_path_buf[128] = {0};

    // TODO: switch to using DVD Mutex (this is all happening in a thread)
    while(1) {
        int ret = dvd_custom_readdir(&ent, dir_fd);
        if (ret != 0) ipl_panic();

        if (ent.name[0] == 0) break; // end of directory
        // if (ent.type == FILE_ENTRY_TYPE_DIR) continue; // TODO: add DIR listing

        // only check file ext for now
        gm_file_type_t file_type = GM_FILE_TYPE_UNKNOWN;
        if (ent.type == FILE_ENTRY_TYPE_DIR) {
            file_type = GM_FILE_TYPE_DIRECTORY;
        } else {
            file_type = gm_get_file_type(ent.name);
        }

        if (file_type == GM_FILE_TYPE_UNKNOWN) continue; // check if the file is valid

#ifdef PRINT_READDIR_NAMES
        // logging
        OSReport("READDIR ent(%u): %s [len=%d]\n", ent.type, ent.name, strlen(ent.name));
#endif

        // combine the path
        strcpy(file_full_path_buf, target_dir);
        strcat(file_full_path_buf, ent.name);
#ifdef PRINT_READDIR_NAMES
        // logging
        OSReport("PATH ent(%u): %s\n", ent.type, file_full_path_buf);
#endif

        // store the path
        gm_path_entry_t *entry = &__gm_early_path_list[path_entry_count];
        strcpy(entry->path, file_full_path_buf);
        entry->type = file_type;

        // setup sort list
        __gm_sorted_path_list[path_entry_count] = entry;
        path_entry_count++;

        if (path_entry_count >= 1920) {
            OSReport("WARNING: Too many files in directory\n");
            break;
        }
    }

    dvd_custom_close(dir_fd);

    f32 runtime = (f32)diff_usec(start_time, gettime()) / 1000.0;
    OSReport("File enum completed! took=%f (%d)\n", runtime, game_backing_count);
    (void)runtime;

    return (gm_list_info){path_entry_count};
}

void gm_sort_files(int path_count) {
    // Sort the paths by name
    u64 start_time = gettime();
    qsort(__gm_sorted_path_list, path_count, sizeof(gm_path_entry_t*), &gm_cmp_path_entry);

    f32 runtime = (f32)diff_usec(start_time, gettime()) / 1000.0;
    OSReport("Sort took=%f\n", runtime);
    (void)runtime;
}
// returns amount of space used in aram
static int gm_load_banner(gm_file_entry_t *entry, u32 aram_offset, bool force_unload) {
    if (entry->extra.dvd_bnr_offset == 0) return false;

    // load the banner
    dvd_custom_open(entry->path, FILE_ENTRY_TYPE_FILE, IPC_FILE_FLAG_DISABLECACHE | IPC_FILE_FLAG_DISABLESPEEDEMU);
    file_status_t *status = dvd_custom_status();
    if (status == NULL || status->result != 0) {
        OSReport("ERROR: could not open file\n");
        return false;
    }

    __attribute_aligned_data__ static BNR banner_buffer;
    DI_Read(&banner_buffer, sizeof(BNR), entry->extra.dvd_bnr_offset, status->fd);
    dvd_custom_close(status->fd);

    entry->asset.banner.state = GM_LOAD_STATE_LOADING;
    gm_banner_buf_t *banner_ptr = gm_get_banner_buf();
    if (banner_ptr == NULL) {
        OSReport("ERROR: could not allocate memory\n");
        return false;
    }

    memcpy(&banner_ptr->data[0], &banner_buffer.pixelData[0], BNR_PIXELDATA_LEN);
    DCFlushRange(&banner_ptr->data[0], BNR_PIXELDATA_LEN);
    entry->asset.banner.buf = banner_ptr;
    if (force_unload) {
        gm_banner_setup_unload(&entry->asset.banner, aram_offset);
    } else {
        gm_banner_setup(&entry->asset.banner, aram_offset);
    }

    // TODO: check current language using extra.dvd_bnr_type
    memcpy(&entry->desc, &banner_buffer.desc[0], sizeof(BNRDesc));

    return true;
}

// returns amount of space used in aram
static bool gm_load_icon(gm_file_entry_t *entry, u32 aram_offset, bool force_unload) {
    // split path and add png extension
    char icon_path[128];
    strcpy(icon_path, entry->path);
    char *ext = strrchr(icon_path, '.');
    if (ext == NULL) return false;
    strcpy(ext, ".png");

    // load the icon
    dvd_custom_open(icon_path, FILE_ENTRY_TYPE_FILE, IPC_FILE_FLAG_DISABLECACHE | IPC_FILE_FLAG_DISABLEFASTSEEK);
    file_status_t *status = dvd_custom_status();
    if (status == NULL || status->result != 0) {
        // OSReport("ERROR: could not open icon file\n");
        return false;
    }

    // allocate file buffer
    u32 file_size = (u32)__builtin_bswap64(*(u64*)(&status->fsize));
    file_size += 31;
    file_size &= 0xffffffe0;
    void *file_buf = gm_malloc(file_size);

    // read
    DI_Read(file_buf, file_size, 0, status->fd);
    dvd_custom_close(status->fd);

    ok_png png = gm_png_decode(file_buf, file_size);
    if (png.error_code != OK_PNG_SUCCESS) {
        OSReport("ERROR: could not decode icon file\n");
        return false;
    }

    OSReport("PNG: %d x %d\n", png.width, png.height);
    gm_free(file_buf);

    entry->asset.icon.state = GM_LOAD_STATE_LOADING;
    gm_icon_buf_t *icon_ptr = gm_get_icon_buf();
    if (icon_ptr == NULL) {
        OSReport("ERROR: could not allocate memory\n");
        return false;
    }

    Metaphrasis_convertBufferToRGB5A3((uint32_t*)png.data, (uint32_t*)&icon_ptr->data[0], png.width, png.height);
    DCFlushRange(&icon_ptr->data[0], ICON_PIXELDATA_LEN);
    pmalloc_free(pm, png.data);

    entry->asset.icon.buf = icon_ptr;
    if (force_unload) {
        gm_icon_setup_unload(&entry->asset.icon, aram_offset);
    } else {
        gm_icon_setup(&entry->asset.icon, aram_offset);
    }

    return true;
}

void gm_check_files(int path_count) {
    // Here we will also check for override assets and matching save icons
    u32 aram_offset = (1 * 1024 * 1024); // 1MB mark

    // Enumerate all of the games
    u64 start_time = gettime();
    for (int i = 0; i < path_count; i++) {
        gm_path_entry_t *entry = __gm_sorted_path_list[i];
        // OSReport("Checking header %s [%d]\n", entry->path, entry->type);

        bool force_unload = false;
        if (gm_entry_count - (top_line_num * ASSETS_PER_LINE) > ASSETS_INITIAL_COUNT) {
            force_unload = true;
        }

        if (!OSTryLockMutex(game_enum_mutex)) {
            OSReport("STOPPING GAME LOADING\n");
            break;
        }
        OSUnlockMutex(game_enum_mutex);

        // Load assets and store them in the auxiliarly data RAM using DMA
        if (entry->type == GM_FILE_TYPE_GAME) {
            // OSReport("DEBUG: Game Check %d\n", i);
            // check if the banner file exists
            dolphin_game_into_t info = get_game_info(entry->path);
            if (!info.valid) continue;
            OSReport("Found game %s (%d)\n", entry->path, force_unload); // lets do this!

            // create a new entry
            gm_file_entry_t *backing = gm_malloc(sizeof(gm_file_entry_t));
            memset(backing, 0, sizeof(gm_file_entry_t));
            memcpy(backing->path, entry->path, sizeof(backing->path));
            backing->type = GM_FILE_TYPE_GAME;

            // copy the extra info
            memcpy(backing->extra.game_id, info.game_id, sizeof(backing->extra.game_id));
            backing->extra.disc_num = info.disc_num;
            backing->extra.disc_ver = info.disc_ver;
            backing->extra.dvd_bnr_offset = info.bnr_offset;
            backing->extra.dvd_bnr_type = info.bnr_type;

            // load the banner
            bool bnr_loaded = gm_load_banner(backing, aram_offset, force_unload);
            if (!bnr_loaded) {
                OSReport("Failed to load banner %s\n", entry->path);
            }
            aram_offset += BNR_PIXELDATA_LEN;

            // load the icon
            bool icon_loaded = gm_load_icon(backing, aram_offset, force_unload);
            if (!icon_loaded) {
                // OSReport("Failed to load icon %s\n", entry->path);
                backing->asset.use_banner = true;
            } else {
                backing->asset.use_banner = false;
            }
            aram_offset += ICON_PIXELDATA_LEN;

            // set heap pointer
            gm_entry_backing[gm_entry_count] = backing;
            gm_entry_count++;
        } else if (entry->type == GM_FILE_TYPE_PROGRAM || entry->type == GM_FILE_TYPE_DIRECTORY) {
            OSReport("Found other %s\n", entry->path); // lets do this!

            // create a new entry
            gm_file_entry_t *backing = gm_malloc(sizeof(gm_file_entry_t));
            memset(backing, 0, sizeof(gm_file_entry_t));
            memcpy(backing->path, entry->path, sizeof(backing->path));
            backing->type = entry->type;

            // get the basename
            char *base = strrchr(entry->path, '/');
            strcpy(backing->desc.fullGameName, base + 1);
            if (entry->type == GM_FILE_TYPE_PROGRAM) {
                strcpy(backing->desc.description, "Homebrew Program");
            } else {
                strcpy(backing->desc.description, "Directory");
            }

            // load the icon
            bool icon_loaded = gm_load_icon(backing, aram_offset, force_unload);
            if (!icon_loaded) {
                // OSReport("Failed to load icon %s\n", entry->path);
            }
            backing->asset.use_banner = false;
            aram_offset += ICON_PIXELDATA_LEN;

            // set heap pointer
            gm_entry_backing[gm_entry_count] = backing;
            gm_entry_count++;
        }

        // udelay_threaded(100 * 1000); // test only
        game_backing_count = gm_entry_count;
    }

    OSReport("Total entries = %d\n", gm_entry_count);

    // find multi-disc games
    for (int i = 0; i < gm_entry_count; i++) {
        gm_file_entry_t *entry = gm_entry_backing[i];
        if (entry->type != GM_FILE_TYPE_GAME) continue;
        if (entry->extra.disc_num == 0) continue; // check Disc 2 only

        OSReport("Checking multi-disc %s[%d] (%d)\n", entry->path, i, entry->extra.disc_num);

        for (int j = 0; j < gm_entry_count; j++) {
            gm_file_entry_t *entry2 = gm_entry_backing[j];
            if (entry2->type != GM_FILE_TYPE_GAME) continue;
            if (entry2 == entry) continue;

            // check if the game is multi-disc
            bool is_same_game = memcmp(entry->extra.game_id, entry2->extra.game_id, 6) == 0;
            bool is_different_disc = entry->extra.disc_num != entry2->extra.disc_num;
            if (is_same_game && is_different_disc) {
                OSReport("Found multi-disc %s[%d] (%d)\n", entry2->path, j, entry2->extra.disc_num);
                entry->second = entry2;
                entry2->second = entry;
            }
        }
    }

    f32 runtime = (f32)diff_usec(start_time, gettime()) / 1000.0;
    OSReport("Header check took=%f\n", runtime);
    (void)runtime;
}

void gm_line_load(int line_num) {
    // OSReport("Line load %d\n", line_num);

    for (int i = 0; i < ASSETS_PER_LINE; i++) {
        int index = (line_num * ASSETS_PER_LINE) + i;
        if (index >= gm_entry_count) break;

        gm_file_entry_t *entry = gm_entry_backing[index];
        if (entry->type == GM_FILE_TYPE_GAME) {
            gm_icon_load(&entry->asset.icon);
            gm_banner_load(&entry->asset.banner);
        } else {
            gm_icon_load(&entry->asset.icon);
        }
    }
}

void gm_line_free(int line_num) {
    // OSReport("Line free %d\n", line_num);

    for (int i = 0; i < ASSETS_PER_LINE; i++) {
        int index = (line_num * ASSETS_PER_LINE) + i;
        if (index >= gm_entry_count) break;

        gm_file_entry_t *entry = gm_entry_backing[index];
        if (entry->type == GM_FILE_TYPE_GAME) {
            // OSReport("Freeing assets %s\n", entry->path);
            gm_icon_free(&entry->asset.icon);
            gm_banner_free(&entry->asset.banner);
        } else {
            gm_icon_free(&entry->asset.icon);
        }
    }
}

void gm_line_changed(int delta) {
    // OSReport("Line changed %+d\n", delta);

    // int previous_line_num = top_line_num;
    int new_line_num = top_line_num + delta;

    // two slots should be processed, one to load and one to unload

    // get number of lines to process
    if (delta < 0) {
        int load_line = new_line_num - PRELOAD_LINE_COUNT + 1;
        if (load_line >= 0) {
            // load
            // OSReport("DEBUG: Load line %d\n", load_line);
            gm_line_load(load_line);
        }

        // unload from the bottom
        int unload_line = new_line_num + DRAW_TOTAL_ROWS + PRELOAD_LINE_COUNT;
        if (unload_line < number_of_lines) {
            // unload
            // OSReport("DEBUG: Unload line %d\n", unload_line);
            gm_line_free(unload_line);
        }
    } else if (delta > 0) {
        int load_line = new_line_num + DRAW_TOTAL_ROWS + PRELOAD_LINE_COUNT - 1;
        if (load_line < number_of_lines) {
            // load
            // OSReport("DEBUG: Load line %d\n", load_line);
            gm_line_load(load_line);
        }

        // unload from the top
        int unload_line = new_line_num - PRELOAD_LINE_COUNT;
        if (unload_line >= 0) {
            // unload
            // OSReport("DEBUG: Unload line %d\n", unload_line);
            gm_line_free(unload_line);
        }
    }

    // int icon_buf_count = gm_count_icon_buf();
    // int banner_buf_count = gm_count_banner_buf();

    // OSReport("icon buf count = %d\n", icon_buf_count);
    // OSReport("banner buf count = %d\n", banner_buf_count);

    // int pending_free = gm_count_pending_free();
    // OSReport("pending free = %d\n", pending_free);
}

// so grid can check if load/unload is possible
bool gm_can_move() {
    return true;
}

#if 0
void gm_debug_func() {
    int icon_buf_count = gm_count_icon_buf();
    int banner_buf_count = gm_count_banner_buf();

    OSReport("icon buf count = %d\n", icon_buf_count);
    OSReport("banner buf count = %d\n", banner_buf_count);

    for (int i = 0; i < gm_entry_count; i++) {
        gm_file_entry_t *entry = gm_entry_backing[i];
        OSReport("Entry %d: %s\n", i, entry->path);

        if (entry->type == GM_FILE_TYPE_GAME) {
            // banner buf
            OSReport("Banner: %p (%d)\n", entry->asset.banner.buf, entry->asset.banner.state);
        }
    }
}
#endif

void gm_setup_grid(int line_count, bool initial) {
    number_of_lines = (line_count + 7) >> 3;
    if (number_of_lines < 4) {
        number_of_lines = 4;
    }

    if (initial) {
        // Setup the grid
        grid_setup_func();
    }
}

void *gm_thread_worker(void* param) {
    const char *target = &game_enum_path[0];
    if (target == NULL || strlen(target) == 0) {
        OSReport("ERROR: target is NULL\n");
        return NULL;
    }

    // Test only
    extern void di_tests();
    di_tests();
    // while(1);

    gm_list_info list_info = gm_list_files(target);
    gm_setup_grid(list_info.num_paths, true);
    gm_sort_files(list_info.num_paths);
    gm_check_files(list_info.num_paths);
    gm_setup_grid(gm_entry_count, false);

    game_enum_running = false;
    // DCBlockStore((void*)OSRoundDown32B((u32)&game_enum_running));
    DCFlushRange((void*)OSRoundDown32B((u32)&game_enum_running), 4);

    // pmalloc_dump_stats(pm);
    return NULL;
}

void gm_init_thread() {
    OSInitMutex(game_enum_mutex);
}

// match https://github.com/projectPiki/pikmin2/blob/snakecrowstate-work/include/Dolphin/OS/OSThread.h#L55-L74
static OSThread thread_obj;
static u8 thread_stack[32 * 1024];
void gm_start_thread(const char *target) {
    if (game_enum_running) {
        OSReport("ERROR: game enum thread is already running\n");
        return;
    }

    // pop one path element
    if (strcmp(target, "..") == 0) {
        OSReport("Previous path...\n");
        // zero the last char
        game_enum_path[strlen(game_enum_path) - 1] = 0;

        // find the last slash
        char *last_slash = strrchr(game_enum_path, '/');
        if (last_slash != NULL) {
            *last_slash++ = 0;
        }

        if (strlen(game_enum_path) == 0) {
            OSReport("ERROR: cannot go back any further\n");
            game_enum_path[0] = '/';
            game_enum_path[1] = 0;
        }

        target = game_enum_path;
    }

    char path[128];
    strcpy(path, target);
    if (path[strlen(path) - 1] != '/') {
        strcat(path, "/");
    }

    OSReport("Starting game thread %s\n", path);
    strcpy(game_enum_path, path);

    game_enum_running = true;
    DCBlockStore((void*)OSRoundDown32B((u32)&game_enum_running));
    
    // OSUnlockMutex(game_enum_mutex);

    if (gm_entry_count > 0) {
        for (int i = 0; i < gm_entry_count; i++) {
            gm_file_entry_t *entry = gm_entry_backing[i];
            if (entry->type == GM_FILE_TYPE_GAME) {
                gm_icon_free(&entry->asset.icon);
                gm_banner_free(&entry->asset.banner);
            } else {
                gm_icon_free(&entry->asset.icon);
            }
            gm_free(entry);
        }
        gm_entry_count = 0;
    }

    number_of_lines = 0;
    DCBlockStore((void*)OSRoundDown32B((u32)&number_of_lines));

    game_backing_count = 0;
    DCBlockStore((void*)OSRoundDown32B((u32)&game_backing_count));

    // Start the thread
    u32 thread_stack_size = sizeof(thread_stack);
    void *thread_stack_top = thread_stack + thread_stack_size;
    s32 thread_priority = DEFAULT_THREAD_PRIO + 3;

    dolphin_OSCreateThread(&thread_obj, gm_thread_worker, NULL, thread_stack_top, thread_stack_size, thread_priority, 0);
    dolphin_OSResumeThread(&thread_obj);
}


void gm_deinit_thread() {
    if (game_enum_running) {
        OSReport("Stopping file enum\n");
        OSLockMutex(game_enum_mutex);
        OSReport("Waiting for thread to exit, %d\n", game_enum_running);
        OSJoinThread(&thread_obj, NULL);
        OSReport("File enum done\n");
        OSUnlockMutex(game_enum_mutex);
    }
}

void gm_debug_func() {
    
}
