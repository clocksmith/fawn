@group(0) @binding(0) var<uniform> input:mat3x2f;
@group(0) @binding(1) var<storage,read_write> output:array<f32>;
fn fetch_column(index:i32)->vec2f{return input[index];}
@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) gid:vec3u){
    let column=fetch_column(i32(gid.x)-1i);
    let whole=input;
    let transposed=transpose(whole)*vec2f(7.0,8.0);
    let product=whole*vec3f(2.0,3.0,4.0);
    output[gid.x*7u]=column.x;
    output[gid.x*7u+1u]=column.y;
    output[gid.x*7u+2u]=input[2u][gid.x%2u];
    output[gid.x*7u+3u]=input[1u][gid.x];
    output[gid.x*7u+4u]=transposed.z;
    output[gid.x*7u+5u]=product.x;
    output[gid.x*7u+6u]=determinant(mat2x2f(whole[0u],whole[2u]));
}
