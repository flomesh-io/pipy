# 使用 Zig 构建 Pipy

本项目现在支持使用 Zig 作为 CMake 的替代构建系统。

## 前提条件

- Zig 0.13.0 或更高版本（已在 0.16.0-dev 上测试）
- C/C++ 编译器（用于构建依赖项）
- Git（用于版本信息）
- Perl（用于构建 OpenSSL）
- Node.js（仅在使用 `-Dgui=true` 构建时需要，用于打包 GUI 和 codebases）

## 构建步骤

### macOS 快速开始（推荐）

在 macOS 上，推荐使用系统库进行构建：

```bash
# 1. 安装 Homebrew OpenSSL（如果尚未安装）
brew install openssl@3

# 2. 配置 yajl（首次构建必需）
cd deps/yajl-2.1.0 && ./configure && cd ../..

# 3. 构建 Pipy（使用系统库，禁用 BPF，优化模式）
# 使用 -Doptimize=ReleaseFast 进行生产构建（二进制文件更小：~15MB vs ~167MB）
zig build -Doptimize=ReleaseFast -Duse-system-zlib=true -Dbpf=false -Duse-system-openssl=true

# 4. 运行
./zig-out/bin/pipy --version
```

**注意**：macOS 不支持 eBPF，必须使用 `-Dbpf=false` 参数。

### Linux 快速开始

在 Linux 上可以选择使用捆绑的 OpenSSL 或系统库：

```bash
# 选项 1: 使用捆绑的 OpenSSL（推荐用于生产环境）
./build-openssl.sh
zig build -Doptimize=ReleaseFast

# 选项 2: 使用系统 OpenSSL（推荐用于生产环境）
zig build -Doptimize=ReleaseFast -Duse-system-zlib=true -Duse-system-openssl=true
```

### 详细构建步骤

#### 1. 构建 OpenSSL（使用捆绑版本时需要）

如果不使用 `-Duse-system-openssl=true`，需要先构建捆绑的 OpenSSL：

```bash
./build-openssl.sh
```

#### 2. 配置依赖库（首次构建）

```bash
# 配置 yajl（生成必要的头文件）
cd deps/yajl-2.1.0 && ./configure && cd ../..
```

#### 3. 构建 Pipy

**基本构建（Debug 模式，用于开发）：**

```bash
# Debug 构建：较大的二进制文件（~167MB），包含调试符号
zig build -Duse-system-zlib=true -Dbpf=false -Duse-system-openssl=true
```

**优化构建（生产环境）：**

```bash
# Release 构建：较小的二进制文件（~15MB），速度优化
zig build -Doptimize=ReleaseFast -Duse-system-zlib=true -Dbpf=false -Duse-system-openssl=true
```

**启用 GUI 和内置 codebases：**

```bash
# 注意：GUI 选项会自动包含内置 codebases（tutorial 和 samples）
# 需要安装 Node.js 来打包 GUI 和 codebases
# 优化模式下：~29MB（相比 Debug 模式的 ~180MB）
zig build -Doptimize=ReleaseFast -Dgui=true -Duse-system-zlib=true -Dbpf=false -Duse-system-openssl=true
```

**构建为共享库：**

```bash
zig build -Doptimize=ReleaseFast -Dshared=true -Duse-system-zlib=true -Dbpf=false -Duse-system-openssl=true
```

**构建为静态库：**

```bash
zig build -Doptimize=ReleaseFast -Dstatic-lib=true -Duse-system-zlib=true -Dbpf=false -Duse-system-openssl=true
```

### 3. 运行 Pipy

```bash
zig build run
```

或带参数运行：

```bash
zig build run -- --help
```

## 构建选项

- `-Doptimize=<mode>` - 构建优化模式（默认：Debug）
  - `Debug` - 无优化，包含调试符号（基础版 ~167MB，带 GUI ~180MB）
  - `ReleaseSafe` - 优化并保留安全检查
  - `ReleaseFast` - 最大速度优化（基础版 ~15MB，带 GUI ~29MB）**推荐用于生产环境**
  - `ReleaseSmall` - 体积优化
- `-Dshared=<bool>` - 构建为共享库（默认：false）
- `-Dstatic-lib=<bool>` - 构建为静态库（默认：false）
- `-Dgui=<bool>` - 包含内置 GUI 和 codebases（tutorial/samples）（默认：false，需要 Node.js）
- `-Dbpf=<bool>` - 启用 eBPF 支持（默认：true，macOS 不支持）
- `-Duse-system-zlib=<bool>` - 使用系统 zlib 而不是捆绑版本（默认：false）
- `-Duse-system-openssl=<bool>` - 使用系统 OpenSSL 而不是捆绑版本（默认：false）

## 输出目录

构建产物放置在 `zig-out/` 目录中：

- 可执行文件：`zig-out/bin/pipy`
- 库文件：`zig-out/lib/`

### 文件大小对比

| 构建配置 | 二进制文件大小 | 说明 |
|---------|--------------|------|
| Debug 模式（基础） | ~167MB | 包含调试符号，用于开发 |
| Debug 模式 + GUI | ~180MB | 包含调试符号 + GUI + codebases |
| ReleaseFast（基础） | ~15MB | 优化后，推荐生产环境 |
| ReleaseFast + GUI | ~29MB | 优化后 + GUI + codebases |

**优化效果：使用 ReleaseFast 可以将文件大小减少约 91%！**

## 清理

清理构建产物：

```bash
rm -rf zig-out .zig-cache
```

完全清理（包括 OpenSSL）：

```bash
rm -rf zig-out .zig-cache deps/openssl-3.2.0/build
```

## 使用 Makefile 包装器

为了方便使用，我们提供了一个 Makefile 包装器：

```bash
# 使用 Makefile.zig
make -f Makefile.zig help       # 显示帮助信息
make -f Makefile.zig build      # 构建 Pipy
make -f Makefile.zig release    # 发布构建
make -f Makefile.zig clean      # 清理构建产物
make -f Makefile.zig install    # 安装到 /usr/local/bin
```

## 与 CMake 的比较

### Zig 构建系统的优势：

1. **单一工具**：无需 CMake、Make、Ninja 等多个工具
2. **交叉编译**：轻松交叉编译到不同目标平台
3. **可重现构建**：跨平台一致的构建结果
4. **快速增量构建**：高效的缓存系统
5. **更简单的语法**：更易读的构建配置
6. **内置包管理器**：轻松管理依赖项

### 迁移说明：

- Zig 构建系统产生与 CMake 相同的输出
- 所有 CMake 构建选项都有对应的 Zig 等价选项
- OpenSSL 仍然需要外部构建（与 CMake 相同）
- 版本信息生成方式相同

## 故障排除

### macOS: elf.h 找不到

**错误信息**：`error: 'elf.h' file not found`

**解决方案**：macOS 不支持 eBPF，需要禁用 BPF 支持：
```bash
zig build -Dbpf=false -Duse-system-zlib=true -Duse-system-openssl=true
```

### macOS: OpenSSL 找不到

**错误信息**：`unable to find dynamic system library 'ssl'`

**解决方案**：安装 Homebrew OpenSSL：
```bash
brew install openssl@3
```

然后使用 `-Duse-system-openssl=true` 构建。

### yajl 头文件缺失

**错误信息**：`'yajl/yajl_version.h' file not found`

**解决方案**：运行 yajl 配置脚本：
```bash
cd deps/yajl-2.1.0 && ./configure && cd ../..
```

### libyaml 版本宏未定义

**错误信息**：`use of undeclared identifier 'YAML_VERSION_STRING'`

**解决方案**：这已在 build.zig 中自动处理，确保使用最新版本的构建脚本。

### zlib 类型冲突

**错误信息**：`conflicting types for 'crc32_combine64'`

**解决方案**：使用系统 zlib 代替捆绑版本：
```bash
zig build -Duse-system-zlib=true
```

### OpenSSL 构建失败

确保已安装 Perl：

```bash
# macOS
brew install perl

# Ubuntu/Debian
sudo apt-get install perl

# CentOS/RHEL
sudo yum install perl
```

### 缺少依赖项

如果遇到缺少系统库的问题：

```bash
# macOS
brew install openssl@3

# Ubuntu/Debian
sudo apt-get install libssl-dev zlib1g-dev

# CentOS/RHEL
sudo yum install openssl-devel zlib-devel
```

### Zig 版本问题

检查 Zig 版本：

```bash
zig version
```

如需要，更新 Zig：https://ziglang.org/download/

## 项目结构

```
pipy/
├── build.zig                # Zig 构建脚本（替代 CMakeLists.txt）
├── build.zig.zon           # Zig 包配置文件
├── build-openssl.sh        # OpenSSL 构建辅助脚本
├── Makefile.zig            # Makefile 包装器
├── README-ZIG.md           # 英文文档
├── README-ZIG-CN.md        # 中文文档（本文件）
├── src/                    # 源代码
├── deps/                   # 依赖库
└── zig-out/                # 构建输出目录
```

## 构建时间对比

使用 Zig 构建系统相比 CMake 的优势：

- **首次构建**：相似或略快
- **增量构建**：明显更快（得益于 Zig 的缓存系统）
- **清理构建**：更快（更好的并行化）

## 常见问题

### 1. Zig 构建系统支持所有 CMake 选项吗？

是的，所有主要的 CMake 选项都已在 Zig 构建系统中实现。

### 2. 可以同时保留 CMake 和 Zig 构建系统吗？

可以，两个构建系统可以共存。`build.zig` 不会影响现有的 CMake 配置。

### 3. 如何在 CI/CD 中使用 Zig 构建？

只需将 CMake 命令替换为 `zig build` 命令即可。例如：

```bash
# 以前使用 CMake
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 现在使用 Zig
./build-openssl.sh
zig build -Doptimize=ReleaseFast
```

### 4. Zig 构建是否支持 Windows？

是的，Zig 构建系统支持 Windows、macOS 和 Linux。

## 更多信息

- Zig 官方文档：https://ziglang.org/documentation/master/
- Zig 构建系统指南：https://ziglang.org/learn/build-system/
- Pipy 主文档：见 README.md

## 贡献

如果您在使用 Zig 构建系统时遇到问题，或有改进建议，欢迎提交 Issue 或 Pull Request。
