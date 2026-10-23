// Fixture appendix only: append these exact bytes to each matched private copy
// of src/dropin/dropin_diagnostics.zig. Never install in production sources.
pub const OwnedDiagnosticRecord = struct {
    symbol: [MAX_SYMBOL_BYTES]u8,
    owner: [MAX_OWNER_BYTES]u8,
    symbol_len: u8,
    owner_len: u8,
    resolved: bool,
    fallback_used: bool,
};

pub fn snapshotOwnedForTest(out: []OwnedDiagnosticRecord) usize {
    if (!@import("builtin").is_test) @compileError("owned diagnostic observation requires a test fixture");
    g_lock.lock();
    defer g_lock.unlock();
    const record_count = @min(g_record_count, MAX_DIAGNOSTIC_RECORDS);
    const max_out = @min(out.len, record_count);
    const first = if (g_record_count < MAX_DIAGNOSTIC_RECORDS) 0 else (g_next_index % MAX_DIAGNOSTIC_RECORDS);
    var cursor = first;
    var written: usize = 0;
    while (written < max_out) : (written += 1) {
        const entry = &g_records[cursor];
        out[written] = .{
            .symbol = entry.symbol,
            .owner = entry.owner,
            .symbol_len = entry.symbol_len,
            .owner_len = entry.owner_len,
            .resolved = entry.resolved,
            .fallback_used = entry.fallback_used,
        };
        cursor = (cursor + 1) % MAX_DIAGNOSTIC_RECORDS;
    }
    return written;
}
