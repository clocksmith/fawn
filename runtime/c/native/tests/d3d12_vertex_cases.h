/* SPDX-License-Identifier: MIT
 * Independent literal source bytes and expected fetched lanes. Retained full
 * vertex_formats.c remains unchanged; this RGBA8 control covers all 41 formats. */
#ifndef DOE_NATIVE_D3D12_VERTEX_CASES_H
#define DOE_NATIVE_D3D12_VERTEX_CASES_H
enum { FLOAT_KIND, UINT_KIND, SINT_KIND };
typedef struct vertex_case {
    WGPUVertexFormat format;
    int kind;
    uint8_t data[16];
    union { float f[4]; uint32_t u[4]; int32_t i[4]; } expected;
} vertex_case;

#define F(...) {.f = {__VA_ARGS__}}
#define U(...) {.u = {__VA_ARGS__}}
#define I(...) {.i = {__VA_ARGS__}}
static const vertex_case cases[] = {
    {WGPUVertexFormat_Uint8, UINT_KIND, {7}, U(7, 0, 0, 1)},
    {WGPUVertexFormat_Uint8x2, UINT_KIND, {3, 250}, U(3, 250, 0, 1)},
    {WGPUVertexFormat_Uint8x4, UINT_KIND, {1, 2, 250, 255}, U(1, 2, 250, 255)},
    {WGPUVertexFormat_Sint8, SINT_KIND, {0xFF}, I(-1, 0, 0, 1)},
    {WGPUVertexFormat_Sint8x2, SINT_KIND, {0x80, 0x7F}, I(-128, 127, 0, 1)},
    {WGPUVertexFormat_Sint8x4, SINT_KIND, {0xFF, 0x80, 0x7F, 5}, I(-1, -128, 127, 5)},
    {WGPUVertexFormat_Unorm8, FLOAT_KIND, {255}, F(1, 0, 0, 1)},
    {WGPUVertexFormat_Unorm8x2, FLOAT_KIND, {0, 255}, F(0, 1, 0, 1)},
    {WGPUVertexFormat_Unorm8x4, FLOAT_KIND, {255, 0, 255, 0}, F(1, 0, 1, 0)},
    {WGPUVertexFormat_Snorm8, FLOAT_KIND, {0x7F}, F(1, 0, 0, 1)},
    {WGPUVertexFormat_Snorm8x2, FLOAT_KIND, {0x81, 0x7F}, F(-1, 1, 0, 1)},
    {WGPUVertexFormat_Snorm8x4, FLOAT_KIND, {0x7F, 0, 0x81, 0x7F}, F(1, 0, -1, 1)},
    {WGPUVertexFormat_Uint16, UINT_KIND, {0xFF, 0xFF}, U(65535, 0, 0, 1)},
    {WGPUVertexFormat_Uint16x2, UINT_KIND, {0x2C, 0x01, 0, 0}, U(300, 0, 0, 1)},
    {WGPUVertexFormat_Uint16x4, UINT_KIND, {1, 0, 0xFF, 0xFF, 0x2C, 0x01, 7, 0}, U(1, 65535, 300, 7)},
    {WGPUVertexFormat_Sint16, SINT_KIND, {0x00, 0x80}, I(-32768, 0, 0, 1)},
    {WGPUVertexFormat_Sint16x2, SINT_KIND, {0xFF, 0x7F, 0xFF, 0xFF}, I(32767, -1, 0, 1)},
    {WGPUVertexFormat_Sint16x4, SINT_KIND, {0xFE, 0xFF, 3, 0, 0x00, 0x80, 0xFF, 0x7F}, I(-2, 3, -32768, 32767)},
    {WGPUVertexFormat_Unorm16, FLOAT_KIND, {0xFF, 0xFF}, F(1, 0, 0, 1)},
    {WGPUVertexFormat_Unorm16x2, FLOAT_KIND, {0, 0, 0xFF, 0xFF}, F(0, 1, 0, 1)},
    {WGPUVertexFormat_Unorm16x4, FLOAT_KIND, {0xFF, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF}, F(1, 0, 0, 1)},
    {WGPUVertexFormat_Snorm16, FLOAT_KIND, {0xFF, 0x7F}, F(1, 0, 0, 1)},
    {WGPUVertexFormat_Snorm16x2, FLOAT_KIND, {0x01, 0x80, 0xFF, 0x7F}, F(-1, 1, 0, 1)},
    {WGPUVertexFormat_Snorm16x4, FLOAT_KIND, {0xFF, 0x7F, 0, 0, 0x01, 0x80, 0, 0}, F(1, 0, -1, 0)},
    {WGPUVertexFormat_Float16, FLOAT_KIND, {0x00, 0x3C}, F(1, 0, 0, 1)},
    {WGPUVertexFormat_Float16x2, FLOAT_KIND, {0x00, 0xC0, 0x00, 0x3E}, F(-2, 1.5f, 0, 1)},
    {WGPUVertexFormat_Float16x4, FLOAT_KIND, {0x00, 0x34, 0x00, 0x3C, 0x00, 0xC0, 0x00, 0x3E}, F(0.25f, 1, -2, 1.5f)},
    {WGPUVertexFormat_Float32, FLOAT_KIND, {0, 0, 0x40, 0x40}, F(3, 0, 0, 1)},
    {WGPUVertexFormat_Float32x3, FLOAT_KIND, {0, 0, 0x80, 0x3F, 0, 0, 0, 0xC0, 0, 0, 0x40, 0x40}, F(1, -2, 3, 1)},
    {WGPUVertexFormat_Uint32x2, UINT_KIND, {0xFF, 0xFF, 0xFF, 0xFF, 9, 0, 0, 0}, U(UINT32_MAX, 9, 0, 1)},
    {WGPUVertexFormat_Sint32x4, SINT_KIND, {0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0x80, 2, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0x7F},
        I(-1, INT32_MIN, 2, INT32_MAX)},
    /* r=1023 g=0 b=1023 a=3 packed little-endian; red is in the low bits. */
    {WGPUVertexFormat_Unorm10_10_10_2, FLOAT_KIND, {0xFF, 0x03, 0xF0, 0xFF}, F(1, 0, 1, 1)},
    /* Memory order B,G,R,A; the shader receives r,g,b,a. */
    {WGPUVertexFormat_Unorm8x4BGRA, FLOAT_KIND, {255, 0, 0, 255}, F(0, 0, 1, 1)},
    {WGPUVertexFormat_Float32x2, FLOAT_KIND, {0,0,0,0xC0,0,0,0xC0,0x3F}, F(-2,1.5f,0,1)},
    {WGPUVertexFormat_Float32x4, FLOAT_KIND, {0,0,0x80,0x3E,0,0,0x80,0x3F,0,0,0,0xC0,0,0,0xC0,0x3F}, F(0.25f,1,-2,1.5f)},
    {WGPUVertexFormat_Uint32, UINT_KIND, {0xFF,0xFF,0xFF,0xFF}, U(UINT32_MAX,0,0,1)},
    {WGPUVertexFormat_Uint32x3, UINT_KIND, {7,0,0,0,0,0,0,0,9,0,0,0}, U(7,0,9,1)},
    {WGPUVertexFormat_Uint32x4, UINT_KIND, {7,0,0,0,0,0,0,0,9,0,0,0,11,0,0,0}, U(7,0,9,11)},
    {WGPUVertexFormat_Sint32, SINT_KIND, {0,0,0,0x80}, I(INT32_MIN,0,0,1)},
    {WGPUVertexFormat_Sint32x2, SINT_KIND, {0xFF,0xFF,0xFF,0xFF,7,0,0,0}, I(-1,7,0,1)},
    {WGPUVertexFormat_Sint32x3, SINT_KIND, {0xFE,0xFF,0xFF,0xFF,3,0,0,0,0,0,0,0x80}, I(-2,3,INT32_MIN,1)},
};

#undef F
#undef U
#undef I
#endif
