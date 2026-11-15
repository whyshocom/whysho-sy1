# whysho SY1 — Packaging & Installers

This directory holds everything needed to turn a built copy of **whysho SY1**
(a synthesizer: VST3 / AU instrument plus a Standalone app) into distributable
installers for Windows and macOS, plus the CI that does it automatically.

- **Windows:** Inno Setup `.exe` installer → `WhyshoSY1-<version>-Windows.exe`
- **macOS:** `pkgbuild` + `productbuild` `.pkg` installer → `WhyshoSY1-<version>-macOS.pkg`

```
.github/workflows/build.yml        CI: matrix build + package + release
packaging/
  EULA.txt                         Commercial license text (placeholder)
  README.md                        This file
  windows/installer.iss            Inno Setup script
  macos/distribution.xml           productbuild distribution (template)
  macos/welcome.html               Installer welcome pane
  macos/conclusion.html            Installer conclusion pane
scripts/
  version.sh / version.ps1         Resolve version from git tag (default 0.1.0)
  build-windows-installer.ps1      Compile the Windows installer via ISCC
  build-macos-installer.sh         Build + (optionally) sign/notarize the .pkg
```

## Install locations

| Format      | Windows                                        | macOS                                        |
|-------------|------------------------------------------------|----------------------------------------------|
| VST3        | `C:\Program Files\Common Files\VST3\`          | `/Library/Audio/Plug-Ins/VST3/`              |
| AU          | — (not built on Windows)                       | `/Library/Audio/Plug-Ins/Components/`        |
| Standalone  | `C:\Program Files\whysho\whysho SY1\` + Start Menu shortcut | (built, but not packaged in the `.pkg`) |

## Build artefacts consumed

Both scripts expect JUCE's standard artefact layout under the CMake build dir:

```
build/WhyshoSY1_artefacts/Release/VST3/whysho SY1.vst3
build/WhyshoSY1_artefacts/Release/AU/whysho SY1.component     (macOS only)
build/WhyshoSY1_artefacts/Release/Standalone/whysho SY1.exe   (Windows)
```

## Building installers locally

First build the plugin (Release):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

On macOS, for a universal2 binary add:
`-DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0`.

### Windows

Requires [Inno Setup 6](https://jrsoftware.org/isinfo.php) (`choco install innosetup`).

```powershell
pwsh scripts/build-windows-installer.ps1              # version from git tag / 0.1.0
pwsh scripts/build-windows-installer.ps1 -Version 1.2.3
```

Output: `dist/WhyshoSY1-<version>-Windows.exe`.

### macOS

Requires the Xcode command-line tools (`pkgbuild`, `productbuild`, `codesign`,
`xcrun notarytool` — all included with Xcode).

```bash
scripts/build-macos-installer.sh                      # version from git tag / 0.1.0
scripts/build-macos-installer.sh --version 1.2.3
```

Output: `dist/WhyshoSY1-<version>-macOS.pkg`.

Without signing secrets the script still produces a working **unsigned** package
(macOS Gatekeeper will warn end users; fine for local testing).

## Versioning

The version is resolved by `scripts/version.sh` (and `version.ps1`) in this order:

1. `$WHYSHO_VERSION` environment variable, if set.
2. The exact git tag on `HEAD`, if it matches `vX.Y.Z`.
3. The most recent reachable `vX.Y.Z` tag.
4. Fallback default: `0.1.0`.

A leading `v` is stripped. So tagging a commit `v1.4.0` and pushing produces
installers named `WhyshoSY1-1.4.0-Windows.exe` / `WhyshoSY1-1.4.0-macOS.pkg`,
and CI attaches them to the `v1.4.0` GitHub Release.

```bash
git tag v1.4.0 && git push origin v1.4.0
```

## CI overview (`.github/workflows/build.yml`)

- **Triggers:** push to `main` or `claude/**`, push of a `v*` tag, or manual
  `workflow_dispatch`.
- **Matrix:** `windows-latest` and a GitHub-hosted **`macos-15`** runner (builds
  universal2). The macOS leg is `continue-on-error`, so a failing macOS build never
  blocks the Windows installer or the release.
- **Steps:** checkout (full history for tags) → resolve version → restore the
  CPM/FetchContent dependency cache → cmake configure + build Release → install
  Inno Setup (Windows) → run the packaging script → upload `dist/*` as an
  artifact (`WhyshoSY1-windows` / `WhyshoSY1-macos`).
- **Release:** on a `v*` tag, a final job downloads both artifacts and publishes
  them to a GitHub Release via `softprops/action-gh-release`.

Signing is entirely driven by repository **secrets** (below). If they are absent,
builds still succeed and produce unsigned installers.

## Required GitHub secrets (signing & notarization)

All signing secrets are optional — omit them for unsigned CI builds.

### macOS (Apple Developer ID + notarization)

| Secret                     | Purpose                                                                 |
|----------------------------|-------------------------------------------------------------------------|
| `APPLE_CERT`               | Base64 of a `.p12` containing the **Developer ID Application** and **Developer ID Installer** certificates. |
| `APPLE_CERT_PWD`           | Password for that `.p12`.                                                |
| `APPLE_TEAM_ID`            | Apple Developer Team ID (e.g. `ABCDE12345`).                             |
| `APPLE_SIGN_ID_APP`        | *(optional)* Explicit "Developer ID Application: …" identity string.    |
| `APPLE_SIGN_ID_INSTALLER`  | *(optional)* Explicit "Developer ID Installer: …" identity string.      |
| `AC_NOTARY_PROFILE`        | *(preferred)* `notarytool` keychain profile name for notarization.      |
| `APPLE_ID` + `AC_PASSWORD` | *(alternative to the profile)* Apple ID + app-specific password (used with `APPLE_TEAM_ID`). |

Encode the certificate:

```bash
base64 -i DeveloperID.p12 | pbcopy   # paste into the APPLE_CERT secret
```

Flow when secrets are present: the `.p12` is imported into a temporary keychain,
the `.vst3` / `.component` bundles are codesigned (hardened runtime), the `.pkg`
is signed with `productsign` (Developer ID Installer), then submitted to Apple
with `notarytool --wait` and the ticket stapled.

### Windows (code signing)

| Secret                     | Purpose                                                                 |
|----------------------------|-------------------------------------------------------------------------|
| `WINDOWS_CERT_PFX_BASE64`  | Base64 of the code-signing / EV `.pfx`. (With a hardware EV token, sign on a self-hosted runner instead.) |
| `WINDOWS_CERT_PWD`         | Password for the `.pfx`.                                                 |
| `WINDOWS_CERT_TIMESTAMP`   | *(optional)* RFC3161 timestamp URL (default `http://timestamp.digicert.com`). |

When present, `signtool` signs both the Standalone `.exe` (before packaging) and
the finished installer. EV certificates that live on a hardware token (common for
the highest SmartScreen reputation) cannot export a `.pfx`; for those, run the
packaging step on a self-hosted runner with the token attached.

### Publisher name ("whysho")

Windows shows a publisher in two places, and they come from different sources:

| Where | Source | State |
|-------|--------|-------|
| **Apps & features**, the installer's and the app's file properties | `AppPublisher` / `VersionInfoCompany` in `installer.iss`, `COMPANY_NAME` in `CMakeLists.txt` | set to `whysho` |
| The **UAC prompt** ("Verified publisher") and SmartScreen | The subject of the code-signing certificate | `Unknown publisher` until the build is signed |

Nothing in a script can set the second one: Windows reads it from the
certificate. To show `whysho` there, buy a code-signing certificate issued to
that name (the legal entity behind whysho, exactly as the CA validates it) and
add the `WINDOWS_CERT_*` secrets above. `signtool` is already called with the
description `whysho SY1` and the link `https://whysho.com`, which appear next
to the publisher in the prompt.

## Installer artwork

The installers use the whysho "signal paper" look (paper, ink, riso pink, log
grid) shared with whysho.com and the plug-in's Paper theme.

| File | Used for |
|------|----------|
| `windows/wizard-large-*.bmp`, `wizard-small-*.bmp` | Inno Setup welcome/finish panel and page-header mark (1x and 2x) |
| `windows/setup.ico` | Icon of the installer `.exe` |
| `macos/background.png`, `background-dark.png` | `.pkg` backdrop (light / Dark Mode) |
| `icon.png` | Standalone app icon (`ICON_BIG` in `CMakeLists.txt`) |

All of it is generated by `python3 packaging/art/make_art.py` (Pillow) and
committed, so CI does not need Python. The wizard's page colours are set in the
`[Code]` section of `installer.iss`; the macOS pane text is styled in
`macos/welcome.html` and `conclusion.html`.
