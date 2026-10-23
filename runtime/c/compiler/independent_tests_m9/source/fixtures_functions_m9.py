"""Independent M9 function sources and expected effects, before candidate use."""
from __future__ import annotations
from fixtures_scalar_m8 import make, S, U
from fixtures_m5 import storage, uniform, shape, packed
from fixtures_m6 import trial, program


def positives() -> list:
    cases=[]
    def add(name: str, functions: str, body: str, values: list[int], **kwargs) -> None:
        cases.append(make(name,body,values,prefix=functions,**kwargs))
    typed=[('u32','17u','x',17),('i32','-7i','bitcast<u32>(x)',U-6),
           ('f32','1.25f','bitcast<u32>(x)',0x3fa00000),('bool','true','u32(x)',1)]
    for typ,value,encode,bits in typed:
        add('identity_'+typ,f'fn f(x:{typ})->{typ}{{return x;}}',f'let x=f({value});out[0]={encode};',[bits])
        add('zero_formals_'+typ,f'fn f()->{typ}{{return {value};}}',f'let x=f();out[0]={encode};',[bits])
    add('no_result','fn f(){out[0]=17u;return;out[1]=99u;}','f();out[2]=23u;',[17,S,23])
    add('implicit_no_result','fn f(){out[0]=17u;}','f();out[1]=23u;',[17,23])
    add('discard_non_must_use','fn f()->u32{out[0]+=1u;return 7u;}','out[0]=3u;f();out[1]=8u;',[4,8])
    add('must_use_consumed','@must_use fn f()->u32{return 7u;}','let ignored=f();out[0]=ignored;',[7])
    add('trailing_commas','fn f(a:u32,b:i32,)->u32{return a+u32(b);}','out[0]=f(4u,5i,);',[9])
    add('formal_name_signature_scope','fn f(f32:u32,x:f32)->f32{return x;}','out[0]=bitcast<u32>(f(1u,2.5f));',[0x40200000])
    add('forward_chain','fn first(x:u32)->u32{return second(x)+1u;}fn second(x:u32)->u32{return last(x)*2u;}fn last(x:u32)->u32{return x+3u;}','out[0]=first(4u);',[15])
    add('forward_diamond','fn f(x:u32)->u32{return g(x)+h(x);}fn h(x:u32)->u32{return leaf(x)+2u;}fn g(x:u32)->u32{return leaf(x)+1u;}fn leaf(x:u32)->u32{return x*3u;}','out[0]=f(4u);',[27])
    cases.append(make('helpers_after_entry','out[0]=f(4u);',[9],suffix='fn f(x:u32)->u32{return x+5u;}'))
    add('unused_helper_emitted','fn unused(a:u32)->u32{return a*7u;}','out[0]=2u;',[2])
    add('same_formal_names','fn a(x:u32)->u32{return x+2u;}fn b(x:u32)->u32{return x+3u;}','out[0]=a(5u)+b(7u);',[17])
    add('nested_formal_shadow','fn f(x:u32)->u32{var y=x;{let x=9u;y+=x;}return y+x;}','out[0]=f(4u);',[17])
    add('formal_shadows_module_value','const x=8u;fn f(x:u32)->u32{return x;}','out[0]=f(3u)+x;',[11])
    add('builtin_name_is_user_function','fn max(a:u32,b:u32)->u32{return a-b;}','out[0]=max(9u,4u);',[5])
    add('local_initializer_resolves_outer','fn f(x:u32)->u32{{let x=x+2u;return x;} }','out[0]=f(7u);',[9])
    add('helper_uses_module_constant','fn f()->u32{return k;}const k=11u;','out[0]=f();',[11])
    add('calls_preserve_live_caller','fn g(x:u32)->u32{return x*3u;}fn f(x:u32)->u32{var keep=x+7u;let a=g(x);let b=g(x+1u);return keep+a+b;}','out[0]=f(5u);',[45])
    add('fresh_function_locals','fn f(x:u32)->u32{var y:u32;y+=x;return y;}','out[0]=f(4u);out[1]=f(9u);',[4,9])
    add('source_initialization_each_loop','fn f(x:u32)->u32{var total=0u;for(var i=0u;i<x;i++){var fresh=2u;fresh+=i;total+=fresh;}return total;}','out[0]=f(4u);out[1]=f(2u);',[14,5])
    add('argument_order_snapshots','fn inc()->u32{out[0]+=1u;return out[0];}fn combine(a:u32,b:u32,c:u32)->u32{return a*100u+b*10u+c;}','out[0]=0u;out[1]=combine(inc(),inc(),inc());out[2]=out[0];',[3,123,3])
    add('argument_resource_snapshot','fn effect()->u32{out[0]=9u;return 2u;}fn combine(a:u32,b:u32)->u32{return a*10u+b;}','out[0]=3u;out[1]=combine(out[0],effect());out[2]=out[0];',[9,32,9])
    add('nested_argument_segments','fn pair(a:u32,b:u32)->u32{return a*10u+b;}','out[0]=pair(pair(1u,2u),pair(3u,4u));',[154])
    add('post_call_resource_reload','fn write(){out[0]=9u;}','out[0]=3u;let old=out[0];write();out[1]=old+out[0];',[9,12])
    add('early_branch_returns','fn f(x:u32)->u32{if(x==0u){return 5u;}else{if(x==1u){return 7u;}return 9u;}}','out[0]=f(0u);out[1]=f(1u);out[2]=f(2u);',[5,7,9])
    add('return_from_loop','fn f(x:u32)->u32{var i=0u;loop{if(i==x){return i+3u;}i++;}}','out[0]=f(4u);out[1]=8u;',[7,8])
    add('while_then_return','fn f(x:u32)->u32{var i=x;while(i>0u){if(i==2u){return 17u;}i--;}return 9u;}','out[0]=f(3u);out[1]=f(1u);',[17,9])
    add('short_circuit_call_effects','fn yes()->bool{out[0]+=1u;return true;}','out[0]=0u;let a=false&&yes();let b=true||yes();let c=true&&yes();let d=false||yes();out[1]=u32(a)+u32(b)*2u+u32(c)*4u+u32(d)*8u;',[2,14])
    add('for_call_statements','fn start(){out[0]=0u;}fn next(){out[0]++;}','for(start();out[0]<3u;next()){out[1]=out[0];}',[3,2])
    add('return_abstract_materialization','fn f()->u32{return 2147483647+1;}','out[0]=f();',[0x80000000])
    add('arg_abstract_materialization','fn f(a:u32,b:f32,c:i32,d:bool)->u32{return a+u32(b)+u32(c)+u32(d);}','out[0]=f(5,1.5,2,true);',[9])
    for n in [17,33,129]:
        parameters=','.join(f'a{i}:u32' for i in range(n))
        arguments=','.join(f'{i+1}u' for i in range(n))
        # Flat loop-free body avoids making arity a recursion-depth test.
        functions=f'fn f({parameters})->u32{{return a0+a{n//2}+a{n-1};}}'
        add(f'wide_{n}',functions,f'out[0]=f({arguments});',[1+(n//2+1)+n])
        cases[-1][2]['job_only']=n>33
    for n in [33,257,1024]:
        functions=''.join(f'fn f{i}(x:u32)->u32{{return f{i+1}(x)+1u;}}' for i in range(n-1))+f'fn f{n-1}(x:u32)->u32{{return x;}}'
        add(f'iterative_function_chain_{n}',functions,'out[0]=f0(5u);',[n+4])
        cases[-1][2]['job_only']=True
    # Entry alias must be explicit, not hardcoded semantic ID1 after helpers.
    source,expected=program('let x=gid.x;out[x]=f(x);',bindings=[storage('out',0)],parameters=[(28,'gid','vec3u')])
    source=b'fn f(x:u32)->u32{return x+5u;}'+source
    expected['vectors']=[trial({(0,0):[S]*3},{(0,0):{i:i+5}},inputs={28:(i,0,0)}) for i in range(3)]
    cases.append(('helper_before_entry_alias',source,expected))
    # Helper-only uniform resource use and unused helper retain declarations.
    structs=[shape('Cfg',[('x',1),('y',1)])]
    bindings=[storage('out',0),uniform('cfg',1,0,2)]
    source,expected=program('out[0]=f();',bindings=bindings,structs=structs,parameters=[])
    source=b'fn f()->u32{return cfg.x+cfg.y;}fn unused()->u32{return cfg.y;}'+source
    expected['vectors']=[trial({(0,0):[S,S]},{(0,0):{0:11}},uniforms={(0,1):packed([4,7])})]
    cases.append(('helper_uniform_declaration_reflection',source,expected))
    for index in [0,2,3,U,0x80000000]:
        src=[7,11,19]
        want=src[index] if index<len(src) else 0
        vectors=[trial({(0,0):[S,S],(0,1):src},{(0,0):{0:want}})]
        add(f'helper_robust_{index}','fn f(i:u32)->u32{return src[i];}',f'out[0]=f({index}u);',[],vectors=vectors)
    return cases


def negatives() -> list:
    cases=[]
    base='@compute @workgroup_size(1) fn main(){}'
    resource='@group(0) @binding(0) var<storage,read_write> out:array<u32>;'
    def add(name: str, functions: str, body: str = '', whole: bool = False) -> None:
        source=functions if whole else resource+functions+'@compute @workgroup_size(1) fn main(){'+body+'}'
        cases.append((name,source.encode(),(2,3,4)))
    rows=[
        ('duplicate_function','fn f(){}fn f(){}',''),
        ('duplicate_formal','fn f(x:u32,x:u32){}',''),
        ('function_resource_duplicate','fn out(){}',''),
        ('function_const_duplicate','const f=1;fn f(){}',''),
        ('function_struct_duplicate','struct F{x:u32,}fn F(){}',''),
        ('helper_unknown_result','fn f()->missing{return 1u;}',''),
        ('helper_void_spelling','fn f()->void{}',''),
        ('helper_vector_result','fn f()->vec2<u32>{return vec2<u32>(1u);}',''),
        ('helper_vector_formal','fn f(x:vec2<u32>){}',''),
        ('helper_pointer_formal','fn f(x:ptr<function,u32>){}',''),
        ('helper_attribute','@workgroup_size(1) fn f(){}',''),
        ('formal_attribute','fn f(@builtin(local_invocation_index) x:u32){}',''),
        ('return_attribute','fn f()->@location(0) u32{return 1u;}',''),
        ('must_use_void','@must_use fn f(){}',''),
        ('must_use_entry','',''),
        ('nested_function','','fn f(){}'),
        ('prototype','fn f();',''),
        ('formal_assignment','fn f(x:u32){x=2u;}',''),
        ('formal_compound','fn f(x:u32){x+=2u;}',''),
        ('formal_increment','fn f(x:u32){x++;}',''),
        ('formal_decrement','fn f(x:u32){x--;}',''),
        ('root_redeclare_formal','fn f(x:u32){let x=2u;}',''),
        ('capture_entry_local','fn f()->u32{return hidden;}','let hidden=3u;out[0]=f();'),
        ('capture_other_formal','fn f(x:u32)->u32{return x;}fn g()->u32{return x;}',''),
        ('capture_other_local','fn f()->u32{let y=2u;return y;}fn g()->u32{return y;}',''),
        ('break_leak','fn f(){loop{break;}}fn g(){break;}',''),
        ('continue_leak','fn f(){loop{break;}}fn g(){continue;}',''),
        ('local_shadow_function','fn f()->u32{return 1u;}','let f=2u;out[0]=f();'),
        ('local_shadow_builtin','','let min=2u;out[0]=min(1u,2u);'),
        ('formal_shadow_function','fn f()->u32{return 1u;}fn g(f:u32)->u32{return f();}',''),
        ('module_shadow_type','fn u32(){}fn f(x:u32){}',''),
        ('wrong_arity_short','fn f(x:u32,y:u32){}','f(1u);'),
        ('wrong_arity_long','fn f(x:u32){}','f(1u,2u);'),
        ('wrong_concrete_arg','fn f(x:u32){}','f(1i);'),
        ('wrong_bool_arg','fn f(x:bool){}','f(1u);'),
        ('arg_no_expected_retype','fn f(x:u32){}','f(9223372036854775807+1);'),
        ('return_no_expected_retype','fn f()->u32{return 9223372036854775807+1;}',''),
        ('entry_call','','main();'),
        ('void_as_initializer','fn f(){}','let x=f();'),
        ('void_as_argument','fn f(){}fn g(x:u32){}','g(f());'),
        ('void_as_return','fn f(){}fn g()->u32{return f();}',''),
        ('function_value','fn f()->u32{return 1u;}','let x=f;'),
        ('scalar_expression_statement','','1u+2u;'),
        ('must_use_discard','@must_use fn f()->u32{return 1u;}','f();'),
        ('must_use_for_init','@must_use fn f()->u32{return 1u;}','for(f();false;){}'),
        ('must_use_for_update','@must_use fn f()->u32{return 1u;}','for(;false;f()){}'),
        ('user_call_module_const','fn f()->u32{return 1u;}const k=f();',''),
        ('user_call_local_const','fn f()->u32{return 1u;}','const k=f();'),
        ('user_call_attribute','fn f()->u32{return 1u;}',''),
        ('value_return_void','fn f(){return 1u;}',''),
        ('bare_return_scalar','fn f()->u32{return;}',''),
        ('missing_return','fn f()->u32{}',''),
        ('incomplete_return','fn f(x:bool)->u32{if(x){return 1u;}}',''),
        ('wrong_return_concrete','fn f()->u32{return 1i;}',''),
        ('wrong_return_bool','fn f()->bool{return 1u;}',''),
        ('dead_return_wrong','fn f()->u32{return 1u;return false;}',''),
        ('dead_return_void','fn f(){return;return 1u;}',''),
        ('dead_bad_name','fn f()->u32{return 1u;let x=missing;}',''),
        ('divergent_scalar','fn f()->u32{loop{}}',''),
        ('divergent_void','fn f(){loop{}}',''),
        ('continuing_return','fn f(){loop{continuing{return;}}}',''),
        ('continue_bypass_declaration','fn f(){loop{continue;let x=1u;continuing{let y=x;break if true;}}}',''),
        ('direct_recursion','fn f()->u32{return f();}','out[0]=f();'),
        ('unused_recursion','fn f()->u32{return f();}',''),
        ('mutual_recursion','fn f()->u32{return g();}fn g()->u32{return f();}',''),
        ('dead_recursion','fn f()->u32{return 1u;return f();}',''),
        ('short_circuit_recursion','fn f()->bool{return false&&f();}',''),
        ('unused_short_circuit_mutual','fn f()->bool{return true||g();}fn g()->bool{return f();}',''),
        ('const_call_cycle','const k=f();fn f()->u32{return k;}',''),
        ('dead_const_call','fn f()->u32{return 1u;}','return;const k=f();'),
    ]
    for name,functions,body in rows:
        if name=='must_use_entry':add(name,'@must_use '+base,whole=True)
        elif name=='user_call_attribute':add(name,functions+'@compute @workgroup_size(f()) fn main(){}',whole=True)
        else:add(name,functions,body)
    for op in ['min','max','clamp','exp','tanh','sqrt','u32','bitcast<u32>']:
        args='1u,2u,3u' if op=='clamp' else '1u,2u' if op in {'min','max'} else '1.0f' if op in {'exp','tanh','sqrt'} else '1u'
        add('discard_'+op.replace('<','_').replace('>',''),' ',op+'('+args+');')
    for n in [33,257,1024]:
        functions=''.join(f'fn f{i}()->u32{{return f{(i+1)%n}();}}' for i in range(n))
        add(f'long_unused_cycle_{n}',functions)
    return cases
