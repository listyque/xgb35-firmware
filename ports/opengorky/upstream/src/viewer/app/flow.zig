//! Viewer transitions: level switch and world to screen (game.switchTo), battle
//! retry, menu actions (new game, save, load), game over choice, screenshot.
//! Viewer functions (first parameter), called through the module.

const std = @import("std");
const loading = @import("loading.zig");
const viewer = @import("viewer.zig");
const level_compose = @import("../level/compose.zig");
const camera_view = @import("../level/camera.zig");
const saves_ui = @import("../menu/saves.zig");
const menu_slots = @import("../menu/slots.zig");
const gui_battle = @import("../battle/gui.zig");
const Viewer = viewer.Viewer;
const c = @import("sdl");
const og = @import("opengorky");
const Io = std.Io;

/// Switch to level `next` (LEVEL_Load, game.switchTo): the left level's view goes
/// to the cache with its world (a battle is destroyed); the new one from cache or fresh.
pub fn switchLevel(self: *Viewer, next: []const u8) !void {
    std.log.info("level -> {s} (tick {d})", .{ next, if (self.level) |l| l.world.ticks else 0 });
    if (self.level == null) return error.NoLevel;
    loading.show(self);
    errdefer loading.end();
    if (self.transition) |tr| if (tr.snap) |t| c.SDL_DestroyTexture(t);
    self.transition = null;
    const sw = self.game.switchTo(next) catch |err| {
        if (self.game.world != self.level.?.world) {
            self.level.?.deinit();
            self.level = null;
        }
        return err;
    };
    var old = self.level.?;
    self.level = null;
    // PLATFORM: always free old LevelView GPU/sprite textures on level switch (World state stays in game.cache)
    old.deinit();
    try showWorld(self, sw.restored);
    try self.game.finish(sw);
}

/// Puts `game.world` on screen: cached view (`restored`) or a new one.
fn showWorld(self: *Viewer, restored: bool) !void {
    const w = self.game.world.?;
    if (restored) if (self.level_cache.fetchRemove(w.level_name)) |kv| {
        self.gpa.free(kv.key);
        self.level = kv.value;
    };
    if (self.level == null)
        self.level = try level_compose.load(self.io, self.gpa, self.renderer, self.dat, w, self.sound, self.models, self.textures);
    self.first_level_frame = true;
    // Level archive, for the title and the [ ] keys.
    var buf: [256]u8 = undefined;
    const arc_name = try std.fmt.bufPrint(&buf, "{s}/sprite.dat", .{w.level_name});
    for (self.archives, 0..) |p, i| if (std.ascii.eqlIgnoreCase(p, arc_name)) {
        self.arc_index = i;
    };
    afterLoad(self);
}

/// Level loaded or taken from the cache (0x41fb09): font, hero follow,
/// zoom 1 without transition, dolly reset; the camera where it was left,
/// otherwise on the hero.
fn afterLoad(self: *Viewer) void {
    const l = &self.level.?;
    l.world.examine_menus = self.inventory != null;
    l.font = self.font;
    l.follow = true;
    l.last_tick_ms = 0;
    self.zoom = 1;
    self.zoom_target = 1;
    self.zoom_rate = 0;
    self.dolly = null;
    self.cam_tick = l.world.ticks;
    self.idle_ticks = 0;
    self.scroll = .{ 0, 0 };
    if (l.left_scroll) |sc| {
        camera_view.scrollBy(self, sc[0], sc[1]);
        l.left_scroll = null;
    } else if (l.cameraGoal()) |goal| camera_view.scrollBy(self, goal[0], goal[1]);
}

/// "Try again" (0x4542c0): the same battle again with the party as it entered; outside
/// battle the "battle" save (no battle entered yet: plays on, the exe finds no file).
fn retryBattle(self: *Viewer) !void {
    const cur = &(self.level orelse return);
    if (!cur.world.isCombat() and self.game.battle_save == null) return;
    loading.show(self);
    errdefer loading.end();
    if (cur.world.isCombat()) {
        cur.deinit();
        self.level = null;
        _ = try self.game.retry();
    } else {
        self.closeAll();
        _ = try self.game.reloadBattle();
    }
    try showWorld(self, false);
}

pub fn menuAction(self: *Viewer, action: @import("../menu/items.zig").Action) !void {
    const m = self.menu orelse return;
    switch (action) {
        .none => {},
        .new_game => {
            self.closeAll();
            self.pending_new_game = true;
            m.open = false;
            if (!self.input_mode) queueVideos(self, og.rules.campaign().intro);
            self.nextVideo();
            if (self.video == null) try enterNewGame(self);
        },
        .resume_game => m.open = false,
        .main_menu => {
            self.closeAll();
            m.show(.main);
        },
        .exit => self.quit = true,
        .retry_battle => {
            retryBattle(self) catch |err| std.log.err("retry: {t}", .{err});
            if (self.level == null) m.show(.main) else m.open = false;
        },
        // Save (0x4157fc → 0x415830): "keyaccept", the menu closes.
        .save => |k| {
            saves_ui.saveGame(self, k, menu_slots.editName(m)) catch |err| {
                std.log.err("save: {t}", .{err});
                self.showMessage("Could not save game");
                return;
            };
            m.editing = null;
            m.sound = og.core.modules.ui().sounds.key_accept;
            m.open = false;
        },
        // "keyaccept" picked the slot (0x41566a).
        .load => |k| {
            loadGame(self, k) catch |err| std.log.err("load: {t}", .{err});
            // A failed load may have closed the game already: back to the main menu.
            if (self.level == null) m.show(.main) else m.open = false;
        },
    }
}

pub fn enterNewGame(self: *Viewer) !void {
    self.pending_new_game = false;
    loading.show(self);
    errdefer loading.end();
    _ = try self.game.newGame();
    try showWorld(self, false);
}

/// Videos of a campaign list ("a.avi b.mp4") queued, at the exe's places for its own
/// (core module "interface" `videos`): introkon (0x413386: x 0, y 60, doubled), those
/// before the menu (0x40fd06).
pub fn queueVideos(self: *Viewer, list: []const u8) void {
    var it = std.mem.tokenizeAny(u8, list, " ,");
    while (it.next()) |name| {
        const r: og.outbox.VideoRequest = for (og.core.modules.ui().videos) |e| {
            if (std.ascii.eqlIgnoreCase(e.name, name)) break .{ .name = e.name, .x = e.x, .y = e.y, .double = e.double };
        } else .{ .name = name, .x = 0, .y = 0, .double = false };
        self.video_queue.append(self.gpa, r) catch {};
    }
}

/// Game over choice: YES — retry the battle, NO — main menu.
pub fn gameOverChoose(v: *Viewer, yes: bool) !void {
    const g = v.gui orelse return;
    gui_battle.clearGameOver(g);
    if (v.level) |*l| l.world.flow.defeat_ticks = null;
    v.uiSound(og.core.modules.ui().sounds.click);
    try menuAction(v, if (yes) .retry_battle else .main_menu);
}

/// Slot load (0x41566a): all levels closed, the save goes to the game
/// (game.load), the world to the screen.
pub fn loadGame(self: *Viewer, slot: usize) !void {
    var buf: [512]u8 = undefined;
    const path = saves_ui.slotPath(self, &buf, slot) orelse return;
    loading.show(self);
    errdefer loading.end();
    const data = try Io.Dir.cwd().readFileAlloc(self.io, path, self.gpa, .limited(64 << 20));
    defer self.gpa.free(data);
    const sv = try og.compose.save.parse(self.gpa, data);
    self.closeAll();
    // The save goes to the game (game.load) even if a level fails to load.
    _ = try self.game.load(sv);
    try showWorld(self, false);
}
