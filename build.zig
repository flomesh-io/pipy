const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // Build options
    const static_lib = b.option(bool, "static-lib", "Build Pipy as static library") orelse false;
    const shared_lib = b.option(bool, "shared", "Build Pipy as dynamic library") orelse false;
    const enable_gui = b.option(bool, "gui", "Include builtin GUI and codebases") orelse false;
    const enable_bpf = b.option(bool, "bpf", "Enable eBPF support") orelse true;
    const use_system_zlib = b.option(bool, "use-system-zlib", "Use system installed zlib") orelse false;
    const use_system_openssl = b.option(bool, "use-system-openssl", "Use system installed OpenSSL") orelse false;

    // Generate version.h
    const gen_version = b.addSystemCommand(&[_][]const u8{
        "sh",
        "generate_version_h.sh",
        "zig-out/version.h",
    });

    // Generate GUI and Codebases tarballs if GUI is enabled
    var gen_gui: ?*std.Build.Step.Run = null;
    var gen_codebases: ?*std.Build.Step.Run = null;
    if (enable_gui) {
        // Create deps directory in zig-out
        const mkdir_deps = b.addSystemCommand(&[_][]const u8{
            "mkdir",
            "-p",
            "zig-out/deps",
        });

        // Generate gui.tar.h
        gen_gui = b.addSystemCommand(&[_][]const u8{
            "node",
            "gui/pack-gui.js",
            "zig-out/deps/gui.tar.h",
        });
        gen_gui.?.step.dependOn(&mkdir_deps.step);

        // Generate codebases.tar.gz.h
        gen_codebases = b.addSystemCommand(&[_][]const u8{
            "node",
            "gui/pack-codebases.js",
            "zig-out/deps/codebases.tar.gz.h",
        });
        gen_codebases.?.step.dependOn(&mkdir_deps.step);
    }

    // Helper function to create C library modules
    const createEmptyModule = struct {
        fn create(builder: *std.Build, tgt: std.Build.ResolvedTarget, opt: std.builtin.OptimizeMode) *std.Build.Module {
            return builder.createModule(.{
                .root_source_file = null,
                .target = tgt,
                .optimize = opt,
            });
        }
    }.create;

    // ==================== Build Dependencies ====================

    // Build yajl (JSON parser)
    const yajl = b.addLibrary(.{
        .name = "yajl",
        .root_module = createEmptyModule(b, target, optimize),
        .linkage = .static,
    });
    yajl.linkLibC();
    yajl.addIncludePath(b.path("deps/yajl-2.1.0/src"));
    yajl.addIncludePath(b.path("deps/yajl-2.1.0/build/yajl-2.1.0/include"));
    yajl.addCSourceFiles(.{
        .files = &[_][]const u8{
            "deps/yajl-2.1.0/src/yajl.c",
            "deps/yajl-2.1.0/src/yajl_alloc.c",
            "deps/yajl-2.1.0/src/yajl_buf.c",
            "deps/yajl-2.1.0/src/yajl_encode.c",
            "deps/yajl-2.1.0/src/yajl_gen.c",
            "deps/yajl-2.1.0/src/yajl_lex.c",
            "deps/yajl-2.1.0/src/yajl_parser.c",
            "deps/yajl-2.1.0/src/yajl_tree.c",
            "deps/yajl-2.1.0/src/yajl_version.c",
        },
        .flags = &[_][]const u8{
            "-Wall",
            "-Wextra",
            "-std=c99",
        },
    });

    // Build libyaml
    const yaml = b.addLibrary(.{
        .name = "yaml",
        .root_module = createEmptyModule(b, target, optimize),
        .linkage = .static,
    });
    yaml.linkLibC();
    yaml.addIncludePath(b.path("deps/libyaml-0.2.5/include"));
    yaml.addIncludePath(b.path("deps/libyaml-0.2.5/src"));
    yaml.addIncludePath(b.path("deps/libyaml-0.2.5/build/include"));
    yaml.addCSourceFiles(.{
        .files = &[_][]const u8{
            "deps/libyaml-0.2.5/src/api.c",
            "deps/libyaml-0.2.5/src/dumper.c",
            "deps/libyaml-0.2.5/src/emitter.c",
            "deps/libyaml-0.2.5/src/loader.c",
            "deps/libyaml-0.2.5/src/parser.c",
            "deps/libyaml-0.2.5/src/reader.c",
            "deps/libyaml-0.2.5/src/scanner.c",
            "deps/libyaml-0.2.5/src/writer.c",
        },
        .flags = &[_][]const u8{ "-Wall", "-DHAVE_CONFIG_H=1" },
    });

    // Build libexpat
    const expat = b.addLibrary(.{
        .name = "expat",
        .root_module = createEmptyModule(b, target, optimize),
        .linkage = .static,
    });
    expat.linkLibC();
    expat.addIncludePath(b.path("deps/libexpat-R_2_2_6/expat/lib"));
    expat.addCSourceFiles(.{
        .files = &[_][]const u8{
            "deps/libexpat-R_2_2_6/expat/lib/xmlparse.c",
            "deps/libexpat-R_2_2_6/expat/lib/xmlrole.c",
            "deps/libexpat-R_2_2_6/expat/lib/xmltok.c",
        },
        .flags = &[_][]const u8{ "-Wall", "-DXML_STATIC=1", "-DHAVE_MEMMOVE=1", "-DHAVE_ARC4RANDOM_BUF=1" },
    });

    // Build zlib
    var zlib: ?*std.Build.Step.Compile = null;
    if (!use_system_zlib) {
        zlib = b.addLibrary(.{
            .name = "z",
            .root_module = createEmptyModule(b, target, optimize),
            .linkage = .static,
        });
        zlib.?.linkLibC();
        zlib.?.addIncludePath(b.path("deps/zlib-1.3.1"));
        zlib.?.addCSourceFiles(.{
            .files = &[_][]const u8{
                "deps/zlib-1.3.1/adler32.c",
                "deps/zlib-1.3.1/compress.c",
                "deps/zlib-1.3.1/crc32.c",
                "deps/zlib-1.3.1/deflate.c",
                "deps/zlib-1.3.1/gzclose.c",
                "deps/zlib-1.3.1/gzlib.c",
                "deps/zlib-1.3.1/gzread.c",
                "deps/zlib-1.3.1/gzwrite.c",
                "deps/zlib-1.3.1/inflate.c",
                "deps/zlib-1.3.1/infback.c",
                "deps/zlib-1.3.1/inftrees.c",
                "deps/zlib-1.3.1/inffast.c",
                "deps/zlib-1.3.1/trees.c",
                "deps/zlib-1.3.1/uncompr.c",
                "deps/zlib-1.3.1/zutil.c",
            },
            .flags = &[_][]const u8{ "-Wall", "-D_LARGEFILE64_SOURCE=1", "-D_FILE_OFFSET_BITS=64" },
        });
    }

    // Build brotli
    const brotli_common = b.addLibrary(.{
        .name = "brotlicommon",
        .root_module = createEmptyModule(b, target, optimize),
        .linkage = .static,
    });
    brotli_common.linkLibC();
    brotli_common.addIncludePath(b.path("deps/brotli-1.0.9/c/include"));
    brotli_common.addCSourceFiles(.{
        .files = &[_][]const u8{
            "deps/brotli-1.0.9/c/common/constants.c",
            "deps/brotli-1.0.9/c/common/context.c",
            "deps/brotli-1.0.9/c/common/dictionary.c",
            "deps/brotli-1.0.9/c/common/platform.c",
            "deps/brotli-1.0.9/c/common/transform.c",
        },
        .flags = &[_][]const u8{"-Wall"},
    });

    const brotli_dec = b.addLibrary(.{
        .name = "brotlidec",
        .root_module = createEmptyModule(b, target, optimize),
        .linkage = .static,
    });
    brotli_dec.linkLibC();
    brotli_dec.addIncludePath(b.path("deps/brotli-1.0.9/c/include"));
    brotli_dec.addCSourceFiles(.{
        .files = &[_][]const u8{
            "deps/brotli-1.0.9/c/dec/bit_reader.c",
            "deps/brotli-1.0.9/c/dec/decode.c",
            "deps/brotli-1.0.9/c/dec/huffman.c",
            "deps/brotli-1.0.9/c/dec/state.c",
        },
        .flags = &[_][]const u8{ "-Wall", "-DBROTLI_BUILD_PORTABLE" },
    });
    brotli_dec.linkLibrary(brotli_common);

    // Build leveldb
    const leveldb = b.addLibrary(.{
        .name = "leveldb",
        .root_module = createEmptyModule(b, target, optimize),
        .linkage = .static,
    });
    leveldb.linkLibCpp();
    leveldb.addIncludePath(b.path("deps/leveldb-1.23/include"));
    leveldb.addIncludePath(b.path("deps/leveldb-1.23"));
    leveldb.addIncludePath(b.path("deps/leveldb-1.23/helpers/memenv"));

    const leveldb_sources = [_][]const u8{
        "deps/leveldb-1.23/db/builder.cc",
        "deps/leveldb-1.23/db/c.cc",
        "deps/leveldb-1.23/db/db_impl.cc",
        "deps/leveldb-1.23/db/db_iter.cc",
        "deps/leveldb-1.23/db/dbformat.cc",
        "deps/leveldb-1.23/db/dumpfile.cc",
        "deps/leveldb-1.23/db/filename.cc",
        "deps/leveldb-1.23/db/log_reader.cc",
        "deps/leveldb-1.23/db/log_writer.cc",
        "deps/leveldb-1.23/db/memtable.cc",
        "deps/leveldb-1.23/db/repair.cc",
        "deps/leveldb-1.23/db/table_cache.cc",
        "deps/leveldb-1.23/db/version_edit.cc",
        "deps/leveldb-1.23/db/version_set.cc",
        "deps/leveldb-1.23/db/write_batch.cc",
        "deps/leveldb-1.23/table/block.cc",
        "deps/leveldb-1.23/table/block_builder.cc",
        "deps/leveldb-1.23/table/filter_block.cc",
        "deps/leveldb-1.23/table/format.cc",
        "deps/leveldb-1.23/table/iterator.cc",
        "deps/leveldb-1.23/table/merger.cc",
        "deps/leveldb-1.23/table/table.cc",
        "deps/leveldb-1.23/table/table_builder.cc",
        "deps/leveldb-1.23/table/two_level_iterator.cc",
        "deps/leveldb-1.23/util/arena.cc",
        "deps/leveldb-1.23/util/bloom.cc",
        "deps/leveldb-1.23/util/cache.cc",
        "deps/leveldb-1.23/util/coding.cc",
        "deps/leveldb-1.23/util/comparator.cc",
        "deps/leveldb-1.23/util/crc32c.cc",
        "deps/leveldb-1.23/util/env.cc",
        "deps/leveldb-1.23/util/env_posix.cc",
        "deps/leveldb-1.23/util/filter_policy.cc",
        "deps/leveldb-1.23/util/hash.cc",
        "deps/leveldb-1.23/util/logging.cc",
        "deps/leveldb-1.23/util/options.cc",
        "deps/leveldb-1.23/util/status.cc",
        "deps/leveldb-1.23/helpers/memenv/memenv.cc",
    };
    leveldb.addCSourceFiles(.{
        .files = &leveldb_sources,
        .flags = &[_][]const u8{
            "-Wall",
            "-std=c++11",
            "-fno-rtti",
            "-Wno-sign-compare",
            "-DLEVELDB_PLATFORM_POSIX=1",
            "-DLEVELDB_COMPILE_LIBRARY=1",
        },
    });

    // ==================== Build SQLite ====================
    const sqlite = b.addLibrary(.{
        .name = "sqlite3",
        .root_module = createEmptyModule(b, target, optimize),
        .linkage = .static,
    });
    sqlite.linkLibC();
    sqlite.addIncludePath(b.path("deps/sqlite-3.43.2"));
    sqlite.addCSourceFiles(.{
        .files = &[_][]const u8{
            "deps/sqlite-3.43.2/sqlite3.c",
        },
        .flags = &[_][]const u8{"-Wall"},
    });

    // ==================== Build Main Pipy ====================

    const pipy_sources = [_][]const u8{
        "src/admin-link.cpp",
        "src/admin-proxy.cpp",
        "src/admin-service.cpp",
        "src/api/algo.cpp",
        "src/api/bgp.cpp",
        "src/api/bpf.cpp",
        "src/api/configuration.cpp",
        "src/api/console.cpp",
        "src/api/crypto.cpp",
        "src/api/c-string.cpp",
        "src/api/c-struct.cpp",
        "src/api/dns.cpp",
        "src/api/hessian.cpp",
        "src/api/http.cpp",
        "src/api/ip.cpp",
        "src/api/json.cpp",
        "src/api/logging.cpp",
        "src/api/os.cpp",
        "src/api/pipeline-api.cpp",
        "src/api/pipy.cpp",
        "src/api/print.cpp",
        "src/api/protobuf.cpp",
        "src/api/resp.cpp",
        "src/api/stats.cpp",
        "src/api/sqlite.cpp",
        "src/api/swap.cpp",
        "src/api/thrift.cpp",
        "src/api/timeout.cpp",
        "src/api/url.cpp",
        "src/api/xml.cpp",
        "src/api/yaml.cpp",
        "src/api/zlib.cpp",
        "src/buffer.cpp",
        "src/codebase.cpp",
        "src/codebase-store.cpp",
        "src/compressor.cpp",
        "src/context.cpp",
        "src/data.cpp",
        "src/deframer.cpp",
        "src/elf.cpp",
        "src/event.cpp",
        "src/event-queue.cpp",
        "src/fetch.cpp",
        "src/file.cpp",
        "src/filter.cpp",
        "src/filters/bgp.cpp",
        "src/filters/branch.cpp",
        "src/filters/chain.cpp",
        "src/filters/compress.cpp",
        "src/filters/connect.cpp",
        "src/filters/decompress.cpp",
        "src/filters/deframe.cpp",
        "src/filters/demux.cpp",
        "src/filters/deposit-message.cpp",
        "src/filters/detect-protocol.cpp",
        "src/filters/dubbo.cpp",
        "src/filters/dummy.cpp",
        "src/filters/dump.cpp",
        "src/filters/exec.cpp",
        "src/filters/fcgi.cpp",
        "src/filters/fork.cpp",
        "src/filters/handle.cpp",
        "src/filters/http.cpp",
        "src/filters/http2.cpp",
        "src/filters/insert.cpp",
        "src/filters/link.cpp",
        "src/filters/link-async.cpp",
        "src/filters/loop.cpp",
        "src/filters/mime.cpp",
        "src/filters/mqtt.cpp",
        "src/filters/mux.cpp",
        "src/filters/netlink.cpp",
        "src/filters/on-body.cpp",
        "src/filters/on-event.cpp",
        "src/filters/on-message.cpp",
        "src/filters/on-start.cpp",
        "src/filters/pack.cpp",
        "src/filters/pipe.cpp",
        "src/filters/print.cpp",
        "src/filters/produce.cpp",
        "src/filters/proxy-protocol.cpp",
        "src/filters/read.cpp",
        "src/filters/repeat.cpp",
        "src/filters/replace.cpp",
        "src/filters/replace-body.cpp",
        "src/filters/replace-event.cpp",
        "src/filters/replace-message.cpp",
        "src/filters/replace-start.cpp",
        "src/filters/replay.cpp",
        "src/filters/resp.cpp",
        "src/filters/socks.cpp",
        "src/filters/split.cpp",
        "src/filters/swap.cpp",
        "src/filters/tee.cpp",
        "src/filters/thrift.cpp",
        "src/filters/throttle.cpp",
        "src/filters/tls.cpp",
        "src/filters/use.cpp",
        "src/filters/wait.cpp",
        "src/filters/websocket.cpp",
        "src/fs.cpp",
        "src/fstream.cpp",
        "src/graph.cpp",
        "src/gui-tarball.cpp",
        "src/inbound.cpp",
        "src/input.cpp",
        "src/kmp.cpp",
        "src/listener.cpp",
        "src/log.cpp",
        "src/main.cpp",
        "src/main-options.cpp",
        "src/message.cpp",
        "src/module.cpp",
        "src/net.cpp",
        "src/nmi.cpp",
        "src/options.cpp",
        "src/os-platform.cpp",
        "src/outbound.cpp",
        "src/pipeline.cpp",
        "src/pipeline-lb.cpp",
        "src/pjs/builtin.cpp",
        "src/pjs/expr.cpp",
        "src/pjs/module.cpp",
        "src/pjs/parser.cpp",
        "src/pjs/stmt.cpp",
        "src/pjs/tree.cpp",
        "src/pjs/types.cpp",
        "src/signal.cpp",
        "src/socket.cpp",
        "src/status.cpp",
        "src/store.cpp",
        "src/str-map.cpp",
        "src/table.cpp",
        "src/tar.cpp",
        "src/task.cpp",
        "src/thread.cpp",
        "src/timer.cpp",
        "src/utils.cpp",
        "src/watch.cpp",
        "src/worker.cpp",
        "src/worker-thread.cpp",
    };

    const pipy_flags = [_][]const u8{
        "-Wall",
        "-Wno-overloaded-virtual",
        "-Wno-delete-non-virtual-dtor",
        "-Wno-sign-compare",
        "-Wno-deprecated-declarations",
        "-std=c++11",
        "-D_GNU_SOURCE",
        "-DXML_STATIC=1",
    };

    const pipy_exe = if (shared_lib)
        b.addLibrary(.{
            .name = "pipy",
            .root_module = createEmptyModule(b, target, optimize),
            .linkage = .dynamic,
        })
    else if (static_lib)
        b.addLibrary(.{
            .name = "pipy",
            .root_module = createEmptyModule(b, target, optimize),
            .linkage = .static,
        })
    else
        b.addExecutable(.{
            .name = "pipy",
            .root_module = createEmptyModule(b, target, optimize),
        });

    pipy_exe.linkLibCpp();
    pipy_exe.linkLibC();

    // Add include paths
    pipy_exe.addIncludePath(b.path("src"));
    pipy_exe.addIncludePath(b.path("include"));
    pipy_exe.addIncludePath(b.path("deps/asio-1.28.0/include"));
    pipy_exe.addIncludePath(b.path("deps/yajl-2.1.0/src"));
    pipy_exe.addIncludePath(b.path("deps/yajl-2.1.0/build/yajl-2.1.0/include"));
    pipy_exe.addIncludePath(b.path("deps/libyaml-0.2.5/include"));
    pipy_exe.addIncludePath(b.path("deps/libexpat-R_2_2_6/expat/lib"));
    pipy_exe.addIncludePath(b.path("deps/leveldb-1.23/include"));
    pipy_exe.addIncludePath(b.path("deps/sqlite-3.43.2"));
    pipy_exe.addIncludePath(b.path("deps/brotli-1.0.9/c/include"));
    pipy_exe.addIncludePath(b.path("zig-out"));

    if (!use_system_zlib) {
        pipy_exe.addIncludePath(b.path("deps/zlib-1.3.1"));
    }

    if (!use_system_openssl) {
        pipy_exe.addIncludePath(b.path("deps/openssl-3.2.0/include"));
        pipy_exe.addIncludePath(b.path("deps/openssl-3.2.0/build/include"));
    }

    // Build compile flags with macros
    var pipy_all_flags: [20][]const u8 = undefined;
    var flag_count: usize = 0;

    // Copy base flags
    for (pipy_flags) |flag| {
        pipy_all_flags[flag_count] = flag;
        flag_count += 1;
    }

    // Add macro definitions
    pipy_all_flags[flag_count] = "-DPIPY_HOST=\"Zig Build\"";
    flag_count += 1;

    if (shared_lib or static_lib) {
        pipy_all_flags[flag_count] = "-DPIPY_SHARED";
        flag_count += 1;
    }
    if (enable_gui) {
        pipy_all_flags[flag_count] = "-DPIPY_USE_GUI";
        flag_count += 1;
        pipy_all_flags[flag_count] = "-DPIPY_USE_CODEBASES";
        flag_count += 1;
    }
    if (enable_bpf) {
        pipy_all_flags[flag_count] = "-DPIPY_USE_BPF";
        flag_count += 1;
    }

    // Add C++ source files
    pipy_exe.addCSourceFiles(.{
        .files = &pipy_sources,
        .flags = pipy_all_flags[0..flag_count],
    });

    // Link dependencies
    pipy_exe.linkLibrary(yajl);
    pipy_exe.linkLibrary(yaml);
    pipy_exe.linkLibrary(expat);
    pipy_exe.linkLibrary(brotli_dec);
    pipy_exe.linkLibrary(leveldb);
    pipy_exe.linkLibrary(sqlite);

    if (zlib) |z| {
        pipy_exe.linkLibrary(z);
    } else {
        pipy_exe.linkSystemLibrary("z");
    }

    // Link OpenSSL
    if (use_system_openssl) {
        // Add Homebrew OpenSSL paths for macOS
        if (target.result.os.tag == .macos) {
            // Use the actual OpenSSL path that exists
            const openssl_path = "/usr/local/opt/openssl@3";
            pipy_exe.addLibraryPath(.{ .cwd_relative = b.fmt("{s}/lib", .{openssl_path}) });
            pipy_exe.addIncludePath(.{ .cwd_relative = b.fmt("{s}/include", .{openssl_path}) });
        }
        pipy_exe.linkSystemLibrary("ssl");
        pipy_exe.linkSystemLibrary("crypto");
    } else {
        // Link statically built OpenSSL
        pipy_exe.addObjectFile(b.path("deps/openssl-3.2.0/build/libssl.a"));
        pipy_exe.addObjectFile(b.path("deps/openssl-3.2.0/build/libcrypto.a"));
    }

    // Platform-specific linking
    if (target.result.os.tag != .windows) {
        pipy_exe.linkSystemLibrary("pthread");
        pipy_exe.linkSystemLibrary("dl");
        pipy_exe.linkSystemLibrary("util");
    }

    // Add version generation dependency
    pipy_exe.step.dependOn(&gen_version.step);

    // Add GUI generation dependency if enabled
    if (gen_gui) |gui| {
        pipy_exe.step.dependOn(&gui.step);
        // Add zig-out/deps as include path for gui.tar.h
        pipy_exe.addIncludePath(b.path("zig-out/deps"));
    }

    // Add Codebases generation dependency if enabled
    if (gen_codebases) |codebases| {
        pipy_exe.step.dependOn(&codebases.step);
        // Add zig-out/deps as include path for codebases.tar.gz.h
        pipy_exe.addIncludePath(b.path("zig-out/deps"));
    }

    // Install artifact
    b.installArtifact(pipy_exe);

    // Add run step
    if (!shared_lib and !static_lib) {
        const run_cmd = b.addRunArtifact(pipy_exe);
        run_cmd.step.dependOn(b.getInstallStep());

        if (b.args) |args| {
            run_cmd.addArgs(args);
        }

        const run_step = b.step("run", "Run pipy");
        run_step.dependOn(&run_cmd.step);
    }

    // Add test step for benchmark baseline
    const test_step = b.step("test", "Run tests");
    _ = test_step;
}
