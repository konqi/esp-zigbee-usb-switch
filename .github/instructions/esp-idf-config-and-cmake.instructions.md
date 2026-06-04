---
description: "Use when editing ESP-IDF build files or configuration such as CMakeLists.txt, *.cmake, sdkconfig defaults, and related component wiring. Covers style and safety conventions for this firmware project."
applyTo: "**/CMakeLists.txt, **/*.cmake, sdkconfig, sdkconfig.defaults, sdkconfig.old"
name: "ESP-IDF Config And CMake Conventions"
---
# ESP-IDF Config and CMake Conventions

- Prefer minimal, targeted changes; avoid broad reformatting in build and config files.
- Preserve existing variable naming and ordering patterns in each CMake file.
- Keep component boundaries explicit with `idf_component_register(...)`; do not add hidden global include paths unless required.
- When adding source files, include only what is needed and keep file lists deterministic.
- Prefer project-local options over global compile definitions unless the setting must affect all components.
- For sdkconfig-related edits, document intent in commit context and avoid toggling unrelated options.
- Treat `sdkconfig.defaults` as the source of durable defaults; avoid relying on machine-specific values in `sdkconfig`.
- If a configuration change can alter flash size, partitions, or boot behavior, call out risk and require explicit validation.
- Keep comments short and practical; explain non-obvious tradeoffs only.
- After changes, validate with build before flash/monitor flows.
