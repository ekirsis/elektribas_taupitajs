import os
import re
import subprocess

Import("env")

FONT_PATTERN = re.compile(r'^const uint8_t (u8g2_font_\w+)\[')
SRC_PATTERN = re.compile(r'u8g2_font_\w+')

def find_used_fonts(project_dir):
    used = set()
    src_dirs = [
        os.path.join(project_dir, "src"),
        os.path.join(project_dir, "include"),
        os.path.join(project_dir, "lib"),
    ]
    for d in src_dirs:
        if not os.path.isdir(d):
            continue
        for root, dirs, files in os.walk(d):
            for f in files:
                if f.endswith((".cpp", ".c", ".h", ".hpp")):
                    path = os.path.join(root, f)
                    with open(path, "r", encoding="utf-8", errors="replace") as fh:
                        for line in fh:
                            for m in SRC_PATTERN.finditer(line):
                                used.add(m.group(0))
    return sorted(used)

def trim_fonts():
    project_dir = env.subst("$PROJECT_DIR")
    env_name = env.subst("$PIOENV")

    if env_name != "arduino_nano_esp32":
        return

    fonts_c = os.path.join(
        project_dir, ".pio", "libdeps", env_name,
        "U8g2", "src", "clib", "u8g2_fonts.c"
    )

    if not os.path.exists(fonts_c):
        print("[trim_fonts] u8g2_fonts.c not found, skipping")
        return

    sentinel = fonts_c + ".trimmed"
    if os.path.exists(sentinel):
        return

    to_keep = find_used_fonts(project_dir)
    if not to_keep:
        print("[trim_fonts] No u8g2 fonts found in source, skipping")
        return

    print(f"[trim_fonts] Fonts in use: {len(to_keep)}")

    with open(fonts_c, "r", encoding="latin-1") as f:
        lines = f.readlines()

    font_lines = []
    for i, line in enumerate(lines):
        m = FONT_PATTERN.match(line)
        if m:
            font_lines.append((i, m.group(1)))

    keep_ranges = []
    for font_name in to_keep:
        matches = [(ln, name) for ln, name in font_lines if name == font_name]
        if not matches:
            print(f"[trim_fonts] WARNING: Font '{font_name}' not found in library!")
            continue

        start_ln = matches[0][0]

        comment_start = start_ln
        while comment_start > 0:
            stripped = lines[comment_start - 1].strip()
            if stripped.startswith("/*"):
                comment_start -= 1
                break
            if stripped == "" or stripped.startswith("//"):
                break
            comment_start -= 1

        next_font = None
        for ln, name in font_lines:
            if ln > start_ln:
                next_font = ln
                break

        end_ln = (next_font - 1) if next_font is not None else (len(lines) - 1)
        while end_ln > comment_start and lines[end_ln].strip() == "":
            end_ln -= 1

        keep_ranges.append((comment_start, end_ln))

    if not keep_ranges:
        print("[trim_fonts] No fonts matched, keeping original")
        return

    keep_ranges.sort(key=lambda r: r[0])

    merged = []
    for start_ln, end_ln in keep_ranges:
        if merged and start_ln <= merged[-1][1] + 1:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end_ln))
        else:
            merged.append((start_ln, end_ln))

    header_end = 0
    for i, line in enumerate(lines):
        if line.strip().startswith("/*") and i > 0:
            header_end = i
            break

    trimmed = []
    for i in range(0, max(header_end, 1)):
        trimmed.append(lines[i])

    for start_ln, end_ln in merged:
        for i in range(start_ln, end_ln + 1):
            trimmed.append(lines[i])

    with open(fonts_c, "w", encoding="latin-1") as f:
        f.writelines(trimmed)

    with open(sentinel, "w") as f:
        f.write("1")

    print(f"[trim_fonts] Trimmed: {len(lines)} lines -> {len(trimmed)} lines "
          f"({len(to_keep)} of {len(font_lines)} fonts kept)")

trim_fonts()
