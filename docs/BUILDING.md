# Building, testing and packaging

Everything a developer needs to go from a fresh clone to the two release packages. The library API is in `docs/API.md`; the design is in `docs/ARCHITECTURE.md`.

## 1. Requirements (Windows x64)

| Tool | Notes |
| --- | --- |
| Visual Studio 2026 (18) with **Desktop development with C++** | Provides MSVC, CMake, Ninja and **vcpkg**. Build from the **x64 Native Tools / Developer** shell, which sets `VCPKG_ROOT`. |
| Git and the **GitHub CLI**, logged in (`gh auth login`) | The OCR models are downloaded from this private repository's release. Alternatively, set `GH_TOKEN`. |
| Python 3 (optional) | Only for the Python example test (`library_python`). It is skipped when Python is missing. |

Everything else is downloaded automatically, once, and checksum-verified.

## 2. Build and test

```powershell
git clone https://github.com/PantaKoda/PDFMegine.git
cd PDFMegine
cmake --preset dev          # first run: downloads dependencies, builds qpdf (a few minutes)
cmake --build --preset dev
ctest --preset dev
```

| Preset | Use |
| --- | --- |
| `dev` | Debug, everything, all tests and test hooks. For day-to-day work. |
| `release` | Release, no tests or test hooks. Builds the end-user ZIP and the SDK's Release half. |
| `sdk-debug` | Debug library only, no test hooks. Builds the SDK's Debug half. |

Build trees go to `out/build/<preset>`.

**Tests:** `ctest --preset dev` runs every suite (22 tests). `golden_parity`, the OCR comparison against the PaddleOCR reference outputs (about 5 minutes), is registered only when the local-only `golden/` folder exists (see `docs/ocr/README.md`).

## 3. Dependencies

All pinned in one place, `cmake/PdfbookmarkDependencies.cmake`, plus `vcpkg.json` for qpdf:

| Package | Version | Source | How |
| --- | --- | --- | --- |
| PDFium | 155.0.8057 | `bblanchon/pdfium-binaries`, release `chromium/8057` | Download and SHA-256 check |
| ONNX Runtime | 1.30.0 | `microsoft/onnxruntime`, release `v1.30.0` | Download and SHA-256 check |
| OpenCV | 5.0.0 | `opencv/opencv`, release `5.0.0` (Windows package) | Download and SHA-256 check |
| qpdf | 12.3.2 | vcpkg, baseline `1460b31b` (`vcpkg-configuration.json`) | Built by vcpkg |
| OCR models | v1 | this repository's release `models-v1` | Download and SHA-256 check into `models/` |
| Clipper2 | commit `f9c5eb6e` | vendored in `third_party/clipper2` | In the repository |

- **Download location:** `.deps/` in the repository root. It is shared by all build trees and ignored by Git. Delete it to force fresh downloads.
- **Your own copies:** pass `-DPDFium_DIR=…`, `-DONNXRUNTIME_ROOT=…` or `-DOpenCV_DIR=…`, and that package is not downloaded.
- **Offline models:** `-DPDFBOOKMARK_MODELS_ARCHIVE=<path to pdfbookmark-models-v1.zip>`.
- **Changing a version:** update the URL and SHA-256 together, run the full `dev` tests including `golden_parity`, and record the change in `docs/IMPLEMENTATION_DECISIONS.md`.

The three prebuilt packages were verified to be byte-identical to the binaries the project was developed and tested with (decision E-21).

## 4. End-user package (the CLI)

```powershell
cmake --preset release
cmake --build --preset release --target pdfbookmarkCli
cmake --install out/build/release --component app --prefix dist/pdfbookmark
cpack --config out/build/release/CPackConfig.cmake
copy out\build\release\package\pdfbookmark-0.2.0-win64.zip dist\
```

**Deliverable: `dist/pdfbookmark-0.2.0-win64.zip`** (141 MB, one top-level folder `pdfbookmark-0.2.0-win64/`). `dist/pdfbookmark/` is the same content unzipped. The version comes from the CMake cache variable `PDFBOOKMARK_VERSION` (default `0.2.0`) and is printed by `pdfbookmark --version`. The user guide inside the package is `packaging/README.txt`.

### Installing as a command (users)

Extract the ZIP and double-click `Install.cmd`; no administrator rights are needed.
- `install.ps1` copies the package to `%LOCALAPPDATA%\Programs\PDF Bookmark` (or `-Destination`) and appends that folder to the **user** `PATH` once.
- It preserves the registry value type (`REG_EXPAND_SZ`) and any unexpanded `%VARIABLES%`, then notifies running programs (`WM_SETTINGCHANGE`). New terminals then run `pdfbookmark` from any folder.
- Re-running upgrades in place. It refuses to overwrite a non-empty folder that is not a PDF Bookmark install. `-NoPath` copies without touching `PATH`.
- `Uninstall.cmd` / `uninstall.ps1` removes only that `PATH` entry and the install folder, and refuses any folder without `pdfbookmark.exe`.

**Verified** in an isolated test that used a scratch folder and a **test registry key** (`HKCU\Software\pdfbookmark-test\Environment`, holding a `REG_EXPAND_SZ` PATH with `%USERPROFILE%\bin`); the real `PATH` was never touched:
- **Install:** the ZIP was extracted and installed; the PATH gained the folder and kept `REG_EXPAND_SZ` and `%USERPROFILE%`; a reinstall added no duplicate.
- **Use:** with only Windows and the install folder on `PATH`, `where pdfbookmark` resolved to the install, `pdfbookmark --version` printed `pdfbookmark 0.1.0`, and `analyze` then `apply` from an unrelated folder wrote 8 verified bookmarks.
- **Uninstall:** run from inside the install folder, it restored `PATH` exactly and removed the folder. A simulated Explorer double-click of `Uninstall.cmd` exited 0 with no stray error. The self-deleting batch uses the `(goto)` idiom.
- The test key was deleted afterwards.

### What is shipped (allow-list, component `app`)

| Item | Why |
| --- | --- |
| `pdfbookmark.exe`, `pdfbookmark.dll` | The CLI and the library it uses (Release, `PDFBOOKMARK_TEST_HOOKS=OFF`) |
| `pdfium.dll`, `qpdf30.dll`, `z.dll`, `jpeg62.dll`, `onnxruntime.dll`, `opencv_world500.dll` | Exactly the DLLs the executable loads, resolved by `file(GET_RUNTIME_DEPENDENCIES)`; Windows system DLLs are excluded |
| `msvcp140.dll`, `msvcp140_1.dll`, `vcruntime140.dll`, `vcruntime140_1.dll`, `concrt140.dll` | Microsoft C++ runtime; only the files imported by the shipped binaries (checked with `dumpbin /dependents`), so no redistributable install is needed |
| `models/det/inference.onnx`, `models/rec/inference.onnx`, `models/rec/charset.txt` | The only model files the OCR code reads (the `.yml` files are not read) |
| `Add bookmarks.cmd`, `Add bookmarks (allow partial).cmd`, `README.txt` | Drag-and-drop use and the non-developer guide (sources in `packaging/`) |
| `Install.cmd`, `install.ps1`, `Uninstall.cmd`, `uninstall.ps1` | Per-user install onto `PATH` and removal |
| `licenses/` | PDFium, qpdf, zlib, libjpeg-turbo, ONNX Runtime (plus third-party notices), OpenCV, and PaddleOCR (PP-OCR models) |

**Not shipped:** tests, fixtures and `tests/books/`; the OCR tools and Python tools; headers, `.lib` and CMake files; test hooks (compiled out); the model `.yml` files, `corpus/` and `golden/`; and the unused runtime DLLs `fmt`, `turbojpeg`, `onnxruntime_providers_shared`, `msvcp140_2`, `msvcp140_atomic_wait` and `msvcp140_codecvt_ids`.

**Verified:**
- The staged folder was copied elsewhere and run with `PATH` set to Windows directories only.
- **Drag-and-drop:** the script ran on files and folders whose names contain spaces and parentheses. It bookmarked a book (8 bookmarks, verified), refused an unresolvable one with plain instructions, and the "allow partial" script then wrote 3 bookmarks, omitting one entry and promoting its child.
- **OCR:** it ran with the packaged default models ("Settings / Account overview / …") with no OpenCV log noise.

**Not met yet (A11):** the guide's goal is *one* self-contained executable. This package is a self-contained portable **folder**. A single file would need static ONNX Runtime, OpenCV and PDFium builds plus embedded models, which the pinned dependencies do not provide. Other platforms are unverified.

## 5. Library SDK (developers)

The CLI is a client of the shared library `pdfbookmark.dll`. The SDK is a CMake package holding both configurations (see `docs/API.md` for its use).

```powershell
cmake --preset release;   cmake --build --preset release --target pdfbookmark
cmake --preset sdk-debug; cmake --build --preset sdk-debug
cmake --install out/build/release   --component sdk --prefix out/sdk/pdfbookmark-0.2.0-win64
cmake --install out/build/sdk-debug --component sdk --prefix out/sdk/pdfbookmark-0.2.0-win64
cd out/sdk; cmake -E tar cf ../../dist/pdfbookmark-sdk-0.2.0-win64.zip --format=zip pdfbookmark-0.2.0-win64
```

**Deliverable: `dist/pdfbookmark-sdk-0.2.0-win64.zip`** (188 MB zipped, 428 MB unzipped; most of it is the Debug OpenCV DLL and the models). It contains:
- `include/`: the public headers, `pdfbookmark.hpp` (C++) and `pdfbookmark.h` (C);
- `lib/`: `pdfbookmark.lib` and `pdfbookmarkd.lib`, plus `lib/cmake/pdfbookmark`;
- `bin/`: the Release DLLs; `bin/debug/`: the Debug dependency DLLs (`pdfbookmarkd.dll` itself is in `bin/`);
- `share/pdfbookmark/models/`;
- `share/doc/pdfbookmark/`: `API.md`, `JSON_FORMATS.md`, `AGENTS.md` (from `docs/AGENTS_SDK.md`), `examples/basic` (C++), `examples/qt-quick` (Qt 6 QML), `examples/python` (a ctypes wrapper over the C API) and licences.

The install fails if any dependency other than an optional Windows component is unresolved. `onnxruntime.dll` is always installed from the pinned ONNX Runtime, because Windows 11 has an unrelated `System32\onnxruntime.dll` that the dependency scan would otherwise pick up and then filter out as a system file.

Clients use `find_package(pdfbookmark 0.1 CONFIG REQUIRED)`, link `pdfbookmark::pdfbookmark`, and call `pdfbookmark_deploy_runtime(<target>)`. On every build, that step copies the matching DLLs and the models (to `<exe dir>/models`, where `find_models()` looks), and skips unchanged files. An upgraded SDK is therefore picked up even when the client doesn't relink.

**Verified (24 Sep 2026)**, with `examples/basic`, a small OCR client and the Python example, built or run from outside the repository against the installed SDK, in both Release and Debug, with only `C:\Windows\System32` on PATH:
- analyze, then apply on a non-ASCII file name: plan ready, 8 verified bookmarks;
- `find_models()` finds the deployed models, and OCR-only text on `tests/text/fixtures/image_only.pdf` reads the 4 expected lines;
- stdout stays clean in Debug.

## 6. Code audit (23 September 2026)

- **Unused functions:** none. A scan of every function defined in `src/` and `apps/` found each one referenced; the strict `/W4 /WX` build also rejects unused internal functions and variables.
- **Removed:** the dead `AnalysisOutcome::Failed` state. It was never produced, because failures are `Result` errors (exit 1).
- **Test-only code:** the S1 fake-OCR factory and the S5 write fault injection are compiled only when `PDFBOOKMARK_TEST_HOOKS=ON` (the default for development builds). Release packages contain neither.
- **Kept on purpose:**
  - Small duplicated helpers across subsystems (UTF-8, trimming, Roman numerals) preserve each subsystem's independence (`AGENTS.md` §2.4).
  - `text/CMakeLists.txt` is a 3-line compatibility entry for existing S1 build scripts.
  - The OCR package's C API is its public ABI.
- **Not in Git** (`.gitignore`): build output (`out/`, `dist/`), IDE state (`.vs/`), downloads (`.deps/`, `models/`), and the local-only OCR reference material (`golden/`, `PaddleOCR/`, `.venv/`).

## 7. Continuous integration and releases (GitHub Actions)

| Workflow | Runs when | Does |
| --- | --- | --- |
| `.github/workflows/ci.yml` | Every push to `main` and every pull request, except documentation-only changes; also manually | `dev` preset: configure, build, all tests (except `golden_parity`, which needs the local-only `golden/`). Then it installs the SDK from that build, installs Qt 6.9.3, builds `examples/qt-quick` against the installed SDK, and runs its `--selftest` |
| `.github/workflows/release.yml` | A pushed tag `vX.Y.Z`; also manually as a dry run | Builds both packages exactly as in §4-5, smoke-tests the CLI with a Windows-only PATH, then publishes a GitHub Release with the two ZIPs. A manual run keeps them as 1-day workflow artifacts instead. |

- **Runner:** `windows-2025-vs2026`, which has Visual Studio 2026, the same as local development.
- **Caching:** pinned downloads (`.deps/downloads`) and vcpkg binaries (qpdf) are cached. Both cache keys change when a pin changes.
- **Credentials:** the OCR models are fetched with the workflow's own `GITHUB_TOKEN` (read access to this repository's releases), so no secrets need to be configured.
- **Minutes:** the repository is private, so runs count against the account's free Actions minutes, and Windows minutes count double. A newer push cancels an older run of the same branch.

**To publish a release:**
1. Set `PDFBOOKMARK_VERSION` in `CMakeLists.txt`.
2. Commit.
3. Push a tag with the same number:

```powershell
git tag v0.2.0
git push origin v0.2.0
```

The workflow refuses a tag that doesn't match `PDFBOOKMARK_VERSION`.

## 8. Measuring OCR memory and time

`tools/bench/` reproduces the figures from issue #1 (decision E-31) for scanned (image-only) pages:

```powershell
python tools/bench/make_scan_fixture.py scan4.pdf 4       # the issue's 4-page fixture (byte-identical)
python tools/bench/make_scan_fixture.py scan20.pdf 20
$exe = "out/build/release/pdfbookmark.exe"; $models = "models"
pwsh tools/bench/ocr_resources.ps1 text4     $exe text     scan4.pdf --mode ocr --models $models --json t4.json --force
pwsh tools/bench/ocr_resources.ps1 metadata4 $exe metadata scan4.pdf --models $models --json m4.json --force
pwsh tools/bench/ocr_resources.ps1 analyze4  $exe analyze  scan4.pdf --models $models --report a4.json --force
pwsh tools/bench/ocr_resources.ps1 text20    $exe text     scan20.pdf --mode ocr --models $models --json t20.json --force
```

- Vary `--dpi` and `--ocr-threads` for the other tables in E-31.
- The script prints elapsed time, peak private bytes, peak working set and system commit.
- The 4-page fixture's SHA-256 is in `make_scan_fixture.py`.
- OCR parity at a given thread count: `out/build/dev/ocr_compare_golden golden models/det/inference.onnx models/rec/inference.onnx models/rec/charset.txt <threads>`. This needs the local-only `golden/`.
