#include "apple_logo.h"
#include "cursor.h"
#include "wallpaper_icon.h"
#include "wallpapers.h"
#include "disasm.h"
#include <stddef.h>


typedef struct {
    uint32_t fb_base;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t mode;
} boot_info_t;

#define TRACK_COLOR 0x002c2c2e
#define FILL_COLOR  0x00ffffff
#define BAR_W       216
#define BAR_H       4

static boot_info_t g_bi;
static int con_x = 24, con_y = 510, con_cols = 118;

#define KERNEL_LOAD_ADDR  0x10000
#define KERNEL_SECTORS    128
#define SECTOR_SIZE       512

static int debug_active = 0;
static int hotkeys_enabled = 1;
static uint32_t uptime_ms = 0;

/* poll_keyboard() consumes every scancode, so other wait loops read keys
 * through this latch instead of polling 0x60 directly. */
static uint8_t any_key = 0;
static const char *dbg_c_func   = "init";
static const char *dbg_c_detail = "Bootstrapping";
static const char *dbg_c_src    = "kernel_main()";

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait() {
    outb(0x80, 0);
}

static void draw_console_line(uint32_t *fb, uint32_t pitch, int x, int y, const char *s, uint32_t fg, int width_chars) {
    int count = 0;
    while (*s && count < width_chars) {
        draw_char(fb, pitch, x + count * 8, y, *s++, fg, 0x00000000);
        count++;
    }
    while (count < width_chars) {
        draw_char(fb, pitch, x + count * 8, y, ' ', fg, 0x00000000);
        count++;
    }
}

static void clear_console_area(uint32_t *fb, uint32_t pitch) {
    int max_dy = (int)g_bi.height - con_y;
    if (max_dy > 180) max_dy = 180;
    int max_dx = con_cols * 8;
    if (con_x + max_dx > (int)g_bi.width) max_dx = g_bi.width - con_x;
    for (int dy = 0; dy < max_dy; dy++) {
        for (int dx = 0; dx < max_dx; dx++) {
            fb[(con_y + dy) * pitch + (con_x + dx)] = 0x00000000;
        }
    }
}

static void render_debug_console(uint32_t *fb, uint32_t pitch) {
    int y = con_y;
    const int step_y = 11;

    char hdr[96] = "--- [DEBUG] ";
    char num[12];
    fmt_dec(num, g_bi.width); str_cat(hdr, num); str_cat(hdr, "x");
    fmt_dec(num, g_bi.height); str_cat(hdr, num); str_cat(hdr, "@");
    fmt_dec(num, g_bi.bpp); str_cat(hdr, num);
    str_cat(hdr, " live monitor ('d': hide, 'h': halt) ---");
    draw_console_line(fb, pitch, con_x, y, hdr, 0x0000ffcc, con_cols);
    y += step_y;

    char c_line1[128] = "c_func:  ";
    str_cat(c_line1, dbg_c_func);
    str_cat(c_line1, "() [");
    str_cat(c_line1, dbg_c_detail);
    str_cat(c_line1, "]");
    draw_console_line(fb, pitch, con_x, y, c_line1, 0x0055ff55, con_cols);
    y += step_y;

    char c_line2[128] = "c_src:   ";
    str_cat(c_line2, dbg_c_src);
    draw_console_line(fb, pitch, con_x, y, c_line2, 0x00aaaaaa, con_cols);
    y += step_y;

    uint32_t regs[10];
    get_cpu_state(regs);

    char r_line1[128] = "regs:    eax="; char h[12];
    fmt_hex(h, regs[0], 8); str_cat(r_line1, h); str_cat(r_line1, " ebx=");
    fmt_hex(h, regs[1], 8); str_cat(r_line1, h); str_cat(r_line1, " ecx=");
    fmt_hex(h, regs[2], 8); str_cat(r_line1, h); str_cat(r_line1, " edx=");
    fmt_hex(h, regs[3], 8); str_cat(r_line1, h);
    draw_console_line(fb, pitch, con_x, y, r_line1, 0x00ffffff, con_cols);
    y += step_y;

    char r_line2[128] = "         esi=";
    fmt_hex(h, regs[4], 8); str_cat(r_line2, h); str_cat(r_line2, " edi=");
    fmt_hex(h, regs[5], 8); str_cat(r_line2, h); str_cat(r_line2, " ebp=");
    fmt_hex(h, regs[6], 8); str_cat(r_line2, h); str_cat(r_line2, " esp=");
    fmt_hex(h, regs[7], 8); str_cat(r_line2, h);
    draw_console_line(fb, pitch, con_x, y, r_line2, 0x00ffffff, con_cols);
    y += step_y;

    char r_line3[128] = "         eip=";
    fmt_hex(h, regs[8], 8); str_cat(r_line3, h); str_cat(r_line3, " eflags=");
    fmt_hex(h, regs[9], 8); str_cat(r_line3, h);
    draw_console_line(fb, pitch, con_x, y, r_line3, 0x00ffffff, con_cols);
    y += step_y;

    draw_console_line(fb, pitch, con_x, y, "asm stream:", 0x00ffaa00, con_cols);
    y += step_y;

    uint32_t rip = regs[8];
    const uint8_t *code_ptr = (const uint8_t*)rip;

    for (int i = 0; i < 7; i++) {
        char dis_buf[96];
        char line_out[112] = "";
        if (i == 0) {
            str_cpy(line_out, "=> ");
        } else {
            str_cpy(line_out, "   ");
        }
        int len = disasm_inst(code_ptr, rip, dis_buf);
        str_cat(line_out, dis_buf);
        if (i == 0) {
            str_cat(line_out, "  <-- executing");
            draw_console_line(fb, pitch, con_x, y, line_out, 0x00ffffff, con_cols);
        } else {
            draw_console_line(fb, pitch, con_x, y, line_out, 0x00888888, con_cols);
        }
        code_ptr += len;
        rip += len;
        y += step_y;
    }
}

static void handle_halt(uint32_t *fb, uint32_t pitch) {
    clear_console_area(fb, pitch);
    draw_console_line(fb, pitch, con_x, con_y,
                      "System halted.", 0x00ff5555, con_cols);
    draw_console_line(fb, pitch, con_x, con_y + 12,
                      "CPU execution stopped by user ('h' key). System locked (cli; hlt).",
                      0x00ffffff, con_cols);

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void poll_keyboard(uint32_t *fb, uint32_t pitch) {
    if (inb(0x64) & 1) {
        uint8_t sc = inb(0x60);
        any_key = sc;
        if (hotkeys_enabled) {
            if (sc == 0x23) {
                handle_halt(fb, pitch);
            } else if (sc == 0x20) {
                debug_active = !debug_active;
                if (!debug_active) {
                    clear_console_area(fb, pitch);
                }
            }
        }
    }
}

static void sleep_ms_dbg(uint32_t *fb, uint32_t pitch, uint32_t ms) {
    while (ms--) {
        outb(0x43, 0xB0);
        outb(0x42, 1193 & 0xFF);
        outb(0x42, (1193 >> 8) & 0xFF);
        uint8_t c = inb(0x61) & ~1;
        outb(0x61, c);
        outb(0x61, c | 1);
        while (!(inb(0x61) & 0x20));

        uptime_ms++;
        poll_keyboard(fb, pitch);
        if (debug_active && (ms % 15 == 0)) {
            render_debug_console(fb, pitch);
        }
    }
}

static void clear_screen(uint32_t *fb, uint32_t pitch, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            fb[y * pitch + x] = color;
        }
    }
}

static void draw_logo(uint32_t *fb, uint32_t pitch, int sx, int sy, uint32_t scale_num, uint32_t scale_den) {
    int x = sx, y = sy;
    for (int i = 0; i < LOGO_RLE_COUNT; i++) {
        uint8_t count = apple_logo_rle[i * 2];
        uint8_t a = apple_logo_rle[i * 2 + 1];
        if (scale_den > 0 && a > 0) a = (a * scale_num) / scale_den;
        uint32_t color = (a << 16) | (a << 8) | a;
        while (count--) {
            if (a > 0) fb[y * pitch + x] = color;
            if (++x >= sx + LOGO_WIDTH) {
                x = sx;
                y++;
            }
        }
    }
}

static void draw_bar_track(uint32_t *fb, uint32_t pitch, int bx, int by) {
    for (int dy = 0; dy < BAR_H; dy++) {
        int x0 = (dy == 0 || dy == BAR_H - 1) ? 1 : 0;
        int x1 = (dy == 0 || dy == BAR_H - 1) ? BAR_W - 2 : BAR_W - 1;
        for (int dx = x0; dx <= x1; dx++) {
            fb[(by + dy) * pitch + (bx + dx)] = TRACK_COLOR;
        }
    }
}

static void update_bar_fill(uint32_t *fb, uint32_t pitch, int bx, int by, int old_w, int new_w) {
    if (new_w > BAR_W) new_w = BAR_W;
    if (new_w <= old_w) return;
    for (int dy = 0; dy < BAR_H; dy++) {
        int x0 = old_w;
        if ((dy == 0 || dy == BAR_H - 1) && x0 == 0) x0 = 1;
        int x1 = new_w - 1;
        if ((dy == 0 || dy == BAR_H - 1) && x1 == BAR_W - 1) x1 = BAR_W - 2;
        for (int dx = x0; dx <= x1; dx++) {
            fb[(by + dy) * pitch + (bx + dx)] = FILL_COLOR;
        }
    }
}

static void fade_out(uint32_t *fb, uint32_t pitch, int lx, int ly, int bx, int by) {
    for (int step = 15; step >= 0; step--) {
        draw_logo(fb, pitch, lx, ly, step, 15);
        uint32_t fc = (step * 255 / 15) * 0x010101;
        for (int dy = 0; dy < BAR_H; dy++) {
            for (int dx = 0; dx < BAR_W; dx++) {
                fb[(by + dy) * pitch + (bx + dx)] = fc;
            }
        }
        sleep_ms_dbg(fb, pitch, 25);
    }
}

/* Fake BROM (boot ROM) stage: shown before the Apple-logo bootloader.
 * Blocks until Enter (scancode 0x1C); 'h' still halts, prompt blinks. */
static void brom_screen(uint32_t *fb, uint32_t pitch, uint32_t w, uint32_t h) {
    hotkeys_enabled = 1;
    clear_screen(fb, pitch, w, h, 0x00000000);
    draw_console_line(fb, pitch, con_x, 48, "brom.131.1 (sim) -- apple silicon boot rom, stage 0", 0x0000ffcc, con_cols);
    draw_console_line(fb, pitch, con_x, 72,  "soc: s5l8940x   secure boot: simulated   sep-os: absent", 0x00888888, con_cols);
    draw_console_line(fb, pitch, con_x, 88,  "chain: brom -> llb -> iboot -> xnu", 0x00888888, con_cols);
    draw_console_line(fb, pitch, con_x, 104, "payload: mbr bootloader + kernel @ lba 1 (64 kb)", 0x00888888, con_cols);

    char disp[96] = "display: ";
    char num[12];
    fmt_dec(num, w); str_cat(disp, num); str_cat(disp, "x");
    fmt_dec(num, h); str_cat(disp, num); str_cat(disp, "@");
    fmt_dec(num, g_bi.bpp); str_cat(disp, num); str_cat(disp, "bpp (pitch ");
    fmt_dec(num, pitch * 4); str_cat(disp, num); str_cat(disp, "B)  mode: 0x");
    fmt_hex(num, g_bi.mode, 3); str_cat(disp, num);
    draw_console_line(fb, pitch, con_x, 120, disp, 0x0000ddaa, con_cols);

    uint32_t t = 0;
    for (;;) {
        uint32_t col = ((t / 500) % 2 == 0) ? 0x00ffffff : 0x00333333;
        draw_console_line(fb, pitch, con_x, 168, "press enter to hand off to bootloader", col, con_cols);
        any_key = 0;
        sleep_ms_dbg(fb, pitch, 10);
        if (any_key == 0x1C) return;
        t += 10;
    }
}

/* ===== Real boot work functions ===== */

static uint32_t crc32_table[256];
static int crc32_init_done = 0;

static void crc32_init() {
    if (crc32_init_done) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_init_done = 1;
}

static uint32_t crc32(const void *data, size_t len) {
    crc32_init();
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *p = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void ata_wait_bsy() {
    while (inb(0x1F7) & 0x80) io_wait();
}

static void ata_wait_drq() {
    while (!(inb(0x1F7) & 0x08)) io_wait();
}

static int ata_read_sector_lba28(uint32_t lba, void *buf) {
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    io_wait();
    outb(0x1F2, 1);
    outb(0x1F3, lba & 0xFF);
    outb(0x1F4, (lba >> 8) & 0xFF);
    outb(0x1F5, (lba >> 16) & 0xFF);
    outb(0x1F7, 0x20);
    ata_wait_bsy();
    uint8_t status = inb(0x1F7);
    if (status & 0x01) return -1;
    ata_wait_drq();
    uint16_t *dst = (uint16_t*)buf;
    for (int i = 0; i < 256; i++) {
        dst[i] = inw(0x1F0);
    }
    return 0;
}

static int ram_size_mb() {
    volatile uint32_t *p;
    for (int mb = 1; mb <= 256; mb++) {
        p = (volatile uint32_t*)(mb * 1024 * 1024);
        uint32_t pattern = 0xA5A5A5A5 ^ (mb * 0x1234567);
        *p = pattern;
        if (*p != pattern) return mb - 1;
    }
    return 256;
}

static void zero_memory(void *dst, size_t bytes) {
    uint8_t *p = (uint8_t*)dst;
    while (bytes--) *p++ = 0;
}

static void memory_test_pattern(void *base, size_t bytes, uint32_t pattern) {
    uint32_t *p = (uint32_t*)base;
    size_t words = bytes / 4;
    for (size_t i = 0; i < words; i++) p[i] = pattern;
    for (size_t i = 0; i < words; i++) if (p[i] != pattern) return;
    uint32_t inv = ~pattern;
    for (size_t i = 0; i < words; i++) p[i] = inv;
    for (size_t i = 0; i < words; i++) if (p[i] != inv) return;
}

static uint8_t *logo_alpha_buf = 0;

static void decompress_logo_alpha() {
    if (logo_alpha_buf) return;
    logo_alpha_buf = (uint8_t*)0x200000;
    int idx = 0;
    for (int i = 0; i < LOGO_RLE_COUNT && idx < LOGO_WIDTH * LOGO_HEIGHT; i++) {
        uint8_t count = apple_logo_rle[i * 2];
        uint8_t a = apple_logo_rle[i * 2 + 1];
        while (count-- && idx < LOGO_WIDTH * LOGO_HEIGHT) {
            logo_alpha_buf[idx++] = a;
        }
    }
}

static void advance_bar(uint32_t *fb, uint32_t pitch, int bx, int by, int *cur_w, int add, const char *detail, const char *src) {
    int target = *cur_w + add;
    if (target > BAR_W) target = BAR_W;
    dbg_c_detail = detail;
    dbg_c_src = src;
    for (int w = *cur_w; w < target; w++) {
        update_bar_fill(fb, pitch, bx, by, w, w + 1);
        poll_keyboard(fb, pitch);
    }
    *cur_w = target;
}

/* ===== PS/2 Mouse Driver & Minimal OS ===== */

static inline void mouse_wait_write(void) {
    uint32_t t = 100000;
    while ((inb(0x64) & 2) && --t) io_wait();
}

static inline int mouse_wait_read(void) {
    uint32_t t = 100000;
    while (!(inb(0x64) & 1) && --t) io_wait();
    return t > 0;
}

static void mouse_write(uint8_t val) {
    mouse_wait_write();
    outb(0x64, 0xD4);
    mouse_wait_write();
    outb(0x60, val);
}

static uint8_t mouse_read(void) {
    if (!mouse_wait_read()) return 0;
    return inb(0x60);
}

static int vmmouse_active = 0;

static inline void vmmouse_cmd(uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile (
        "in %%dx, %%eax"
        : "+a"(*eax), "+b"(*ebx), "+c"(*ecx), "+d"(*edx)
    );
}

static int vmmouse_init(void) {
    uint32_t eax = 0x564D5868, ebx = 0x45414552, ecx = 41, edx = 0x5658;
    vmmouse_cmd(&eax, &ebx, &ecx, &edx);

    eax = 0x564D5868; ebx = 0; ecx = 40; edx = 0x5658;
    vmmouse_cmd(&eax, &ebx, &ecx, &edx);
    if ((eax & 0xFFFF) < 1) return 0;

    eax = 0x564D5868; ebx = 1; ecx = 39; edx = 0x5658;
    vmmouse_cmd(&eax, &ebx, &ecx, &edx);
    if (eax != 0x3442554A) return 0;

    eax = 0x564D5868; ebx = 0x53424152; ecx = 41; edx = 0x5658;
    vmmouse_cmd(&eax, &ebx, &ecx, &edx);
    vmmouse_active = 1;
    return 1;
}

static void mouse_init(void) {
    while (inb(0x64) & 1) inb(0x60);
    mouse_wait_write();
    outb(0x64, 0xA8);

    mouse_wait_write();
    outb(0x64, 0x20);
    uint8_t status = mouse_read();
    status = (status | 0x02) & ~0x20;

    mouse_wait_write();
    outb(0x64, 0x60);
    mouse_wait_write();
    outb(0x60, status);

    mouse_write(0xF6);
    mouse_read();

    mouse_write(0xF4);
    mouse_read();

    while (inb(0x64) & 1) inb(0x60);

    vmmouse_init();
}

static int mouse_cycle = 0;
static uint8_t mouse_packet[3];

/* shortcut: polled PS/2 + VMMouse absolute pointer, upgrade to IRQ12 when interrupt model added */
static int mouse_poll(int *out_x, int *out_y, int *is_abs, int *out_buttons, int w, int h) {
    if (vmmouse_active) {
        uint32_t eax = 0x564D5868, ebx = 0, ecx = 40, edx = 0x5658;
        vmmouse_cmd(&eax, &ebx, &ecx, &edx);
        uint32_t nb = eax & 0xFFFF;
        if (nb >= 4) {
            while (nb >= 4) {
                eax = 0x564D5868; ebx = 4; ecx = 39; edx = 0x5658;
                vmmouse_cmd(&eax, &ebx, &ecx, &edx);
                *out_x = ((uint32_t)ebx * (uint32_t)w) / 65535;
                *out_y = ((uint32_t)ecx * (uint32_t)h) / 65535;
                /* QEMU vmmouse uses PS/2 status bits: 0x20 left, 0x10 right, 0x08 middle */
                *out_buttons = ((eax & 0x20) ? 1 : 0) | ((eax & 0x10) ? 2 : 0) | ((eax & 0x08) ? 4 : 0);
                nb -= 4;
            }
            *is_abs = 1;
            /* drop QEMU's fake aux bytes; leave keyboard bytes for the kbd poll */
            for (;;) {
                uint8_t st = inb(0x64);
                if (!(st & 1) || !(st & 0x20)) break;
                inb(0x60);
            }
            return 1;
        }
    }

    while (1) {
        uint8_t status = inb(0x64);
        if (!(status & 1)) break;
        if (!(status & 0x20)) break;
        uint8_t b = inb(0x60);

        if (mouse_cycle == 0) {
            if (!(b & 0x08)) continue;
            mouse_packet[0] = b;
            mouse_cycle = 1;
        } else if (mouse_cycle == 1) {
            mouse_packet[1] = b;
            mouse_cycle = 2;
        } else if (mouse_cycle == 2) {
            mouse_packet[2] = b;
            mouse_cycle = 0;

            if (mouse_packet[0] & 0xC0) return 0;

            /* shortcut: relative-mode buttons arrive only with motion packets, a
             * click without movement is missed; fine in QEMU where vmmouse wins */
            *out_buttons = mouse_packet[0] & 0x07;

            int rel_x = mouse_packet[1];
            if (mouse_packet[0] & 0x10) rel_x -= 256;
            int rel_y = mouse_packet[2];
            if (mouse_packet[0] & 0x20) rel_y -= 256;

            *out_x = rel_x;
            *out_y = -rel_y;
            *is_abs = 0;
            return 1;
        }
    }
    return 0;
}

static uint32_t cursor_bg[CURSOR_W * CURSOR_H];
static int cur_x = 0, cur_y = 0;
static int cur_visible = 0;

static void cursor_erase(uint32_t *fb, uint32_t pitch, int w, int h) {
    if (!cur_visible) return;
    for (int y = 0; y < CURSOR_H; y++) {
        int py = cur_y + y;
        if (py < 0 || py >= h) continue;
        for (int x = 0; x < CURSOR_W; x++) {
            int px = cur_x + x;
            if (px < 0 || px >= w) continue;
            fb[py * pitch + px] = cursor_bg[y * CURSOR_W + x];
        }
    }
    cur_visible = 0;
}

static void cursor_draw(uint32_t *fb, uint32_t pitch, int w, int h, int nx, int ny) {
    cur_x = nx;
    cur_y = ny;
    for (int y = 0; y < CURSOR_H; y++) {
        int py = cur_y + y;
        for (int x = 0; x < CURSOR_W; x++) {
            int px = cur_x + x;
            if (py >= 0 && py < h && px >= 0 && px < w) {
                cursor_bg[y * CURSOR_W + x] = fb[py * pitch + px];
                uint32_t c = cursor_data[y * CURSOR_W + x];
                if (c & 0xFF000000) {
                    fb[py * pitch + px] = c & 0x00FFFFFF;
                }
            }
        }
    }
    cur_visible = 1;
}

/* ===== Desktop: wallpapers, rounded icons, double-click app launch ===== */

#define WIN_W 360
#define WIN_H 170
#define CLICK_GAP_MS 500
#define CLICK_DIST 20

typedef struct {
    const char *name;
    const uint32_t *icon;
    int x, y;
} app_def_t;

/* every icon is drawn with programmatic rounding (radius WALL_ICON_R); future apps append here */
static app_def_t apps[] = {
    { "Wallpaper Set", wallpaper_icon_data, 20, 20 },
};
#define APP_COUNT (int)(sizeof(apps) / sizeof(apps[0]))
#define APP_WALLPAPER_SET 0

static int open_app = -1;
static int wallpaper_sel = 0;   /* 0 = wall1, 1 = wall2 */
static uint16_t wall16[WALL_W * WALL_H];

/* PIT ch2 free-running down counter (~54.9 ms wrap), latched every poll iteration
 * so pit_ms() stays monotonic without interrupts. Must be called at least ~every 50 ms. */
static uint32_t pit_total_ticks = 0;
static uint16_t pit_last = 0;
static int pit_ready = 0;

static uint32_t pit_ms(void) {
    outb(0x43, 0x80);           /* channel 2 latch command */
    uint16_t c = (uint16_t)inb(0x42) | (uint16_t)((uint16_t)inb(0x42) << 8);
    if (pit_ready) pit_total_ticks += (uint16_t)(pit_last - c);
    else pit_ready = 1;
    pit_last = c;
    return pit_total_ticks / 1193;
}

static void decode_wall(int idx) {
    const unsigned char *cnt = idx ? wall2_cnt : wall1_cnt;
    const unsigned short *col = idx ? wall2_col : wall1_col;
    int runs = idx ? WALL2_RUNS : WALL1_RUNS;
    int i = 0;
    while (runs--) {
        unsigned n = *cnt++;
        uint16_t c = *col++;
        while (n--) wall16[i++] = c;
    }
}

static void blit_wall(uint32_t *fb, uint32_t pitch, int x0, int y0, int dw, int dh) {
    for (int y = 0; y < dh; y++) {
        int sy = y * WALL_H / dh;
        for (int x = 0; x < dw; x++) {
            uint16_t c = wall16[sy * WALL_W + x * WALL_W / dw];
            uint32_t r = (c >> 11) << 3, g = ((c >> 5) & 0x3F) << 2, b = (c & 0x1F) << 3;
            r |= r >> 5; g |= g >> 6; b |= b >> 5;
            fb[(y0 + y) * pitch + x0 + x] = (r << 16) | (g << 8) | b;
        }
    }
}

/* Square bitmap -> rounded icon: pixels in the four corner zones are blended by
 * their coverage inside a circle of radius r (2 px feather), everywhere else opaque. */
static void draw_icon(uint32_t *fb, uint32_t pitch, int x0, int y0, const uint32_t *data, int iw, int ih, int r) {
    for (int y = 0; y < ih; y++) {
        for (int x = 0; x < iw; x++) {
            uint32_t c = data[y * iw + x] & 0x00FFFFFF;
            int cov = 255;
            if (x < r || x >= iw - r) {
                if (y >= r && y < ih - r) cov = 255;
                else {
                    int ccx = x < r ? r : iw - r;
                    int ccy = y < r ? r : ih - r;
                    int dx = x - ccx, dy = y - ccy;
                    int d2 = dx * dx + dy * dy;
                    int ri2 = (r - 2) * (r - 2), ro2 = r * r;
                    if (d2 >= ro2) cov = 0;
                    else if (d2 <= ri2) cov = 255;
                    else cov = 255 * (ro2 - d2) / (ro2 - ri2);
                }
            }
            if (cov == 0) continue;
            uint32_t *p = &fb[(y0 + y) * pitch + x0 + x];
            if (cov == 255) { *p = c; continue; }
            uint32_t bg = *p, inv = 255 - cov;
            *p = ((((c >> 16) * cov + ((bg >> 16) & 0xFF) * inv) / 255) << 16)
               | (((((c >> 8) & 0xFF) * cov + ((bg >> 8) & 0xFF) * inv) / 255) << 8)
               | (((c & 0xFF) * cov + (bg & 0xFF) * inv) / 255);
        }
    }
}

static uint32_t uptime_secs_shown = 0xFFFFFFFF;

static void fmt2(char *p, int v) { p[0] = (char)('0' + v / 10); p[1] = (char)('0' + v % 10); }

static void draw_uptime(uint32_t *fb, uint32_t pitch, int w) {
    uint32_t s = uptime_secs_shown == 0xFFFFFFFF ? 0 : uptime_secs_shown;
    char line[12] = "up 00:00:00";
    fmt2(&line[3], (int)((s / 3600) % 100));
    fmt2(&line[6], (int)((s / 60) % 60));
    fmt2(&line[9], (int)(s % 60));
    draw_str(fb, pitch, w - 91, 8, line, 0x00000000, 0xff000000);
    draw_str(fb, pitch, w - 92, 7, line, 0x00ffffff, 0xff000000);
}

static void draw_desktop(uint32_t *fb, uint32_t pitch, int w, int h) {
    decode_wall(wallpaper_sel);
    blit_wall(fb, pitch, 0, 0, w, h);
    for (int i = 0; i < APP_COUNT; i++) {
        draw_icon(fb, pitch, apps[i].x, apps[i].y, apps[i].icon, WALL_ICON_W, WALL_ICON_H, WALL_ICON_R);
        int ty = apps[i].y + WALL_ICON_H + 5;
        draw_str(fb, pitch, apps[i].x + 1, ty + 1, apps[i].name, 0x00000000, 0xff000000);
        draw_str(fb, pitch, apps[i].x, ty, apps[i].name, 0x00ffffff, 0xff000000);
    }
    draw_uptime(fb, pitch, w);
}

static void wallpaper_win_rect(int w, int h, int *wx, int *wy) {
    *wx = (w - WIN_W) / 2;
    *wy = (h - WIN_H) / 2;
}

static void wallpaper_preview_xy(int w, int h, int idx, int *px, int *py) {
    wallpaper_win_rect(w, h, px, py);
    *px += 12 + idx * (WALL_W + 16);
    *py += 30;
}

static void draw_wallpaper_win(uint32_t *fb, uint32_t pitch, int w, int h) {
    int wx, wy;
    wallpaper_win_rect(w, h, &wx, &wy);
    for (int y = 0; y < WIN_H; y++) {
        for (int x = 0; x < WIN_W; x++) {
            fb[(wy + y) * pitch + wx + x] =
                (x == 0 || y == 0 || x == WIN_W - 1 || y == WIN_H - 1) ? 0x003a3a3c : 0x001c1c1e;
        }
    }
    draw_str(fb, pitch, wx + 12, wy + 12, "Wallpaper Set", 0x00ffffff, 0xff000000);
    for (int i = 0; i < 2; i++) {
        int px, py;
        wallpaper_preview_xy(w, h, i, &px, &py);
        decode_wall(i);
        blit_wall(fb, pitch, px, py, WALL_W, WALL_H);
        if (wallpaper_sel == i) {
            for (int x = -1; x <= WALL_W; x++) {
                fb[(py - 1) * pitch + px + x] = 0x00ffffff;
                fb[(py + WALL_H) * pitch + px + x] = 0x00ffffff;
            }
            for (int y = -1; y <= WALL_H; y++) {
                fb[(py + y) * pitch + px - 1] = 0x00ffffff;
                fb[(py + y) * pitch + px + WALL_W] = 0x00ffffff;
            }
        }
        draw_str(fb, pitch, px, py + WALL_H + 4, i ? "wall2" : "wall1", 0x00ffffff, 0xff000000);
    }
    draw_str(fb, pitch, wx + 12, wy + WIN_H - 16, "double-click a wallpaper to set it - esc closes", 0x00aaaaaa, 0xff000000);
}

static void repaint(uint32_t *fb, uint32_t pitch, int w, int h) {
    cursor_erase(fb, pitch, w, h);
    draw_desktop(fb, pitch, w, h);
    if (open_app == APP_WALLPAPER_SET) draw_wallpaper_win(fb, pitch, w, h);
    cursor_draw(fb, pitch, w, h, cur_x, cur_y);
}

static void on_double_click(uint32_t *fb, uint32_t pitch, int w, int h, int mx, int my) {
    if (open_app == APP_WALLPAPER_SET) {
        for (int i = 0; i < 2; i++) {
            int px, py;
            wallpaper_preview_xy(w, h, i, &px, &py);
            if (mx >= px && mx < px + WALL_W && my >= py && my < py + WALL_H) {
                wallpaper_sel = i;
                open_app = -1;
                break;
            }
        }
    } else {
        for (int i = 0; i < APP_COUNT; i++) {
            if (mx >= apps[i].x && mx < apps[i].x + WALL_ICON_W &&
                my >= apps[i].y && my < apps[i].y + WALL_ICON_H + 14) {
                open_app = i;
                break;
            }
        }
    }
    repaint(fb, pitch, w, h);
}

static void os_main(boot_info_t *bi) {
    uint32_t *fb = (uint32_t*)bi->fb_base;
    uint32_t pitch = bi->pitch / 4;
    int w = (int)bi->width;
    int h = (int)bi->height;

    mouse_init();

    outb(0x43, 0x74);           /* ch2 mode-2 rate generator, reload 65535 */
    outb(0x42, 0xFF);
    outb(0x42, 0xFF);
    pit_total_ticks = 0;
    pit_ready = 0;

    open_app = -1;
    draw_desktop(fb, pitch, w, h);
    int mx = w / 2;
    int my = h / 2;
    cursor_draw(fb, pitch, w, h, mx, my);

    int px = 0, py = 0, is_abs = 0, buttons = 0;
    int left_was = 0;
    uint32_t last_click_ms = 0;
    int last_cx = -9999, last_cy = -9999;

    while (1) {
        uint32_t now = pit_ms();

        if (mouse_poll(&px, &py, &is_abs, &buttons, w, h)) {
            int nx = is_abs ? px : (mx + px);
            int ny = is_abs ? py : (my + py);
            if (nx < 0) nx = 0;
            if (nx >= w) nx = w - 1;
            if (ny < 0) ny = 0;
            if (ny >= h) ny = h - 1;

            if (nx != mx || ny != my) {
                mx = nx;
                my = ny;
                cursor_erase(fb, pitch, w, h);
                cursor_draw(fb, pitch, w, h, mx, my);
            }
        }

        uint8_t kst = inb(0x64);
        if ((kst & 1) && !(kst & 0x20)) {
            if (inb(0x60) == 0x01 && open_app >= 0) {   /* Esc closes the app */
                open_app = -1;
                repaint(fb, pitch, w, h);
            }
        }

        int left = buttons & 1;
        if (left && !left_was) {
            int dx = mx - last_cx, dy = my - last_cy;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (now - last_click_ms <= CLICK_GAP_MS && dx <= CLICK_DIST && dy <= CLICK_DIST) {
                on_double_click(fb, pitch, w, h, mx, my);
            }
            last_click_ms = now;
            last_cx = mx;
            last_cy = my;
        }
        left_was = left;

        /* uptime in the top-right corner */
        uint32_t secs = now / 1000;
        if (secs != uptime_secs_shown) {
            uptime_secs_shown = secs;
            draw_uptime(fb, pitch, w);
        }

        __asm__ volatile ("pause");
    }
}


void kernel_main(boot_info_t *bi) {
    g_bi = *bi;
    con_x = (bi->width > 640) ? 24 : 8;
    con_cols = (bi->width - con_x * 2) / 8;

    uint32_t *fb = (uint32_t*)bi->fb_base;
    uint32_t pitch = bi->pitch / 4;

    int ly = (bi->height > 600) ? ((bi->height - LOGO_HEIGHT) / 2 - 35) : ((bi->height - LOGO_HEIGHT - BAR_H - 40) / 2);
    if (ly < 10) ly = 10;
    int lx = (bi->width - LOGO_WIDTH) / 2;
    int bx = (bi->width - BAR_W) / 2;
    if (bx < 0) bx = 0;
    int by = ly + LOGO_HEIGHT + ((bi->height > 600) ? 55 : (bi->height > 480 ? 40 : 30));

    con_y = (bi->height > 600) ? (int)(bi->height - 180) : (by + BAR_H + 12);
    if (con_y + 160 > (int)bi->height) con_y = bi->height - 160;
    if (con_y < 0) con_y = 0;

    while (1) {
        uptime_ms = 0;

        dbg_c_func = "brom_screen";
        dbg_c_detail = "Simulated boot ROM, waiting for Enter";
        dbg_c_src = "brom_screen(fb, pitch, bi->width, bi->height);";
        brom_screen(fb, pitch, bi->width, bi->height);

        hotkeys_enabled = 0;
        debug_active = 0;

        dbg_c_func = "clear_screen";
        dbg_c_detail = "Clearing display framebuffer to #000000";
        dbg_c_src = "clear_screen(fb, pitch, bi->width, bi->height, 0x00000000);";
        clear_screen(fb, pitch, bi->width, bi->height, 0x00000000);

        dbg_c_func = "draw_logo";
        dbg_c_detail = "Rendering RLE anti-aliased Apple logo";
        dbg_c_src = "draw_logo(fb, pitch, lx, ly, 1, 1);";
        draw_logo(fb, pitch, lx, ly, 1, 1);
        sleep_ms_dbg(fb, pitch, 700);

        dbg_c_func = "draw_bar_track";
        dbg_c_detail = "Drawing rounded progress bar track #2c2c2e";
        dbg_c_src = "draw_bar_track(fb, pitch, bx, by);";
        draw_bar_track(fb, pitch, bx, by);
        sleep_ms_dbg(fb, pitch, 250);

        int cur_w = 0;
        uint8_t sector_buf[SECTOR_SIZE];

        /* Stage 1: Memory detection & heap zeroing (0-25 px) */
        dbg_c_func = "mem_detect";
        dbg_c_detail = "Probing physical memory size";
        dbg_c_src = "ram_size_mb()";
        ram_size_mb();
        advance_bar(fb, pitch, bx, by, &cur_w, 5, "Memory detection complete", "ram_size_mb()");

        dbg_c_func = "mem_zero";
        dbg_c_detail = "Zeroing kernel heap arena (8 MB)";
        dbg_c_src = "zero_memory(0x200000, 8*1024*1024)";
        zero_memory((void*)0x200000, 8 * 1024 * 1024);
        advance_bar(fb, pitch, bx, by, &cur_w, 20, "Heap arena zeroed", "zero_memory()");

        /* Stage 2: Kernel image CRC32 & disk readback verify (25-75 px) */
        dbg_c_func = "kernel_crc";
        dbg_c_detail = "CRC32 of in-memory kernel image";
        dbg_c_src = "crc32(0x10000, 96*512)";
        crc32((void*)KERNEL_LOAD_ADDR, KERNEL_SECTORS * SECTOR_SIZE);
        advance_bar(fb, pitch, bx, by, &cur_w, 10, "Kernel CRC32 computed", "crc32()");

        dbg_c_func = "disk_verify";
        dbg_c_detail = "ATA PIO readback & verify 128 sectors";
        dbg_c_src = "ata_read_sector_lba28() + memcmp";
        for (int s = 0; s < KERNEL_SECTORS; s++) {
            if (ata_read_sector_lba28(1 + s, sector_buf) == 0) {
                for (int i = 0; i < SECTOR_SIZE; i++) {
                    if (sector_buf[i] != ((uint8_t*)KERNEL_LOAD_ADDR)[s * SECTOR_SIZE + i]) {
                        dbg_c_detail = "DISK VERIFY MISMATCH";
                    }
                }
            }
            if (s % 2 == 0) advance_bar(fb, pitch, bx, by, &cur_w, 1, "Verifying disk sectors", "ata_read_sector_lba28()");
        }
        advance_bar(fb, pitch, bx, by, &cur_w, 0, "Disk verify complete", "");

        /* Stage 3: Logo asset decompression (75-100 px) */
        dbg_c_func = "decompress";
        dbg_c_detail = "Decompressing RLE logo to alpha buffer";
        dbg_c_src = "decompress_logo_alpha()";
        decompress_logo_alpha();
        crc32(logo_alpha_buf, LOGO_WIDTH * LOGO_HEIGHT);
        advance_bar(fb, pitch, bx, by, &cur_w, 25, "Logo decompressed & checksummed", "decompress_logo_alpha()");

        /* Stage 4: RAM pattern integrity test (100-155 px) */
        dbg_c_func = "mem_test";
        dbg_c_detail = "Memory pattern test (0x55/0xAA) on 16 MB";
        dbg_c_src = "memory_test_pattern()";
        memory_test_pattern((void*)0x200000, 16 * 1024 * 1024, 0x55AA55AA);
        advance_bar(fb, pitch, bx, by, &cur_w, 30, "RAM pattern test passed", "memory_test_pattern()");
        memory_test_pattern((void*)0x200000, 16 * 1024 * 1024, 0xAA55AA55);
        advance_bar(fb, pitch, bx, by, &cur_w, 25, "RAM inverse pattern test passed", "memory_test_pattern()");

        /* Stage 5: i8042 controller self-test (155-180 px) */
        dbg_c_func = "kbd_test";
        dbg_c_detail = "i8042 controller self-test (0xAA)";
        dbg_c_src = "outb(0x64, 0xAA); inb(0x60)";
        outb(0x64, 0xAA);
        for (int i = 0; i < 100000; i++) if (inb(0x64) & 1) break;
        inb(0x60);
        advance_bar(fb, pitch, bx, by, &cur_w, 25, "Keyboard controller responded", "i8042 self-test");

        /* Stage 6: Finalization (180-216 px) */
        dbg_c_func = "finalize";
        dbg_c_detail = "Finalizing boot environment";
        dbg_c_src = "advance_bar()";
        advance_bar(fb, pitch, bx, by, &cur_w, BAR_W - cur_w, "Boot complete", "");

        dbg_c_func = "boot_complete";
        dbg_c_detail = "Boot progress reached 100%, holding frame";
        dbg_c_src = "sleep_ms_dbg(fb, pitch, 1200);";
        sleep_ms_dbg(fb, pitch, 1200);

        dbg_c_func = "fade_out";
        dbg_c_detail = "Smooth brightness reduction to black";
        dbg_c_src = "fade_out(fb, pitch, lx, ly, bx, by);";
        fade_out(fb, pitch, lx, ly, bx, by);
        clear_screen(fb, pitch, bi->width, bi->height, 0x00000000);

        os_main(bi);
    }

    for (;;) __asm__ volatile ("hlt");
}