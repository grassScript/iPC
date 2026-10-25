#!/usr/bin/env python3
import sys, os
from PIL import Image, ImageFont, ImageDraw
import numpy as np

def generate_logo(src_path, dst_path, target_h=140):
    img = Image.open(src_path).convert('RGBA')
    arr = np.array(img)
    y_idx, x_idx = np.where(arr[:, :, 3] > 0)
    cropped = img.crop((x_idx.min(), y_idx.min(), x_idx.max() + 1, y_idx.max() + 1))

    target_w = int(cropped.width * (target_h / cropped.height))
    resized = cropped.resize((target_w, target_h), Image.Resampling.LANCZOS)
    alpha = np.array(resized)[:, :, 3]

    runs = []
    curr_val, curr_len = alpha[0, 0], 0
    for val in alpha.flatten():
        if val == curr_val and curr_len < 255:
            curr_len += 1
        else:
            if curr_len > 0:
                runs.append((curr_len, curr_val))
            curr_val, curr_len = val, 1
    runs.append((curr_len, curr_val))

    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    with open(dst_path, 'w') as f:
        f.write("#pragma once\n\n")
        f.write(f"#define LOGO_WIDTH  {target_w}\n")
        f.write(f"#define LOGO_HEIGHT {target_h}\n")
        f.write(f"#define LOGO_RLE_COUNT {len(runs)}\n\n")
        f.write("static const unsigned char apple_logo_rle[] = {\n")
        for i, (cnt, val) in enumerate(runs):
            f.write(f"{cnt},{val},")
            if (i + 1) % 12 == 0:
                f.write("\n")
        f.write("\n};\n")

def generate_font(dst_path):
    font = ImageFont.load_default()
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    with open(dst_path, 'w') as f:
        f.write("#pragma once\n\nstatic const unsigned char font8x8[95][8] = {\n")
        for c in range(32, 127):
            im = Image.new('1', (8, 8), 0)
            draw = ImageDraw.Draw(im)
            draw.text((0, -2), chr(c), font=font, fill=1)
            bytes_c = []
            for y in range(8):
                row = 0
                for x in range(8):
                    if im.getpixel((x, y)):
                        row |= (1 << (7 - x))
                bytes_c.append(f"0x{row:02x}")
            comment = chr(c) if chr(c) not in ('\\', '/*') else 'slash'
            f.write(f"    {{{', '.join(bytes_c)}}}, // {comment}\n")
        f.write("};\n")

def generate_cursor(src_path, dst_path):
    img = Image.open(src_path).convert('RGBA')
    w, h = img.size
    pixels = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = img.getpixel((x, y))
            val = ((0xFF if a > 0 else 0) << 24) | (r << 16) | (g << 8) | b
            pixels.append(f"0x{val:08x}")
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    with open(dst_path, 'w') as f:
        f.write("#pragma once\n\n")
        f.write(f"#define CURSOR_W {w}\n#define CURSOR_H {h}\n\n")
        f.write("static const unsigned int cursor_data[] = {\n")
        for i in range(0, len(pixels), 8):
            f.write("    " + ", ".join(pixels[i:i+8]) + ",\n")
        f.write("};\n")

def generate_wallpaper_icon(src_path, dst_path, size=48):
    img = Image.open(src_path).convert('RGB')
    resized = img.resize((size, size), Image.Resampling.LANCZOS)
    pixels = []
    for y in range(size):
        for x in range(size):
            r, g, b = resized.getpixel((x, y))
            val = (r << 16) | (g << 8) | b
            pixels.append(f"0x{val:06x}")
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    with open(dst_path, 'w') as f:
        f.write("#pragma once\n\n")
        f.write(f"#define WALL_ICON_W {size}\n#define WALL_ICON_H {size}\n#define WALL_ICON_R 8\n\n")
        f.write("static const unsigned int wallpaper_icon_data[] = {\n")
        for i in range(0, len(pixels), 8):
            f.write("    " + ", ".join(pixels[i:i+8]) + ",\n")
        f.write("};\n")

def generate_wallpapers(wall1_path, wall2_path, dst_path, w=160, h=90):
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    def rle_encode(path):
        im = Image.open(path).convert('RGB').resize((w, h), Image.Resampling.LANCZOS)
        arr = np.array(im)
        r = (arr[:,:,0] >> 3).astype(np.uint16)
        g = (arr[:,:,1] >> 2).astype(np.uint16)
        b = (arr[:,:,2] >> 3).astype(np.uint16)
        c16 = (r << 11) | (g << 5) | b
        flat = c16.flatten()
        runs = []
        cval, clen = flat[0], 0
        for v in flat:
            if v == cval and clen < 255:
                clen += 1
            else:
                runs.append((clen, cval))
                cval, clen = v, 1
        runs.append((clen, cval))
        return runs

    r1 = rle_encode(wall1_path)
    r2 = rle_encode(wall2_path)
    with open(dst_path, 'w') as f:
        f.write("#pragma once\n\n")
        f.write(f"#define WALL_W {w}\n#define WALL_H {h}\n\n")
        f.write(f"#define WALL1_RUNS {len(r1)}\n")
        f.write("static const unsigned char wall1_cnt[] = {\n")
        for i in range(0, len(r1), 16):
            f.write("    " + ", ".join(str(cnt) for cnt, _ in r1[i:i+16]) + ",\n")
        f.write("};\n")
        f.write("static const unsigned short wall1_col[] = {\n")
        for i in range(0, len(r1), 8):
            f.write("    " + ", ".join(f"0x{col:04x}" for _, col in r1[i:i+8]) + ",\n")
        f.write("};\n\n")

        f.write(f"#define WALL2_RUNS {len(r2)}\n")
        f.write("static const unsigned char wall2_cnt[] = {\n")
        for i in range(0, len(r2), 16):
            f.write("    " + ", ".join(str(cnt) for cnt, _ in r2[i:i+16]) + ",\n")
        f.write("};\n")
        f.write("static const unsigned short wall2_col[] = {\n")
        for i in range(0, len(r2), 8):
            f.write("    " + ", ".join(f"0x{col:04x}" for _, col in r2[i:i+8]) + ",\n")
        f.write("};\n")

if __name__ == '__main__':
    logo_src = sys.argv[1] if len(sys.argv) > 1 else 'apple.png'
    logo_dst = sys.argv[2] if len(sys.argv) > 2 else 'src/apple_logo.h'
    font_dst = sys.argv[3] if len(sys.argv) > 3 else 'src/font8x8.h'
    cur_src  = sys.argv[4] if len(sys.argv) > 4 else 'cur.png'
    cur_dst  = sys.argv[5] if len(sys.argv) > 5 else 'src/cursor.h'
    generate_logo(logo_src, logo_dst)
    generate_font(font_dst)
    generate_cursor(cur_src, cur_dst)
    generate_wallpaper_icon('wallpaper_icon.bmp', 'src/wallpaper_icon.h')
    generate_wallpapers('wall1.png', 'wall2.png', 'src/wallpapers.h')


