Repository Style Guide
======================

This guide defines default naming and formatting conventions for a repository. Follow the local style of the file you are editing first, then use these rules for new work or unclear cases.


Core Style
----------

- Keep changes narrow, direct, and consistent with nearby code or docs.
- Prefer readable names over abbreviations for new systems.
- Use ASCII text unless a file already uses a broader character set or the content needs a specific symbol.
- Preserve generated, vendor, and third-party structure unless the repository explicitly owns it.
- Do not mass-rename legacy files just to match this guide. Treat renames as deliberate migrations.
- Choose one stable project prefix for engine-, editor-, or runtime-facing identifiers, and keep that prefix consistent.


Header Formatting
-----------------

Use divider headers for files that benefit from clear navigation. Match the header style to the file type and the weight of the section.

Markdown:

- Keep Markdown prose and lists in normal Markdown instead of wrapping divider sections in `text` fences just to preserve their source layout.
- Use one Markdown file header with the title on its own line and a Setext `=` underline.
- Use the title on its own line with a Setext `-` underline for every Markdown section after the file header.
- Do not wrap Markdown heading text in decorative `---` markers.
- The first Markdown section after opening prose uses one blank line before the header.
- Later Markdown sections use exactly two blank lines before the header.
- Fenced examples and language-specific comment dividers are exempt from Markdown spacing rules.

Markdown file header:

```md
Project Name
============
```

Markdown section:

```md
Current TODO
------------
```

Use `=` divider comments for major implementation sections outside Markdown.

Adapt the comment prefix to the file type:

Papyrus:

```papyrus
; --- Section Name ---
; ====================
```

C++:

```cpp
// --- Section Name ---
// ====================
```

INI:

```ini
; --- Section Name ---
; ====================
```

Keep short local labels simple when a full divider would add noise:

```papyrus
; Logging
; Runtime / Polling
```


Naming By Layer
---------------

| Area | Convention | Example |
|---|---|---|
| Special root docs | Uppercase conventional names | `README.md`, `AGENTS.md`, `LICENSE`, `CHANGELOG.md` |
| Repo docs | lower-kebab | `docs/style-guide.md`, `docs/release-checklist.md` |
| Repo tools | lower-kebab | `tools/build-plugin.ps1` |
| Internal repo buckets | Top-level lowercase folders | `docs`, `tools`, `assets`, `reference` |
| General source files | Follow language or local project style | `config.cpp`, `data_store.py`, `FeaturePanel.tsx` |
| Papyrus scripts | PascalCase, no dashes | `ProjectExample.psc` |
| Quest fragments | `<ProjectPrefix>_QF_*` | `Project_QF_MainQuest.psc` |
| EditorIDs | `<ProjectPrefix>_*` | `Project_MainQuest` |
| INI sections | PascalCase | `[Feature]`, `[Integration]` |
| INI keys | PascalCase | `FeatureEnabled`, `IntegrationMode` |
| Skyrim runtime assets | lower_snake_case | `splash_01_project.png`, `feature_icon.swf` |
| SKSE plugin files | lowercase | `projectplugin.ini`, `projectplugin.dll` |
| Public titles | Title Case | `Project Name` |

Use lower-kebab for repo-owned docs, tools, and source/design assets when the file is not consumed directly by Skyrim, Papyrus, SKSE, the CK, xEdit, or another tool with stricter expectations.

Use lower_snake_case for repo-owned Skyrim runtime asset filenames that are loaded directly by game/runtime systems, such as interface SWFs/PNGs, JSON/icon assets, textures, and meshes when safe. Preserve established external stems when they are part of a shipped runtime contract.

Do not use dashes in Papyrus script names, Papyrus identifiers, quest fragment names, EditorIDs, aliases, properties, globals, or CK-facing identifiers.


Papyrus
-------

- Use PascalCase for script names, functions, and public-facing helper names.
- Keep script names and file names identical: `Scriptname ProjectExample` belongs in `ProjectExample.psc`.
- Add a table of contents near the top of a `.psc` file only when the script has more than 200 lines of code or more than 6 functions/events. Smaller scripts should not have a table of contents.
- Papyrus tables of contents use the three-line banner form with an `=` ruler above and below `; --- Table of Contents ---`.
- After the final table-of-contents entry, use two blank lines before the first implementation section divider.
- When a `.psc` file has a table of contents, keep it current when adding, removing, renaming, or reorganizing functions/events.
- Keep explanatory comments short and practical. Use bullets for policy blocks, persistence models, and failure-mode notes.
- Preserve existing generated quest-fragment structure unless intentionally migrating it.
- Do not rename fragment functions such as `Fragment_0` unless the owning quest fragment data is regenerated and verified.

Papyrus Logging:

- Components should route runtime logs through local `Log<Component>()` helpers when the repository has a shared config/logger object.
- Components that write snapshot diagnostics should use local `Log<Component>Snapshot()` helpers when snapshot logging exists.
- Local logging helpers should call the shared logger when it is wired, and fall back to `Debug.Trace("[Project] [<LEVEL>] [<Component>] " + msg)` when required runtime objects are unavailable.
- Keep configured-log message text unchanged in the normal path; add component labels only to fallback traces.
- Do not replace user-facing `Debug.MessageBox`, intentional `Debug.Notification`, or central logging code with component wrappers.

Papyrus Boolean predicate names:

| Pattern | Use For | Example |
|---|---|---|
| `HasCoreRuntime()` | Required controller-owned component wiring only. Do not include player refs, GUIDs, INI feature flags, external mod availability, or optional integrations. | `if !HasCoreRuntime()` |
| `IsRuntimeAvailable()` | External integration or feature runtime state. Must call `HasCoreRuntime()` first when used on controller-owned components. | `Integration.IsRuntimeAvailable()` |
| `IsAvailable(...)` | User-facing feature or action availability using live state such as player, GUID, limits, resources, or settings. | `Feature.IsAvailable(player, guid)` |
| `CanX(...)` | Pure or mostly-pure action predicates, especially when arguments carry the facts being tested. | `CanStartFeature(...)` |
| `HasXRuntime()` | Narrower runtime dependency subset inside one component when not all core component services are required. | `HasPresentationRuntime(requireUi)` |

Papyrus function verb names:

| Pattern | Use For | Example |
|---|---|---|
| `GetX()` | Read or compute a value without persistence writes or other state mutation. | `GetCurrentStage(player, guid)` |
| `SetX()` | Mutate authoritative state; return `Bool` only when failure is meaningful to the caller. | `SetCurrentStage(player, guid, stage)` |
| `TryX()` | Attempt an operation that can fail normally and report success or a recovered value. | `TryStartFeature(player, guid)` |
| `EnsureX()` | Idempotently create, repair, or restore missing required state. | `EnsureGuid(player)` |
| `RefreshX()` | Rebuild cached or discovered runtime state from authoritative sources. | `RefreshRuntime()` |
| `SyncX()` | Mirror authoritative state to globals, UI, native state, or external-facing outputs. | `SyncIntegrationStatus(player)` |
| `ResetX()` | Reset scoped gameplay state; reserve `ResetTransientState()` for runtime-only fields. | `ResetCurrentCharacterData(player, guid)` |
| `RemoveX()` | Delete scoped data or detach hooks without implying permanent destruction. | `RemoveTrackedData(player, guid)` |
| `DeleteX()` | Permanently remove a specific key, index, marker, or snapshot. | `DeleteGuidMarker(guid)` |
| `ConsumeX()` | Read and clear one-shot or pending state. | `ConsumePendingAction()` |
| `HandleX()` | Orchestrate event flow and broad side effects. | `HandlePlayerEvent(player, source)` |
| `ResolveX()` | Perform lookup or discovery and return an object, value, fallback, or `None`. | `ResolveMainQuest()` |
| `MaybeX()` | Conditionally perform an optional action. | `MaybeNotifyThreshold(player, guid, value)` |
| `QueueX()` / `ScheduleX()` / `StartX()` / `StopX()` | Runtime jobs, polling, monitors, and delayed work. | `QueueUpdate(delay)` |

Papyrus state and data names:

- Persistence key names should stay consistent with the existing repository convention. Avoid introducing a new suffix such as `*_key` unless the repo already uses it.
- New persistence key properties should be descriptive lower camel case, `AutoReadOnly`, and documented when the key has special lifetime or reset behavior.
- Runtime-only private fields should use the local convention already present in the file. A leading underscore is preferred when no convention exists, such as `_pendingMenu`, `_runtimeDirty`, and `_mainQuest`.
- Boolean fields should prefer affirmative names, such as `_pendingX`, `_xDirty`, `_xLoaded`, `_xArmed`, and `_xActive`.
- Constants exposed as properties may stay uppercase when they behave as constants, such as `MAX_RETRIES` or `FEATURE_MODE_ENABLED`.

Papyrus boundary names:

- CK/ESP-facing properties, globals, EditorIDs, aliases, and asset names must preserve their CK names. Do not rename them for style alone.
- Component properties on a central controller should use concise PascalCase nouns, such as `Config`, `Persistence`, `Identity`, `Runtime`, and `Presentation`.
- Native bridge functions should use domain prefixes when helpful, such as `DataGetInt`, `DataSetStringChecked`, and `AudioFadeOut`.
- Console command helpers may use parser and normalizer names such as `ParseX`, `NormalizeX`, `ClampX`, and `ResolveX`.

Recommended placeholder names for new Papyrus work:

```text
ProjectMain.psc
ProjectGlobals.psc
ProjectFeatureHandler.psc
Project_QF_MainQuest.psc
Project_QF_FeatureQuest.psc
```


C++ / SKSE Plugin
-----------------

- Follow the existing C++ style in the touched file.
- Use the repository's chosen namespace for plugin code.
- Use `PascalCase` for functions and types when matching the existing plugin style.
- Use `g_` prefixes for file-static global state where the surrounding file already does.
- Use lowercase file names with no dashes for C++ source/header files unless the project already uses another convention.
- Keep comments concise and behavior-focused.
- Keep native bindings and Papyrus bridge declarations in sync when either side changes.
- Use divider headers for larger C++ sections when the file grows enough to need navigation.

Example:

```cpp
// --- Config Parsing ---
// ======================
```


INI Configuration
-----------------

- Use PascalCase for section names and keys unless the target parser requires another style.
- Keep comments directly above the setting they explain.
- Describe valid ranges, default behavior, and disabled values where useful.
- Use `0 = Off` and compact enumerations for mode keys.
- When adding, removing, or renaming public INI keys, update the shipped INI, native allowlist, docs, and console/help listings together when those artifacts exist.

Example:

```ini
[Feature]

; Enable the feature.
FeatureEnabled = 1
```


README And Documentation
------------------------

- Preserve the README's existing brand/header style unless intentionally refreshing it.
- Use Markdown section dividers from this guide in Markdown docs.
- Use concise prose first, then short one-level bullet lists.
- Prefer public-facing clarity over implementation detail in README prose.
- Use lower-kebab filenames for additional docs under `docs`.
- Avoid nested bullets unless the extra hierarchy prevents confusion.
- Keep commit-message and commit-grouping policy in `docs/commit-style.md` or a similarly named repository document.
