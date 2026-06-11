//! curspan — the framework's component CLI.
//!
//! ShadCN-style distribution for terminal-UI components: a local-first registry
//! (registry/registry.json) plus an `add` command that copies a component and
//! its transitive dependency closure into your project, so you OWN the source.
//!
//!   curspan list                 # the catalog, grouped by category
//!   curspan info <name>          # a component's files + dependency closure
//!   curspan add <name> [opts]    # copy the component + deps into your project
//!   curspan check                # validate the registry (files exist, deps resolve)
//!
//! Options: --registry PATH (default registry/registry.json), --root PATH
//! (source tree root, default "."), --dest PATH (add target, default "."),
//! --dry-run (print actions without writing).

const std = @import("std");
const Io = std.Io;
const Dir = std.Io.Dir;
const File = std.Io.File;
const Allocator = std.mem.Allocator;

const Component = struct {
    name: []const u8,
    title: []const u8 = "",
    category: []const u8 = "component",
    surfaces: []const []const u8 = &.{},
    description: []const u8 = "",
    files: []const []const u8 = &.{},
    dependencies: []const []const u8 = &.{},
    since: []const u8 = "",
};

const Registry = struct {
    name: []const u8 = "",
    version: []const u8 = "",
    components: []const Component,
};

const Options = struct {
    registry: []const u8 = "registry/registry.json",
    root: []const u8 = ".",
    dest: []const u8 = ".",
    dry_run: bool = false,
};

// Everything a command needs: the Io handle to touch stdout and the filesystem,
// and an arena for scratch allocations (freed when the process exits).
const Ctx = struct {
    io: Io,
    alloc: Allocator,
};

fn fail(comptime fmt: []const u8, args: anytype) noreturn {
    std.debug.print("curspan: " ++ fmt ++ "\n", args);
    std.process.exit(1);
}

fn findComponent(reg: Registry, name: []const u8) ?Component {
    for (reg.components) |c| {
        if (std.mem.eql(u8, c.name, name)) return c;
    }
    return null;
}

fn contains(list: []const []const u8, name: []const u8) bool {
    for (list) |n| {
        if (std.mem.eql(u8, n, name)) return true;
    }
    return false;
}

// Post-order dependency closure: dependencies appear before their dependents,
// which is also the order to add the sources to a build. Detects cycles.
fn addClosure(
    alloc: Allocator,
    reg: Registry,
    name: []const u8,
    seen: *std.ArrayList([]const u8),
    visiting: *std.ArrayList([]const u8),
) !void {
    if (contains(seen.items, name)) return;
    if (contains(visiting.items, name)) {
        fail("dependency cycle through '{s}'", .{name});
    }
    const comp = findComponent(reg, name) orelse
        fail("unknown component '{s}'", .{name});
    try visiting.append(alloc, name);
    for (comp.dependencies) |dep| try addClosure(alloc, reg, dep, seen, visiting);
    _ = visiting.pop();
    try seen.append(alloc, name);
}

fn closureOf(alloc: Allocator, reg: Registry, name: []const u8) ![]const []const u8 {
    var seen: std.ArrayList([]const u8) = .empty;
    var visiting: std.ArrayList([]const u8) = .empty;
    try addClosure(alloc, reg, name, &seen, &visiting);
    return seen.items;
}

fn loadRegistry(ctx: Ctx, path: []const u8) !std.json.Parsed(Registry) {
    const bytes = Dir.cwd().readFileAlloc(ctx.io, path, ctx.alloc, .limited(4 * 1024 * 1024)) catch
        fail("cannot read registry '{s}' (try --registry PATH)", .{path});
    return std.json.parseFromSlice(Registry, ctx.alloc, bytes, .{ .ignore_unknown_fields = true }) catch
        fail("registry '{s}' is not valid JSON", .{path});
}

// Append `fmt`-formatted text to `buf`. Output is built fully in memory, then
// written to stdout once in `flush` — so the body of a command never touches Io.
fn emit(buf: *std.ArrayList(u8), alloc: Allocator, comptime fmt: []const u8, args: anytype) !void {
    const line = try std.fmt.allocPrint(alloc, fmt, args);
    try buf.appendSlice(alloc, line);
}

// Append `text` left-justified into a field `width` columns wide (byte width;
// registry names are ASCII). Avoids relying on std.fmt alignment specifiers.
fn emitPadded(buf: *std.ArrayList(u8), alloc: Allocator, text: []const u8, width: usize) !void {
    try buf.appendSlice(alloc, text);
    var i: usize = text.len;
    while (i < width) : (i += 1) try buf.append(alloc, ' ');
}

fn flush(ctx: Ctx, buf: *std.ArrayList(u8)) !void {
    try File.stdout().writeStreamingAll(ctx.io, buf.items);
}

// A registry file path must be relative and stay within the tree. Reject
// absolute paths and any ".." segment so `add`/`check` can never reach outside
// the source root or the destination project. Split on both POSIX and Windows
// separators regardless of the build host: on a Windows host `std.fs.path.join`
// honors `\`, so a "..\\.." segment in an untrusted registry entry would escape
// a gate that only split on '/'.
fn pathIsUnsafe(p: []const u8) bool {
    if (std.fs.path.isAbsolute(p)) return true;
    return pathHasParentSegment(p);
}

// CLI options such as --registry and --root may be absolute because build.zig
// passes LazyPath values to `curspan check`, but they must not contain parent
// traversal segments. Keep this separate from pathIsUnsafe(), which guards
// untrusted registry entries that are always expected to be relative.
fn pathHasParentSegment(p: []const u8) bool {
    var it = std.mem.splitAny(u8, p, "/\\");
    while (it.next()) |seg| {
        if (std.mem.eql(u8, seg, "..")) return true;
    }
    return false;
}

test "pathIsUnsafe rejects traversal with either path separator" {
    try std.testing.expect(pathIsUnsafe("../registry.json"));
    try std.testing.expect(pathIsUnsafe("foo/../bar"));
    try std.testing.expect(pathIsUnsafe("foo\\..\\bar"));
    try std.testing.expect(!pathIsUnsafe("src/components/cs_table.c"));
}

test "pathHasParentSegment allows absolute LazyPaths but rejects traversal" {
    try std.testing.expect(pathHasParentSegment("../registry.json"));
    try std.testing.expect(pathHasParentSegment("foo\\..\\bar"));
    try std.testing.expect(!pathHasParentSegment("/abs/path/registry.json"));
}

fn validateOptions(opts: Options) void {
    if (pathHasParentSegment(opts.registry)) {
        fail("unsafe --registry path '{s}'", .{opts.registry});
    }
    if (pathHasParentSegment(opts.root)) {
        fail("unsafe --root path '{s}'", .{opts.root});
    }
    if (pathIsUnsafe(opts.dest)) {
        fail("unsafe --dest path '{s}'", .{opts.dest});
    }
}

// One catalog row: "  <name padded>  <description>".
fn emitCatalogRow(buf: *std.ArrayList(u8), alloc: Allocator, c: Component, name_width: usize) !void {
    try buf.appendSlice(alloc, "  ");
    try emitPadded(buf, alloc, c.name, name_width);
    try emit(buf, alloc, "  {s}\n", .{c.description});
}

fn cmdList(ctx: Ctx, reg: Registry) !void {
    const alloc = ctx.alloc;
    var buf: std.ArrayList(u8) = .empty;
    try emit(&buf, alloc, "{s} registry v{s}\n\n", .{ reg.name, reg.version });

    // Pad the name column to the widest name so descriptions line up.
    var name_width: usize = 1;
    for (reg.components) |c| {
        if (c.name.len > name_width) name_width = c.name.len;
    }

    const categories = [_][]const u8{ "foundation", "component" };
    for (categories) |cat| {
        try emit(&buf, alloc, "{s}s:\n", .{cat});
        for (reg.components) |c| {
            if (!std.mem.eql(u8, c.category, cat)) continue;
            try emitCatalogRow(&buf, alloc, c, name_width);
        }
        try buf.append(alloc, '\n');
    }

    // Any component whose category is not one of the known sections still
    // belongs in the catalog; group the leftovers so `list` never hides them.
    var printed_other = false;
    for (reg.components) |c| {
        if (std.mem.eql(u8, c.category, "foundation") or
            std.mem.eql(u8, c.category, "component")) continue;
        if (!printed_other) {
            try buf.appendSlice(alloc, "other:\n");
            printed_other = true;
        }
        try emitCatalogRow(&buf, alloc, c, name_width);
    }
    if (printed_other) try buf.append(alloc, '\n');

    try buf.appendSlice(alloc, "Add one with: curspan add <name>\n");
    try flush(ctx, &buf);
}

fn cmdInfo(ctx: Ctx, reg: Registry, name: []const u8) !void {
    const alloc = ctx.alloc;
    const comp = findComponent(reg, name) orelse
        fail("unknown component '{s}'", .{name});
    var buf: std.ArrayList(u8) = .empty;
    try emit(&buf, alloc, "{s} ({s})\n  {s}\n\n", .{ comp.name, comp.category, comp.description });
    try buf.appendSlice(alloc, "files:\n");
    for (comp.files) |f| try emit(&buf, alloc, "  {s}\n", .{f});
    try buf.appendSlice(alloc, "\ndependency closure (add order):\n");
    const closure = try closureOf(alloc, reg, name);
    for (closure) |n| try emit(&buf, alloc, "  {s}\n", .{n});
    try flush(ctx, &buf);
}

fn cmdAdd(ctx: Ctx, reg: Registry, name: []const u8, opts: Options) !void {
    const alloc = ctx.alloc;
    const closure = try closureOf(alloc, reg, name);
    var buf: std.ArrayList(u8) = .empty;
    var copied: usize = 0;
    var added_files: std.ArrayList([]const u8) = .empty;

    try emit(&buf, alloc, "Adding '{s}' ({d} units in closure) into {s}\n", .{ name, closure.len, opts.dest });
    for (closure) |unit_name| {
        const comp = findComponent(reg, unit_name).?;
        for (comp.files) |rel| {
            // Never write outside --dest, even from an untrusted --registry.
            if (pathIsUnsafe(rel)) {
                fail("unsafe file path '{s}' in component '{s}'", .{ rel, unit_name });
            }
            const src = try std.fs.path.join(alloc, &.{ opts.root, rel });
            const dst = try std.fs.path.join(alloc, &.{ opts.dest, rel });
            if (opts.dry_run) {
                try emit(&buf, alloc, "  would copy {s}\n", .{rel});
            } else {
                const data = Dir.cwd().readFileAlloc(ctx.io, src, alloc, .limited(4 * 1024 * 1024)) catch
                    fail("missing source file '{s}'", .{src});
                if (std.fs.path.dirname(dst)) |dir| {
                    Dir.cwd().createDirPath(ctx.io, dir) catch |e|
                        fail("cannot create '{s}': {s}", .{ dir, @errorName(e) });
                }
                Dir.cwd().writeFile(ctx.io, .{ .sub_path = dst, .data = data }) catch |e|
                    fail("cannot write '{s}': {s}", .{ dst, @errorName(e) });
                try emit(&buf, alloc, "  copied {s}\n", .{rel});
            }
            copied += 1;
            if (std.mem.endsWith(u8, rel, ".c")) try added_files.append(alloc, rel);
        }
    }

    try emit(&buf, alloc, "\n{s} {d} file(s).\n", .{ if (opts.dry_run) "Would copy" else "Copied", copied });
    if (added_files.items.len > 0) {
        try buf.appendSlice(alloc, "\nAdd these sources to your build.zig:\n");
        for (added_files.items) |f| try emit(&buf, alloc, "    \"{s}\",\n", .{f});
    }
    try flush(ctx, &buf);
}

fn cmdCheck(ctx: Ctx, reg: Registry, opts: Options) !void {
    const alloc = ctx.alloc;
    var problems: usize = 0;
    var buf: std.ArrayList(u8) = .empty;
    for (reg.components) |c| {
        for (c.files) |rel| {
            if (pathIsUnsafe(rel)) {
                try emit(&buf, alloc, "  unsafe file path: {s} (in '{s}')\n", .{ rel, c.name });
                problems += 1;
                continue;
            }
            const path = try std.fs.path.join(alloc, &.{ opts.root, rel });
            Dir.cwd().access(ctx.io, path, .{}) catch {
                try emit(&buf, alloc, "  missing file: {s} (in '{s}')\n", .{ rel, c.name });
                problems += 1;
            };
        }
        for (c.dependencies) |dep| {
            if (findComponent(reg, dep) == null) {
                try emit(&buf, alloc, "  unresolved dependency: {s} -> {s}\n", .{ c.name, dep });
                problems += 1;
            }
        }
    }
    // Closure resolution also detects cycles, but it aborts on the first one;
    // run it only once the per-component report is clean so that report is never
    // suppressed by a terse closure error.
    if (problems == 0) {
        for (reg.components) |c| _ = try closureOf(alloc, reg, c.name);
    }

    if (problems == 0) {
        try emit(&buf, alloc, "registry ok: {d} components, all files present and dependencies resolve\n", .{reg.components.len});
        try flush(ctx, &buf);
    } else {
        try emit(&buf, alloc, "registry has {d} problem(s)\n", .{problems});
        try flush(ctx, &buf);
        std.process.exit(1);
    }
}

fn usage() void {
    std.debug.print(
        \\curspan — Curspan component CLI
        \\
        \\Usage:
        \\  curspan list
        \\  curspan info <name>
        \\  curspan add <name> [--dest DIR] [--root DIR] [--dry-run]
        \\  curspan check
        \\
        \\Options:
        \\  --registry PATH   registry file (default registry/registry.json)
        \\  --root DIR        source tree root to copy from (default .)
        \\  --dest DIR        destination project root (default .)
        \\  --dry-run         print actions without writing files
        \\
    , .{});
}

pub fn main(init: std.process.Init) !void {
    const ctx = Ctx{ .io = init.io, .alloc = init.arena.allocator() };

    const args = try std.process.Args.toSlice(init.minimal.args, ctx.alloc);
    if (args.len < 2) {
        usage();
        std.process.exit(2);
    }

    const cmd = args[1];
    var opts = Options{};
    var positional: ?[]const u8 = null;

    var i: usize = 2;
    while (i < args.len) : (i += 1) {
        const a = args[i];
        if (std.mem.eql(u8, a, "--registry")) {
            i += 1;
            if (i >= args.len) fail("--registry needs a path", .{});
            opts.registry = args[i];
        } else if (std.mem.eql(u8, a, "--root")) {
            i += 1;
            if (i >= args.len) fail("--root needs a path", .{});
            opts.root = args[i];
        } else if (std.mem.eql(u8, a, "--dest")) {
            i += 1;
            if (i >= args.len) fail("--dest needs a path", .{});
            opts.dest = args[i];
        } else if (std.mem.eql(u8, a, "--dry-run")) {
            opts.dry_run = true;
        } else if (std.mem.startsWith(u8, a, "--")) {
            fail("unknown option '{s}'", .{a});
        } else if (positional == null) {
            positional = a;
        } else {
            fail("unexpected argument '{s}'", .{a});
        }
    }

    validateOptions(opts);

    const parsed = try loadRegistry(ctx, opts.registry);
    const reg = parsed.value;

    if (std.mem.eql(u8, cmd, "list")) {
        try cmdList(ctx, reg);
    } else if (std.mem.eql(u8, cmd, "info")) {
        try cmdInfo(ctx, reg, positional orelse fail("info needs a component name", .{}));
    } else if (std.mem.eql(u8, cmd, "add")) {
        try cmdAdd(ctx, reg, positional orelse fail("add needs a component name", .{}), opts);
    } else if (std.mem.eql(u8, cmd, "check")) {
        try cmdCheck(ctx, reg, opts);
    } else if (std.mem.eql(u8, cmd, "help") or std.mem.eql(u8, cmd, "--help")) {
        usage();
    } else {
        fail("unknown command '{s}' (try: list, info, add, check)", .{cmd});
    }
}
