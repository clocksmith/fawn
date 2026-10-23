"""Independent exact and interval builtin cases frozen before candidate use."""
from __future__ import annotations
from fractions import Fraction
import struct
from exact_numeric_m8 import finite_value, literal_bits, round_bits, fp_operation, signed
from interval_math_m9 import reference, parts
from fixtures_scalar_m8 import make, expression, literal, S, U
from fixtures_m6 import trial


def float_literal(bits: int, width: int = 32) -> str:
    value=struct.unpack('<f' if width==32 else '<d',struct.pack('<I' if width==32 else '<Q',bits))[0]
    text=value.hex()
    return text+('f' if width==32 else '')


def selected(op: str, a: int, b: int, c: int, typ: str) -> int:
    decode=finite_value if typ=='f32' else signed if typ=='i32' else int
    def low(x: int,y: int) -> int:return x if decode(x)<decode(y) else y
    def high(x: int,y: int) -> int:return x if decode(x)>decode(y) else y
    return low(a,b) if op=='min' else high(a,b) if op=='max' else low(high(a,b),c)


def positives() -> list:
    cases=[]
    integers=[(0,1,2),(7,3,9),(U,0,0x80000000),(0x80000000,0x7fffffff,U),(1,9,3),(0,U,1),(7,7,7)]
    floats=[(0,0x80000000,0),(0x80000000,0,0x80000000),(0x3f800000,0x3f800000,0x3f800000),
            (0xbf800000,0,0x3f800000),(0x40000000,0x40400000,0x3f800000),(1,0,0x00800000),
            (0x7f7fffff,0xff7fffff,0x7f7fffff),(0x80000001,0x80000000,1)]
    for typ in ['u32','i32','f32']:
        pairs=floats if typ=='f32' else integers
        encode='bitcast<u32>' if typ!='u32' else 'u32'
        decode='bitcast<'+typ+'>' if typ!='u32' else 'u32'
        lit=(lambda value:float_literal(value)) if typ=='f32' else lambda value:literal(value,typ)
        for op in ['min','max','clamp']:
            vectors=[]
            for i,(a,b,c) in enumerate(pairs):
                expected=selected(op,a,b,c,typ)
                vectors.append(trial({(0,0):[S,S],(0,1):[a,b,c]},{(0,0):{0:expected}}))
                order=(finite_value if typ=='f32' else signed if typ=='i32' else int)
                if op=='clamp' and order(b)>order(c):continue
                args=','.join(lit(v) for v in ([a,b,c] if op=='clamp' else [a,b]))
                cases.append(make(f'constant_{typ}_{op}_{i}',f'const v={op}({args});out[0]={encode}(v);',[expected]))
            args=','.join(f'{decode}(src[{i}])' for i in range(3 if op=='clamp' else 2))
            cases.append(make(f'runtime_{typ}_{op}',f'out[0]={encode}({op}({args}));',vectors=vectors))
    abstract=[('min', '3,5',3),('max','3,5',5),('clamp','9,3,5',5),
              ('min','1,2.5',0x3f800000),('max','1,2.5',0x40200000),('clamp','3.5,1,2.5',0x40200000)]
    for i,(op,args,want) in enumerate(abstract):
        convert='bitcast<u32>(f32(v))' if '.' in args else 'u32(v)'
        cases.append(make(f'abstract_rank_{i}',f'const v={op}({args});out[0]={convert};',[want]))
    for typ in ['u32','i32','f32']:
        suffix={'u32':'u','i32':'i','f32':'f'}[typ]
        convert='u32' if typ=='u32' else 'bitcast<u32>'
        cases.append(make(f'abstract_to_concrete_rank_{typ}',f'out[0]={convert}(min(1,2{suffix}));',[1 if typ!='f32' else 0x3f800000]))
    for op in ['exp','tanh','sqrt']:
        values=[0,0x80000000,1,0x80000001,0x007fffff,0x00800000,0x3dcccccd,0x3e800000,
                0x3f000000,0x3f800000,0xbf800000,0x40000000,0x40800000,0x41200000]
        if op=='exp':values += [literal_bits('80f'),literal_bits('-80f'),literal_bits('88f'),literal_bits('-103f'),literal_bits('-104f')]
        if op=='tanh':values += [literal_bits('20f'),literal_bits('-20f'),literal_bits('80f')]
        if op=='sqrt':values += [0x7f7fffff,0x00800001,0x3f800001]
        values=[v for v in values if op!='sqrt' or finite_value(v)>=0]
        vectors=[]
        for i,bits in enumerate(values):
            ref=reference(op,bits)
            case=make(f'constant_{op}_f32_{i}',f'const v={op}({float_literal(bits)});out[0]=bitcast<u32>(v);',[ref['nearest_bits']])
            case[2]['vectors'][0]['allowed']={(0,0):{0:ref['allowed_bits']}}
            case[2]['numeric_reference']=ref
            cases.append(case)
            vectors.append(trial({(0,0):[S,S],(0,1):[bits]},{(0,0):{0:ref['nearest_bits']}}))
        cases.append(make('runtime_'+op+'_f32',f'out[0]=bitcast<u32>({op}(bitcast<f32>(src[0])));',vectors=vectors))
        # Force binary64 quality observable before f32 conversion, rather than
        # silently accepting a low-precision answer hidden by a final f32 cast.
        abstract_values=['0.0','-0.0','0.1','0.25','1.0','2.0','4.0','10.0']
        if op=='exp':abstract_values+=['-1.0','-700.0','709.0','-745.0']
        if op=='tanh':abstract_values+=['-1.0','-20.0','20.0']
        for i,text in enumerate(abstract_values):
            bits=literal_bits(text,64);ref=reference(op,bits,64);center=ref['nearest_bits']
            if not ref['allowed_bits']:raise ValueError('unexpected abstract nonfinite fixture')
            if finite_value(center,64)==0:
                source_expr=f'f32({op}({text}))'
                expected_bits=[round_bits(finite_value(v,64),32,bool(v>>63)) for v in ref['allowed_bits']]
            else:
                exponent=((center>>52)&2047)-1023
                scale=max(-1000,min(1000,52-exponent))
                scale_bits=round_bits(Fraction(1<<scale) if scale>=0 else Fraction(1,1<<-scale),64)
                source_expr=f'f32(({op}({text}) - {float_literal(center,64)}) * {float_literal(scale_bits,64)})'
                expected_bits=[round_bits(finite_value(fp_operation('*',fp_operation('-',v,center,64),scale_bits,64),64)) for v in ref['allowed_bits']]
            case=make(f'constant_{op}_abstract_quality_{i}',f'const v={source_expr};out[0]=bitcast<u32>(v);',[expected_bits[len(expected_bits)//2]])
            case[2]['vectors'][0]['allowed']={(0,0):{0:sorted(set(expected_bits))}}
            case[2]['numeric_reference']=ref
            cases.append(case)
    # Literal let bounds are runtime stage and reversed composition is valid.
    for typ,suffix,bits in [('u32','u',3),('i32','i',3),('f32','f',0x40400000)]:
        case=make('let_reversed_bounds_'+typ,f'let low=9{suffix};let high=3{suffix};out[0]=bitcast<u32>(clamp(5{suffix},low,high));',[bits])
        cases.append(case)
    for op,expr in [('sqrt','sqrt(-1f)'),('exp','exp(1000f)')]:
        cases.append(make('skipped_numeric_'+op,f'const x=false&&({expr}>0f);out[0]=u32(x);',[0]))
    cases.append(make('user_call_arguments_math','out[0]=bitcast<u32>(f(exp(0f),sqrt(4f)));',[0x40400000],prefix='fn f(a:f32,b:f32)->f32{return a+b;}'))
    cases.append(make('builtin_shadow_const_stage','out[0]=min(9u,4u);',[5],prefix='fn min(a:u32,b:u32)->u32{return a-b;}'))
    return cases


def negatives() -> list:
    cases=[]
    def add(name: str,body: str,prefix: str='') -> None:
        source=make(name,body,prefix=prefix)[1]
        cases.append((name,source,(2,3,4)))
    for op,arity in [('min',2),('max',2),('clamp',3),('exp',1),('tanh',1),('sqrt',1)]:
        for count in sorted({0,arity-1,arity+1,arity+2}):
            add(f'bad_arity_{op}_{count}','let x='+op+'('+','.join(['1f']*count)+');')
        add('bool_'+op,'let x='+op+'('+','.join(['true']*arity)+');')
        if arity>1:
            add('mixed_concrete_'+op,'let x='+op+'('+','.join(['1i']+['2u']*(arity-1))+');')
            add('mixed_float_integer_'+op,'let x='+op+'('+','.join(['1f']+['2u']*(arity-1))+');')
        else:
            for typ in ['u','i']:add('concrete_integer_'+op+'_'+typ,'let x='+op+'(1'+typ+');')
    for typ in ['u','i','f']:
        add('reversed_const_'+typ,f'let x=clamp(5{typ},9{typ},3{typ});')
        add('runtime_value_const_bounds_'+typ,f'let v=bitcast<{dict(u="u32",i="i32",f="f32")[typ]}>(src[0]);let x=clamp(v,9{typ},3{typ});')
        add('skipped_const_bounds_'+typ,f'const x=false&&(clamp(5{typ},9{typ},3{typ})==0{typ});')
        add('dead_const_bounds_'+typ,f'return;let x=clamp(5{typ},9{typ},3{typ});')
    for name,expr in [('sqrt_negative_f32','sqrt(-1f)'),('sqrt_negative_abstract','sqrt(-1.0)'),
                      ('exp_overflow_f32','exp(1000f)'),('exp_overflow_abstract','exp(1000.0)'),
                      ('exp_nonfinite_intermediate','exp(1e38f*10f)'),('sqrt_nonfinite_intermediate','sqrt(1e308*10.0)'),
                      ('exp_result_width_before_arithmetic','exp(100f)*0f')]:
        add(name,'const x='+expr+';')
        add('evaluated_'+name,'const x=true&&('+expr+'>0f);' if 'abstract' not in name else 'const x=true&&('+expr+'>0.0);')
    add('const_builtin_shadow_user_call','const x=min(1u,2u);','fn min(a:u32,b:u32)->u32{return a+b;}')
    add('float_to_unsigned_no_implicit','let x=min(1.5,2u);')
    add('type_shadow_builtin','let sqrt=1u;let x=sqrt(4f);')
    return cases
