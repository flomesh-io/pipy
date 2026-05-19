# Building Pipy with Zig

This project now supports building with Zig as an alternative to CMake.

## Prerequisites

- Zig 0.13.0 or later
- A C/C++ compiler (for building dependencies)
- Git (for version information)
- Perl (for building OpenSSL)
- Node.js (required only when building with `-Dgui=true` for packing GUI and codebases)

## Building

### macOS Quick Start (Recommended)

On macOS, it's recommended to use system libraries:

```bash
# 1. Install Homebrew OpenSSL (if not already installed)
brew install openssl@3

# 2. Configure yajl (required for first build)
cd deps/yajl-2.1.0 && ./configure && cd ../..

# 3. Build Pipy (using system libraries, BPF disabled, optimized)
# Use -Doptimize=ReleaseFast for production builds (much smaller binary: ~15MB vs ~167MB)
zig build -Doptimize=ReleaseFast -Duse-system-zlib=true -Dbpf=false -Duse-system-openssl=true

# 4. Run
./zig-out/bin/pipy --version
```

**Note**: macOS doesn't support eBPF, so you must use `-Dbpf=false`.

### Linux Quick Start

On Linux, you can choose between bundled OpenSSL or system libraries:

```bash
# Option 1: Use bundled OpenSSL (recommended for production)
./build-openssl.sh
zig build -Doptimize=ReleaseFast

# Option 2: Use system OpenSSL (recommended for production)
zig build -Doptimize=ReleaseFast -Duse-system-zlib=true -Duse-system-openssl=true
```

### Detailed Build Steps

#### 1. Build OpenSSL (when using bundled version)

If not using `-Duse-system-openssl=true`, build the bundled OpenSSL first:

```bash
./build-openssl.sh
```

#### 2. Configure Dependencies (first-time build)

```bash
# Configure yajl (generates necessary headers)
cd deps/yajl-2.1.0 && ./configure && cd ../..
```

#### 3. Build Pipy

**Basic build (Debug mode, for development):**

```bash
# Debug build: large binary (~167MB), includes debug symbols
zig build -Duse-system-zlib=true -Dbpf=false -Duse-system-openssl=true
```

**Optimized build (Production):**

```bash
# Release build: small binary (~15MB), optimized for speed
zig build -Doptimize=ReleaseFast -Duse-system-zlib=true -Dbpf=false -Duse-system-openssl=true
```

**Build with GUI and builtin codebases:**

```bash
# Note: GUI option automatically includes builtin codebases (tutorial and samples)
# Requires Node.js to be installed for packing GUI and codebases
# With optimization: ~29MB (vs ~180MB in Debug mode)
zig build -Doptimize=ReleaseFast -Dgui=true -Duse-system-zlib=true -Dbpf=false -Duse-system-openssl=true
```

**Build as a shared library:**

```bash
zig build -Doptimize=ReleaseFast -Dshared=true -Duse-system-zlib=true -Dbpf=false -Duse-system-openssl=true
```

**Build as a static library:**

```bash
zig build -Doptimize=ReleaseFast -Dstatic-lib=true -Duse-system-zlib=true -Dbpf=false -Duse-system-openssl=true
```

### 3. Run Pipy

```bash
zig build run
```

Or run with arguments:

```bash
zig build run -- --help
```

## Build Options

- `-Doptimize=<mode>` - Build optimization mode (default: Debug)
  - `Debug` - No optimization, includes debug symbols (~167MB for base, ~180MB with GUI)
  - `ReleaseSafe` - Optimized with safety checks
  - `ReleaseFast` - Maximum optimization for speed (~15MB for base, ~29MB with GUI) **Recommended for production**
  - `ReleaseSmall` - Optimized for size
- `-Dshared=<bool>` - Build Pipy as a shared library (default: false)
- `-Dstatic-lib=<bool>` - Build Pipy as a static library (default: false)
- `-Dgui=<bool>` - Include builtin GUI and codebases (tutorial/samples) (default: false, requires Node.js)
- `-Dbpf=<bool>` - Enable eBPF support (default: true, not supported on macOS)
- `-Duse-system-zlib=<bool>` - Use system zlib instead of bundled (default: false)
- `-Duse-system-openssl=<bool>` - Use system OpenSSL instead of bundled (default: false)

## Output

Built artifacts are placed in `zig-out/`:

- Executable: `zig-out/bin/pipy`
- Libraries: `zig-out/lib/`

### Binary Size Comparison

| Build Configuration | Binary Size | Notes |
|---------------------|-------------|-------|
| Debug (base) | ~167MB | Includes debug symbols, for development |
| Debug + GUI | ~180MB | Debug symbols + GUI + codebases |
| ReleaseFast (base) | ~15MB | Optimized, recommended for production |
| ReleaseFast + GUI | ~29MB | Optimized + GUI + codebases |

**Optimization Impact: ReleaseFast reduces binary size by approximately 91%!**

## Cleaning

To clean build artifacts:

```bash
rm -rf zig-out .zig-cache
```

## Comparison with CMake

### Advantages of Zig build system:

1. **Single tool**: No need for CMake, Make, Ninja, etc.
2. **Cross-compilation**: Easy cross-compilation to different targets
3. **Reproducible builds**: Consistent builds across platforms
4. **Fast incremental builds**: Efficient caching system
5. **Simpler syntax**: More readable build configuration
6. **Built-in package manager**: Easy dependency management

### Migration notes:

- The Zig build system produces the same output as CMake
- All CMake build options have Zig equivalents
- OpenSSL still requires external build (same as CMake)
- Version information is generated the same way

## Troubleshooting

### macOS: elf.h not found

**Error**: `error: 'elf.h' file not found`

**Solution**: macOS doesn't support eBPF, disable BPF support:
```bash
zig build -Dbpf=false -Duse-system-zlib=true -Duse-system-openssl=true
```

### macOS: OpenSSL not found

**Error**: `unable to find dynamic system library 'ssl'`

**Solution**: Install Homebrew OpenSSL:
```bash
brew install openssl@3
```

Then build with `-Duse-system-openssl=true`.

### yajl headers missing

**Error**: `'yajl/yajl_version.h' file not found`

**Solution**: Run the yajl configure script:
```bash
cd deps/yajl-2.1.0 && ./configure && cd ../..
```

### libyaml version macros undefined

**Error**: `use of undeclared identifier 'YAML_VERSION_STRING'`

**Solution**: This is automatically handled in build.zig, ensure you're using the latest build script.

### zlib type conflicts

**Error**: `conflicting types for 'crc32_combine64'`

**Solution**: Use system zlib instead of bundled version:
```bash
zig build -Duse-system-zlib=true
```

### OpenSSL build fails

Make sure you have Perl installed:
```bash
# macOS
brew install perl

# Ubuntu/Debian
sudo apt-get install perl

# CentOS/RHEL
sudo yum install perl
```

### Missing dependencies

If you encounter missing system libraries:

```bash
# macOS
brew install openssl@3

# Ubuntu/Debian
sudo apt-get install libssl-dev zlib1g-dev

# CentOS/RHEL
sudo yum install openssl-devel zlib-devel
```

### Zig version issues

Check your Zig version:
```bash
zig version
```

Update Zig if needed: https://ziglang.org/download/

## Further Information

- Zig documentation: https://ziglang.org/documentation/master/
- Zig build system guide: https://ziglang.org/learn/build-system/
- Pipy documentation: See main README.md
