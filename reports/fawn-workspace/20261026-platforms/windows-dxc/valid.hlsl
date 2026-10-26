RWStructuredBuffer<uint> result : register(u0);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) {
    result[id.x] = id.x + 17u;
}
