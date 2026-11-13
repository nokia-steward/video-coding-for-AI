# VCAI HTML manual

Static HTML documentation built with **MkDocs** using the **readthedocs** theme (built-in). This replaces Material to avoid sidebar scroll/drawer/tab issues with a large nav.

## Build

```bash
pip install -r requirements-docs.txt   # repo root
./docs/build_manual.sh
```

Output: **`manual/`** (gitignored). Open [**manual/index.html**](../manual/index.html) in a browser (file:// from repo root). Do not use `mkdocs serve` — repo-file links only work when viewing the built site from the filesystem.

- [**requirements-docs.txt**](../requirements-docs.txt) — Python deps for MkDocs.
- [**docs/build_manual.sh**](build_manual.sh) — build script (runs [**generate_inventories.sh**](generate_inventories.sh) then `mkdocs build`).

**file:// hosting:** `use_directory_urls: false` emits `page.html` links so opening the folder directly works without directory indexes.

## Theme

- **readthedocs** – classic left nav that scrolls as a normal column; no Material drawer/scrollwrap.
- **mkdocs-material** is no longer in [**requirements-docs.txt**](../requirements-docs.txt). To switch back, restore the previous [**mkdocs.yml**](../mkdocs.yml) theme block and add `mkdocs-material` again.

## Content layout

| Area | Location |
|------|----------|
| Parts I–X + appendices | `docs/part01/` … `docs/part09/`, `docs/appendix/` |
| Algorithms / dataflow / PRI and EOI SEI | `docs/part03/encoder-end-to-end-dataflow.md`, `pri-sei-lifecycle.md` |
| Encoder architecture / packing / QP map | `docs/part03/ch14-encoder-architecture-deep-dive.md`, `ch15-roi-packing-modes.md`, `ch16-background-modes-and-qp-map.md` |
| Build & runtime dependencies | `docs/part05/ch23-dependencies-build-graph.md` |
| rtp_server modes (transport, TET, Test, etc.) | `docs/part04/ch18-rtp-server-modes.md` |
| Design rationale | `docs/appendix/design-decisions.md` |
| Config reference | `docs/config/` — ini snippets |
| Scripts catalog | `docs/part07/scripts-readme-part*.md` |
| Snippets | `pymdownx.snippets` with `--8<--` from repo root; **fence `.ini` snippets** in ` ```ini ` blocks so `#` comments are not parsed as Markdown headings |

## Regenerate appendix inventories

```bash
./docs/generate_inventories.sh
```

[**build_manual.sh**](build_manual.sh) runs this before `mkdocs build`.

## Theme CSS tweaks

[**docs/stylesheets/extra.css**](stylesheets/extra.css) is enabled in [**mkdocs.yml**](../mkdocs.yml). It overrides the ReadTheDocs default **800px** max-width on `.wy-nav-content` so tables and long code lines use the full area beside the sidebar.
