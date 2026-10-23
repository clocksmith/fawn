"""Independent scalar cases frozen from M8 contract before candidate inspection."""
from __future__ import annotations

from fractions import Fraction
from exact_numeric_m8 import finite_value, fp_operation, literal_bits, round_bits, signed, trunc_div
from fixtures_m5 import storage
from fixtures_m6 import program, trial

U = 0xffffffff
S = 0xa55a36c9
BINDINGS = [storage('out', 0), storage('src', 1, access=1)]
RELATIONS = {'==': lambda a,b:a==b, '!=':lambda a,b:a!=b,
             '<':lambda a,b:a<b, '<=':lambda a,b:a<=b,
             '>':lambda a,b:a>b, '>=':lambda a,b:a>=b}
NAMES = {'+':'add','-':'sub','*':'mul','/':'div','%':'rem','max':'max',
         '&':'and','|':'or','^':'xor','<<':'shl','>>':'shr',
         '==':'eq','!=':'ne','<':'lt','<=':'le','>':'gt','>=':'ge'}


def integer_result(op: str, left: int, right: int, is_signed: bool) -> int:
    a,b = (signed(left), signed(right)) if is_signed else (left,right)
    if op in RELATIONS:
        return int(RELATIONS[op](a,b))
    if op in {'/','%'}:
        if not b or (is_signed and a == -0x80000000 and b == -1):
            return (left if op == '/' else 0)
        quotient = trunc_div(a,b)
        return (quotient if op == '/' else a - quotient*b) & U
    values = {'+':lambda:a+b,'-':lambda:a-b,'*':lambda:a*b,
              'max':lambda:max(a,b),'&':lambda:a&b,'|':lambda:a|b,
              '^':lambda:a^b,'<<':lambda:a<<(right&31),
              '>>':lambda:a>>(right&31)}
    return values[op]() & U


def literal(value: int, typ: str) -> str:
    if typ == 'u32':
        return str(value & U) + 'u'
    value = signed(value)
    if value == -0x80000000:
        return '(-2147483647i - 1i)'
    return str(value) + 'i' if value >= 0 else '(' + str(value) + 'i)'


def make(name: str, body: str, outputs: list[int] | None = None,
         vectors: list | None = None, prefix: str = '', suffix: str = '',
         bindings: list | None = None, **kwargs) -> tuple:
    source, expected = program(body, bindings=BINDINGS if bindings is None else bindings,
                               parameters=[], **kwargs)
    if vectors is None:
        values = [] if outputs is None else outputs
        initial={(0,0):[S]*(len(values)+2)}
        if bindings is None:
            initial[(0,1)]=[0,1]
        vectors=[trial(initial,{(0,0):dict(enumerate(values))})]
    expected['vectors']=vectors
    return name, prefix.encode()+source+suffix.encode(), expected


def expression(name: str, expr: str, value: int, typ: str = 'u32',
               prefix: str = '', annotation: str = '') -> tuple:
    encode = 'u32(answer)' if typ in {'u32','bool'} else 'bitcast<u32>(answer)'
    return make(name, f'const answer{annotation} = {expr}; out[0] = {encode};',
                [value], prefix=prefix)


def positives() -> list:
    cases=[]
    pairs=[(0,1),(1,2),(7,3),(U-6,3),(7,U-2),(U-6,U-2),
           (0x7fffffff,1),(0x80000000,1),(0x80000000,U),
           (U,2),(U,U),(0,0),(0x80000000,0),(1,31),(0x12345678,0x87654321)]
    operations=list(NAMES)
    for typ in ['u32','i32']:
        is_signed=typ=='i32'
        for op in operations:
            relevant=(pairs+[(x,c) for x in [0,1,U,0x80000000,0x7fffffff]
                             for c in [0,1,31,32,33,63,64,U]]) if op in {'<<','>>'} else pairs
            vectors=[trial({(0,0):[S,S],(0,1):[a,b]},
                           {(0,0):{0:integer_result(op,a,b,is_signed)}}) for a,b in relevant]
            load='bitcast<i32>' if is_signed else 'u32'
            right='src[1]' if op in {'<<','>>'} else f'{load}(src[1])'
            text=f'max(a,b)' if op=='max' else f'a {op} b'
            encode=f'u32({text})' if op in RELATIONS else f'bitcast<u32>({text})'
            cases.append(make(f'runtime_{typ}_{NAMES[op]}',
                f'let a={load}(src[0]); let b={right}; out[0]={encode};',vectors=vectors))
            for index,(a,b) in enumerate(relevant[:15]):
                if op in {'/','%'} and (b==0 or (is_signed and a==0x80000000 and b==U)):
                    continue
                if op in {'<<','>>'}:
                    if b>=32:
                        continue
                    if op=='<<':
                        v=(signed(a) if is_signed else a)<<b
                        if not ((-0x80000000<=v<=0x7fffffff) if is_signed else 0<=v<=U):
                            continue
                left,right=literal(a,typ),literal(b,'u32' if op in {'<<','>>'} else typ)
                text=f'max({left},{right})' if op=='max' else f'{left} {op} {right}'
                cases.append(expression(f'const_{typ}_{NAMES[op]}_{index}',text,
                    integer_result(op,a,b,is_signed),'bool' if op in RELATIONS else typ))
        for op,label in [('~','not')]+([('-','negate')] if is_signed else []):
            vectors=[trial({(0,0):[S,S],(0,1):[v]}, {(0,0):{0:(~v if op=='~' else -v)&U}})
                     for v in [0,1,U,0x80000000,0x7fffffff]]
            cases.append(make(f'runtime_{typ}_{label}',
                f'let a={"bitcast<i32>" if is_signed else "u32"}(src[0]);out[0]=bitcast<u32>({op}a);',vectors=vectors))
    for op in ['&','|','&&','||','==','!=']:
        vectors=[]
        for a in [0,1]:
            for b in [0,1]:
                want={'&':a&b,'|':a|b,'&&':a&b,'||':a|b,'==':int(a==b),'!=':int(a!=b)}[op]
                v=trial({(0,0):[S,S],(0,1):[a,b]}, {(0,0):{0:want}})
                reads=[('read',(0,1),0)]
                if op not in {'&&','||'} or (op=='&&' and a) or (op=='||' and not a):
                    reads.append(('read',(0,1),1))
                v['accesses']=reads+[('write',(0,0),0)]
                vectors.append(v)
        cases.append(make('bool_'+{'&':'eager_and','|':'eager_or','&&':'short_and','||':'short_or','==':'eq','!=':'ne'}[op],
            f'out[0]=u32(bool(src[0]) {op} bool(src[1]));',vectors=vectors))
    cases.append(make('bool_not', 'out[0]=u32(!bool(src[0]));',vectors=[
        trial({(0,0):[S,S],(0,1):[v]}, {(0,0):{0:int(not v)}}) for v in [0,1,U]]))
    for expr,value in [('~0',U),('~(-1)',0),('(-7)/3',U-1),('7/(-3)',U-1),
                       ('(-7)%3',U),('7%(-3)',1),('max(-7,3)',3),
                       ('(9223372036854775807 >> 32u)',0x7fffffff),
                       ('((-9223372036854775807-1) >> 63u)',U),
                       ('((-9223372036854775807-1) >> 64u)',U),
                       ('(9223372036854775807 >> 64u)',0),
                       ('((-1) >> 4294967295u)',U),('0 << 4294967295u',0),
                       ('((-1) << 63u) >> 63u',U)]:
        # Materialize signed outputs before bitcast; AbstractInt->u32 is not a wrap.
        expr='('+expr+')'
        name='abstract_integer_'+str(len(cases))
        cases.append(expression(name,f'i32({expr})',value,'i32'))
    minimum='(-9223372036854775807 - 1)'
    cases.append(expression('abstract_min_negation_rule',f'(-{minimum}) == {minimum}',1,'bool'))
    cases.append(expression('abstract_const_preserved', 'u32(wide >> 32u)', 0x7fffffff,
                            prefix='const wide = 9223372036854775807;'))
    cases.append(expression('abstract_arithmetic_before_u32_context','(4294967295 + 1) - 1',U,annotation=':u32'))
    cases.append(expression('abstract_f32_annotation_ranking','4294967295',0x4f800000,'f32',annotation=':f32'))
    cases.append(expression('concrete_f32_constructor_ranking','f32(4294967295u)',0x4f800000,'f32'))
    cases.append(make('default_i32_materialization','let a=1;let b=-2;out[0]=u32(a+b);',[U]))
    cases.append(make('default_f32_materialization','let a=1.5;out[0]=bitcast<u32>(a);',[0x3fc00000]))
    cases += floating_cases()
    cases += conversion_cases()
    cases += literal_cases()
    cases += extreme_literal_cases()
    cases += scope_stage_cases()
    return cases


def floating_cases() -> list:
    cases=[]
    values=[('1.5','2.0'),('-7.5','2.0'),('7.5','-2.0'),('-7.5','-2.0'),
            ('16777216.0','1.0'),('0.1','0.2'),('0.0','1.0'),('-0.0','1.0')]
    for op in ['+','-','*','/','%','max']:
        vectors=[]
        for a,b in values:
            left,right=literal_bits(a),literal_bits(b)
            vectors.append(trial({(0,0):[S,S],(0,1):[left,right]},
                                 {(0,0):{0:fp_operation(op,left,right)}}))
        expr='max(a,b)' if op=='max' else f'a {op} b'
        cases.append(make('runtime_f32_'+NAMES[op],
             f'let a=bitcast<f32>(src[0]);let b=bitcast<f32>(src[1]);out[0]=bitcast<u32>({expr});',vectors=vectors))
        for i,(a,b) in enumerate(values):
            for stage,width in [('f32',32),('abstract',64)]:
                suffix='f' if stage=='f32' else ''
                left,right=literal_bits(a,width),literal_bits(b,width)
                result=fp_operation(op,left,right,width)
                want=result if width==32 else round_bits(finite_value(result,64),32,bool(result>>63))
                expr=f'max({a}{suffix},{b}{suffix})' if op=='max' else f'({a}{suffix}) {op} ({b}{suffix})'
                cases.append(expression(f'const_{stage}_{NAMES[op]}_{i}',expr,want,'f32',annotation=':f32'))
    cases.append(expression('abstract_not_premature_binary32','(16777216.0 + 1.0) - 16777216.0',0x3f800000,'f32',annotation=':f32'))
    cases.append(expression('concrete_each_operation_binary32','(16777216.0f + 1.0f) - 16777216.0f',0,'f32'))
    cases.append(expression('abstract_each_operation_binary64','(9007199254740992.0 + 1.0) - 9007199254740992.0',0,'f32',annotation=':f32'))
    cases.append(expression('abstract_binary64_next_even','(9007199254740992.0 + 3.0) - 9007199254740992.0',0x40800000,'f32',annotation=':f32'))
    cases.append(expression('no_constant_contraction','(0x1.000002p0f * 0x1.fffffcp-1f) - 1f',0,'f32'))
    cases.append(make('no_runtime_contraction','let a=bitcast<f32>(src[0]);let b=bitcast<f32>(src[1]);out[0]=bitcast<u32>((a*b)-1f);',
        vectors=[trial({(0,0):[S,S],(0,1):[0x3f800001,0x3f7ffffe]}, {(0,0):{0:0}})]))
    for op,relation in RELATIONS.items():
        vectors=[]
        for a,b in [(0,0x80000000),(0x3f800000,0x40000000),(0xbf800000,0),
                    (0x7fc00001,0),(0,0x7fc00001),(0x7f800000,0xff800000)]:
            import struct
            left,right=[struct.unpack('<f',struct.pack('<I',x))[0] for x in [a,b]]
            vectors.append(trial({(0,0):[S,S],(0,1):[a,b]}, {(0,0):{0:int(relation(left,right))}}))
        cases.append(make('runtime_f32_'+NAMES[op],f'out[0]=u32(bitcast<f32>(src[0]) {op} bitcast<f32>(src[1]));',vectors=vectors))
    cases.append(make('runtime_f32_negate_bits','out[0]=bitcast<u32>(-bitcast<f32>(src[0]));',vectors=[
        trial({(0,0):[S,S],(0,1):[v]}, {(0,0):{0:v^0x80000000}})
        for v in [0,0x80000000,0x3f800000,0xbf800000,1,0x00800000]]))
    return cases


def conversion_cases() -> list:
    cases=[]
    for typ in ['bool','u32','i32','f32']:
        cases.append(expression('zero_constructor_'+typ,typ+'()',0,typ))
        for source,number in [('true',1),('false',0)]:
            want=round_bits(number) if typ=='f32' else number
            cases.append(expression('bool_conversion_'+typ+'_'+source,typ+'('+source+')',want,typ))
    for dest in ['u32','i32','f32','bool']:
        for source in ['u32','i32','f32']:
            vectors=[]
            data=([0,0x80000000,0x3ff33333,0xbff33333,0x4f000000,0x4f7fffff,
                   0x4f800000,0xcf000000,0xcf000001,0x7f800000,0xff800000,
                   0x7fc00001,0x7fa00001,0xffc12345,1] if source=='f32'
                  else [0,1,U,0x80000000,0x7fffffff,16777217])
            for bits in data:
                if source=='f32':
                    if (bits&0x7f800000)==0x7f800000:
                        if bits&0x7fffff:
                            number=None
                        else:
                            number=Fraction(-(1<<200) if bits>>31 else 1<<200)
                    else:
                        number=finite_value(bits)
                else:
                    number=signed(bits) if source=='i32' else bits
                if dest=='bool':
                    want=int(number is None or number!=0)
                elif dest=='f32':
                    want=bits if source=='f32' else round_bits(number)
                elif source=='f32':
                    low,high=(0,4294967040) if dest=='u32' else (-2147483648,2147483520)
                    clamp=Fraction(0) if number is None else max(low,min(high,number))
                    want=trunc_div(clamp.numerator,clamp.denominator)&U
                else:
                    want=bits
                vectors.append(trial({(0,0):[S,S],(0,1):[bits]}, {(0,0):{0:want}}))
            source_expr='src[0]' if source=='u32' else f'bitcast<{source}>(src[0])'
            out_expr=f'u32({dest}({source_expr}))' if dest=='bool' else f'bitcast<u32>({dest}({source_expr}))'
            cases.append(make(f'convert_{source}_to_{dest}',f'out[0]={out_expr};',vectors=vectors))
    for source in ['u32','i32','f32']:
        for dest in ['u32','i32','f32']:
            values=[0,0x80000000,1,U,0x7f800000,0xff800000,0x7fc00001,0x7fa12345,0x3f800000]
            cases.append(make(f'bitcast_{source}_to_{dest}',
                f'let x=bitcast<{source}>(src[0]);out[0]=bitcast<u32>(bitcast<{dest}>(x));',
                vectors=[trial({(0,0):[S,S],(0,1):[v]}, {(0,0):{0:v}}) for v in values]))
    for expr,value,typ in [('u32(4294967296.0)',U,'u32'),('u32(4294967296.0f)',0xffffff00,'u32'),
            ('i32(2147483648.0)',0x7fffffff,'i32'),('i32(2147483648.0f)',0x7fffff80,'i32'),
            ('u32(-1.5)',0,'u32'),('i32(-1.5)',U,'i32'),('i32(-2147483649.0)',0x80000000,'i32'),
            ('bitcast<u32>(4294967295)',U,'u32'),('bitcast<i32>(-1)',U,'i32'),
            ('bitcast<f32>(1065353216)',0x3f800000,'f32'),('bitcast<u32>(1.0)',0x3f800000,'u32')]:
        cases.append(expression('const_conversion_'+str(len(cases)),expr,value,typ))
    cases.append(make('runtime_let_nonfinite_bitcast','let raw=0x7f800000u;let f=bitcast<f32>(raw);out[0]=bitcast<u32>(f);',[0x7f800000]))
    return cases


def literal_cases() -> list:
    cases=[]
    ints=[('0',0),('0i',0),('0u',0),('2147483647i',0x7fffffff),
          ('4294967295u',U),('0XfF',255),('0xabcdefu',0xabcdef),('0X7FFFFFFFi',0x7fffffff)]
    for i,(text,value) in enumerate(ints):
        cases.append(expression('literal_integer_'+str(i),'u32('+text+')',value))
    floats=['.5','1.','1e2','1E+2','1e-2','1f','0.0','-0.0',
            '0x1p0','0X1.Ap+2','0x.8p0','0x1.','0x1p-149','0x1p-150',
            '0x1.fffffep127','0x1.000001p0','0x1.000003p0',
            '1.000000059604644775390625','1.000000178813934326171875',
            '0.000000000000000000000000000000000000000000001401298464324817',
            '1.17549435082228750796873653722224568e-38',
            '1234567890123456789012345678901234567890e-30',
            '0x123456789abcdef0123456789p-80']
    for i,text in enumerate(floats):
        direct=literal_bits(text,32) if text.endswith('f') else round_bits(finite_value(literal_bits(text,64),64),32,text.startswith('-'))
        cases.append(expression('literal_float_'+str(i),text,direct,'f32',annotation=':f32'))
        if not text.endswith('f'):
            cases.append(expression('named_literal_float_'+str(i),'named',direct,'f32',
                annotation=':f32',prefix=f'const named={text};'))
    for i,text in enumerate(['0x1p-149f','0x1.fffffep127f','0x1.000002p0f','-0x0p0f',
                             '0x0.fffffep-126f','3.4028234663852886e38f','1.000000059604644775390625f']):
        cases.append(expression('literal_concrete_float_'+str(i),text,literal_bits(text),'f32'))
    return cases


def extreme_literal_cases() -> list:
    cases=[]
    texts=[('decimal_leading_zeros','0'*400+'1.0',0x3f800000),
           ('decimal_fraction_offset','0.'+'0'*500+'1e501',0x3f800000),
           ('hex_fraction_offset','0x0.'+'0'*300+'1p1204',0x3f800000),
           ('decimal_exponent_leading_zeros','1e+'+'0'*300+'1',0x41200000),
           ('hex_exponent_leading_zeros','0x1p+'+'0'*300+'1',0x40000000),
           ('integer_hex_leading_zeros','u32(0x'+'0'*500+'1)',1),
           ('zero_huge_positive_decimal','0e'+'9'*300,0),
           ('zero_huge_negative_decimal','0e-'+'9'*300,0),
           ('negative_zero_huge_decimal','-0e'+'9'*300,0x80000000),
           ('zero_huge_positive_hex','0x0p'+'9'*300,0),
           ('zero_huge_negative_hex','0x0p-'+'9'*300,0),
           ('underflow_huge_negative_decimal','1e-'+'9'*300,0),
           ('underflow_huge_negative_hex_abstract','0x1p-'+'9'*300,0),
           ('truncation_decimal_tail','1.000000059604644775399999999999999999999999f',0x3f800000),
           ('truncation_hex_tail','0x1.000000000000000ffffffffffffffffp0f',0x3f800000),
           ('subnormal_exact_max','0x0.fffffep-126f',0x007fffff),
           ('normal_exact_min','0x1p-126f',0x00800000),
           ('subnormal_rounds_above_half','0x1.000002p-150',1),
           ('subnormal_rounds_below_half','0x1.fffffcp-151',0),
           ('finite_just_below_overflow','0x1.fffffe8p127',0x7f7fffff)]
    for name,text,value in texts:
        typ='u32' if name=='integer_hex_leading_zeros' else 'f32'
        cases.append(expression(name,text,value,typ,annotation=':'+typ))
    cases.append(expression('binary64_largest_finite','large > 1e308',1,'bool',prefix='const large=0x1.fffffffffffffp1023;'))
    cases.append(expression('binary64_smallest_subnormal','small > 0.0',1,'bool',prefix='const small=0x1p-1074;'))
    cases.append(expression('binary64_half_subnormal_to_zero','small == 0.0',1,'bool',prefix='const small=0x1p-1075;'))
    return cases


def scope_stage_cases() -> list:
    cases=[]
    cases.append(expression('short_const_skips_numeric_error','false && (10i < i32(5*1000*1000*1000))',0,'bool'))
    cases.append(expression('short_const_or_skips_numeric_error','true || (10i < i32(5*1000*1000*1000))',1,'bool'))
    cases.append(make('nested_const_shadow_initializer','const x=3;{const x=x+4;out[0]=u32(x);}out[1]=u32(x);',[7,3]))
    cases.append(make('constructor_initializer_before_binding','let i32=i32(7u);out[0]=u32(i32);',[7]))
    cases.append(make('bitcast_initializer_before_binding','let bitcast=bitcast<u32>(1.0f);out[0]=bitcast;',[0x3f800000]))
    cases.append(make('const_builtin_initializer_before_binding','const max=max(2,3);out[0]=u32(max);',[3]))
    cases.append(make('signed_mutable_snapshot','var a:i32=-1;let old=a;a+=2;out[0]=u32(old);out[1]=u32(a);',[U,1]))
    body='var x:i32=-7;x/=3;out[0]=u32(x);x=-7;x%=3;out[1]=u32(x);x=15;x&=6;out[2]=u32(x);x|=8;out[3]=u32(x);x^=3;out[4]=u32(x);x<<=1u;out[5]=u32(x);x>>=2u;out[6]=u32(x);'
    cases.append(make('signed_compound_matrix',body,[U-1,U,6,14,13,26,6]))
    cases.append(make('signed_increment_wrap','var x:i32=2147483647;x++;out[0]=u32(x);x--;out[1]=u32(x);',[0x80000000,0x7fffffff]))
    for op in ['/', '%', '&', '|', '^', '<<', '>>']:
        old,right=13,2
        value=integer_result(op,old,right,False)
        vector=trial({(0,0):[2,right,old,S],(0,1):[]},{(0,0):{2:value}})
        vector['accesses']=[('read',(0,0),0),('read',(0,0),2),('read',(0,0),1),('write',(0,0),2)]
        cases.append(make('compound_target_once_'+NAMES[op],f'out[out[0]] {op}= out[1];',vectors=[vector]))
    return cases


def negatives() -> list:
    cases=[]
    def add(name: str, body: str, status: tuple=(4,), prefix: str='') -> None:
        source=program(body,bindings=BINDINGS,parameters=[])[0]
        cases.append((name,prefix.encode()+source,status))
    for typ in ['u32','i32']:
        for op in ['/','%']:
            add(f'constant_{typ}_{NAMES[op]}_zero',f'let a:{typ}=1;out[0]=u32(a {op} 0);')
            add(f'skipped_{typ}_{NAMES[op]}_zero',f'if false && ((1{typ[0]} {op} 0{typ[0]}) == 1{typ[0]}) {{return;}}')
        for op in ['<<','>>']:
            add(f'constant_count_{typ}_{NAMES[op]}',f'let a:{typ}=1;out[0]=u32(a {op} 32u);')
    for op in ['/','%']:
        add('constant_signed_min_'+NAMES[op],f'const a=(-2147483647i-1i) {op} -1i;')
        add('constant_abstract_min_'+NAMES[op],f'const a=(-9223372036854775807-1) {op} -1;')
    for name,expr in [
        ('abstract_add_overflow','9223372036854775807+1'),
        ('abstract_sub_overflow','(-9223372036854775807-1)-1'),
        ('abstract_mul_overflow','3037000500*3037000500'),
        ('abstract_shift_overflow','1 << 63u'),('abstract_shift_huge','1 << 4294967295u'),
        ('u32_shift_overflow','0x80000000u << 1u'),('i32_shift_overflow','0x40000000i << 1u'),
        ('i32_shift_negative_overflow','(-1073741825i) << 1u'),
        ('abstract_to_u32_negative','u32(-1)'),('abstract_to_i32_large','i32(2147483648)'),
        ('constructor_f32_rank','f32(4294967295)'),
        ('bitcast_abstract_u32_negative','bitcast<u32>(-1)'),
        ('bitcast_abstract_u32_large','bitcast<u32>(4294967296)'),
        ('bitcast_abstract_f32_rank','bitcast<f32>(4294967295)'),
        ('constant_infinite_bitcast','bitcast<f32>(0x7f800000u)'),
        ('constant_nan_bitcast','bitcast<f32>(0x7fc00001u)'),
        ('constant_infinite_bitcast_back','bitcast<u32>(bitcast<f32>(0x7f800000u))'),
        ('f32_overflow','3e38f*2f'),('abstract_float_overflow','1e308*10.0'),
        ('float_zero_divide','1f/0f'),('float_nonfinite_intermediate','(3e38f*2f)*0f'),
        ('float_materialization_overflow','f32(1e100)'),('short_true_numeric_error','true && (10i < i32(5*1000*1000*1000))'),
        ('bool_numeric','true+false'),('bool_order','true<false'),('bool_xor','true^false'),
        ('unsigned_negation','-1u'),('float_complement','~1f'),('float_bitand','1f&1f'),
        ('signed_shift_count','1u << 1i'),('negative_shift_count','1u << -1'),
        ('mixed_concrete_add','1i+1u'),('mixed_concrete_compare','1f==1u'),
        ('bitcast_bool_source','bitcast<u32>(true)'),('bitcast_bool_target','bitcast<bool>(1u)'),
        ('constructor_two_arguments','u32(1,2)'),('max_bool','max(true,false)'),
        ('max_mixed_types','max(1i,1u)')]:
        add(name, 'const answer='+expr+';')
    for name,body in [
        ('let_literal_nonfinite_initializer','let x=bitcast<f32>(0x7f800000u);'),
        ('abstract_default_overflow','let x=2147483648;'),
        ('local_const_from_runtime','let x=1u;const y=x;'),
        ('local_const_from_storage','const x=src[0];'),
        ('const_assignment','const x=1;x=2;'),('const_increment','const x=1;x++;'),
        ('implicit_bool_numeric','var x:u32=true;'),('implicit_numeric_bool','var x:bool=1u;'),
        ('const_self','const x=x;'),('const_missing','const x=missing;'),
        ('const_duplicate','const x=1;const x=2;'),
        ('const_sibling_leak','{const x=1;}out[0]=u32(x);'),
        ('short_missing','if false && missing {return;}'),
        ('short_bad_type','if false && 1u {return;}'),
        ('dead_bad_const','return;const x=missing;'),
        ('shadow_constructor','let u32=1u;out[0]=u32(1i);'),
        ('shadow_bitcast','let bitcast=1u;out[0]=bitcast<u32>(1f);'),
        ('shadow_type','let i32=1u;var x:i32=1;'),
        ('shadow_max','const max=1;out[0]=u32(max(1,2));'),
        ('negative_constant_index','out[-1]=1u;'),
        ('float_index','out[1f]=1u;'),('bool_index','out[true]=1u;')]:
        add(name,body)
    for i,text in enumerate(['00','01','09','0x','0xG','0x1g','1e','1e+','1e-','1uf','1fu','1ii','1uu',
                             '1_0','0b10','1.0ff','1.0i','1foo','0x1.2p','0x1p+',
                             '4294967296u','2147483648i','9223372036854775808',
                             '0x100000000u','0x80000000i','0x8000000000000000',
                             '1e999f','0x1.000001p0f','0x1p-150f']):
        add('malformed_or_out_of_range_literal_'+str(i),'let x='+text+';',status=(2,3,4))
    for i,expr in enumerate(['1u & 2u | 3u','1u ^ 2u & 3u','1u + 2u & 3u',
                              '1u << 1u << 1u','1u + 2u << 1u','1u << 1u + 1u',
                              'true && false || true','1u < 2u < 3u','1u == 2u == false']):
        add('grammar_forbids_c_precedence_'+str(i),'let x='+expr+';',status=(2,))
    for i,text in enumerate(['1e'+'9'*300,'0x1p'+'9'*300,'0x1.ffffffp127f',
                               '0x1p128f','0x1.000001p-149f','0x1p-150f','0x1p1024']):
        add('extreme_literal_reject_'+str(i),'const x='+text+';',status=(4,))
    for i,text in enumerate(['1h','1.0h','bitcast<f16>(1u)','vec2u(1u)','sqrt(1f)','exp(1f)','clamp(1f,0f,2f)']):
        add('excluded_numeric_feature_'+str(i),'let x='+text+';',status=(2,3,4))
    return cases
