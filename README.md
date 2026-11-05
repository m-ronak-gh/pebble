# PebbleBit

PebbleBit is a small, statically typed programming language designed for teaching compiler construction and systems programming. It compiles directly to native executables using LLVM.

---

## Supported Platforms

PebbleBit compiles to native binaries on:

1. **Linux** (x86_64, aarch64) — ELF binaries linked via `cc`/`clang`.
2. **macOS** (Apple Silicon arm64, Intel x86_64) — Mach-O binaries linked via Apple `clang`.
3. **Windows** (x86_64) — Native PE/COFF `.exe` binaries. A standalone Windows installer is available, and source builds are supported through MSYS2 / MinGW-w64.

---

## Standalone Windows Installer (x64)

PebbleBit v1.0.0 is available as a standalone installer for 64-bit Windows.

**Installer:** `PebbleBit-Setup-1.0.0.exe`

Download the installer from the [PebbleBit GitHub Releases page](https://github.com/m-ronak-gh/pebblebit/releases).

The standalone distribution includes the PebbleBit compiler and the toolchain required to compile PebbleBit programs into native Windows executables.

### Installation

1. Download `PebbleBit-Setup-1.0.0.exe`.
2. Run the installer.
3. Follow the installation wizard.
4. The default installation directory is:

   `%LOCALAPPDATA%\Programs\PebbleBit`

5. Administrator privileges are not required.

During installation, you can optionally select:

**Add PebbleBit to your user PATH (recommended)**

When selected, the installer's `bin` directory is added to your user PATH, allowing you to invoke `pbit` and `clang` from a new terminal.

When this option is not selected, PebbleBit can still be used by specifying its installation paths directly.

### Included Components

The standalone Windows distribution bundles:

- **PebbleBit compiler:** `pbit.exe`
- **Clang:** Bundled C compiler (version 22.1.8).
- **LLD:** LLVM linker.
- Required LLVM and toolchain runtime DLLs.
- Clang resource headers.
- Windows C headers and linker libraries.
- PebbleBit C runtime.
- PebbleBit standard library modules.
- Project documentation.
- Third-party license notices.

The standalone installer does not require users to separately install MSYS2, LLVM, Clang, GCC, or GNU Make.

### Verify Installation

After installation, open a new PowerShell or Command Prompt window.

If PebbleBit was added to PATH, run:

```powershell
pbit
clang --version
```

The first command displays PebbleBit compiler usage information. The second displays the bundled Clang version.

### Using PebbleBit Without Adding It to PATH

If you did not select the PATH option, you can invoke PebbleBit directly.

In PowerShell:

```powershell
$pebbleBin = Join-Path $env:LOCALAPPDATA 'Programs\PebbleBit\bin'

$env:PEBBLEBIT_CC = Join-Path $pebbleBin 'clang.exe'

& (Join-Path $pebbleBin 'pbit.exe') build main.pb
```

The `PEBBLEBIT_CC` environment variable tells PebbleBit which C compiler to use for linking generated native executables.

The commands above configure the current PowerShell session.

### Compile and Run Programs

Compile a PebbleBit source file into a native Windows executable:

```powershell
pbit build main.pb
```

By default, this produces:

```text
main.exe
```

Specify an explicit output filename:

```powershell
pbit build main.pb -o myprogram.exe
```

Compile and execute a program in one step:

```powershell
pbit run main.pb
```

This compiles the source file and executes the resulting native program.

To preserve generated LLVM IR:

```powershell
pbit build main.pb --keep-ir
```

### Uninstallation

PebbleBit can be removed through Windows Installed Apps or the supplied uninstaller.

The uninstaller removes the PebbleBit installation directory.

PATH cleanup follows these rules:

- If the installer added PebbleBit to PATH, the corresponding entry is removed during uninstallation.
- If the PATH entry existed before installation, it is preserved.
- Other user PATH entries are preserved.

---

## Toolchain & Dependencies

The following dependencies are required when building PebbleBit from source.

Users of the standalone Windows installer do not need to install these dependencies separately.

PebbleBit relies on LLVM for code generation and a host C compiler/linker to produce final executables.

- **LLVM Version:** LLVM 18 (recommended: 18.1.x; compatible with LLVM 16+ via LLVM C API).
  - Tools needed: `llvm-config`, `opt`, `clang`.
- **C Compiler:** C17-compliant compiler (`gcc` or `clang`).
- **Make:** GNU Make.

### Installing Dependencies

#### Ubuntu / Debian (Linux)

```bash
sudo apt-get update
sudo apt-get install -y gcc clang llvm-18 llvm-18-dev make
sudo update-alternatives --install /usr/bin/llvm-config llvm-config /usr/bin/llvm-config-18 100
sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-18 100
sudo update-alternatives --install /usr/bin/opt opt /usr/bin/opt-18 100
```

#### macOS (Homebrew)

```bash
brew install llvm@18 make
export PATH="$(brew --prefix llvm@18)/bin:$PATH"
export CC="$(brew --prefix llvm@18)/bin/clang"
```

#### Windows (MSYS2 / MinGW64 or CLANG64)

From an MSYS2 shell (`clang64` environment):

```bash
pacman -S mingw-w64-clang-x86_64-clang \
          mingw-w64-clang-x86_64-llvm \
          mingw-w64-clang-x86_64-make \
          git
```

---

## Building PebbleBit from Source

Clone the repository and build using GNU Make:

```bash
make
```

This compiles the driver binary `pbit` (or `pbit.exe` on Windows).

### Running Tests

```bash
# Run unit tests, codegen tests, and end-to-end driver integration tests:
make test
```

### Packaging & Installation

To create a release package:

```bash
make package
```

This produces `dist/pebblebit-<platform>.tar.gz` containing `bin/pbit`, `runtime/pebblebit_runtime.c`, `runtime/pebblebit_runtime.h`, and documentation.

To install system-wide (default `PREFIX=/usr/local`):

```bash
sudo make install
```

---

## Usage Guide

### Compiling a PebbleBit Program (`build`)

To compile a PebbleBit program into a native executable:

```bash
pbit build main.pb
```

By default, the executable is created with the source stem name (`main` on Linux/macOS, `main.exe` on Windows).

To specify an explicit output path:

```bash
pbit build main.pb -o myprogram
```

To preserve generated LLVM IR (`.ll` file):

```bash
pbit build main.pb --keep-ir
```

### Compiling and Running in One Step (`run`)

```bash
pbit run main.pb
```

This compiles the program and executes it immediately, returning the program's exit code.

---

## Modules and Imports

PebbleBit supports multi-file programs via relative imports:

```pebblebit
import "math_helper.pb";

function number main() {
    print multiply(6, 7);
    return 0;
}
```

Imports are resolved relative to the directory of the file containing the `import` statement. Diamond dependencies and circular imports are deduplicated automatically.

---

## Runtime Resource Discovery

The PebbleBit compiler (`pbit`) locates its C runtime (`pebblebit_runtime.c` and `pebblebit_runtime.h`) automatically, completely independent of the user's current working directory:

1. **Environment Variable Override:** If `PEBBLEBIT_RUNTIME_PATH` or `PEBBLEBIT_RUNTIME_DIR` is set, the specified path/directory is checked first.
2. **Compiler Binary Directory:** The compiler discovers its own executable location and checks:
   - `<bin_dir>/runtime/pebblebit_runtime.c` (repository layout)
   - `<bin_dir>/../runtime/pebblebit_runtime.c` (release package layout)
   - `<bin_dir>/pebblebit_runtime.c` (flat/portable layout)
   - `<bin_dir>/../lib/pebblebit/pebblebit_runtime.c` (system installation layout)
3. **Current Working Directory:** Fallback to `./runtime/pebblebit_runtime.c` or `./pebblebit_runtime.c`.
