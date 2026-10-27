"""Native metadata-only inventories for a single-hardware-adapter experiment.

No device, queue or command buffer is created. A sole hardware adapter on the
enforced native backend removes ambiguity from provider-specific display names.
Multiple adapters, software adapters and missing stable identities fail closed.
"""
from __future__ import annotations

import ctypes as c
import ctypes.util
import platform
from typing import Any


def _metal_devices() -> list[dict[str, Any]]:
    metal = c.CDLL('/System/Library/Frameworks/Metal.framework/Metal')
    core = c.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')
    objc = c.CDLL('/usr/lib/libobjc.A.dylib')
    objc.sel_registerName.argtypes = [c.c_char_p]
    objc.sel_registerName.restype = c.c_void_p
    message = c.cast(objc.objc_msgSend, c.c_void_p).value
    pointer = c.CFUNCTYPE(c.c_void_p, c.c_void_p, c.c_void_p)(message)
    integer = c.CFUNCTYPE(c.c_uint64, c.c_void_p, c.c_void_p)(message)
    string = c.CFUNCTYPE(c.c_char_p, c.c_void_p, c.c_void_p)(message)
    metal.MTLCopyAllDevices.restype = c.c_void_p
    metal.MTLCopyAllDevices.argtypes = []
    core.CFArrayGetCount.argtypes = [c.c_void_p]
    core.CFArrayGetCount.restype = c.c_long
    core.CFArrayGetValueAtIndex.argtypes = [c.c_void_p, c.c_long]
    core.CFArrayGetValueAtIndex.restype = c.c_void_p
    core.CFRelease.argtypes = [c.c_void_p]
    core.CFRelease.restype = None
    array = metal.MTLCopyAllDevices()
    if not array:
        raise ValueError('MTLCopyAllDevices returned no inventory')
    try:
        count = core.CFArrayGetCount(array)
        if not 0 <= count <= 256:
            raise ValueError('invalid Metal adapter count')
        rows = []
        for index in range(count):
            device = core.CFArrayGetValueAtIndex(array, index)
            name = pointer(device, objc.sel_registerName(b'name'))
            text = string(name, objc.sel_registerName(b'UTF8String'))
            identity = integer(device, objc.sel_registerName(b'registryID'))
            rows.append({'name': text.decode('utf-8') if text else '',
                         'hardware': True, 'registryID': identity})
        return rows
    finally:
        core.CFRelease(array)


class _Application(c.Structure):
    _fields_ = [('sType', c.c_uint32), ('pNext', c.c_void_p),
                ('pApplicationName', c.c_char_p), ('applicationVersion', c.c_uint32),
                ('pEngineName', c.c_char_p), ('engineVersion', c.c_uint32),
                ('apiVersion', c.c_uint32)]


class _InstanceInfo(c.Structure):
    _fields_ = [('sType', c.c_uint32), ('pNext', c.c_void_p), ('flags', c.c_uint32),
                ('pApplicationInfo', c.POINTER(_Application)),
                ('enabledLayerCount', c.c_uint32), ('ppEnabledLayerNames', c.c_void_p),
                ('enabledExtensionCount', c.c_uint32), ('ppEnabledExtensionNames', c.c_void_p)]


class _DeviceID(c.Structure):
    _fields_ = [('sType', c.c_uint32), ('pNext', c.c_void_p),
                ('deviceUUID', c.c_ubyte * 16), ('driverUUID', c.c_ubyte * 16),
                ('deviceLUID', c.c_ubyte * 8), ('deviceNodeMask', c.c_uint32),
                ('deviceLUIDValid', c.c_uint32)]


class _Properties(c.Structure):
    # VkPhysicalDeviceProperties starts with five uint32 values and a 256-byte
    # name. An aligned oversized tail lets the driver write the complete pinned
    # core properties structure without mirroring its unused limits members.
    _fields_ = [('sType', c.c_uint32), ('pNext', c.c_void_p), ('properties', c.c_uint64 * 512)]


def _vulkan_devices() -> list[dict[str, Any]]:
    library = ctypes.util.find_library('vulkan')
    if not library:
        raise ValueError('native Vulkan loader unavailable')
    vk = c.CDLL(library)
    vk.vkCreateInstance.argtypes = [c.POINTER(_InstanceInfo), c.c_void_p, c.POINTER(c.c_void_p)]
    vk.vkCreateInstance.restype = c.c_int32
    vk.vkDestroyInstance.argtypes = [c.c_void_p, c.c_void_p]
    vk.vkDestroyInstance.restype = None
    vk.vkEnumeratePhysicalDevices.argtypes = [c.c_void_p, c.POINTER(c.c_uint32), c.POINTER(c.c_void_p)]
    vk.vkEnumeratePhysicalDevices.restype = c.c_int32
    vk.vkGetPhysicalDeviceProperties2.argtypes = [c.c_void_p, c.POINTER(_Properties)]
    vk.vkGetPhysicalDeviceProperties2.restype = None
    app = _Application(sType=0, pApplicationName=b'doe-baseline-inventory', apiVersion=(1 << 22) | (1 << 12))
    info = _InstanceInfo(sType=1, pApplicationInfo=c.pointer(app))
    instance = c.c_void_p()
    result = vk.vkCreateInstance(c.byref(info), None, c.byref(instance))
    if result != 0 or not instance:
        raise ValueError(f'Vulkan inventory instance failed: {result}')
    try:
        count = c.c_uint32()
        if vk.vkEnumeratePhysicalDevices(instance, c.byref(count), None) != 0 or count.value > 256:
            raise ValueError('Vulkan physical-device count failed')
        handles = (c.c_void_p * count.value)()
        if vk.vkEnumeratePhysicalDevices(instance, c.byref(count), handles) != 0:
            raise ValueError('Vulkan physical-device inventory changed')
        rows = []
        for handle in handles[:count.value]:
            identity = _DeviceID(sType=1000071004)
            props = _Properties(sType=1000059001, pNext=c.cast(c.pointer(identity), c.c_void_p))
            vk.vkGetPhysicalDeviceProperties2(handle, c.byref(props))
            raw = bytes(props.properties)
            values = (c.c_uint32 * 5).from_buffer_copy(raw[:20])
            rows.append({'name': raw[20:276].split(b'\0')[0].decode('utf-8'),
                         'hardware': True if values[4] in (1, 2) else False if values[4] == 4 else None,
                         'deviceType': values[4],
                         'vendorID': values[2], 'deviceID': values[3],
                         'apiVersion': values[0], 'driverVersion': values[1],
                         'deviceUUID': bytes(identity.deviceUUID).hex(),
                         'driverUUID': bytes(identity.driverUUID).hex()})
        return rows
    finally:
        vk.vkDestroyInstance(instance, None)


def probe(backend: str) -> dict[str, Any]:
    """Query the actual host API; there is no caller-supplied inventory file."""
    if backend == 'metal' and platform.system() == 'Darwin':
        devices = _metal_devices()
    elif backend == 'vulkan' and platform.system() == 'Linux':
        devices = _vulkan_devices()
    else:
        raise ValueError('single-adapter inventory supports only native macOS Metal or Linux Vulkan')
    inventory = {'backend': backend, 'host': platform.platform(), 'devices': devices,
                 'scope': 'native metadata only; no device, queue or GPU commands'}
    single_identity(inventory)
    return inventory


def single_identity(inventory: dict[str, Any]) -> tuple[Any, ...]:
    devices = inventory.get('devices')
    if not isinstance(devices, list) or any(not isinstance(row, dict) or
                                           type(row.get('hardware')) is not bool for row in devices):
        raise ValueError('incomplete hardware classification')
    hardware = [row for row in devices if row['hardware']]
    if len(hardware) != 1 or not hardware[0].get('name') or not inventory.get('host'):
        raise ValueError('exactly one named native hardware adapter required')
    row = hardware[0]
    backend = inventory.get('backend')
    if backend == 'metal':
        identity = row.get('registryID')
        if type(identity) is not int or identity <= 0:
            raise ValueError('Metal registry identity missing')
        return backend, inventory['host'], row['name'], identity
    if backend == 'vulkan':
        values = tuple(row.get(key) for key in ('deviceUUID', 'driverUUID'))
        if any(not isinstance(value, str) or len(value) != 32 or
               any(char not in '0123456789abcdef' for char in value) or int(value, 16) == 0 for value in values):
            raise ValueError('Vulkan device/driver UUID missing')
        return backend, inventory['host'], row['name'], *values, row.get('driverVersion')
    raise ValueError('unsupported native inventory backend')


def pair_is_bound(left: dict[str, Any], right: dict[str, Any], inventory: dict[str, Any]) -> bool:
    """Only an enforced non-fallback native backend can use the sole-adapter proof."""
    backend = single_identity(inventory)[0]
    for meta in (left, right):
        selection = meta.get('packageBackendSelection') or {}
        info = meta.get('adapterInfo') or {}
        if (selection.get('requested') != backend or selection.get('enforced') is not True
                or selection.get('mechanism') not in ('native-adapter-backend-type', 'dawn-create-backend-filter')
                or info.get('isFallbackAdapter') is not False or not info.get('device')):
            return False
        observed = selection.get('observedBackendType')
        expected = {'metal': 5, 'vulkan': 6}[backend]
        if ((selection['mechanism'] == 'native-adapter-backend-type' and observed != expected)
                or (observed is not None and observed != expected)):
            return False
    return True
