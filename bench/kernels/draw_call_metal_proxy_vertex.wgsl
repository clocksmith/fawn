// Metal native draw-call proxy: match the retained Metal zero-area primitive.
// Every vertex occupies the same clip-space point; no fragment work is needed.
@vertex
fn main() -> @builtin(position) vec4f {
    return vec4f(0.0, 0.0, 0.0, 1.0);
}
