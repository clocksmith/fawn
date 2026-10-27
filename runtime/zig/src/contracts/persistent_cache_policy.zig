//! Shared one-way authorization for Doe-owned disk shader/pipeline caches.
//! Reserve before the first persistent operation, including an in-flight attempt.
const std = @import("std");

pub const Policy = struct {
    pub const State = enum { fresh, disabled, used };
    mutex: std.Thread.Mutex = .{},
    state: State = .fresh,

    pub fn disable(self: *Policy) bool {
        self.mutex.lock();
        defer self.mutex.unlock();
        if (self.state == .used) return false;
        self.state = .disabled;
        return true;
    }

    pub fn reserve(self: *Policy) bool {
        self.mutex.lock();
        defer self.mutex.unlock();
        if (self.state == .disabled) return false;
        self.state = .used;
        return true;
    }

    pub fn persistenceAllowed(self: *Policy) bool {
        self.mutex.lock();
        defer self.mutex.unlock();
        return self.state != .disabled;
    }
};
