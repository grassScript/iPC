#pragma once
#include "font8x8.h"

typedef unsigned int   uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char  uint8_t;
typedef signed char    int8_t;
typedef signed int     int32_t;

static const char *r32[8] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
static const char *r8[8]  = {"al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"};
static const char *jcc_n[16] = {
    "jo", "jno", "jb", "jae", "je", "jne", "jbe", "ja",
    "js", "jns", "jp", "jnp", "jl", "jge", "jle", "jg"
};
static const char *ari_n[8] = {"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"};
static const char *shift_n[8] = {"rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar"};

static inline int str_len(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}

static inline void str_cat(char *dst, const char *src) {
    while (*dst) dst++;
    while ((*dst++ = *src++));
}

static inline void str_cpy(char *dst, const char *src) {
    while ((*dst++ = *src++));
}

static inline int fmt_hex(char *b, uint32_t val, int digits) {
    static const char h[] = "0123456789abcdef";
    for (int i = digits - 1; i >= 0; i--) {
        b[i] = h[val & 0xf];
        val >>= 4;
    }
    b[digits] = 0;
    return digits;
}

static inline int fmt_dec(char *b, uint32_t val) {
    if (val == 0) { b[0] = '0'; b[1] = 0; return 1; }
    char tmp[12]; int len = 0;
    while (val > 0) { tmp[len++] = '0' + (val % 10); val /= 10; }
    for (int i = 0; i < len; i++) b[i] = tmp[len - 1 - i];
    b[len] = 0;
    return len;
}

static inline int decode_rm(const uint8_t *p, int is_r8, char *buf) {
    uint8_t b = *p++;
    int mod = b >> 6;
    int rm = b & 7;
    int len = 1;
    if (mod == 3) {
        str_cpy(buf, is_r8 ? r8[rm] : r32[rm]);
        return len;
    }
    char base[32] = "";
    if (rm == 4) {
        uint8_t sib = *p++; len++;
        int ss = sib >> 6;
        int idx = (sib >> 3) & 7;
        int b_reg = sib & 7;
        if (b_reg == 5 && mod == 0) {
            uint32_t disp = *(const uint32_t*)p; p += 4; len += 4;
            char h[12]; fmt_hex(h, disp, 8);
            if (idx == 4) { str_cpy(base, "0x"); str_cat(base, h); }
            else {
                str_cpy(base, r32[idx]); str_cat(base, "*");
                char d[4]; fmt_dec(d, 1<<ss); str_cat(base, d);
                str_cat(base, "+0x"); str_cat(base, h);
            }
        } else {
            if (idx == 4) str_cpy(base, r32[b_reg]);
            else {
                str_cpy(base, r32[b_reg]); str_cat(base, "+");
                str_cat(base, r32[idx]); str_cat(base, "*");
                char d[4]; fmt_dec(d, 1<<ss); str_cat(base, d);
            }
        }
    } else if (mod == 0 && rm == 5) {
        uint32_t disp = *(const uint32_t*)p; p += 4; len += 4;
        char h[12]; fmt_hex(h, disp, 8);
        str_cpy(base, "0x"); str_cat(base, h);
    } else {
        str_cpy(base, r32[rm]);
    }

    if (mod == 1) {
        int8_t disp8 = (int8_t)*p++; len++;
        str_cpy(buf, "["); str_cat(buf, base);
        if (disp8 >= 0) {
            str_cat(buf, "+0x"); char h[4]; fmt_hex(h, disp8, 2); str_cat(buf, h);
        } else {
            str_cat(buf, "-0x"); char h[4]; fmt_hex(h, -disp8, 2); str_cat(buf, h);
        }
        str_cat(buf, "]");
    } else if (mod == 2) {
        int32_t disp32 = *(const int32_t*)p; p += 4; len += 4;
        str_cpy(buf, "["); str_cat(buf, base);
        if (disp32 >= 0) {
            str_cat(buf, "+0x"); char h[10]; fmt_hex(h, disp32, 8); str_cat(buf, h);
        } else {
            str_cat(buf, "-0x"); char h[10]; fmt_hex(h, -disp32, 8); str_cat(buf, h);
        }
        str_cat(buf, "]");
    } else {
        str_cpy(buf, "["); str_cat(buf, base); str_cat(buf, "]");
    }
    return len;
}

static inline int disasm_inst(const uint8_t *p, uint32_t addr, char *out) {
    const uint8_t *start = p;
    char mnem[16] = "", ops[48] = "";
    uint8_t b = *p++;
    
    if (b >= 0x50 && b <= 0x57) { str_cpy(mnem, "push"); str_cpy(ops, r32[b - 0x50]); }
    else if (b >= 0x58 && b <= 0x5f) { str_cpy(mnem, "pop"); str_cpy(ops, r32[b - 0x58]); }
    else if (b == 0x68) {
        uint32_t imm = *(const uint32_t*)p; p += 4;
        char h[12]; fmt_hex(h, imm, 8);
        str_cpy(mnem, "push"); str_cpy(ops, "0x"); str_cat(ops, h);
    }
    else if (b == 0x6a) {
        uint8_t imm = *p++;
        char h[4]; fmt_hex(h, imm, 2);
        str_cpy(mnem, "push"); str_cpy(ops, "0x"); str_cat(ops, h);
    }
    else if (b == 0x69 || b == 0x6b) {
        char rm[32]; int l = decode_rm(p, 0, rm);
        int reg = (*p >> 3) & 7; p += l;
        str_cpy(mnem, "imul"); str_cpy(ops, r32[reg]); str_cat(ops, ", "); str_cat(ops, rm);
        if (b == 0x69) {
            uint32_t imm = *(const uint32_t*)p; p += 4;
            char h[12]; fmt_hex(h, imm, 8);
            str_cat(ops, ", 0x"); str_cat(ops, h);
        } else {
            int8_t imm = (int8_t)*p++;
            char h[6]; fmt_hex(h, (uint8_t)imm, 2);
            str_cat(ops, ", 0x"); str_cat(ops, h);
        }
    }
    else if (b >= 0xb8 && b <= 0xbf) {
        uint32_t imm = *(const uint32_t*)p; p += 4;
        char h[12]; fmt_hex(h, imm, 8);
        str_cpy(mnem, "mov"); str_cpy(ops, r32[b - 0xb8]); str_cat(ops, ", 0x"); str_cat(ops, h);
    }
    else if (b >= 0xb0 && b <= 0xb7) {
        uint8_t imm = *p++;
        char h[4]; fmt_hex(h, imm, 2);
        str_cpy(mnem, "mov"); str_cpy(ops, r8[b - 0xb0]); str_cat(ops, ", 0x"); str_cat(ops, h);
    }
    else if (b >= 0x70 && b <= 0x7f) {
        int8_t rel = (int8_t)*p++;
        char h[12]; fmt_hex(h, addr + (p - start) + rel, 8);
        str_cpy(mnem, jcc_n[b - 0x70]); str_cpy(ops, "0x"); str_cat(ops, h);
    }
    else if (b == 0xeb) {
        int8_t rel = (int8_t)*p++;
        char h[12]; fmt_hex(h, addr + (p - start) + rel, 8);
        str_cpy(mnem, "jmp"); str_cpy(ops, "0x"); str_cat(ops, h);
    }
    else if (b == 0xe9) {
        int32_t rel = *(const int32_t*)p; p += 4;
        char h[12]; fmt_hex(h, addr + (p - start) + rel, 8);
        str_cpy(mnem, "jmp"); str_cpy(ops, "0x"); str_cat(ops, h);
    }
    else if (b == 0xe8) {
        int32_t rel = *(const int32_t*)p; p += 4;
        char h[12]; fmt_hex(h, addr + (p - start) + rel, 8);
        str_cpy(mnem, "call"); str_cpy(ops, "0x"); str_cat(ops, h);
    }
    else if (b == 0xc3) { str_cpy(mnem, "ret"); }
    else if (b == 0xf4) { str_cpy(mnem, "hlt"); }
    else if (b == 0x90) { str_cpy(mnem, "nop"); }
    else if (b == 0x89) {
        char rm[32]; int l = decode_rm(p, 0, rm);
        int reg = (*p >> 3) & 7; p += l;
        str_cpy(mnem, "mov"); str_cpy(ops, rm); str_cat(ops, ", "); str_cat(ops, r32[reg]);
    }
    else if (b == 0x8b) {
        char rm[32]; int l = decode_rm(p, 0, rm);
        int reg = (*p >> 3) & 7; p += l;
        str_cpy(mnem, "mov"); str_cpy(ops, r32[reg]); str_cat(ops, ", "); str_cat(ops, rm);
    }
    else if (b == 0x88) {
        char rm[32]; int l = decode_rm(p, 1, rm);
        int reg = (*p >> 3) & 7; p += l;
        str_cpy(mnem, "mov"); str_cpy(ops, rm); str_cat(ops, ", "); str_cat(ops, r8[reg]);
    }
    else if (b == 0x8a) {
        char rm[32]; int l = decode_rm(p, 1, rm);
        int reg = (*p >> 3) & 7; p += l;
        str_cpy(mnem, "mov"); str_cpy(ops, r8[reg]); str_cat(ops, ", "); str_cat(ops, rm);
    }
    else if (b == 0xc7) {
        char rm[32]; int l = decode_rm(p, 0, rm); p += l;
        uint32_t imm = *(const uint32_t*)p; p += 4;
        char h[12]; fmt_hex(h, imm, 8);
        str_cpy(mnem, "mov"); str_cpy(ops, rm); str_cat(ops, ", 0x"); str_cat(ops, h);
    }
    else if (b == 0xc1) {
        int reg = (*p >> 3) & 7;
        char rm[32]; int l = decode_rm(p, 0, rm); p += l;
        uint8_t imm = *p++;
        char h[6]; fmt_hex(h, imm, 2);
        str_cpy(mnem, shift_n[reg]); str_cpy(ops, rm); str_cat(ops, ", 0x"); str_cat(ops, h);
    }
    else if (b == 0x8d) {
        char rm[32]; int l = decode_rm(p, 0, rm);
        int reg = (*p >> 3) & 7; p += l;
        str_cpy(mnem, "lea"); str_cpy(ops, r32[reg]); str_cat(ops, ", "); str_cat(ops, rm);
    }
    else if (b == 0x83) {
        int reg = (*p >> 3) & 7;
        char rm[32]; int l = decode_rm(p, 0, rm); p += l;
        int8_t imm = (int8_t)*p++;
        char h[6]; fmt_hex(h, (uint8_t)imm, 2);
        str_cpy(mnem, ari_n[reg]); str_cpy(ops, rm); str_cat(ops, ", 0x"); str_cat(ops, h);
    }
    else if (b == 0x31) {
        char rm[32]; int l = decode_rm(p, 0, rm);
        int reg = (*p >> 3) & 7; p += l;
        str_cpy(mnem, "xor"); str_cpy(ops, rm); str_cat(ops, ", "); str_cat(ops, r32[reg]);
    }
    else if (b == 0x01) {
        char rm[32]; int l = decode_rm(p, 0, rm);
        int reg = (*p >> 3) & 7; p += l;
        str_cpy(mnem, "add"); str_cpy(ops, rm); str_cat(ops, ", "); str_cat(ops, r32[reg]);
    }
    else if (b == 0x29) {
        char rm[32]; int l = decode_rm(p, 0, rm);
        int reg = (*p >> 3) & 7; p += l;
        str_cpy(mnem, "sub"); str_cpy(ops, rm); str_cat(ops, ", "); str_cat(ops, r32[reg]);
    }
    else if (b == 0x39 || b == 0x3b) {
        char rm[32]; int l = decode_rm(p, 0, rm);
        int reg = (*p >> 3) & 7; p += l;
        str_cpy(mnem, "cmp");
        if (b == 0x39) { str_cpy(ops, rm); str_cat(ops, ", "); str_cat(ops, r32[reg]); }
        else { str_cpy(ops, r32[reg]); str_cat(ops, ", "); str_cat(ops, rm); }
    }
    else if (b == 0x84 || b == 0x85) {
        char rm[32]; int l = decode_rm(p, b == 0x84, rm);
        int reg = (*p >> 3) & 7; p += l;
        str_cpy(mnem, "test"); str_cpy(ops, rm); str_cat(ops, ", ");
        str_cat(ops, (b == 0x84) ? r8[reg] : r32[reg]);
    }
    else if (b == 0x0f && *p == 0xb6) {
        p++;
        char rm[32]; int l = decode_rm(p, 1, rm);
        int reg = (*p >> 3) & 7; p += l;
        str_cpy(mnem, "movzx"); str_cpy(ops, r32[reg]); str_cat(ops, ", byte "); str_cat(ops, rm);
    }
    else if (b == 0x0f && *p == 0xb7) {
        p++;
        char rm[32]; int l = decode_rm(p, 0, rm);
        int reg = (*p >> 3) & 7; p += l;
        str_cpy(mnem, "movzx"); str_cpy(ops, r32[reg]); str_cat(ops, ", word "); str_cat(ops, rm);
    }
    else if (b == 0x0f && *p == 0xaf) {
        p++;
        char rm[32]; int l = decode_rm(p, 0, rm);
        int reg = (*p >> 3) & 7; p += l;
        str_cpy(mnem, "imul"); str_cpy(ops, r32[reg]); str_cat(ops, ", "); str_cat(ops, rm);
    }
    else if (b == 0xe4) {
        uint8_t port = *p++; char h[4]; fmt_hex(h, port, 2);
        str_cpy(mnem, "in"); str_cpy(ops, "al, 0x"); str_cat(ops, h);
    }
    else if (b == 0xe6) {
        uint8_t port = *p++; char h[4]; fmt_hex(h, port, 2);
        str_cpy(mnem, "out"); str_cpy(ops, "0x"); str_cat(ops, h); str_cat(ops, ", al");
    }
    else if (b == 0xee) { str_cpy(mnem, "out"); str_cpy(ops, "dx, al"); }
    else if (b == 0xec) { str_cpy(mnem, "in"); str_cpy(ops, "al, dx"); }
    else {
        char h[4]; fmt_hex(h, b, 2);
        str_cpy(mnem, "db"); str_cpy(ops, "0x"); str_cat(ops, h);
    }

    int total_len = p - start;
    char hex[24] = "";
    for (int i = 0; i < total_len && i < 4; i++) {
        char h[4]; fmt_hex(h, start[i], 2);
        str_cat(hex, h); str_cat(hex, " ");
    }
    char addr_str[12]; fmt_hex(addr_str, addr, 8);
    str_cpy(out, "0x"); str_cat(out, addr_str); str_cat(out, ": ");
    while (str_len(hex) < 12) str_cat(hex, " ");
    str_cat(out, hex); str_cat(out, " ");
    while (str_len(mnem) < 7) str_cat(mnem, " ");
    str_cat(out, mnem); str_cat(out, " ");
    str_cat(out, ops);
    return total_len;
}

static inline void get_cpu_state(uint32_t regs[10]) {
    __asm__ volatile (
        "mov %%eax, %0\n"
        "mov %%ebx, %1\n"
        "mov %%ecx, %2\n"
        "mov %%edx, %3\n"
        "mov %%esi, %4\n"
        "mov %%edi, %5\n"
        "mov %%ebp, %6\n"
        "mov %%esp, %7\n"
        "pushfl\n"
        "pop %%eax\n"
        "mov %%eax, %9\n"
        "call 1f\n"
        "1: pop %%eax\n"
        "mov %%eax, %8\n"
        : "=m"(regs[0]), "=m"(regs[1]), "=m"(regs[2]), "=m"(regs[3]),
          "=m"(regs[4]), "=m"(regs[5]), "=m"(regs[6]), "=m"(regs[7]),
          "=m"(regs[8]), "=m"(regs[9])
        :
        : "eax"
    );
}

static inline void draw_char(uint32_t *fb, uint32_t pitch, int x, int y, char c, uint32_t fg, uint32_t bg) {
    if (c < 32 || c > 126) c = ' ';
    const uint8_t *glyph = font8x8[c - 32];
    for (int dy = 0; dy < 8; dy++) {
        uint8_t row = glyph[dy];
        for (int dx = 0; dx < 8; dx++) {
            uint32_t col = (row & (0x80 >> dx)) ? fg : bg;
            if (col != 0xff000000) {
                fb[(y + dy) * pitch + (x + dx)] = col;
            }
        }
    }
}

static inline void draw_str(uint32_t *fb, uint32_t pitch, int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    while (*s) {
        draw_char(fb, pitch, x, y, *s++, fg, bg);
        x += 8;
    }
}
