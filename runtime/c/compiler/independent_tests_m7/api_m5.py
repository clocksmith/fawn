"""Independent ctypes consumer of the frozen public v2 ABI, caller malloc owned."""
from __future__ import annotations
import ctypes as C
from pathlib import Path
from evaluate import Diagnostic, U32, SIZE, SENTINEL

class Requirements(C.Structure):
    _fields_ = [('size_bytes', SIZE), ('alignment_bytes', SIZE)]
class Member(C.Structure):
    _fields_ = [('name', C.c_char * 64), ('scalar', U32), ('offset_bytes', U32), ('size_bytes', U32), ('alignment_bytes', U32)]
class Struct(C.Structure):
    _fields_ = [('name', C.c_char * 64), ('member_count', U32), ('size_bytes', U32), ('alignment_bytes', U32), ('members', Member * 8)]
class Binding(C.Structure):
    _fields_ = [('name', C.c_char * 64)] + [(name, U32) for name in ['group', 'binding', 'kind', 'access', 'scalar', 'element_stride', 'runtime_sized', 'minimum_binding_size_bytes', 'type_alignment_bytes', 'address_space_alignment_bytes', 'struct_index']]
class Reflection(C.Structure):
    _fields_ = [('entry_point', C.c_char * 64), ('workgroup_size', U32 * 3), ('binding_count', U32), ('bindings', Binding * 8), ('struct_count', U32), ('structs', Struct * 8)]
class Result(C.Structure):
    _fields_ = [('word_count', SIZE), ('reflection', Reflection), ('diagnostic', Diagnostic)]

class Api:
    def __init__(self, library: Path) -> None:
        self.lib = C.CDLL(str(library.resolve()))
        self.query = self.lib.doe_compiler_workspace_requirements
        self.query.argtypes = [U32, C.POINTER(Requirements), SIZE]
        self.query.restype = C.c_int
        self.compile = self.lib.doe_compiler_compile_v2
        self.compile.argtypes = [U32, C.c_void_p, SIZE, C.c_void_p, SIZE, C.POINTER(U32), SIZE, C.POINTER(Result), SIZE]
        self.compile.restype = C.c_int
        self.req = Requirements()
        assert self.query(2, C.byref(self.req), C.sizeof(self.req)) == 0
        assert self.req.size_bytes and self.req.alignment_bytes and not self.req.alignment_bytes & (self.req.alignment_bytes - 1)
        self.libc = C.CDLL(None)
        self.libc.malloc.argtypes = [SIZE]
        self.libc.malloc.restype = C.c_void_p
        self.libc.free.argtypes = [C.c_void_p]
        self.workspace = self.libc.malloc(self.req.size_bytes)
        assert self.workspace and self.workspace % self.req.alignment_bytes == 0
        C.memset(self.workspace, 0xA5, self.req.size_bytes)

    def close(self) -> None:
        if self.workspace:
            self.libc.free(self.workspace)
            self.workspace = None

    def call(self, source: bytes, capacity: int = 4096) -> tuple:
        src = C.create_string_buffer(source, max(1, len(source)))
        words = (U32 * 4112)(*[SENTINEL] * 4112)
        before = bytes(words)
        ptr = C.cast(C.byref(words, 32), C.POINTER(U32))
        result = Result()
        C.memset(C.byref(result), 0xA5, C.sizeof(result))
        status = self.compile(2, self.workspace, self.req.size_bytes, src, len(source), ptr, capacity, C.byref(result), C.sizeof(result))
        errors = []
        if bytes(words)[:32] != before[:32] or bytes(words)[32 + capacity * 4:] != before[32 + capacity * 4:]:
            errors.append('word output guards modified')
        if status:
            if bytes(words) != before:
                errors.append('failure published words')
            if result.word_count or bytes(result.reflection) != bytes(C.sizeof(Reflection)):
                errors.append('failure published count/reflection')
            d = result.diagnostic
            if d.status != status or d.offset > len(source) or d.line < 1 or d.column < 1:
                errors.append('invalid original source diagnostic')
            elif b'\r' not in source:
                prefix = source[:d.offset]
                if (d.line, d.column) != (prefix.count(b'\n') + 1, len(prefix.rsplit(b'\n', 1)[-1]) + 1):
                    errors.append('diagnostic line/column differs from original source')
        elif not 0 < result.word_count <= capacity:
            errors.append('success word count invalid')
        return status, result, list(words[8:8 + min(result.word_count, capacity)]) if not status else [], errors


def reflection_dict(r: Reflection) -> dict:
    def fields(obj: C.Structure, omit: set[str] = set()) -> dict:
        return {name: value.decode() if isinstance(value, bytes) else value for name, _ in obj._fields_ if name not in omit for value in [getattr(obj, name)]}
    return dict(entry=r.entry_point.decode(), n=list(r.workgroup_size), bindings=[fields(b) for b in r.bindings[:r.binding_count]], structs=[dict(**fields(s, {'members'}), members=[fields(m) for m in s.members[:s.member_count]]) for s in r.structs[:r.struct_count]])
