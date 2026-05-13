import hashlib
import os
import re

Import("env")


def _minify_css(css: str) -> str:
    s = re.sub(r"/\*.*?\*/", "", css, flags=re.DOTALL)
    s = re.sub(r"\s*([{};,:])\s*", r"\1", s)
    s = re.sub(r";}", "}", s)
    s = re.sub(r"\s{2,}", " ", s).strip()
    return s


def _minify_js(js: str) -> str:
    s = re.sub(r"//[^\n]*", "\n", js)
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.DOTALL)
    s = re.sub(r"\s+", " ", s)
    s = re.sub(r"\s*([{}();,:!<>+\-*/%|&^~?])\s*", r"\1", s)
    s = re.sub(r"([;}])\s*", r"\1", s)
    s = s.strip()
    return s


def _minify_html(html: str, css: str, js: str) -> str:
    html = html.replace("__STYLE__", css)
    html = html.replace("__SCRIPT__", js)
    s = re.sub(r"<!--.*?-->", "", html, flags=re.DOTALL)
    # protect <pre> blocks from whitespace-collapsing passes
    pre_blocks = []
    def _save_pre(m):
        pre_blocks.append(m.group(1))
        return f"<pre>__PRE_{len(pre_blocks)-1}__</pre>"
    s = re.sub(r"<pre>(.*?)</pre>", _save_pre, s, flags=re.DOTALL)
    s = re.sub(r">\s+<", "><", s)
    s = re.sub(r"\s{2,}", " ", s)
    s = s.strip()
    # restore protected <pre> blocks
    for i, content in enumerate(pre_blocks):
        s = s.replace(f"__PRE_{i}__", content)
    return s


def generate():
    project_dir = env.subst("$PROJECT_DIR")
    env_name = env.subst("$PIOENV")
    if env_name != "arduino_nano_esp32":
        return

    webui_dir = os.path.join(project_dir, "webui")
    output = os.path.join(project_dir, "src", "wifi_setup_page.h")

    with open(os.path.join(webui_dir, "index.html"), encoding="utf-8") as f:
        html_src = f.read()
    with open(os.path.join(webui_dir, "style.css"), encoding="utf-8") as f:
        css_src = f.read()
    with open(os.path.join(webui_dir, "script.js"), encoding="utf-8") as f:
        js_src = f.read()

    css_min = _minify_css(css_src)
    js_min = _minify_js(js_src)
    html_min = _minify_html(html_src, css_min, js_min)

    version = hashlib.sha1(html_min.encode()).hexdigest()[:8]

    with open(output, "w", encoding="utf-8") as f:
        f.write("#pragma once\n\n#include <Arduino.h>\n\n")
        f.write("const char WIFI_SETUP_PAGE[] PROGMEM = R\"rawliteral(\n")
        f.write(html_min)
        f.write("\n)rawliteral\";\n\n")
        f.write('const char WEBUI_VERSION[] = "')
        f.write(version)
        f.write('";\n')

    raw = len(html_src) + len(css_src) + len(js_src)
    print(f"[build_webui] {raw} B -> {len(html_min)} B  v{version}  ({raw - len(html_min)} B saved)")


generate()
