"""Independent bounded SPIR-V CPU oracle, NOT a Khronos validator or GPU runner.

Instruction numbers follow Khronos SPIRV-Headers spirv.core.grammar.json.
Only the declared subset below is interpreted. Unsupported opcodes are reported
as oracle limitations, never evidence that another compiler's output is invalid.
"""
import struct
from dataclasses import dataclass

class OracleLimit(Exception): pass
class StructuralError(Exception): pass
class ExecutionError(Exception): pass

NAMES={0:'Nop',3:'Source',4:'SourceExtension',5:'Name',6:'MemberName',7:'String',8:'Line',11:'ExtInstImport',14:'MemoryModel',15:'EntryPoint',16:'ExecutionMode',17:'Capability',19:'TypeVoid',20:'TypeBool',21:'TypeInt',23:'TypeVector',28:'TypeArray',29:'TypeRuntimeArray',30:'TypeStruct',32:'TypePointer',33:'TypeFunction',41:'ConstantTrue',42:'ConstantFalse',43:'Constant',44:'ConstantComposite',54:'Function',55:'FunctionParameter',56:'FunctionEnd',57:'FunctionCall',59:'Variable',61:'Load',62:'Store',65:'AccessChain',66:'InBoundsAccessChain',68:'ArrayLength',71:'Decorate',72:'MemberDecorate',80:'CompositeConstruct',81:'CompositeExtract',83:'CopyObject',113:'UConvert',124:'Bitcast',128:'IAdd',130:'ISub',132:'IMul',164:'LogicalEqual',165:'LogicalNotEqual',166:'LogicalOr',167:'LogicalAnd',168:'LogicalNot',169:'Select',170:'IEqual',171:'INotEqual',172:'UGreaterThan',174:'UGreaterThanEqual',176:'ULessThan',178:'ULessThanEqual',245:'Phi',247:'SelectionMerge',248:'Label',249:'Branch',250:'BranchConditional',253:'Return',254:'ReturnValue'}
RESULT_FIRST={7,11,19,20,21,23,28,29,30,32,33,248}
RESULT_SECOND={41,42,43,44,54,55,57,59,61,65,66,68,80,81,83,113,124,128,130,132,164,165,166,167,168,169,170,171,172,174,176,178,245}
MIN_ARGS={0:0,14:2,15:3,16:2,17:1,19:1,20:1,21:3,23:3,29:2,30:2,32:3,33:2,43:3,54:4,56:0,59:3,61:3,62:2,65:4,66:4,68:4,71:2,72:3,81:4,128:4,176:4,247:2,248:1,249:1,250:3,253:0}

def literal_string(words):
    raw=b''.join(struct.pack('<I',w) for w in words)
    try: end=raw.index(0)
    except ValueError: raise StructuralError('unterminated SPIR-V literal string')
    if any(raw[end+1:((end//4)+1)*4]): raise StructuralError('nonzero string padding')
    return raw[:end].decode('ascii'),end//4+1

@dataclass(frozen=True)
class Pointer:
    root:int
    indices:tuple=()

class Module:
    def __init__(self, words):
        self.words=list(words)
        if len(words)<5 or words[0]!=0x07230203: raise StructuralError('bad SPIR-V header')
        self.version,self.generator,self.bound,self.schema=words[1:5]
        if self.schema!=0 or self.bound<1: raise StructuralError('bad bound/schema')
        self.instructions=[]; self.defs={}; self.types={}; self.constants={}; self.variables={}; self.decorations={}; self.member_decorations={}; self.entries=[]; self.modes=[]; self.capabilities=[]; self.memory_models=[]; self.functions={}
        offset=5; current=None
        while offset<len(words):
            n=words[offset]>>16; op=words[offset]&65535
            if n==0 or offset+n>len(words): raise StructuralError(f'bad instruction length at word {offset}')
            args=tuple(words[offset+1:offset+n]); ins=(op,args,offset)
            if op not in NAMES: raise OracleLimit(f'unsupported opcode {op} at word {offset}')
            if len(args)<MIN_ARGS.get(op,0): raise StructuralError(f'short {NAMES[op]} at word {offset}')
            self.instructions.append(ins)
            if op in RESULT_FIRST|RESULT_SECOND:
                rid=args[0 if op in RESULT_FIRST else 1]
                if not (0<rid<self.bound) or rid in self.defs: raise StructuralError(f'duplicate/out-of-bound result ID {rid}')
                self.defs[rid]=ins
            if op in {19,20,21,23,28,29,30,32,33}: self.types[args[0]]=(op,args[1:])
            elif op==43: self.constants[args[1]]=args[2]
            elif op in {41,42}: self.constants[args[1]]=(op==41)
            elif op==44: self.constants[args[1]]=tuple(self.constants[x] for x in args[2:])
            elif op==59: self.variables[args[1]]=(args[0],args[2],args[3:])
            elif op==71: self.decorations.setdefault(args[0],{})[args[1]]=args[2:]
            elif op==72: self.member_decorations.setdefault((args[0],args[1]),{})[args[2]]=args[3:]
            elif op==15:
                name,used=literal_string(args[2:]); self.entries.append((args[0],args[1],name,args[2+used:]))
            elif op==16: self.modes.append(args)
            elif op==17: self.capabilities.append(args[0])
            elif op==14: self.memory_models.append(args)
            if op==54:
                if current is not None: raise StructuralError('nested function')
                current=args[1]; self.functions[current]=[]
            elif op==56:
                if current is None: raise StructuralError('function end without function')
                current=None
            elif current is not None: self.functions[current].append(ins)
            offset+=n
        if current is not None: raise StructuralError('unclosed function')

    def interface(self, entry, local_size, strict=True):
        if strict and self.version!=0x00010300: raise StructuralError(f'not SPIR-V 1.3: {self.version:#x}')
        if 1 not in self.capabilities: raise StructuralError('Shader capability missing')
        if self.memory_models!=[(0,1)]: raise StructuralError(f'not Logical GLSL450 memory model: {self.memory_models}')
        if len(self.entries)!=1: raise StructuralError('expected one entry point')
        model,fn,name,interfaces=self.entries[0]
        if model!=5 or name!=entry or fn not in self.functions: raise StructuralError('wrong compute entry point')
        if (fn,17,*local_size) not in self.modes: raise StructuralError('wrong LocalSize execution mode')
        buffers=[rid for rid,(_,sc,_) in self.variables.items() if sc==12]
        if len(buffers)!=1: raise StructuralError(f'expected one StorageBuffer, found {len(buffers)}')
        buffer=buffers[0]; dec=self.decorations.get(buffer,{})
        if dec.get(34)!=(0,) or dec.get(33)!=(0,): raise StructuralError('wrong descriptor set/binding')
        ptr_type=self.variables[buffer][0]; op,p=self.types[ptr_type]
        if op!=32 or p[0]!=12: raise StructuralError('storage buffer pointer type mismatch')
        struct_id=p[1]; op,members=self.types[struct_id]
        if op!=30 or len(members)!=1: raise StructuralError('expected single-member storage struct')
        if 2 not in self.decorations.get(struct_id,{}): raise StructuralError('Block decoration absent')
        if self.member_decorations.get((struct_id,0),{}).get(35)!=(0,): raise StructuralError('member offset not zero')
        array_id=members[0]; op,a=self.types[array_id]
        if op!=29 or self.decorations.get(array_id,{}).get(6)!=(4,): raise StructuralError('runtime array/stride mismatch')
        if self.types[a[0]]!=(21,(32,0)): raise StructuralError('element type is not u32')
        inputs=[rid for rid,(_,sc,_) in self.variables.items() if sc==1 and self.decorations.get(rid,{}).get(11)==(28,)]
        if len(inputs)!=1: raise StructuralError('expected one GlobalInvocationId input')
        iid=inputs[0]; op,p=self.types[self.variables[iid][0]]
        if op!=32 or p[0]!=1: raise StructuralError('wrong input pointer')
        op,v=self.types[p[1]]
        if op!=23 or v[1]!=3 or self.types[v[0]]!=(21,(32,0)): raise StructuralError('wrong input vector type')
        if iid not in interfaces: raise StructuralError('GlobalInvocationId absent from entry interface')
        if strict and any(self.variables.get(i,(None,None,None))[1] not in {1,3} for i in interfaces): raise StructuralError('SPIR-V 1.3 entry interface includes non-input/output global')
        self.buffer=buffer; self.input=iid; self.entry_fn=fn
        return {'instruction_count':len(self.instructions),'word_count':len(self.words),'version':self.version,'opcodes':sorted(set(op for op,_,_ in self.instructions))}

    def execute(self, data, x, y=37, z=91):
        env=dict(self.constants); memory={}; accesses=[]
        for rid,(_,storage,init) in self.variables.items():
            env[rid]=Pointer(rid)
            if storage==7: memory[rid]=env.get(init[0],None) if init else None
        def load(ptr):
            if not isinstance(ptr,Pointer): raise ExecutionError('load of non-pointer')
            if ptr.root==self.input:
                obj=(x,y,z)
                for ix in ptr.indices: obj=obj[ix]
                return obj
            if ptr.root==self.buffer:
                if len(ptr.indices)!=2 or ptr.indices[0]!=0: raise ExecutionError(f'unsupported storage pointer path {ptr.indices}')
                ix=ptr.indices[1]; accesses.append(('read',ix))
                if not 0<=ix<len(data): raise ExecutionError(f'out-of-range read {ix} for length {len(data)}')
                return data[ix]
            obj=memory[ptr.root]
            for ix in ptr.indices: obj=obj[ix]
            return obj
        def store(ptr,value):
            if not isinstance(ptr,Pointer): raise ExecutionError('store to non-pointer')
            if ptr.root==self.buffer:
                if len(ptr.indices)!=2 or ptr.indices[0]!=0: raise ExecutionError('bad storage write path')
                ix=ptr.indices[1]; accesses.append(('write',ix))
                if not 0<=ix<len(data): raise ExecutionError(f'out-of-range write {ix} for length {len(data)}')
                data[ix]=value&0xffffffff
            elif ptr.indices: raise OracleLimit('local aggregate pointer stores unsupported')
            else: memory[ptr.root]=value
        def run_function(fn,arguments=(),depth=0):
            if depth>10: raise ExecutionError('function-call depth exceeded')
            body=self.functions[fn]; labels={a[0]:i for i,(op,a,_) in enumerate(body) if op==248}
            pc=0; steps=0; block=None; previous=None; parameter_index=0
            while pc<len(body):
                steps+=1
                if steps>1000: raise ExecutionError('execution budget exceeded')
                op,a,_=body[pc]; pc+=1
                if op==55:
                    if parameter_index>=len(arguments): raise ExecutionError('missing function argument')
                    env[a[1]]=arguments[parameter_index]; parameter_index+=1
                elif op==57:
                    env[a[1]]=run_function(a[2],[env[x] for x in a[3:]],depth+1)
                elif op==248: block=a[0]
                elif op in {0,8,247}: pass
                elif op==59:
                    env[a[1]]=Pointer(a[1]); memory[a[1]]=env.get(a[3],None) if len(a)>3 else None
                elif op==61: env[a[1]]=load(env[a[2]])
                elif op==62: store(env[a[0]],env[a[1]])
                elif op in {65,66}:
                    base=env[a[2]]
                    if not isinstance(base,Pointer): raise ExecutionError('AccessChain base is not pointer')
                    env[a[1]]=Pointer(base.root,base.indices+tuple(env[i] for i in a[3:]))
                elif op==68:
                    base=env[a[2]]
                    if base!=Pointer(self.buffer) or a[3]!=0: raise ExecutionError('ArrayLength does not address storage runtime array')
                    env[a[1]]=len(data)
                elif op==81:
                    obj=env[a[2]]
                    for ix in a[3:]: obj=obj[ix]
                    env[a[1]]=obj
                elif op==80: env[a[1]]=tuple(env[i] for i in a[2:])
                elif op in {83,113,124}: env[a[1]]=env[a[2]]
                elif op in {128,130,132}:
                    lhs,rhs=env[a[2]],env[a[3]]
                    env[a[1]]=({128:lambda:lhs+rhs,130:lambda:lhs-rhs,132:lambda:lhs*rhs}[op]())&0xffffffff
                elif op in {164,165,166,167,170,171,172,174,176,178}:
                    lhs,rhs=env[a[2]],env[a[3]]
                    env[a[1]]={164:lambda:lhs==rhs,165:lambda:lhs!=rhs,166:lambda:bool(lhs or rhs),167:lambda:bool(lhs and rhs),170:lambda:lhs==rhs,171:lambda:lhs!=rhs,172:lambda:lhs>rhs,174:lambda:lhs>=rhs,176:lambda:lhs<rhs,178:lambda:lhs<=rhs}[op]()
                elif op==168: env[a[1]]=not env[a[2]]
                elif op==169: env[a[1]]=env[a[3] if env[a[2]] else a[4]]
                elif op==245:
                    incoming=[a[i] for i in range(2,len(a),2) if a[i+1]==previous]
                    if len(incoming)!=1: raise ExecutionError('Phi predecessor missing')
                    env[a[1]]=env[incoming[0]]
                elif op in {249,250}:
                    target=a[0] if op==249 else a[1] if env[a[0]] else a[2]
                    if target not in labels: raise ExecutionError('branch target absent')
                    previous=block; pc=labels[target]
                elif op==253: return None
                elif op==254: return env[a[0]]
                else: raise OracleLimit(f'execution of {NAMES.get(op,op)} unsupported')
            raise ExecutionError('function did not return')
        run_function(self.entry_fn)
        return accesses

    def check_vectors(self, addend):
        trials=0
        pattern=[0,0xffffffff,1,0x80000000,0x7fffffff,42,0xfffffffe]
        for length in (0,1,4,17):
            initial=[pattern[i%len(pattern)] for i in range(length)]
            for x in list(range(length+4))+[0xfffffffe,0xffffffff]:
                data=initial.copy(); accesses=self.execute(data,x)
                expected=initial.copy()
                if x<length: expected[x]=(expected[x]+addend)&0xffffffff
                if data!=expected: raise ExecutionError(f'wrong u32 result length={length} x={x}: {data} != {expected}')
                if x>=length and accesses: raise ExecutionError(f'out-of-range invocation touched memory: {accesses}')
                if x<length and not any(op=='write' and ix==x for op,ix in accesses): raise ExecutionError('in-range invocation made no store')
                trials+=1
            data=initial.copy()
            for x in range(length+7): self.execute(data,x)
            if data!=[(v+addend)&0xffffffff for v in initial]: raise ExecutionError('full dispatch result mismatch')
            trials+=1
        return trials
