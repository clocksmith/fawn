struct Box { values: array<u32, 3>, tail: u32 }
@group(0) @binding(0) var<storage, read_write> output: array<u32>;
fn mutate(p: ptr<function, Box>, index: i32) {
    (*p).values[index] += 100u;
    (*p).tail += 1u;
}
fn next(p: ptr<function, u32>) -> u32 {
    let old = *p;
    *p += 1u;
    return old;
}
fn forward(p: ptr<function, u32>) -> u32 { return next(p); }
fn combine(a: u32, b: u32) -> u32 { return a * 10u + b; }
fn sum(a: ptr<function, u32>, b: ptr<function, u32>) -> u32 { return *a + *b; }
fn fresh() -> u32 { var local = 11u; next(&local); return local; }
@compute @workgroup_size(5)
fn main(@builtin(local_invocation_index) lane: u32) {
    var data = Box(array(10u, 20u, 30u), 40u);
    var index = i32(lane) - 1;
    let p = &data.values[index];
    index = 0;
    *p += 7u;
    let before = *p;
    mutate(&data, i32(lane) - 1);
    let base = lane * 11u;
    output[base] = before;
    output[base + 1u] = data.values[0];
    output[base + 2u] = data.values[1];
    output[base + 3u] = data.values[2];
    output[base + 4u] = data.tail;
    var state = 3u;
    let q = &state;
    output[base + 5u] = combine(forward(q), next(&state));
    output[base + 6u] = state + next(q) + state;
    output[base + 7u] = state;
    *(&state) = 9u;
    output[base + 8u] = *q;
    output[base + 9u] = sum(q, q);
    output[base + 10u] = fresh();
}
