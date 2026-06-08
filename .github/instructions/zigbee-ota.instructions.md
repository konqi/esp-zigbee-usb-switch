---
description: "Reference for the Zigbee OTA update pipeline: firmware versioning, CI release workflow, z2m OTA index, and known constraints. Use when working on OTA versioning, release automation, sdkconfig, or zigbee2mqtt integration."
applyTo: "**/*.yml, z2m_converter/**, main/CMakeLists.txt, CMakeLists.txt, partitions.csv"
name: "Zigbee OTA Pipeline"
---

# Zigbee OTA Pipeline

## Overview

This project supports over-the-air firmware updates via the Zigbee OTA Upgrade cluster
(cluster ID 0x0019). Updates are delivered through zigbee2mqtt (z2m) using a custom OTA
index file hosted on GitHub.

The full pipeline is:

1. Tag is pushed → CI builds firmware → creates `.ota` file → commits updated
   `z2m_converter/ota-index.json` → publishes GitHub Release.
2. z2m polls the custom index URL and compares `fileVersion` against the device's current
   version reported over Zigbee.
3. When a newer version is found, z2m initiates OTA transfer over Zigbee.

---

## OTA File Version Packing

The 32-bit Zigbee OTA `fileVersion` is constructed by the CI release workflow from the
semver release tag `vM.m.p`:

| Byte 3 (MSB)                | Byte 2            | Byte 1          | Byte 0 (LSB)  |
| --------------------------- | ----------------- | --------------- | ------------- |
| `APP_RELEASE` = `(M<<4)\|m` | `APP_BUILD` = `p` | `STACK_RELEASE` | `STACK_BUILD` |

- **App word** (bytes 3–2) encodes major/minor/patch of the product release.
- **Stack word** (bytes 1–0) encodes the Espressif `esp-zigbee-lib` version
  (`major<<4 | minor`, patch) read from
  `managed_components/espressif__esp-zigbee-lib/idf_component.yml` at build time.

Example: tag `v0.1.5` with stack lib `1.6.8` →

```
APP_RELEASE = (0<<4)|1 = 0x01
APP_BUILD   = 5         = 0x05
STACK_RELEASE = (1<<4)|6 = 0x16
STACK_BUILD   = 8         = 0x08
fileVersion = 0x01051608
```

z2m displays this as **App: 0.1 build 5 / Stack: 1.6 build 8**.

**Limits:** major and minor each must fit in 4 bits (max 15). Patch must fit in 8 bits
(max 255). The workflow validates and fails if out of range.

---

## Version Constraints (version monotonicity)

The device only accepts an OTA upgrade if the new `fileVersion` is **numerically greater**
than its current `ota_upgrade_file_version`. Downgrades are rejected at the device level.

This means:

- Bumping major from 0 to 1 produces a massive jump in the packed integer, which is fine.
- Never reuse a version number for a different binary.
- Legacy entries in `ota-index.json` used the old byte packing scheme (raw semver bytes).
  The `create-release-tag` workflow decodes using `decode_from_new_packed` only now;
  legacy `decode_from_legacy_bytes` was removed to prevent incorrect version comparison.

---

## CMake Version Injection

`main/CMakeLists.txt` accepts CMake cache variables at configure time:

| Variable                | Default                | Description                            |
| ----------------------- | ---------------------- | -------------------------------------- |
| `ESP_OTA_APP_RELEASE`   | 1                      | Packed app release byte (major\|minor) |
| `ESP_OTA_APP_BUILD`     | git commit count % 256 | App build/patch byte                   |
| `ESP_OTA_STACK_RELEASE` | 0                      | Stack release byte                     |
| `ESP_OTA_STACK_BUILD`   | 0                      | Stack build byte                       |

CI passes all four explicitly. Local dev builds fall back to the git commit count for
`APP_BUILD` when not specified.

`PROJECT_VER` (CMake-level) is injected from `$ENV{PROJECT_VER}` in `CMakeLists.txt`.
CI sets it to the release tag so the boot log shows e.g. `App version: v0.1.5`.

---

## CI Workflows

### `create-release-tag.yml` (manual dispatch)

- Triggered from GitHub Actions UI with a `bump_part` dropdown: `major`, `minor`, `patch`.
- Computes next semver from max of: latest semver git tag and highest `fileVersion` in
  `ota-index.json` (decoded with the new-packed scheme only).
- Creates and pushes the tag.
- Dispatches `release.yml` via GitHub API (requires `actions: write` permission).

### `release.yml` (tag push or manual dispatch with `release_tag` input)

Steps in order:

1. **Resolve release tag** – uses pushed tag or `release_tag` input.
2. **Parse version** – decodes semver, packs `fileVersion`, validates ranges.
3. **Build firmware** – `espressif/esp-idf-ci-action@v1` with explicit `-D` overrides
   and `PROJECT_VER` env var; target: `esp32c6`; IDF version: `v6.0.1`.
4. **Download OTA image builder** – fetches `image_builder_tool.py` from
   `espressif/esp-zigbee-sdk`.
5. **Create OTA image** – wraps `.bin` in Zigbee OTA format with manufacturer code
   `0x131B` and image type `0x0001`.
6. **Compute checksum** – sha512 + file size.
7. **Fetch release notes** – reads GitHub release body via API.
8. **Update ota-index.json** – prepends new entry, deduplicates by
   `(fileVersion, imageType, manufacturerCode, modelId, manufacturerName)`.
   Adds `releaseNotes` field if release body is non-empty.
9. **Commit ota-index.json** – rebases onto `origin/main` with up to 3 push retries
   to handle concurrent pushes.
10. **Create GitHub Release** – uploads `.ota` asset, generates release notes.

Release creation happens **after** index commit to avoid publishing a stale release if
the commit step fails.

---

## OTA Index (`z2m_converter/ota-index.json`)

Served from GitHub main branch. z2m reads this via `zigbee_ota_override_index_location`
in `configuration.yaml`:

```yaml
ota:
  zigbee_ota_override_index_location: https://raw.githubusercontent.com/konqi/esp-zigbee-usb-switch/main/z2m_converter/ota-index.json
```

Each entry:

```json
{
  "fileVersion":      <uint32>,
  "fileSize":         <bytes>,
  "url":              "https://github.com/konqi/esp-zigbee-usb-switch/releases/download/<tag>/<filename>.ota",
  "imageType":        1,
  "manufacturerCode": 4891,
  "modelId":          "zigbee-usb-switch",
  "manufacturerName": "KONQI",
  "sha512":           "<hex>",
  "releaseNotes":     "<text or empty>"
}
```

`manufacturerCode 4891 = 0x131B`, `imageType 1 = 0x0001`.

---

## Firmware OTA Cluster Configuration

Defined in `main/zigbee_usb_switch.c`:

```c
esp_zb_ota_cluster_cfg_t ota_cfg = {
    .ota_upgrade_file_version = ESP_OTA_FILE_VERSION,  // from CMake
    .ota_upgrade_manufacturer = ESP_OTA_MANUFACTURER_CODE,
    .ota_upgrade_image_type   = ESP_OTA_IMAGE_TYPE,
    ...
};
esp_zb_zcl_ota_upgrade_client_variable_t ota_client_data = {
    .max_data_size = 128,  // max bytes per OTA block request
    ...
};
```

**Note:** `max_data_size` is a device-side maximum advertisement. The actual block size
used during transfer is chosen by the coordinator/z2m, not the device. In practice
z2m/coordinator sends ~50-byte blocks regardless of this setting. This is a coordinator
firmware constraint, not a firmware bug.

OTA query interval is set to `ESP_OTA_QUERY_INTERVAL_MIN` (360 minutes by default) after
joining the network.

---

## Partition Layout

`partitions.csv` defines two OTA app slots:

```
ota_0  app  ota_0  0x20000  928K
ota_1  app  ota_1           928K
```

Current app binary is ~529KB, leaving ~44% headroom. `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`
is set so rollback to previous slot is possible on boot failure.

---

## Known Constraints and Gotchas

- **OTA speed** is ~50 bytes/block × ~280ms/block ≈ 50 minutes for a 529KB image. This
  is a coordinator-side limit, not configurable from the device firmware.
- **Downgrade blocked** at device level; version must be numerically larger.
- **Tag trigger limitation**: tags pushed by GITHUB_TOKEN do not trigger `push` events in
  other workflows. This is why `create-release-tag` explicitly dispatches `release.yml`
  via API after pushing the tag.
- **Wi-Fi is disabled** in sdkconfig (not needed for Zigbee-only operation), but LWIP
  and ESP-TLS must remain enabled because transitive dependencies
  (`espressif__esp-zigbee-lib` → `openthread`) require them.
- **sdkconfig vs sdkconfig.defaults**: durable settings go in `sdkconfig.defaults`.
  Machine-generated `sdkconfig` should not be relied on for CI reproducibility.
- **Build type**: CI always builds with the exact committed `sdkconfig.defaults`. Local
  dev builds may differ if `sdkconfig` has uncommitted overrides.
