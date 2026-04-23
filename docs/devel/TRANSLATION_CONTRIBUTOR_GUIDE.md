# Translation Contributor Guide

HelixScreen currently ships in nine languages — English, German, Spanish, French, Italian, Japanese, Portuguese, Russian, and Chinese. Every one of them exists because someone sat down with a YAML file and a coffee and did the work.

This doc is for people who want to improve an existing translation, fix a bad one, or add a language that isn't there yet. You don't need to write any code. You do need to know the language you're translating into well enough to make judgment calls — especially around domain terminology (3D printing is vocabulary-heavy).

For the *implementation* of the translation system — how strings get from YAML to rendered pixels — read [TRANSLATION_SYSTEM.md](TRANSLATION_SYSTEM.md) instead.

---

## Current languages

| Locale | File | Status |
|---|---|---|
| `en` | `translations/en.yml` | Source of truth — English is the reference |
| `de` | `translations/de.yml` | German |
| `es` | `translations/es.yml` | Spanish |
| `fr` | `translations/fr.yml` | French |
| `it` | `translations/it.yml` | Italian |
| `ja` | `translations/ja.yml` | Japanese |
| `pt` | `translations/pt.yml` | Portuguese |
| `ru` | `translations/ru.yml` | Russian |
| `zh` | `translations/zh.yml` | Chinese (Simplified) |

All files have identical keys — 1,971 strings — and differ only in their translated values. If English adds a new string, `make translation-sync` propagates the key to every other language file with the English value as a placeholder, marked for a human to fix.

---

## File format

Each file is YAML. A small sample from `en.yml`:

```yaml
locale: en
translations:
  "Retraction Settings": "Retraction Settings"
  "Retract Length": "Retract Length"
  "Speed of retraction movement": "Speed of retraction movement"
  "0.4-2mm direct drive, 4-6mm bowden": "0.4-2mm direct drive, 4-6mm bowden"
  "%u layers": "%u layers"
```

And the same keys in `de.yml`:

```yaml
locale: de
translations:
  "Retraction Settings": "Einzugeinstellungen"
  "Retract Length": "Einzugslänge"
  "Speed of retraction movement": "Geschwindigkeit der Rückzugsbewegung"
  "0.4-2mm direct drive, 4-6mm bowden": "0.4-2mm Direktantrieb, 4-6mm Bowden"
  "%u layers": "%u Schichten"
```

Rules:

- **The key is always the English source string.** Don't change it.
- **The value is the translated string** in the target language.
- **Format specifiers** (`%d`, `%u`, `%lu`, `%s`, `%.2f`, `{0}`, `{count}`) must be preserved exactly. Change their *position* to fit grammar if needed, but don't rename or remove them.
- **Trailing/leading whitespace** in the key is significant. `" elapsed · "` with the spaces is a different key than `"elapsed ·"`.

---

## Improving an existing translation

If you see a string that's translated awkwardly, plainly wrong, or inconsistent with how that term is used elsewhere in HelixScreen, you can fix it.

1. **Open the YAML file** for the language — `translations/<lang>.yml`.
2. **Find the English key** (ctrl-F or grep).
3. **Edit the value** — the right-hand side — to the improved translation.
4. **Test** locally (see below).
5. **Submit a PR** with a description of what changed and why.

If you're improving many strings in one pass (a general pass over a language that feels stiff), group them by panel or feature in separate commits — it makes review and rollback easier.

---

## Adding a new language

If you want to add a language HelixScreen doesn't ship with yet:

### Step 1: Copy `en.yml` to your new locale

```bash
cp translations/en.yml translations/<locale>.yml
```

Use the [ISO 639-1 code](https://en.wikipedia.org/wiki/List_of_ISO_639-1_codes) for `<locale>` — `pl` for Polish, `ko` for Korean, `nl` for Dutch, etc. For variants (`pt-BR` vs `pt-PT`), open a Discussion first — HelixScreen doesn't currently support locale variants, and adding one has implications.

### Step 2: Change `locale:` at the top

```yaml
locale: pl
translations:
  ...
```

### Step 3: Translate

Walk through every entry and replace the English value with the translated value. All 1,971 strings.

This is a significant undertaking — realistically 10–20 hours for a solo translator who knows the domain well. Break it into sessions. Commit as you go so your work is never in a half-state.

### Step 4: Handle plural forms (if your language needs them)

Most entries are flat `"key": "value"` pairs. But for entries that describe a count of something, some languages need plural variants. HelixScreen supports the [CLDR plural categories](https://cldr.unicode.org/index/cldr-spec/plural-rules) (`zero`, `one`, `two`, `few`, `many`, `other`).

Russian example from `translations/ru.yml`:

```yaml
"items_selected":
  one: "{count} элемент выбран"       # 1, 21, 31...
  few: "{count} элемента выбрано"     # 2-4, 22-24...
  many: "{count} элементов выбрано"   # 5-20, 25-30...
  other: "{count} элемента выбрано"   # decimals
```

If your language has plural variation and the string is count-dependent, convert it from a flat string to the nested form above. See [TRANSLATION_SYSTEM.md § Plural Rules](TRANSLATION_SYSTEM.md#plural-rules) for which categories apply to your language.

Languages that need this most heavily: Russian, Polish, Arabic, Czech, and most Slavic languages. English, German, Spanish, French, and Italian get away with `one`/`other` only. Japanese and Chinese have no plural inflection at all — flat strings are fine.

### Step 5: Register the language in code

Add an entry to `src/system/translation_manager.cpp` (language name display) and any dropdown lists in settings. Open a PR without these changes — we'll guide you through the small code additions during review. The YAML file is the heavy lift; the registration is a few lines.

---

## What NOT to translate

Some strings must stay in English regardless of the target language:

- **Product names.** HelixScreen, Klipper, Moonraker, Spoolman, Mainsail, Fluidd, OrcaSlicer.
- **Material codes.** PLA, PETG, ABS, TPU, PA, ASA, PC.
- **Technical abbreviations used as standalone labels.** AMS, QGL, ADXL, PID, IFS, CFS, MCU, RPM.
- **URLs and domains.** `helixscreen.org`, `github.com/...`.
- **Universal terms.** OK, WiFi, USB.
- **G-code commands and parameter names.** `G28`, `SET_RETRACTION`, `RETRACT_LENGTH`.

If an English string is just "Klipper" or "OrcaSlicer" or "PLA", **copy it verbatim to your translation** — don't translate or transliterate.

If an English string *contains* a product name, translate the surrounding words and leave the product name as-is:

| English | Correct (German) | Wrong |
|---|---|---|
| `"Restarting HelixScreen..."` | `"HelixScreen wird neu gestartet..."` | `"Schraubenschirm wird neu gestartet..."` |
| `"Connect to Klipper"` | `"Mit Klipper verbinden"` | `"Mit Kleberer verbinden"` |

---

## Style and tone

HelixScreen's English source text aims for:

- **Short.** Touchscreen space is limited. A button label that's one word in English might be three in German — that's unavoidable, but don't add decoration.
- **Direct.** "Home printer" not "Click here to home the printer."
- **Power-user friendly.** The audience knows G-code. Don't dumb down technical terms — a 3D printing user understands "retraction," "bed mesh," "PID tune."
- **Neutral register.** No marketing voice. No "Awesome!" "Oops!" "Let's get you started!"

Match that register in your translation. If your language has a formal/informal `you` distinction (Sie/du, vous/tu), pick the register the existing translations use and stay consistent — check how other strings address the user and match them.

---

## Testing locally

You don't need a build setup to edit YAML. You do need a build to see your translation in action.

### Quick test (if you have the dev environment)

```bash
make -j                              # rebuild with regenerated translation tables
./build/bin/helix-screen --test -vv  # run with mock printer
# Settings → Display & Sound → Language → <your language>
```

### If you don't want to set up the full dev environment

Submit the PR without local testing. Note in the PR description that you didn't test-run it; a maintainer will do a rebuild-and-screenshot pass as part of review. This is a reasonable contribution shape — many of the current translations were accepted on good-faith review without local builds.

### What to look for when testing

- **Truncation.** Buttons and labels have fixed widths. If your translation is much longer than English, it may clip. Flag specific cases in the PR so we can either resize the widget or shorten the translation.
- **Wrong character set.** Chinese and Japanese need specific fonts loaded. If your text shows as tofu (□), that's a font issue — file an issue, don't try to fix it in the YAML.
- **Plurals behaving wrong.** If your language needed plural forms and a count displays wrong, double-check your CLDR category coverage.

---

## Submitting your contribution

1. **Make sure the YAML is valid.** A syntax error in one entry breaks the whole file. If you have `yamllint` available: `yamllint translations/<lang>.yml`.
2. **Commit with a clear message:**
   - For existing language improvements: `i18n(<lang>): improve <area> translations`
   - For a new language: `i18n: add <language> translation`
3. **Open a PR** on `prestonbrown/helixscreen`. In the description, mention:
   - What language / what area
   - Your qualifications (native speaker? professional translator? just fluent?)
   - Anything you were uncertain about and want a second opinion on
4. **Regenerated artifacts** (`src/generated/lv_i18n_translations.*`, `ui_xml/translations/translations.xml`) should also be committed. They're not gitignored because cross-compile targets need them. If you didn't run the build, note that and we'll regenerate during review.

Translations don't need to be complete in a single PR. If you want to translate a panel's worth of strings and submit, then do another panel next week, that's welcome. Partial translations are better than no translation.

---

## When you're stuck

- **Can't find a string in the YAML.** Confirm the key spelling is exact, including leading/trailing spaces and punctuation. Grep in English first to be sure the key exists.
- **Unsure about domain terminology.** Look at [Slic3r's translations](https://github.com/slic3r/Slic3r), [OrcaSlicer's](https://github.com/SoftFever/OrcaSlicer/tree/main/localization), or [Klipper3d.org](https://klipper3d.org) in your language to see how the community translates things. Consistency across the ecosystem is more important than individual stylistic preference.
- **Not sure if something should be translated.** Default to leaving product-sounding names in English and asking in the PR. It's easier to translate later than to un-translate.

---

## Related docs

- [TRANSLATION_SYSTEM.md](TRANSLATION_SYSTEM.md) — implementation internals (for developers modifying the translation system itself)
- [CONTRIBUTOR_GOTCHAS.md § Translations](CONTRIBUTOR_GOTCHAS.md#translations) — silent-failure traps when working with translations in code
- [CONTRIBUTING.md](../../CONTRIBUTING.md) — general contribution guide
