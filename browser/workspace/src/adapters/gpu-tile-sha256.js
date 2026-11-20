// Serialized once into the grant's isolated world. The input is the same
// rgba8unorm texture and row-major tile rectangle used by the CPU reader.
export function createGpuTileDigest() {
  const programs = new WeakMap();
  const blocksPerPass = 64;
  const code = `
    struct State { h: array<u32, 8>, nextBlock: u32 }
    @group(0) @binding(0) var image: texture_2d<f32>;
    @group(0) @binding(1) var<storage,read> jobs: array<vec4u>;
    @group(0) @binding(2) var<storage,read_write> states: array<State>;
    @group(0) @binding(3) var<storage,read_write> result: array<u32>;
    const K = array<u32,64>(
      0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
      0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
      0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
      0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
      0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
      0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
      0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
      0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u);
    fn rot(x:u32,n:u32)->u32 { return (x>>n)|(x<<(32u-n)); }
    fn swap(x:u32)->u32 {
      return (x>>24u)|((x>>8u)&0xff00u)|((x<<8u)&0xff0000u)|(x<<24u);
    }
    @compute @workgroup_size(8)
    fn digest(@builtin(global_invocation_id) global: vec3u) {
      let job = global.x;
      if (job >= arrayLength(&jobs)) { return; }
      let rect = jobs[job];
      let pixels = rect.z * rect.w;
      // Every RGBA pixel is one SHA-256 big-endian input word. Always include
      // the 0x80 padding word and the big-endian 64-bit bit count.
      let blocks = (pixels + 18u) / 16u;
      let start = states[job].nextBlock;
      if (start >= blocks) { return; }
      var h = array<u32,8>(0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                           0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u);
      if (start != 0u) { h = states[job].h; }
      let end = min(blocks, start + ${blocksPerPass}u);
      for (var b = start; b < end; b++) {
        var w: array<u32,16>;
        for (var i=0u; i<16u; i++) {
          let index=b*16u+i;
          if (index<pixels) {
            let p=vec2i(rect.xy+vec2u(index % rect.z,index/rect.z));
            let rgba=vec4u(round(textureLoad(image,p,0)*255.0));
            w[i]=(rgba.r<<24u)|(rgba.g<<16u)|(rgba.b<<8u)|rgba.a;
          } else if (index==pixels) { w[i]=0x80000000u; }
          else if (index==blocks*16u-1u) { w[i]=pixels*32u; }
          // The schedule is a ring reused for every padded block.
          else { w[i]=0u; }
        }
        var a = h;
        for (var i=0u; i<64u; i++) {
          let j=i%16u;
          if (i>=16u) {
            let x=w[(i-15u)%16u]; let y=w[(i-2u)%16u];
            let sigma0=rot(x,7u)^rot(x,18u)^(x>>3u);
            let sigma1=rot(y,17u)^rot(y,19u)^(y>>10u);
            w[j]+=sigma0+w[(i-7u)%16u]+sigma1;
          }
          let choose=(a[4]&a[5])^(~a[4]&a[6]);
          let t1=a[7]+(rot(a[4],6u)^rot(a[4],11u)^rot(a[4],25u))+choose+K[i]+w[j];
          let majority=(a[0]&a[1])^(a[0]&a[2])^(a[1]&a[2]);
          let t2=(rot(a[0],2u)^rot(a[0],13u)^rot(a[0],22u))+majority;
          a=array<u32,8>(t1+t2,a[0],a[1],a[2],a[3]+t1,a[4],a[5],a[6]);
        }
        for (var i=0u; i<8u; i++) { h[i]+=a[i]; }
      }
      states[job].nextBlock=end;
      states[job].h=h;
      if (end==blocks) {
        for (var i=0u; i<8u; i++) { result[job*8u+i]=swap(h[i]); }
      }
    }`;
  return async function hashTiles(gpu, texture, tiles, hashes) {
    const n = tiles.length;
    const size = n * 32;
    if (!n) return { hashes, rehashedTiles: 0, cpuPixelReadBytes: 0, tilePixelReadbackBytes: 0,
      mappedReadbackBytes: 0, digestReadbackBytes: 0, digestJobUploadBytes: 0, digestComputePasses: 0 };
    if (n > 256 || texture.format !== 'rgba8unorm' || texture.width * texture.height > 4 * 1024 * 1024) {
      throw Error('GPU tile SHA-256 exceeds the supported capture bounds');
    }
    let pipe = programs.get(gpu);
    if (!pipe) {
      pipe = await gpu.createComputePipelineAsync({ layout: 'auto',
        compute: { module: gpu.createShaderModule({ code }), entryPoint: 'digest' } });
      programs.set(gpu, pipe);
    }
    const work = new Uint32Array(n * 4);
    let maxBlocks = 0;
    for (const [i, tile] of tiles.entries()) {
      work.set([tile.x, tile.y, tile.w, tile.h], i * 4);
      maxBlocks = Math.max(maxBlocks, Math.ceil((tile.w * tile.h * 4 + 9) / 64));
    }
    const passes = Math.ceil(maxBlocks / blocksPerPass);
    let jobs, state, output, mapped;
    try {
      jobs = gpu.createBuffer({ size: work.byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
      state = gpu.createBuffer({ size: n * 36, usage: GPUBufferUsage.STORAGE });
      output = gpu.createBuffer({ size, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
      mapped = gpu.createBuffer({ size, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
      gpu.queue.writeBuffer(jobs, 0, work);
      const group = gpu.createBindGroup({ layout: pipe.getBindGroupLayout(0), entries: [
        { binding: 0, resource: texture.createView() }, { binding: 1, resource: { buffer: jobs } },
        { binding: 2, resource: { buffer: state } }, { binding: 3, resource: { buffer: output } },
      ] });
      const encoder = gpu.createCommandEncoder();
      // Separate passes order state updates even for very large tiles. Their
      // intermediate chaining words stay on the device.
      for (let i = 0; i < passes; i++) {
        const pass = encoder.beginComputePass();
        pass.setPipeline(pipe); pass.setBindGroup(0, group); pass.dispatchWorkgroups(Math.ceil(n / 8)); pass.end();
      }
      encoder.copyBufferToBuffer(output, 0, mapped, 0, size);
      gpu.queue.submit([encoder.finish()]);
      await mapped.mapAsync(GPUMapMode.READ);
      const bytes = new Uint8Array(mapped.getMappedRange());
      for (const [i, tile] of tiles.entries()) {
        hashes[tile.index] = Array.from(bytes.subarray(i*32, (i+1)*32), byte => byte.toString(16).padStart(2, '0')).join('');
      }
      return { hashes, rehashedTiles: n, cpuPixelReadBytes: 0, tilePixelReadbackBytes: 0, mappedReadbackBytes: bytes.length,
        digestReadbackBytes: bytes.length, digestJobUploadBytes: work.byteLength, digestComputePasses: passes };
    } finally { mapped?.destroy(); output?.destroy(); state?.destroy(); jobs?.destroy(); }
  };
}
