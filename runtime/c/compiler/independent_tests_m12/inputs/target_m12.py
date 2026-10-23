"""Independent, fail-closed SPIR-V subset execution with sparse immutable values.

Reads only emitted SPIR-V words and caller byte bindings. No compiler IR, type
reflection, generated tags, or production layout routines enter this oracle.
Extended independently for M11 interleaved invocation/barrier/race modeling.
It is not a physical-device or validator qualification.
"""
from __future__ import annotations
from dataclasses import dataclass
from fractions import Fraction
import struct
from exact_numeric_m8 import signed, trunc_div, finite_value, round_bits, fp_operation
from interval_math_m9 import reference
from target_numeric import numeric_instruction

class Invalid(Exception): pass
class Unsupported(Exception): pass
class Unfinished(Exception): pass

@dataclass(frozen=True)
class Aggregate:
    typ: int
    children: tuple | None = None
    changes: tuple = ()

@dataclass(frozen=True)
class Pointer:
    root: int
    indices: tuple = ()

TYPE_OPS=set(range(19,34))
UNARY={109,110,111,112,113,114,115,124,126,127,156,157,158,159,160,168,200}
BINARY={142}|set(range(128,142))|set(range(161,168))|set(range(170,192))|set(range(194,200))
RESULT2={148,12,41,42,43,44,46,54,55,57,59,61,65,66,68,77,78,79,80,81,82,83,169,245}|UNARY|BINARY
NOEXEC={0,8,246,247}

def literal(words):
    b=struct.pack('<'+'I'*len(words),*words)
    if b'\0' not in b: raise Invalid('unterminated target string')
    return b[:b.index(0)].decode('utf8')

def float_order(bits):
    exp=bits&0x7f800000; frac=bits&0x7fffff
    if exp==0x7f800000:
        return None if frac else (0 if bits>>31 else 2,Fraction())
    return 1,finite_value(bits)

class Module:
    def __init__(self, words):
        if len(words)<5 or words[0]!=0x07230203 or words[4]!=0: raise Invalid('SPIR-V header')
        self.words=tuple(words);self.bound=words[3];self.ins=[];self.defs={};self.types={};self.constants={}
        self.decor={};self.member={};self.functions={};self.variables={};self.idtype={};self.entry=None;self.local=None
        p=5;fn=None
        while p<len(words):
            size,op=words[p]>>16,words[p]&65535
            if not size or p+size>len(words):raise Invalid('instruction framing')
            a=tuple(words[p+1:p+size]);row=(op,a,p);self.ins.append(row);p+=size
            rid=a[0] if op in TYPE_OPS|{11,248} else a[1] if op in RESULT2 else None
            if rid is not None:
                if not 0<rid<self.bound or rid in self.defs:raise Invalid('ID duplicate or bound')
                self.defs[rid]=row
                if op in RESULT2:self.idtype[rid]=a[0]
            if op in TYPE_OPS:self.types[a[0]]=(op,a[1:])
            elif op==71:self.decor.setdefault(a[0],{})[a[1]]=a[2:]
            elif op==72:self.member.setdefault((a[0],a[1]),{})[a[2]]=a[3:]
            elif op==15:
                if a[0]!=5 or self.entry is not None:raise Unsupported('single compute entry required')
                self.entry=a[1]
            elif op==16 and a[1]==17:self.local=a[2:5]
            if op==54:
                fn=a[1];self.functions[fn]=[]
            elif op==56:fn=None
            elif fn is not None:self.functions[fn].append(row)
            if op==59:self.variables[a[1]]=(a[0],a[2],a[3] if len(a)>3 else None)
        for op,a,_ in self.ins:
            if op==41:self.constants[a[1]]=True
            elif op==42:self.constants[a[1]]=False
            elif op==43:
                typ=self.types[a[0]]
                if typ not in {(21,(32,0)),(21,(32,1)),(22,(32,))}:raise Unsupported('non32 scalar constant')
                if len(a)!=3:raise Invalid('scalar constant words')
                self.constants[a[1]]=a[2]
            elif op==44:
                if any(x not in self.constants for x in a[2:]):raise Invalid('constant DAG order')
                self.constants[a[1]]=Aggregate(a[0],tuple(self.constants[x] for x in a[2:]))
                self.check(self.constants[a[1]],a[0])
            elif op==46:self.constants[a[1]]=self.zero(a[0])
        self.resources={};self.inputs={}
        for rid,(ptr,storage,_) in self.variables.items():
            if storage in {2,12}:
                d=self.decor.get(rid,{})
                if 33 not in d or 34 not in d:raise Invalid('missing resource binding')
                self.resources[rid]=(d[34][0],d[33][0])
            elif storage==1:
                d=self.decor.get(rid,{})
                if 11 not in d:raise Unsupported('nonbuiltin input')
                self.inputs[rid]=d[11][0]
            elif storage not in {4,7}:raise Unsupported('storage class '+str(storage))
        if self.entry is None:raise Invalid('no compute entry')

    def children(self, typ):
        op,a=self.types[typ]
        if op==23:return a[0],a[1]
        if op==28:return a[0],self.constants[a[1]]
        if op==29:return a[0],None
        if op==30:return a,len(a)
        raise Invalid('not composite')

    def childtype(self,typ,i):
        child,n=self.children(typ)
        if i<0 or (n is not None and i>=n):raise Invalid('composite index outside target type')
        return child[i] if isinstance(child,tuple) else child

    def zero(self,typ):
        op,_=self.types[typ]
        if op==20:return False
        if op in {21,22}:return 0
        if op in {23,28,30}:return Aggregate(typ)
        raise Invalid('zero of nonconstructible target type')

    def check(self,obj,typ):
        op,a=self.types[typ]
        if op in {23,28,30}:
            if not isinstance(obj,Aggregate) or obj.typ!=typ:raise Invalid('composite value target type mismatch')
            if obj.children is not None and len(obj.children)!=self.children(typ)[1]:raise Invalid('composite constituent count')
        elif op==20:
            if type(obj) is not bool:raise Invalid('boolean value type')
        elif op in {21,22}:
            if type(obj) is not int or not 0<=obj<=0xffffffff:raise Invalid('scalar value bits')
        else:raise Unsupported('value type '+str(op))

    def extract(self,obj,path):
        for i in path:
            if not isinstance(obj,Aggregate):raise Invalid('extract scalar')
            t=self.childtype(obj.typ,i)
            change=dict(obj.changes)
            obj=change[i] if i in change else obj.children[i] if obj.children is not None else self.zero(t)
        return obj

    def insert(self,obj,path,val):
        if not path:return val
        parents=[];cur=obj
        for i in path:
            parents.append((cur,i));cur=self.extract(cur,(i,))
        self.check(val,self.childtype(parents[-1][0].typ,parents[-1][1]))
        for base,i in reversed(parents):
            changes=dict(base.changes);changes[i]=val
            val=Aggregate(base.typ,base.children,tuple(sorted(changes.items())))
        return val

    def scalar(self,op,a):
        x=a[0];y=a[1] if len(a)>1 else 0
        if op in {83,113,114,115,124}:return x
        if op in {109,110}:
            if x&0x7f800000==0x7f800000:raise Invalid('unsafe nonfinite conversion before masking')
            f=finite_value(x);v=trunc_div(f.numerator,f.denominator)
            lo,hi=(0,0xffffffff) if op==109 else (-0x80000000,0x7fffffff)
            if not lo<=v<=hi:raise Invalid('unsafe out-of-range conversion before masking')
            return v&0xffffffff
        if op in {111,112}:return round_bits(signed(x) if op==111 else x)
        if op==126:return -x&0xffffffff
        if op==127:return x^0x80000000
        if op in {128,130,132}:return {128:lambda:x+y,130:lambda:x-y,132:lambda:x*y}[op]()&0xffffffff
        if op in {129,131,133,142}:return fp_operation({129:'+',131:'-',133:'*',142:'*'}[op],x,y)
        if op in {134,135,137,138,139}:
            if op in {135,138,139}:x,y=signed(x),signed(y)
            if not y or (op in {135,138,139} and x==-0x80000000 and y==-1):raise Invalid('unsafe integer divide/rem before masking')
            q=trunc_div(x,y)
            return (q if op in {134,135} else x%y if op==139 else x-q*y)&0xffffffff
        if op in {136,140}:return fp_operation('/' if op==136 else '%',x,y)
        if op==141:
            xx,yy=finite_value(x),finite_value(y)
            if not yy:raise Invalid('unsafe FMod')
            return round_bits(xx-(xx//yy)*yy)
        if op in {194,195,196}:
            if not 0<=y<32:raise Invalid('unsafe shift before masking')
            return (x>>y if op==194 else signed(x)>>y if op==195 else x<<y)&0xffffffff
        if op in {197,198,199,200}:return {197:lambda:x|y,198:lambda:x^y,199:lambda:x&y,200:lambda:~x}[op]()&0xffffffff
        if op in {164,165,166,167,168}:return {164:lambda:x==y,165:lambda:x!=y,166:lambda:x or y,167:lambda:x and y,168:lambda:not x}[op]()
        if op in {170,171,172,173,174,175,176,177,178,179}:
            if op in {173,175,177,179}:x,y=signed(x),signed(y)
            return {170:lambda:x==y,171:lambda:x!=y,172:lambda:x>y,173:lambda:x>y,174:lambda:x>=y,175:lambda:x>=y,176:lambda:x<y,177:lambda:x<y,178:lambda:x<=y,179:lambda:x<=y}[op]()
        if 156<=op<=163:
            xx,yy=float_order(x),float_order(y)
            return {156:lambda:xx is None,157:lambda:xx is not None and xx[0]!=1,158:lambda:xx is not None and xx[0]==1,159:lambda:0<x&0x7f800000<0x7f800000,160:lambda:bool(x>>31),161:lambda:xx is not None and yy is not None and xx!=yy,162:lambda:xx is not None and yy is not None,163:lambda:xx is None or yy is None}[op]()
        if 180<=op<=191:
            xx,yy=float_order(x),float_order(y);unordered=xx is None or yy is None
            answer=False if unordered else (xx==yy,xx!=yy,xx<yy,xx>yy,xx<=yy,xx>=yy)[(op-180)//2]
            return (unordered or answer) if op%2 else (not unordered and answer)
        raise Unsupported('numeric opcode '+str(op))

    def componentwise(self,typ,op,args):
        if self.types[typ][0]==23:
            n=self.types[typ][1][1]
            return Aggregate(typ,tuple(self.scalar(op,[self.extract(x,(i,)) if isinstance(x,Aggregate) else x for x in args]) for i in range(n)))
        return self.scalar(op,args)

    def invocation(self,buffers,builtins,shared,observer,identity,readonly=frozenset(),step_limit=500000000,pure_quantum=64):
        builtins={28:(0,0,0)} if builtins is None else builtins
        memory={};env=dict(self.constants);steps=0;opcounts={};calls=[];pure_steps=0
        for rid,(_,storage,_) in self.variables.items():
            if storage!=7:env[rid]=Pointer(rid)
        globals_=dict(env)
        def ptr_type(ptr):
            if not isinstance(ptr,Pointer):raise Invalid('not pointer')
            return self.types[self.variables[ptr.root][0]][1][1]
        def location(ptr):
            typ=ptr_type(ptr);offset=0;key=self.resources.get(ptr.root)
            for index in ptr.indices:
                op,a=self.types[typ]
                if type(index) is not int or index<0:raise Invalid('negative/noninteger access index')
                child,count=self.children(typ)
                if op==29:
                    if key is None:raise Invalid('runtime array outside resource')
                    stride=self.decor.get(typ,{}).get(6,(0,))[0]
                    if not stride:raise Invalid('missing array stride')
                    count=max(0,(len(buffers[key])-offset)//stride)
                if index>=count:raise Invalid('OOB pointer formed before guard')
                if op==30:
                    if key is not None and ((typ,index) not in self.member or 35 not in self.member[(typ,index)]):raise Invalid('missing member offset')
                    if key is not None:offset+=self.member[(typ,index)][35][0]
                    typ=child[index]
                else:
                    if key is not None:offset+=index*(4 if op==23 else self.decor.get(typ,{}).get(6,(0,))[0])
                    typ=child
            return key,offset,typ
        def read_obj(key,offset,typ):
            # Explicit postorder stack: nominal dependency depth is not a host
            # recursion limit and never expands beyond the supplied byte span.
            tasks=[('visit',offset,typ)];values=[]
            while tasks:
                action,off,t=tasks.pop()
                if action=='join':
                    count=off;children=tuple(values[-count:]);del values[-count:]
                    values.append(Aggregate(t,children));continue
                op,a=self.types[t]
                if op in {21,22}:
                    if off<0 or off+4>len(buffers[key]):raise Invalid('short buffer scalar load')
                    values.append(struct.unpack_from('<I',buffers[key],off)[0]);continue
                if op not in {23,28,30}:raise Invalid('nonfixed aggregate load')
                child,count=self.children(t)
                if count>len(buffers[key])//4+1:raise Invalid('aggregate load exceeds finite byte span')
                tasks.append(('join',count,t))
                for i in reversed(range(count)):
                    ct=child[i] if op==30 else child
                    delta=self.member[(t,i)][35][0] if op==30 else i*(4 if op==23 else self.decor[t][6][0])
                    tasks.append(('visit',off+delta,ct))
            if len(values)!=1:raise Invalid('aggregate assembly')
            return values[0]
        def load(ptr):
            key,offset,typ=location(ptr)
            if key is not None:return read_obj(key,offset,typ)
            if self.variables[ptr.root][1]==4:
                return self.extract(shared[ptr.root],ptr.indices)
            if ptr.root in self.inputs:
                raw=builtins[self.inputs[ptr.root]]
                obj=Aggregate(ptr_type(ptr),tuple(raw)) if isinstance(raw,tuple) else raw
            else:
                obj=memory[ptr.root]
                if obj is None:raise Invalid('uninitialized local load')
            return self.extract(obj,ptr.indices)
        def write_obj(key,offset,typ,value):
            tasks=[(offset,typ,value)]
            while tasks:
                off,t,v=tasks.pop();op,a=self.types[t];self.check(v,t)
                if op in {21,22}:
                    if off<0 or off+4>len(buffers[key]):raise Invalid('short buffer scalar store')
                    struct.pack_into('<I',buffers[key],off,v)
                else:
                    child,count=self.children(t)
                    if count>len(buffers[key])//4+1:raise Invalid('aggregate store exceeds byte span')
                    for i in reversed(range(count)):
                        ct=child[i] if op==30 else child
                        delta=self.member[(t,i)][35][0] if op==30 else i*(4 if op==23 else self.decor[t][6][0])
                        tasks.append((off+delta,ct,self.extract(v,(i,))))
        def store(ptr,value):
            key,offset,typ=location(ptr);self.check(value,typ)
            if key is not None:
                if key in readonly or self.variables[ptr.root][1]==2:raise Invalid('write to read-only resource')
                write_obj(key,offset,typ,value)
            elif ptr.root in self.inputs:raise Invalid('store to builtin')
            elif self.variables[ptr.root][1]==4:
                shared[ptr.root]=self.insert(shared[ptr.root],ptr.indices,value)
            elif not ptr.indices:memory[ptr.root]=value
            else:
                if memory[ptr.root] is None:raise Invalid('partial write to uninitialized local')
                memory[ptr.root]=self.insert(memory[ptr.root],ptr.indices,value)
        fn=self.entry;body=self.functions[fn];pc=0;arguments=();param=0;block=previous=None;stack=[];loops=[]
        loop_info={}
        for fid,rows in self.functions.items():
            header=None
            for opcode,args,pos in rows:
                if opcode==248:header=args[0]
                if opcode==246:loop_info[(fid,header)]=(args[0],args[1])
        def labels(body):return {a[0]:i for i,(op,a,_) in enumerate(body) if op==248}
        label=labels(body)
        while True:
            if pc>=len(body):raise Invalid('fell off target function')
            steps+=1
            if steps>step_limit:raise Unfinished('independent execution instruction limit')
            op,a,word_offset=body[pc];pc+=1;opcounts[op]=opcounts.get(op,0)+1;observable=False
            if op==55:
                if param>=len(arguments):raise Invalid('missing call parameter')
                env[a[1]]=arguments[param];self.check(env[a[1]],a[0]);param+=1
            elif op==57:
                actual=tuple(env[x] for x in a[3:]);sig=self.defs[a[2]][1];ft=self.types[sig[3]]
                if ft[0]!=33 or len(ft[1])-1!=len(actual) or sig[0]!=a[0]:raise Invalid('call signature')
                calls.append((fn,a[2],len(stack)+1))
                stack.append((fn,body,pc,arguments,param,block,previous,env,memory,label,a[1],loops,word_offset))
                fn=a[2];body=self.functions[fn];label=labels(body);pc=param=0;arguments=actual;block=previous=None;env=dict(globals_);memory={};loops=[]
            elif op==248:
                block=a[0];phis={}
                while pc<len(body) and body[pc][0]==245:
                    aa=body[pc][1];inc=[aa[i] for i in range(2,len(aa),2) if aa[i+1]==previous]
                    if len(inc)!=1:raise Invalid('phi predecessor')
                    phis[aa[1]]=env[inc[0]];pc+=1
                    steps+=1;opcounts[245]=opcounts.get(245,0)+1
                    if steps>step_limit:raise Unfinished('independent execution instruction limit')
                    pure_steps+=1
                    if pure_steps>=pure_quantum:
                        pure_steps=0
                        yield ('instruction',)
                env.update(phis)
            elif op in NOEXEC:pass
            elif op==59:
                if a[2]!=7:raise Invalid('function variable storage')
                env[a[1]]=Pointer(a[1]);memory[a[1]]=env[a[3]] if len(a)>3 else None
            elif op==61:
                ptr=env[a[2]]
                if self.variables[ptr.root][1] in {4,12}:
                    observer.access(identity,ptr,False)
                    env[a[1]]=load(ptr)
                    observable=True
                    yield ('memory',)
                else:env[a[1]]=load(ptr)
            elif op==62:
                ptr=env[a[0]]
                if self.variables[ptr.root][1] in {4,12}:
                    observer.access(identity,ptr,True)
                    store(ptr,env[a[1]])
                    observable=True
                    yield ('memory',)
                else:store(ptr,env[a[1]])
            elif op==224:
                scopes=tuple(env[x] for x in a)
                callers=tuple((f[0],f[12],tuple(f[11])) for f in stack)
                phase=(fn,word_offset,callers,tuple(loops))
                observable=True
                yield ('barrier',phase,scopes)
            elif op in {65,66}:
                ptr=env[a[2]]
                if not isinstance(ptr,Pointer):raise Invalid('access base not pointer')
                env[a[1]]=Pointer(ptr.root,ptr.indices+tuple(env[x] for x in a[3:]));location(env[a[1]])
            elif op==68:
                ptr=env[a[2]];key,off,typ=location(ptr)
                if key is None or self.types[typ][0]!=30:raise Invalid('arrayLength nonresource block')
                child=self.childtype(typ,a[3])
                if self.types[child][0]!=29:raise Invalid('arrayLength nonruntime member')
                off+=self.member[(typ,a[3])][35][0];stride=self.decor[child][6][0]
                env[a[1]]=max(0,(len(buffers[key])-off)//stride)
            elif op==80:
                env[a[1]]=Aggregate(a[0],tuple(env[x] for x in a[2:]));self.check(env[a[1]],a[0])
            elif op in {81,77}:
                path=a[3:] if op==81 else (env[a[3]],)
                env[a[1]]=self.extract(env[a[2]],path)
            elif op in {82,78}:
                path=a[4:] if op==82 else (env[a[4]],)
                base=env[a[3]] if op==82 else env[a[2]];val=env[a[2]] if op==82 else env[a[3]]
                env[a[1]]=self.insert(base,path,val)
            elif op==79:
                x,y=env[a[2]],env[a[3]];nx=self.children(x.typ)[1];ny=self.children(y.typ)[1]
                if any(i>=nx+ny for i in a[4:]):raise Invalid('undefined shuffle lane')
                env[a[1]]=Aggregate(a[0],tuple(self.extract(x,(i,)) if i<nx else self.extract(y,(i-nx,)) for i in a[4:]))
            elif op==83:env[a[1]]=env[a[2]]
            elif op==148:env[a[1]]=numeric_instruction(self,a[0],148,[env[x] for x in a[2:]])
            elif op in UNARY|BINARY:env[a[1]]=self.componentwise(a[0],op,[env[x] for x in a[2:]])
            elif op==169:
                cond=env[a[2]]
                if isinstance(cond,Aggregate):env[a[1]]=Aggregate(a[0],tuple(self.extract(env[a[3] if self.extract(cond,(i,)) else a[4]],(i,)) for i in range(self.children(cond.typ)[1])))
                else:env[a[1]]=env[a[3] if cond else a[4]]
            elif op==12:
                if literal(self.defs[a[2]][1][1:])!='GLSL.std.450':raise Unsupported('extended instruction set')
                k=a[3];vals=[env[x] for x in a[4:]]
                if k in {21,27,31,66,67,68,69}:env[a[1]]=numeric_instruction(self,a[0],k,vals)
                elif k in {37,38,39,40,41,42,43,44,45}:
                    conv=float_order if k in {37,40,43} else signed if k in {39,42,45} else int
                    x,y=vals[:2]
                    if k<=39:env[a[1]]=x if conv(x)<=conv(y) else y
                    elif k<=42:env[a[1]]=x if conv(x)>=conv(y) else y
                    else:
                        z=vals[2]
                        if conv(y)>conv(z):raise Invalid('reversed clamp before guard')
                        env[a[1]]=y if conv(x)<conv(y) else z if conv(x)>conv(z) else x
                elif k==50:env[a[1]]=round_bits(finite_value(vals[0])*finite_value(vals[1])+finite_value(vals[2]))
                else:raise Unsupported('extended instruction '+str(k))
            elif op in {249,250}:
                target=a[0] if op==249 else a[1] if env[a[0]] else a[2]
                if target not in label:raise Invalid('branch label')
                while loops and loop_info[(fn,loops[-1][0])][0]==target:
                    loops.pop()
                if (fn,target) in loop_info:
                    if loops and loops[-1][0]==target:
                        loops[-1]=(target,loops[-1][1]+1)
                    else:loops.append((target,0))
                previous=block;pc=label[target]
            elif op in {253,254}:
                result=None if op==253 else env[a[0]]
                if not stack:return dict(steps=steps,opcounts=opcounts,calls=calls)
                fn,body,pc,arguments,param,block,previous,env,memory,label,rid,loops,callsite=stack.pop()
                if result is not None:env[rid]=result
            elif op==255:raise Invalid('executed unreachable')
            else:raise Unsupported('target opcode '+str(op))
            pure_steps=0 if observable else pure_steps+1
            if not observable and pure_steps>=pure_quantum:
                pure_steps=0
                yield ('instruction',)
