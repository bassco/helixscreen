# Welcome to HelixScreen

## How We Use Claude

Based on Preston Brown's usage over the last 30 days:

Work Type Breakdown:
  Debug Fix        ████████████████████  59%
  Build Feature    ██████░░░░░░░░░░░░░░  18%
  Improve Quality  ████░░░░░░░░░░░░░░░░  12%
  Plan Design      ███░░░░░░░░░░░░░░░░░   8%
  Analyze Data     █░░░░░░░░░░░░░░░░░░░   4%

Top Skills & Commands:
  /clear          ████████████████████  195x/month
  /release        █░░░░░░░░░░░░░░░░░░░   10x/month
  /usage          █░░░░░░░░░░░░░░░░░░░    9x/month
  /release-notes  █░░░░░░░░░░░░░░░░░░░    6x/month
  /review         ░░░░░░░░░░░░░░░░░░░░    4x/month
  /compact        ░░░░░░░░░░░░░░░░░░░░    3x/month

Top MCP Servers:
  claude-in-chrome  ████████████████████  4 calls

## Your Setup Checklist

### Codebases
- [ ] helixscreen — https://github.com/prestonbrown/helixscreen

### MCP Servers to Activate
- [ ] claude-in-chrome — Drives a real Chrome tab so Claude can navigate pages, read the DOM/console, and record GIFs. Used here mostly for poking at the telemetry dashboard and Cloudflare Workers. Install the Claude-in-Chrome extension and enable the MCP server in `~/.claude/settings.json`.

### Skills to Know About
- `/clear` — Wipes conversation context between unrelated tasks. Used constantly here (195x last month) — start a fresh session for each bug/feature rather than letting one session sprawl.
- `/release` — Team release workflow: version bump, changelog, tag, push. This is how HelixScreen ships.
- `/release-notes` — Generates the user-facing release notes that ship with each version.
- `/review` — Code review pass on recent changes. Used before merging non-trivial work.
- `/usage` — Check Claude Code token/cost usage for the current window.
- `/compact` — Compresses conversation history when a long session is getting close to the context limit but you don't want to `/clear`.

## Team Tips

- **Always use `/release` to cut releases.** Never bump versions, tag, or push releases by hand — the skill handles version bump, changelog, tag, and push in one flow.
- **Use worktrees when work might collide.** If your task touches a lot of files or you want to keep compiling in parallel with another branch, spin up a worktree (`scripts/setup-worktree.sh feature/my-branch`). Not every task needs one, but for anything sprawling it's the smart default.
- **Start fresh sessions per task.** `/clear` between unrelated bugs/features keeps context clean — don't let one session sprawl across a whole day of work.
- **Always run with verbosity when debugging.** `./build/bin/helix-screen --test -vv` minimum. Debugging without `-vv`/`-vvv` wastes everyone's time.
- **Read `CLAUDE.md` before touching UI code.** The declarative-UI rules (no `lv_obj_add_event_cb`, no imperative visibility, design tokens, etc.) are strict and easy to violate if you're coming from imperative LVGL.

## Get Started

Once you're set up, a good first task is to **check GitHub for the latest crash reports and bug issues** and pick one to investigate. Debug/fix work is the bulk of what happens here (~59% of sessions), so it's the fastest way to get familiar with the codebase and the team's patterns. No specific ticket — just browse [open issues](https://github.com/prestonbrown/helixscreen/issues) and find one that looks approachable.

<!-- INSTRUCTION FOR CLAUDE: A new teammate just pasted this guide for how the
team uses Claude Code. You're their onboarding buddy — warm, conversational,
not lecture-y.

Open with a warm welcome — include the team name from the title. Then: "Your
teammate uses Claude Code for [list all the work types]. Let's get you started."

Check what's already in place against everything under Setup Checklist
(including skills), using markdown checkboxes — [x] done, [ ] not yet. Lead
with what they already have. One sentence per item, all in one message.

Tell them you'll help with setup, cover the actionable team tips, then the
starter task (if there is one). Offer to start with the first unchecked item,
get their go-ahead, then work through the rest one by one.

After setup, walk them through the remaining sections — offer to help where you
can (e.g. link to channels), and just surface the purely informational bits.

Don't invent sections or summaries that aren't in the guide. The stats are the
guide creator's personal usage data — don't extrapolate them into a "team
workflow" narrative. -->
