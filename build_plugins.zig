// Builds clumsy plugin DLLs independently from the main clumsy build.
//
// USAGE
//
//   zig build --build-file build_plugins.zig
//   zig build --build-file build_plugins.zig -Darch=x86
//   zig build --build-file build_plugins.zig -Dconf=Release
//   zig build --build-file build_plugins.zig -Dclumsy_dir=PATH
//
// Plugins link back into clumsy.exe for node-list/UI-sync helpers, so
// clumsy.exe must be built (with matching -Darch/-Dconf/-Dsign) before you
// build plugins - otherwise clumsy.lib won't exist yet and linking fails.
//
// OUTPUT
//   plugins/lag.dll (and any other plugins you add)
//
// ADDING A NEW PLUGIN
//   1. Write src/yourmodule.c (use lag_module.c as template)
//   2. Call addPlugin(b, ...) at the bottom of build() copy the lag example
//
// REQUIREMENTS
//   - Zig 0.9.1
//   - WinDivert 2.2.0 in a external folder
//   - IUP DLL package in external/iup-3.30_Win*_dll16_lib
//   - clumsy.lib from a matching main `zig build` run
//   - clumsy_plugin.h in src/

const std = @import("std");
const Builder = std.build.Builder;
const Step = std.build.Step;
const CrossTarget = std.zig.CrossTarget;
const debug = std.debug;

const ClumsyArch = enum { x86, x64 };
const ClumsyConf = enum { Debug, Release };
const ClumsyWinDivertSign = enum { A, B, C };

// Shared config passed to addPlugin()
const PluginConfig = struct {
    arch: ClumsyArch,
    conf: ClumsyConf,
    target: CrossTarget,
    iup_lib: []const u8,
    windivert_inc: []const u8,
    windivert_lib: []const u8,
    clumsy_lib_dir: []const u8,
    out_dir: []const u8,
};

pub fn build(b: *Builder) void {
    const arch = b.option(ClumsyArch, "arch", "x86 or x64 (default: x64)") orelse .x64;
    const conf = b.option(ClumsyConf, "conf", "Debug or Release (default: Debug)") orelse .Debug;
    const sign = b.option(ClumsyWinDivertSign, "sign", "WinDivert package letter A/B/C (default: A)") orelse .A;
    const arch_tag = @tagName(arch);
    const conf_tag = @tagName(conf);
    const sign_tag = @tagName(sign);

    debug.print("build_plugins.zig — arch:{s}, conf:{s}, sign:{s}\n", .{ arch_tag, conf_tag, sign_tag });

    const windivert_dir = b.fmt("external/WinDivert-2.2.0-{s}", .{sign_tag});
    const iup_lib = switch (arch) {
        .x64 => "external/iup-3.30_Win64_dll16_lib",
        .x86 => "external/iup-3.30_Win32_dll16_lib",
    };

    const triple = switch (arch) {
        .x64 => "x86_64-windows-gnu",
        .x86 => "i386-windows-gnu",
    };

    const target = CrossTarget.parse(.{ .arch_os_abi = triple }) catch unreachable;
    const out_dir = b.fmt("plugins/{s}_{s}", .{ arch_tag, conf_tag });
    b.exe_dir = b.fmt("{s}/{s}", .{ b.install_path, out_dir });
    const default_clumsy_dir = b.fmt("zig-out/{s}_{s}_{s}", .{ arch_tag, conf_tag, sign_tag });
    const clumsy_lib_dir = b.option([]const u8, "clumsy_dir", "Folder containing clumsy.lib (default: zig-out/<arch>_<conf>_<sign> from the main build, matching -Darch/-Dconf/-Dsign)") orelse default_clumsy_dir;

    const cfg = PluginConfig{
        .arch = arch,
        .conf = conf,
        .target = target,
        .iup_lib = iup_lib,
        .windivert_inc = b.fmt("{s}/include", .{windivert_dir}),
        .windivert_lib = b.fmt("{s}/{s}", .{ windivert_dir, arch_tag }),
        .clumsy_lib_dir = clumsy_lib_dir,
        .out_dir = out_dir,
    };

    // Register mdoules here
    addPlugin(b, cfg, "lag", "src/lag_module.c");
    // addPlugin(b, cfg, "drop", "src/drop_module.c");
    // addPlugin(b, cfg, "throttle", "src/throttle_module.c");

    const clean = b.step("clean", "Remove zig-out and zig-cache");
    clean.dependOn(&b.addRemoveDirTree(b.install_path).step);
}

// addPlugin
fn addPlugin(
    b: *Builder,
    cfg: PluginConfig,
    name: []const u8,
    src: []const u8,
) void {
    const dll = b.addSharedLibrary(name, null, .unversioned);
    dll.setTarget(cfg.target);
    switch (cfg.conf) {
        .Debug => dll.setBuildMode(.Debug),
        .Release => dll.setBuildMode(.ReleaseFast),
    }

    dll.addCSourceFile(src, &.{""});

    // Headers
    dll.addIncludeDir(cfg.windivert_inc);
    dll.addIncludeDir("src");
    dll.addIncludeDir(b.pathJoin(&.{ cfg.iup_lib, "include" }));
    dll.linkLibC();

    // WinDivert import lib
    dll.addLibPath(cfg.windivert_lib);
    dll.linkSystemLibrary("WinDivert");
    dll.linkSystemLibrary("Winmm");
    dll.addLibPath(cfg.iup_lib);
    dll.linkSystemLibrary("iup");
    dll.addLibPath(cfg.clumsy_lib_dir);
    dll.linkSystemLibrary("clumsy");

    b.getInstallStep().dependOn(&b.addInstallArtifact(dll).step);
    debug.print("plugin:{s}.dll <- {s}\n", .{ name, src });
}
