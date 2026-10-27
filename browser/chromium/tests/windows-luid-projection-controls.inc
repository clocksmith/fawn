int main() {
    RequestAdapterOptionsLUID luid;
    luid.adapterLUID.LowPart=0x90abcdefu; luid.adapterLUID.HighPart=-0x1234567;
    // Address comparison is the only conversion of the input chain address;
    // no native C++ payload is read through an API-layout cast.
    auto* luidChain = reinterpret_cast<WGPUChainedStruct*>(&luid);
    WGPUDawnTogglesDescriptor toggles=WGPU_DAWN_TOGGLES_DESCRIPTOR_INIT;
    WGPUDawnCacheDeviceDescriptor cache=WGPU_DAWN_CACHE_DEVICE_DESCRIPTOR_INIT;
    WGPUDawnConsumeAdapterDescriptor consume=WGPU_DAWN_CONSUME_ADAPTER_DESCRIPTOR_INIT;
    WGPURequestAdapterWebGPUBackendOptions route=WGPU_REQUEST_ADAPTER_WEBGPU_BACKEND_OPTIONS_INIT;
    WGPUChainedStruct unknown{nullptr,static_cast<WGPUSType>(0x6ffffff0)};
    InnerRuntimeWindowsAdapterLuid storage;
    WGPURequestAdapterOptions options=WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.backendType=WGPUBackendType_D3D12;
    options.forceFallbackAdapter=WGPU_FALSE;
    // Counterfactual old projection: exact API-layout LUID retains private tail,
    // so actual C19 rejects without changing the output selection.
    native_windows_adapter_luid oldBoundary{{&toggles.chain,static_cast<WGPUSType>(NATIVE_DAWN_REQUEST_ADAPTER_LUID)},0x90abcdefu,-0x1234567};
    options.nextInChain=InnerRuntimeDescriptorChain(&toggles,&oldBoundary.chain,true);
    require(options.nextInChain==&oldBoundary.chain && options.nextInChain->next==&toggles.chain);
    native_adapter_selection oldRejected{false,7,11};
    require(!native_adapter_request_decode(WGPUBackendType_D3D12,&options,&oldRejected));
    require(oldRejected.luid_low==7 && oldRejected.luid_high==11);
    luid.nextInChain=reinterpret_cast<const wgpu::ChainedStruct*>(&toggles.chain);
    auto* projected=InnerRuntimeWindowsAdapterChain(&toggles,luidChain,&luid,true,storage);
    require(projected==&storage.chain && !projected->next);
    require(storage.adapterLUID.LowPart==luid.adapterLUID.LowPart && storage.adapterLUID.HighPart==luid.adapterLUID.HighPart);
    require(luid.nextInChain==reinterpret_cast<const wgpu::ChainedStruct*>(&toggles.chain) && !toggles.chain.next);
    native_adapter_selection selected{};options.nextInChain=projected;
    require(native_adapter_request_decode(WGPUBackendType_D3D12,&options,&selected));
    require(selected.has_luid && selected.luid_low==0x90abcdefu && selected.luid_high==-0x1234567);
    options.forceFallbackAdapter=WGPU_TRUE;
    native_adapter_selection untouched{false,17,19};selected=untouched;
    require(!native_adapter_request_decode(WGPUBackendType_D3D12,&options,&selected));
    require(selected.luid_low==17 && selected.luid_high==19);options.forceFallbackAdapter=WGPU_FALSE;
    require(InnerRuntimeWindowsAdapterChain(&toggles,luidChain,&luid,false,storage)==&toggles.chain);
    require(!InnerRuntimeWindowsAdapterChain(nullptr,luidChain,&luid,false,storage));
    // Every already-consumed leading/tail private type, preserving originals.
    WGPUChainedStruct* privateNodes[]={&toggles.chain,&cache.chain,&consume.chain,&route.chain};
    for(auto* node:privateNodes) {
        node->next=luidChain;luid.nextInChain=nullptr;
        projected=InnerRuntimeWindowsAdapterChain(&toggles,node,&luid,true,storage);
        require(projected==&storage.chain && !projected->next && node->next==luidChain);
        node->next=nullptr;luid.nextInChain=reinterpret_cast<const wgpu::ChainedStruct*>(node);
        projected=InnerRuntimeWindowsAdapterChain(&toggles,luidChain,&luid,true,storage);
        require(projected==&storage.chain && !projected->next && luid.nextInChain==reinterpret_cast<const wgpu::ChainedStruct*>(node));
    }
    toggles.chain.next=&unknown;
    luid.nextInChain=reinterpret_cast<const wgpu::ChainedStruct*>(&toggles.chain);
    projected=InnerRuntimeWindowsAdapterChain(&toggles,luidChain,&luid,true,storage);
    require(projected==&storage.chain && projected->next==&unknown && toggles.chain.next==&unknown);
    options.nextInChain=projected;require(!native_adapter_request_decode(WGPUBackendType_D3D12,&options,&selected));
    // Do not traverse a required unknown node's borrowed tail.
    unknown.next=reinterpret_cast<WGPUChainedStruct*>(static_cast<uintptr_t>(1));
    require(InnerRuntimeWindowsAdapterChain(&toggles,&unknown,&luid,true,storage)==&unknown);unknown.next=nullptr;
    RequestAdapterOptionsLUID duplicate;
    toggles.chain.next=reinterpret_cast<WGPUChainedStruct*>(&duplicate);
    projected=InnerRuntimeWindowsAdapterChain(&toggles,luidChain,&luid,true,storage);
    require(projected->next==reinterpret_cast<WGPUChainedStruct*>(&duplicate));
    options.nextInChain=projected;require(!native_adapter_request_decode(WGPUBackendType_D3D12,&options,&selected));
    require(InnerRuntimeWindowsAdapterChain(&toggles,luidChain,&duplicate,true,storage)==luidChain);
    require(InnerRuntimeWindowsAdapterChain(&toggles,luidChain,nullptr,true,storage)==luidChain);
    // All-private cycles remain required, never NULL; includes cycles after LUID.
    toggles.chain.next=&cache.chain;cache.chain.next=&toggles.chain;
    require(InnerRuntimeWindowsAdapterChain(&toggles,&toggles.chain,&luid,true,storage)==&toggles.chain);
    projected=InnerRuntimeWindowsAdapterChain(&toggles,luidChain,&luid,true,storage);
    require(projected==&storage.chain && projected->next==&toggles.chain);
    options.nextInChain=projected;require(!native_adapter_request_decode(WGPUBackendType_D3D12,&options,&selected));
    cache.chain.next=luidChain; // LUID -> toggles -> cache -> LUID remains non-null.
    projected=InnerRuntimeWindowsAdapterChain(&toggles,luidChain,&luid,true,storage);
    require(projected==&storage.chain && projected->next==luidChain);
    options.nextInChain=projected;require(!native_adapter_request_decode(WGPUBackendType_D3D12,&options,&selected));
    return 0;
}
