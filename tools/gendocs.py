#!/usr/bin/env python3
import re
import json
import subprocess
import html
from dataclasses import dataclass, field, asdict
from pathlib import Path
from datetime import datetime

try:
    import markdown
    from markdown.extensions.tables import TableExtension
    from markdown.extensions.fenced_code import FencedCodeExtension
    from markdown.extensions.toc import TocExtension
    HAS_MARKDOWN = True
except ImportError:
    HAS_MARKDOWN = False

REPO_URL = "https://github.com/andrewmd5/cyaml"
SITE_URL = "https://cyaml.org"

@dataclass
class Param:
    name: str
    type: str
    doc: str = ""

@dataclass
class Function:
    name: str
    return_type: str
    params: list[Param] = field(default_factory=list)
    summary: str = ""
    return_doc: str = ""
    examples: list[str] = field(default_factory=list)

@dataclass
class EnumValue:
    name: str
    value: str | None
    doc: str = ""

@dataclass
class Enum:
    name: str
    values: list[EnumValue] = field(default_factory=list)
    doc: str = ""

@dataclass
class StructField:
    name: str
    type: str
    doc: str = ""

@dataclass
class Struct:
    name: str
    fields: list[StructField] = field(default_factory=list)
    doc: str = ""

@dataclass
class Region:
    name: str
    functions: list[Function] = field(default_factory=list)
    enums: list[Enum] = field(default_factory=list)
    structs: list[Struct] = field(default_factory=list)

def esc(s: str) -> str:
    return html.escape(s) if s else ""

def parse_doc_comment(lines: list[str], end_idx: int) -> tuple[str, dict[str, str], str, list[str]]:
    summary_parts = []
    param_docs = {}
    return_doc = ""
    examples = []
    in_code_block = False
    current_example = []

    i = end_idx - 1
    while i >= 0:
        line = lines[i].strip()
        if not line.startswith("//!"):
            break

        comment = line[3:].strip()

        if comment.startswith("```"):
            if in_code_block:
                examples.insert(0, "\n".join(current_example))
                current_example = []
            in_code_block = not in_code_block
            i -= 1
            continue

        if comment.startswith("@param "):
            match = re.match(r"@param\s+(\w+)\s+(.*)", comment)
            if match:
                param_docs[match.group(1)] = match.group(2)
        elif comment.startswith("@return "):
            return_doc = comment[8:].strip()
        elif comment.startswith("@example "):
            examples.insert(0, comment[9:].strip())
        elif comment.startswith("@example") or comment.startswith("@"):
            pass
        elif in_code_block:
            current_example.insert(0, comment)
        elif comment and not comment.startswith("**") and not comment.startswith("-"):
            summary_parts.insert(0, comment)

        i -= 1

    summary = " ".join(summary_parts)
    for marker in ["Format:", "**Format"]:
        if marker in summary:
            summary = summary.split(marker)[0].strip()

    return summary, param_docs, return_doc, examples

def parse_function(decl: str, summary: str, param_docs: dict, return_doc: str, examples: list) -> Function | None:
    decl = " ".join(decl.split())
    match = re.match(r"CYAML_API\s+(.+?)\s*\*?\s*(cyaml_\w+)\s*\(([^)]*)\)\s*;", decl)
    if not match:
        return None

    return_type = match.group(1).strip()
    name = match.group(2)
    params_str = match.group(3).strip()

    if "*" in decl.split("(")[0] and not return_type.endswith("*"):
        return_type += " *"

    params = []
    if params_str and params_str != "void":
        for ptype, pname in re.findall(r"((?:const\s+)?[\w_]+\s*\**)\s*(\w+)", params_str):
            params.append(Param(name=pname, type=ptype.strip(), doc=param_docs.get(pname, "")))
        if "..." in params_str:
            params.append(Param(name="...", type="", doc="Variable arguments"))

    return Function(name=name, return_type=return_type, params=params, summary=summary, return_doc=return_doc, examples=examples)

def parse_enum(lines: list[str], start_idx: int) -> tuple[Enum | None, int]:
    i = start_idx
    started = False
    enum_lines = []

    while i < len(lines):
        line = lines[i]

        if "{" in line and not started:
            started = True
            i += 1
            continue

        name_match = re.match(r"\s*}\s*(\w+)\s*;", line)
        if name_match and started:
            values = []
            for eline in enum_lines:
                eline = eline.strip()
                if not eline or eline.startswith("//"):
                    continue

                doc = ""
                for marker in ["//!<", "//!"]:
                    if marker in eline:
                        parts = eline.split(marker, 1)
                        eline, doc = parts[0], parts[1].strip()
                        break

                val_match = re.match(r"(\w+)\s*(?:=\s*([^,]+?))?\s*,?\s*$", eline.strip())
                if val_match:
                    values.append(EnumValue(val_match.group(1), val_match.group(2).strip() if val_match.group(2) else None, doc))

            summary, _, _, _ = parse_doc_comment(lines, start_idx)
            return Enum(name=name_match.group(1), values=values, doc=summary), i

        if started:
            enum_lines.append(line)
        i += 1

    return None, i

def parse_struct(lines: list[str], start_idx: int) -> tuple[Struct | None, int]:
    i = start_idx
    started = False
    struct_lines = []

    while i < len(lines):
        line = lines[i]

        if "{" in line and not started:
            started = True
            i += 1
            continue

        name_match = re.match(r"\s*}\s*(\w+)\s*;", line)
        if name_match and started:
            fields = []
            for sline in struct_lines:
                sline = sline.strip()
                if not sline or sline.startswith("//"):
                    continue

                doc = ""
                for marker in ["//!<", "//!"]:
                    if marker in sline:
                        parts = sline.split(marker, 1)
                        sline, doc = parts[0], parts[1].strip()
                        break

                # Parse field: type name; or type* name; or type *name;
                field_match = re.match(r"(.+?)\s+(\w+)\s*;\s*$", sline.strip())
                if field_match:
                    fields.append(StructField(field_match.group(2), field_match.group(1).strip(), doc))

            summary, _, _, _ = parse_doc_comment(lines, start_idx)
            return Struct(name=name_match.group(1), fields=fields, doc=summary), i

        if started:
            struct_lines.append(line)
        i += 1

    return None, i

def parse_header(content: str) -> list[Region]:
    lines = content.split("\n")
    regions = []
    current_region = Region(name="Overview")

    i = 0
    while i < len(lines):
        line = lines[i]

        if "// #region" in line:
            if current_region.functions or current_region.enums or current_region.structs:
                regions.append(current_region)
            current_region = Region(name=line.split("// #region")[1].strip())
            i += 1
            continue

        if "// #endregion" in line:
            i += 1
            continue

        if "CYAML_API" in line and "define" not in line:
            decl_lines = [line]
            j = i + 1
            while j < len(lines) and ";" not in decl_lines[-1]:
                decl_lines.append(lines[j])
                j += 1

            summary, param_docs, return_doc, examples = parse_doc_comment(lines, i)
            func = parse_function(" ".join(decl_lines), summary, param_docs, return_doc, examples)
            if func:
                current_region.functions.append(func)
            i = j
            continue

        if line.strip().startswith("typedef enum"):
            enum, end_i = parse_enum(lines, i)
            if enum:
                current_region.enums.append(enum)
            i = end_i + 1
            continue

        if line.strip().startswith("typedef struct") and "{" in line:
            struct, end_i = parse_struct(lines, i)
            if struct:
                current_region.structs.append(struct)
            i = end_i + 1
            continue

        i += 1

    if current_region.functions or current_region.enums or current_region.structs:
        regions.append(current_region)

    return regions

def get_version() -> str:
    try:
        return subprocess.check_output(
            ["git", "describe", "--tags", "--always"],
            cwd=Path(__file__).parent.parent,
            text=True, stderr=subprocess.DEVNULL
        ).strip()
    except Exception:
        return "dev"

def render_markdown(content: str) -> str:
    if not HAS_MARKDOWN:
        return f"<pre>{esc(content)}</pre>"
    md = markdown.Markdown(extensions=[
        TableExtension(),
        FencedCodeExtension(),
        TocExtension(permalink=False),
        'def_list',
    ])
    return md.convert(content)

def format_type(t: str) -> str:
    t = esc(t)
    keywords = ['const', 'void', 'bool', 'int', 'char', 'size_t', 'uint8_t', 'uint32_t', 'int64_t', 'uint64_t', 'double', 'float']
    for kw in keywords:
        t = re.sub(rf'\b{kw}\b', f'<span class="kw">{kw}</span>', t)
    t = re.sub(r'\b(cyaml_\w+)\b', r'<span class="type">\1</span>', t)
    return t

def render_signature(fn: Function) -> str:
    params = []
    for p in fn.params:
        if p.name == "...":
            params.append('<span class="param">...</span>')
        else:
            params.append(f'{format_type(p.type)} <span class="param">{esc(p.name)}</span>')
    params_str = ", ".join(params) if params else "void"
    return f'{format_type(fn.return_type)} <span class="fn">{esc(fn.name)}</span>({params_str})'

def render_enum_html(en: Enum) -> str:
    rows = []
    for v in en.values:
        val = f" = {esc(v.value)}" if v.value else ""
        doc = f'<span class="doc">{esc(v.doc)}</span>' if v.doc else ""
        rows.append(f'<tr><td><code>{esc(v.name)}{val}</code></td><td>{doc}</td></tr>')
    doc_p = f"<p>{esc(en.doc)}</p>" if en.doc else ""
    return f'''<div class="enum" id="{en.name}">
    <h3><code>{esc(en.name)}</code></h3>
    {doc_p}
    <table class="enum-table">{"".join(rows)}</table>
  </div>'''

def render_struct_html(st: Struct) -> str:
    rows = []
    for f in st.fields:
        doc = f'<span class="doc">{esc(f.doc)}</span>' if f.doc else ""
        rows.append(f'<tr><td><code>{format_type(f.type)} {esc(f.name)}</code></td><td>{doc}</td></tr>')
    doc_p = f"<p>{esc(st.doc)}</p>" if st.doc else ""
    return f'''<div class="struct" id="{st.name}">
    <h3><code>{esc(st.name)}</code></h3>
    {doc_p}
    <table class="struct-table">{"".join(rows)}</table>
  </div>'''

def render_function_html(fn: Function) -> str:
    sig = render_signature(fn)
    summary = f'<p class="summary">{esc(fn.summary)}</p>' if fn.summary else ""

    params = [p for p in fn.params if p.name != "..."]
    params_html = ""
    if params:
        items = "".join(f'<dt><code>{esc(p.name)}</code></dt><dd>{esc(p.doc)}</dd>' for p in params)
        params_html = f'<div class="params"><h4>Parameters</h4><dl>{items}</dl></div>'

    returns_html = f'<div class="returns"><h4>Returns</h4><p>{esc(fn.return_doc)}</p></div>' if fn.return_doc else ""

    examples_html = ""
    if fn.examples:
        cards = []
        for ex in fn.examples:
            cards.append(f'''<div class="example-card">
        <pre><code>{esc(ex)}</code></pre>
        <button class="copy-btn" aria-label="Copy code"></button>
      </div>''')
        examples_html = f'<div class="examples"><h4>Examples</h4>{"".join(cards)}</div>'

    return f'''<div class="function" id="{fn.name}">
    <h3><code>{sig}</code></h3>
    {summary}{params_html}{returns_html}{examples_html}
  </div>'''

def render_api_content(regions: list[Region]) -> str:
    sections = []
    for region in regions:
        if region.name == "Platform":
            continue
        anchor = re.sub(r'[^a-z0-9]+', '-', region.name.lower())
        structs = "".join(render_struct_html(s) for s in region.structs)
        enums = "".join(render_enum_html(e) for e in region.enums)
        funcs = "".join(render_function_html(f) for f in region.functions)
        sections.append(f'<section id="{anchor}"><h2>{esc(region.name)}</h2>{structs}{enums}{funcs}</section>')
    return "".join(sections)

def render_sidebar(regions: list[Region]) -> str:
    sections = []
    for region in regions:
        if region.name == "Platform":
            continue
        items = []
        for s in region.structs:
            items.append(f'<a class="sidebar-item" href="#{s.name}">{esc(s.name)}</a>')
        for e in region.enums:
            items.append(f'<a class="sidebar-item" href="#{e.name}">{esc(e.name)}</a>')
        for f in region.functions:
            items.append(f'<a class="sidebar-item" href="#{f.name}">{esc(f.name)}</a>')
        sections.append(f'''<div class="sidebar-section">
      <div class="sidebar-section-title">{esc(region.name)}</div>
      <div class="sidebar-items">{"".join(items)}</div>
    </div>''')
    return "".join(sections)

def generate_html(title: str, description: str, canonical: str, content: str, page: str, sidebar: str = "", version: str = "", versions: list[str] = None, assets_prefix: str = "") -> str:
    versions = versions or []
    if page == "home":
        nav_prefix = ""
    else:
        nav_prefix = "../"
    nav_tabs = f'''<a class="nav-tab{' active' if page == 'home' else ''}" href="{nav_prefix}">Home</a>
      <a class="nav-tab{' active' if page == 'api' else ''}" href="{nav_prefix}api/">API Reference</a>
      <a class="nav-tab{' active' if page == 'ypath' else ''}" href="{nav_prefix}ypath/">YPATH Spec</a>'''

    sidebar_html = f'<aside class="sidebar" id="sidebar">{sidebar}</aside>' if sidebar else '<aside class="sidebar hidden" id="sidebar"></aside>'
    main_class = "main" if sidebar else "main no-sidebar"

    version_options = []
    for v in versions:
        selected = " selected" if v == version else ""
        version_options.append(f'<option value="{esc(v)}"{selected}>{esc(v)}</option>')
    version_select = f'''<select class="version-select" id="version-select" onchange="(function(sel){{var p=location.pathname.match(/v[0-9.]+\\/(.*)$/);window.location.href='{assets_prefix}'+sel.value+'/'+(p?p[1]:'')}})(this)">{"".join(version_options)}</select>''' if versions else ""

    search_display = 'style="display:none"' if page != 'api' else ''

    return f'''<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>{esc(title)}</title>
  <meta name="description" content="{esc(description)}">
  <link rel="canonical" href="{canonical}">

  <meta property="og:type" content="website">
  <meta property="og:title" content="{esc(title)}">
  <meta property="og:description" content="{esc(description)}">
  <meta property="og:url" content="{canonical}">
  <meta property="og:site_name" content="cyaml">

  <meta name="twitter:card" content="summary">
  <meta name="twitter:title" content="{esc(title)}">
  <meta name="twitter:description" content="{esc(description)}">

  <script type="application/ld+json">
  {{
    "@context": "https://schema.org",
    "@type": "SoftwareSourceCode",
    "name": "cyaml",
    "description": "{esc(description)}",
    "url": "{SITE_URL}",
    "codeRepository": "{REPO_URL}",
    "programmingLanguage": "C",
    "license": "https://opensource.org/licenses/MIT"
  }}
  </script>

  <link rel="stylesheet" href="{assets_prefix}assets/styles.css">
</head>
<body>
  <nav class="navbar">
    <a class="navbar-brand" href="{assets_prefix}">cyaml</a>
    <div class="nav-tabs">
      {nav_tabs}
    </div>
    <div class="navbar-spacer"></div>
    {version_select}
    <button class="search-trigger" id="search-trigger" {search_display}>
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
        <circle cx="11" cy="11" r="8"></circle>
        <path d="m21 21-4.35-4.35"></path>
      </svg>
      <span>Search...</span>
      <kbd>⌘K</kbd>
    </button>
    <a class="github-link" href="{REPO_URL}" target="_blank" rel="noopener">
      <svg viewBox="0 0 24 24" fill="currentColor">
        <path d="M12 0C5.37 0 0 5.37 0 12c0 5.31 3.435 9.795 8.205 11.385.6.105.825-.255.825-.57 0-.285-.015-1.23-.015-2.235-3.015.555-3.795-.735-4.035-1.41-.135-.345-.72-1.41-1.23-1.695-.42-.225-1.02-.78-.015-.795.945-.015 1.62.87 1.845 1.23 1.08 1.815 2.805 1.305 3.495.99.105-.78.42-1.305.765-1.605-2.67-.3-5.46-1.335-5.46-5.925 0-1.305.465-2.385 1.23-3.225-.12-.3-.54-1.53.12-3.18 0 0 1.005-.315 3.3 1.23.96-.27 1.98-.405 3-.405s2.04.135 3 .405c2.295-1.56 3.3-1.23 3.3-1.23.66 1.65.24 2.88.12 3.18.765.84 1.23 1.905 1.23 3.225 0 4.605-2.805 5.625-5.475 5.925.435.375.81 1.095.81 2.22 0 1.605-.015 2.895-.015 3.3 0 .315.225.69.825.57A12.02 12.02 0 0 0 24 12c0-6.63-5.37-12-12-12z"/>
      </svg>
    </a>
  </nav>

  {sidebar_html}

  <main class="{main_class}" id="main">
    {content}
  </main>

  <div class="search-modal" id="search-modal">
    <div class="search-dialog">
      <div class="search-input-wrap">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <circle cx="11" cy="11" r="8"></circle>
          <path d="m21 21-4.35-4.35"></path>
        </svg>
        <input class="search-input" id="search-input" placeholder="Search documentation..." type="text">
      </div>
      <div class="search-results" id="search-results"></div>
    </div>
  </div>

  <script src="{assets_prefix}assets/docs.js"></script>
</body>
</html>
'''

def generate_sitemap(pages: list[tuple[str, str]], version: str) -> str:
    now = datetime.now().strftime("%Y-%m-%d")
    urls = []
    for filename, priority in pages:
        urls.append(f'''  <url>
    <loc>{SITE_URL}/{filename}</loc>
    <lastmod>{now}</lastmod>
    <priority>{priority}</priority>
  </url>''')
    return f'''<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">
{"".join(urls)}
</urlset>
'''

def generate_version_select(versions: list[str], current: str, prefix: str = "") -> str:
    options = []
    for v in versions:
        selected = " selected" if v == current else ""
        options.append(f'<option value="{prefix}{esc(v)}/"{selected}>{esc(v)}</option>')
    return "".join(options)

def generate_root_index(versions: list[str]) -> str:
    latest = versions[0] if versions else "dev"
    return f'''<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>cyaml - YAML 1.2 Parser for C</title>
  <meta name="description" content="A portable, fully compliant YAML 1.2 parser, emitter, and document manipulation library written in C11.">
  <link rel="canonical" href="{SITE_URL}/">
  <meta http-equiv="refresh" content="0; url={latest}/">
  <script>window.location.href = "{latest}/";</script>
</head>
<body>
  <p>Redirecting to <a href="{latest}/">latest version ({latest})</a>...</p>
</body>
</html>
'''

def generate_redirect(target: str, title: str) -> str:
    return f'''<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>{esc(title)} - cyaml</title>
  <link rel="canonical" href="{SITE_URL}/{target}">
  <meta http-equiv="refresh" content="0; url=../{target}">
  <script>window.location.href = "../{target}";</script>
</head>
<body>
  <p>Redirecting to <a href="../{target}">{esc(title)}</a>...</p>
</body>
</html>
'''

def main():
    import sys

    root_dir = Path(__file__).parent.parent
    header_path = root_dir / "src" / "cyaml.h"
    docs_dir = root_dir / "docs"
    readme_path = root_dir / "README.md"
    ypath_path = root_dir / "refs" / "ypath" / "spec.md"

    version = sys.argv[1] if len(sys.argv) > 1 else get_version()

    versions_file = docs_dir / "versions.json"
    versions = json.loads(versions_file.read_text()) if versions_file.exists() else []

    if version not in versions:
        versions.append(version)
    versions.sort(key=lambda v: [int(x) for x in v.lstrip('v').split('.')], reverse=True)
    versions_file.write_text(json.dumps(versions, indent=2))

    # Create version directory
    version_dir = docs_dir / version
    version_dir.mkdir(exist_ok=True)

    regions = parse_header(header_path.read_text())

    readme_html = ""
    if readme_path.exists():
        readme_content = readme_path.read_text()
        readme_content = readme_content.replace('](refs/ypath/spec.md)', '](ypath/)')
        readme_html = render_markdown(readme_content)

    ypath_html = ""
    if ypath_path.exists():
        ypath_html = render_markdown(ypath_path.read_text())

    version_select_html = generate_version_select(versions, version, prefix="../")

    # Generate {version}/index.html (Home)
    home_content = f'<div class="markdown">{readme_html}</div>'
    home_html = generate_html(
        title="cyaml - YAML 1.2 Parser for C",
        description="A portable, fully compliant YAML 1.2 parser, emitter, and document manipulation library written in C11.",
        canonical=f"{SITE_URL}/{version}/",
        content=home_content,
        page="home",
        version=version,
        versions=versions,
        assets_prefix="../"
    )
    (version_dir / "index.html").write_text(home_html)
    print(f"Generated docs/{version}/index.html")

    # Generate {version}/api/index.html
    api_dir = version_dir / "api"
    api_dir.mkdir(exist_ok=True)
    api_content = render_api_content(regions)
    sidebar = render_sidebar(regions)
    api_html = generate_html(
        title=f"cyaml {version} - API Reference",
        description="Complete API reference for cyaml, a YAML 1.2 parser library for C. Includes functions for parsing, emitting, and manipulating YAML documents.",
        canonical=f"{SITE_URL}/{version}/api/",
        content=api_content,
        page="api",
        sidebar=sidebar,
        version=version,
        versions=versions,
        assets_prefix="../../"
    )
    (api_dir / "index.html").write_text(api_html)
    print(f"Generated docs/{version}/api/index.html")

    # Output JSON for search functionality
    data = {
        "version": version,
        "regions": [asdict(r) for r in regions],
    }
    (api_dir / "api.json").write_text(json.dumps(data, indent=2))
    print(f"Generated docs/{version}/api/api.json")

    # Generate {version}/ypath/index.html
    ypath_dir = version_dir / "ypath"
    ypath_dir.mkdir(exist_ok=True)
    ypath_content = f'<div class="markdown">{ypath_html}</div>'
    ypath_page = generate_html(
        title="YPATH Specification - cyaml",
        description="YPATH is a query language for traversing and selecting nodes within YAML documents, similar to JSONPath or XPath.",
        canonical=f"{SITE_URL}/{version}/ypath/",
        content=ypath_content,
        page="ypath",
        version=version,
        versions=versions,
        assets_prefix="../../"
    )
    (ypath_dir / "index.html").write_text(ypath_page)
    print(f"Generated docs/{version}/ypath/index.html")

    # Generate root index.html (redirect to latest)
    (docs_dir / "index.html").write_text(generate_root_index(versions))
    print("Generated docs/index.html")

    # Generate redirect pages for /api/ and /ypath/
    latest = versions[0] if versions else "dev"
    api_redirect_dir = docs_dir / "api"
    api_redirect_dir.mkdir(exist_ok=True)
    (api_redirect_dir / "index.html").write_text(generate_redirect(f"{latest}/api/", "API Reference"))
    print("Generated docs/api/index.html")

    ypath_redirect_dir = docs_dir / "ypath"
    ypath_redirect_dir.mkdir(exist_ok=True)
    (ypath_redirect_dir / "index.html").write_text(generate_redirect(f"{latest}/ypath/", "YPATH Specification"))
    print("Generated docs/ypath/index.html")

    # Generate sitemap.xml with all versions
    sitemap_urls = []
    for v in versions:
        sitemap_urls.append((f"{v}/", "1.0" if v == versions[0] else "0.7"))
        sitemap_urls.append((f"{v}/api/", "0.9" if v == versions[0] else "0.6"))
        sitemap_urls.append((f"{v}/ypath/", "0.8" if v == versions[0] else "0.5"))
    sitemap = generate_sitemap(sitemap_urls, version)
    (docs_dir / "sitemap.xml").write_text(sitemap)
    print("Generated docs/sitemap.xml")

if __name__ == "__main__":
    main()
