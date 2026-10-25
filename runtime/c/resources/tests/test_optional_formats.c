/* SPDX-License-Identifier: MIT */
#include "fixture.h"
static const WGPUTextureFormat storage[]={
 WGPUTextureFormat_R8Unorm,WGPUTextureFormat_R8Snorm,WGPUTextureFormat_R8Uint,WGPUTextureFormat_R8Sint,
 WGPUTextureFormat_RG8Unorm,WGPUTextureFormat_RG8Snorm,WGPUTextureFormat_RG8Uint,WGPUTextureFormat_RG8Sint,
 WGPUTextureFormat_R16Unorm,WGPUTextureFormat_R16Snorm,WGPUTextureFormat_R16Uint,WGPUTextureFormat_R16Sint,WGPUTextureFormat_R16Float,
 WGPUTextureFormat_RG16Unorm,WGPUTextureFormat_RG16Snorm,WGPUTextureFormat_RG16Uint,WGPUTextureFormat_RG16Sint,WGPUTextureFormat_RG16Float,
 WGPUTextureFormat_RGBA16Unorm,WGPUTextureFormat_RGBA16Snorm,WGPUTextureFormat_RGB10A2Uint,WGPUTextureFormat_RGB10A2Unorm,WGPUTextureFormat_RG11B10Ufloat};
static const WGPUTextureFormat render[]={WGPUTextureFormat_R8Snorm,WGPUTextureFormat_RG8Snorm,WGPUTextureFormat_RGBA8Snorm,
 WGPUTextureFormat_R16Unorm,WGPUTextureFormat_R16Snorm,WGPUTextureFormat_RG16Unorm,WGPUTextureFormat_RG16Snorm,
 WGPUTextureFormat_RGBA16Unorm,WGPUTextureFormat_RGBA16Snorm};
static void storage_case(WGPUTextureFormat format,uint32_t feature)
{
 fixture f=fresh(); doe_resource_config c=config(&f);
 doe_resource_texture_desc desc=texture_desc(); doe_resource_texture_limits limits=texture_limits();
 desc.format=format; desc.usage=WGPUTextureUsage_StorageBinding;
 assert(doe_resource_texture_validate(&desc,&limits)==DOE_RESOURCE_USAGE);
 assert(doe_resource_texture_validate_with_features(&desc,&limits,feature)==DOE_RESOURCE_OK);
 doe_resource_layout_entry entry={.visibility=WGPUShaderStage_Compute,.kind=DOE_BIND_STORAGE_TEXTURE,
  .storage_access=WGPUStorageTextureAccess_WriteOnly,.storage_format=format};
 doe_resource_layout *layout=NULL;
 assert(doe_resource_layout_create(&c,&entry,1,false,&layout)==DOE_RESOURCE_LAYOUT && !layout);
 c.format_features=feature;
 assert(doe_resource_layout_create(&c,&entry,1,false,&layout)==DOE_RESOURCE_OK);
 doe_resource_layout_release(layout); empty(&f);
}
static void tier2_access(void)
{
 static const WGPUTextureFormat allowed[]={WGPUTextureFormat_R8Unorm,WGPUTextureFormat_R8Uint,WGPUTextureFormat_R8Sint,WGPUTextureFormat_RGBA8Unorm,WGPUTextureFormat_RGBA8Uint,WGPUTextureFormat_RGBA8Sint,WGPUTextureFormat_R16Uint,WGPUTextureFormat_R16Sint,WGPUTextureFormat_R16Float,WGPUTextureFormat_RGBA16Uint,WGPUTextureFormat_RGBA16Sint,WGPUTextureFormat_RGBA16Float,WGPUTextureFormat_RGBA32Uint,WGPUTextureFormat_RGBA32Sint,WGPUTextureFormat_RGBA32Float};
 static const WGPUTextureFormat denied[]={WGPUTextureFormat_RG32Float,WGPUTextureFormat_RG32Uint,
  WGPUTextureFormat_RG32Sint,WGPUTextureFormat_RGBA8Snorm,WGPUTextureFormat_BGRA8Unorm,
  WGPUTextureFormat_RG16Float,WGPUTextureFormat_R8Snorm};
 fixture f=fresh(); doe_resource_config c=config(&f);
 for(size_t i=0;i<sizeof(allowed)/sizeof(*allowed);++i){
  doe_resource_layout_entry entry={.visibility=WGPUShaderStage_Compute,.kind=DOE_BIND_STORAGE_TEXTURE,
   .storage_access=WGPUStorageTextureAccess_ReadWrite,.storage_format=allowed[i]};
  doe_resource_layout *layout=NULL; c.format_features=DOE_RESOURCE_FORMAT_TIER1;
  assert(doe_resource_layout_create(&c,&entry,1,false,&layout)==DOE_RESOURCE_LAYOUT && !layout);
  c.format_features=DOE_RESOURCE_FORMAT_TIER2;
  assert(doe_resource_layout_create(&c,&entry,1,false,&layout)==DOE_RESOURCE_OK);
  doe_resource_layout_release(layout);
 }
 for(size_t i=0;i<sizeof(denied)/sizeof(*denied);++i){
  doe_resource_layout_entry entry={.visibility=WGPUShaderStage_Compute,.kind=DOE_BIND_STORAGE_TEXTURE,
   .storage_access=WGPUStorageTextureAccess_ReadWrite,.storage_format=denied[i]};
  doe_resource_layout *layout=NULL; c.format_features=DOE_RESOURCE_FORMAT_TIER2|DOE_RESOURCE_FORMAT_BGRA8_STORAGE;
  assert(doe_resource_layout_create(&c,&entry,1,false,&layout)==DOE_RESOURCE_LAYOUT && !layout);
 }
 empty(&f);
}
static void properties_query(void)
{
 doe_resource_format_properties p={0};
 assert(doe_resource_format_properties_get(WGPUTextureFormat_RGBA8Unorm,0,&p)==DOE_RESOURCE_OK);
 assert(p.block_bytes==4 && p.block_width==1 && p.sampled && p.filterable && p.renderable &&
  p.blendable && p.multisample && p.storage && !p.read_write && !p.depth && !p.stencil);
 assert(doe_resource_format_properties_get(WGPUTextureFormat_R32Float,0,&p)==DOE_RESOURCE_OK);
 assert(!p.filterable && !p.blendable && p.multisample && p.read_write);
 assert(doe_resource_format_properties_get(WGPUTextureFormat_R32Float,
  DOE_RESOURCE_FORMAT_FLOAT32_FILTERABLE|DOE_RESOURCE_FORMAT_FLOAT32_BLENDABLE,&p)==DOE_RESOURCE_OK);
 assert(p.filterable && p.blendable);
 assert(doe_resource_format_properties_get(WGPUTextureFormat_RG11B10Ufloat,0,&p)==DOE_RESOURCE_OK);
 assert(!p.renderable && !p.storage);
 assert(doe_resource_format_properties_get(WGPUTextureFormat_RG11B10Ufloat,DOE_RESOURCE_FORMAT_TIER2,&p)==DOE_RESOURCE_OK);
 assert(p.renderable && p.storage && !p.read_write);
 assert(doe_resource_format_properties_get(WGPUTextureFormat_RGBA8Uint,DOE_RESOURCE_FORMAT_TIER2,&p)==DOE_RESOURCE_OK);
 assert(!p.filterable && !p.blendable && p.read_write);
 assert(doe_resource_format_properties_get(WGPUTextureFormat_Depth32FloatStencil8,0,&p)==DOE_RESOURCE_OK);
 assert(p.depth && p.stencil && p.renderable && p.multisample && !p.storage && !p.blendable);
 assert(doe_resource_format_properties_get(WGPUTextureFormat_BC1RGBAUnorm,0,&p)==DOE_RESOURCE_OK);
 assert(p.block_bytes==8 && p.block_width==4 && p.block_height==4 && !p.renderable && !p.storage);
 doe_resource_format_properties saved=p;
 assert(doe_resource_format_properties_get(WGPUTextureFormat_Undefined,0,&p)==DOE_RESOURCE_UNSUPPORTED);
 assert(!memcmp(&saved,&p,sizeof(p)));
}
int main(void)
{
 tier2_access();
 properties_query();
 for(size_t i=0;i<sizeof(storage)/sizeof(*storage);++i) storage_case(storage[i],DOE_RESOURCE_FORMAT_TIER1);
 storage_case(WGPUTextureFormat_BGRA8Unorm,DOE_RESOURCE_FORMAT_BGRA8_STORAGE);
 for(size_t i=0;i<sizeof(render)/sizeof(*render);++i){
  doe_resource_texture_desc desc=texture_desc(); doe_resource_texture_limits limits=texture_limits();
  desc.format=render[i]; desc.usage=WGPUTextureUsage_RenderAttachment;
  assert(!doe_resource_format_color_renderable(desc.format,0));
  assert(doe_resource_texture_validate(&desc,&limits)==DOE_RESOURCE_USAGE);
  assert(doe_resource_texture_validate_with_features(&desc,&limits,DOE_RESOURCE_FORMAT_TIER1)==DOE_RESOURCE_OK);
  desc.samples=4;
  assert(doe_resource_texture_validate_with_features(&desc,&limits,DOE_RESOURCE_FORMAT_TIER1)==DOE_RESOURCE_OK);
 }
 puts("PASS requested BGRA storage and23 Tier1 storage/9 render format admission; canonical host owner");
}
