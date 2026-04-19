# docs/CLAUDE.md — Documentation Hub

## Structure

```
docs/
├── README.md          # Full index with links (browsable)
├── CLAUDE.md          # This file — routing guide
├── user/              # End-user docs (install, usage, config, troubleshooting)
│   └── CLAUDE.md      # Style guide for user-facing docs
├── devel/             # Developer docs (architecture, features, platforms, plans)
│   └── CLAUDE.md      # Full developer doc index by topic
├── specs/             # Public, vendor-neutral convention specs (third-party adopters)
│   └── CLAUDE.md      # Specs subtree routing
├── audits/            # Security reviews, memory analysis, test coverage
├── archive/           # Historical implementation plans
└── images/            # Screenshots and diagrams
```

Each subdirectory has its own `CLAUDE.md` with context-specific guidance.

## Quick Routing

| I need to... | Go to |
|--------------|-------|
| Look up how a system works | `devel/CLAUDE.md` — find the feature doc |
| Understand a shared convention spec | `specs/CLAUDE.md` — public, vendor-neutral specs |
| Write/update user-facing docs | `user/CLAUDE.md` — style rules for end users |
| Find XML widget docs | `devel/LVGL9_XML_GUIDE.md` |
| Understand modal patterns | `devel/MODAL_SYSTEM.md` |
| Check a feature's architecture | `devel/ARCHITECTURE.md` |
| See what's planned/complete | `devel/ROADMAP.md` |
| Update the doc index | `README.md` + this file + relevant `CLAUDE.md` |

## Writing Documentation

- Developer docs go in `devel/`, user docs go in `user/`
- Follow the style of existing docs (see `devel/SOUND_SYSTEM.md` or `devel/MODAL_SYSTEM.md`)
- Developer docs: include overview, key files table, architecture, real code examples, developer extension guide
- User docs: step-by-step, no source code references, copy-pasteable commands
- No SPDX headers on docs (only on source code)
- Keep code examples real — pull from actual files, don't invent
- When adding new docs, update: this file, `README.md`, and `devel/CLAUDE.md` or `user/CLAUDE.md`
