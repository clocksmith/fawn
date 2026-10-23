"""Public-header-only ctypes consumer. Loading a candidate is opt-in by runner."""
from __future__ import annotations
import ctypes as C
U32=C.c_uint32;U64=C.c_uint64;SZ=C.c_size_t
class Record(C.Structure):
    def fields(self):return {k:getattr(self,k) for k,_ in self._fields_}
class Capacities(Record):
    _fields_=[(x,SZ) for x in ('source_bytes','tokens','statements','expressions','types','members','resources','access_steps','constants','constant_edges','ir_instructions','ir_operands','target_declarations','traversal_items','words')]+[(x,U32) for x in ('expression_depth','control_depth','type_syntax_depth','reserved')]
class Name(Record):_fields_=[('offset',U32),('length',U32)]
class Type(Record):_fields_=[('kind',U32),('flags',U32),('name',Name)]+[(x,U32) for x in ('scalar_kind','element_type','vector_width','array_count','first_member','member_count')]+[(x,U64) for x in ('size_bytes','alignment_bytes','stride_bytes')]
class Member(Record):_fields_=[('name',Name),('type_index',U32),('flags',U32)]+[(x,U64) for x in ('offset_bytes','size_bytes','alignment_bytes','explicit_align_bytes','explicit_size_bytes')]
class Resource(Record):_fields_=[('name',Name)]+[(x,U32) for x in ('group','binding','kind','access','type_index','flags')]+[(x,U64) for x in ('minimum_binding_size_bytes','type_alignment_bytes','address_space_alignment_bytes','runtime_array_offset_bytes','runtime_array_stride_bytes')]+[('runtime_array_type',U32),('reserved',U32)]
class Counts(Record):_fields_=[(x,SZ) for x in ('words','types','members','resources','name_bytes')]
class Outputs(Record):_fields_=[('words',C.POINTER(U32)),('word_capacity',SZ),('types',C.POINTER(Type)),('type_capacity',SZ),('type_record_size',SZ),('members',C.POINTER(Member)),('member_capacity',SZ),('member_record_size',SZ),('resources',C.POINTER(Resource)),('resource_capacity',SZ),('resource_record_size',SZ),('names',C.POINTER(C.c_char)),('name_capacity',SZ)]
class Module(Record):_fields_=[('schema_version',U32),('layout_profile',U32),('entry_point',Name),('workgroup_size',U32*3),('declared_struct_count',U32)]
class Diagnostic(Record):_fields_=[('status',C.c_int),('offset',SZ),('line',U32),('column',U32),('message',C.c_char*192)]
class Result(Record):_fields_=[('required',Counts),('published',Counts),('required_is_exact',U32),('exhausted_pool',U32),('minimum_pool_capacity',SZ),('module',Module),('diagnostic',Diagnostic)]
class Requirements(Record):_fields_=[('size_bytes',SZ),('alignment_bytes',SZ)]

def profile():return Capacities(131072,16384,2048,8192,1024,2048,64,16385,4096,16384,49153,16384,32768,4096,262144,64,32,64,0)

class Compiler:
    def __init__(self,path,caps=None):
        self.lib=C.CDLL(str(path));self.caps=profile() if caps is None else caps
        self.query=self.lib.doe_compiler_typed_requirements
        self.query.argtypes=[U32,C.POINTER(Capacities),SZ,C.POINTER(Requirements),SZ];self.query.restype=C.c_int
        self.compile=self.lib.doe_compiler_compile_typed
        self.compile.argtypes=[U32,C.POINTER(Capacities),SZ,C.c_void_p,SZ,C.c_void_p,SZ,U32,C.POINTER(Outputs),SZ,C.POINTER(Result),SZ];self.compile.restype=C.c_int
        req=Requirements();status=self.query(4,C.byref(self.caps),C.sizeof(self.caps),C.byref(req),C.sizeof(req))
        if status:raise AssertionError(('requirements',status))
        if not req.alignment_bytes or req.alignment_bytes&(req.alignment_bytes-1):raise AssertionError('query alignment')
        self.backing=C.create_string_buffer(req.size_bytes+req.alignment_bytes)
        self.address=(C.addressof(self.backing)+req.alignment_bytes-1)&-req.alignment_bytes;self.size=req.size_bytes
    def call(self,source,flags,counts=None):
        raw=source.encode();src=C.create_string_buffer(raw);r=Result();o=Outputs();o.type_record_size=C.sizeof(Type);o.member_record_size=C.sizeof(Member);o.resource_record_size=C.sizeof(Resource)
        arrays={}
        if counts:
            for key,ctype,ptr,capacity in [('words',U32,'words','word_capacity'),('types',Type,'types','type_capacity'),('members',Member,'members','member_capacity'),('resources',Resource,'resources','resource_capacity'),('name_bytes',C.c_char,'names','name_capacity')]:
                n=counts[key];arrays[key]=(ctype*n)();setattr(o,ptr,arrays[key]);setattr(o,capacity,n)
                C.memset(C.addressof(arrays[key]),0xA5,C.sizeof(arrays[key]))
        status=self.compile(4,C.byref(self.caps),C.sizeof(self.caps),self.address,self.size,src,len(raw),flags,None if flags&4 else C.byref(o),0 if flags&4 else C.sizeof(o),C.byref(r),C.sizeof(r))
        return status,r,arrays

def read_reflection(result,arrays):
    names=bytes(arrays['name_bytes']);cursor=0
    def name(n):
        nonlocal cursor
        if not n.length:
            if n.offset:raise AssertionError('empty name offset')
            return ''
        if n.offset!=cursor or n.offset+n.length>=len(names) or names[n.offset+n.length]!=0:raise AssertionError('name span/order/NUL')
        text=names[n.offset:n.offset+n.length].decode();cursor+=n.length+1;return text
    m=result.module
    if (m.schema_version,m.layout_profile)!=(4,1):raise AssertionError('module schema/profile')
    reflection=dict(entry=name(m.entry_point),workgroup_size=list(m.workgroup_size),declared_struct_count=m.declared_struct_count)
    for key in ('types','members','resources'):
        rows=[]
        for rec in arrays[key]:
            obj=rec.fields();obj['name']=name(rec.name);rows.append(obj)
        reflection[key]=rows
    if cursor!=len(names):raise AssertionError('name count')
    return reflection
