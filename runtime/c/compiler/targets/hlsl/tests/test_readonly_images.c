/* SPDX-License-Identifier: MIT */
#include "test_support.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_readonly_images(const char *path)
{
    const char *source =
        "@group(0) @binding(0) var sampled:texture_2d<u32>;"
        "@group(1) @binding(3) var input_image:texture_storage_2d<r32uint,read>;"
        "@group(2) @binding(5) var output_image:texture_storage_2d<r32uint,read_write>;"
        "@group(3) @binding(7) var<storage,read_write> output_values:array<vec4u>;"
        "fn fetch(tex:texture_storage_2d<r32uint,read>,xy:vec2i)->vec4u{return textureLoad(tex,xy);}"
        "@compute @workgroup_size(1) fn readonly_image(@builtin(global_invocation_id) id:vec3u){"
        "let xy=vec2i(id.xy);let read_value=fetch(input_image,xy);let sampled_value=textureLoad(sampled,xy,0);"
        "let prior=textureLoad(output_image,xy);textureStore(output_image,xy,read_value+sampled_value+prior);"
        "output_values[id.x]=read_value+vec4u(textureDimensions(input_image),0u,0u);}";
    Fixture *f = prepare(source);
    char *text = emit(f,0);
    has(text,"Texture2D<uint> r1 : register(t3, space1)");
    has(text,"RWTexture2D<uint> r2 : register(u5, space2)");
    has(text,".GetDimensions(0u, image_x, image_y, image_levels)");
    has(text,".Load(int3(");
    has(text,", 0u))");
    assert(strstr(text,"RWTexture2D<uint> r1") == NULL);
    DoeCompilerHlslBindingV11 bindings[4] = {0};
    DoeCompilerHlslEntryV11 entries[1] = {0};
    DoeHlslResult result;
    assert(doe_hlsl_emit_metadata(&f->source,f->module,0,f->scratch,
        (DoeHlslMetadata){bindings,entries},&result) == DOE_COMPILER_OK);
    assert(bindings[0].register_kind == DOE_COMPILER_HLSL_REGISTER_SRV);
    assert(bindings[1].register_kind == DOE_COMPILER_HLSL_REGISTER_SRV && bindings[1].shader_register == 3 &&
        bindings[1].register_space == 1 && bindings[1].stride == 0);
    assert(bindings[2].register_kind == DOE_COMPILER_HLSL_REGISTER_UAV && bindings[2].shader_register == 5);
    if (path) {
        FILE *file = fopen(path,"wb");
        assert(file && fwrite(text,1,strlen(text),file) == strlen(text));
        assert(fclose(file) == 0);
    }
    free(text);
    release(f);
    static const struct { const char *shape,*coordinate,*native,*load; } cases[] = {
        {"1d","0","Texture1D<uint>",".Load(int2("},
        {"2d_array","vec2i(0),0","Texture2DArray<uint>",".Load(int4("},
        {"3d","vec3i(0)","Texture3D<uint>",".Load(int4("}
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(*cases); ++i) {
        char input[512];
        int length = snprintf(input,sizeof(input),
            "@group(0) @binding(0) var image:texture_storage_%s<r32uint,read>;"
            "@compute @workgroup_size(1) fn main(){let value=textureLoad(image,%s);}",
            cases[i].shape,cases[i].coordinate);
        assert(length > 0 && (size_t)length < sizeof(input));
        f = prepare(input);
        text = emit(f,0);
        has(text,cases[i].native);
        has(text,cases[i].load);
        assert(strstr(text,"RWTexture") == NULL);
        free(text);
        release(f);
    }
}
