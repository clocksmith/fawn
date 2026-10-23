"""Post-observation, exact source-bound evaluator repairs, with original evidence kept."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
primary=json.loads((ROOT/'cases.json').read_text())['cases']
supplement=json.loads((ROOT/'NUMERIC_SUPPLEMENT.json').read_text())['cases']
corrections=[]
for cases,name,why in [(primary,'ai_explicit_f32_conversion','The scalar conversion constructor accepts concrete scalar input. AI to i32 has lower rank than AI to u32 or f32; materialization of 2147483649 to i32 fails before conversion. The accepted M11 scalar control has identical status4.'),(supplement,'latitude_sqrt_nonfinite','The literal bitcast calls are const expressions and create nonfinite f32 values before a runtime let binding. WGSL requires creation error; accepted M11 rejects the same scalar bitcast.')]:
 c=next(c for c in cases if c['id']==name);assert c['expected_status']==0
 corrections.append({'id':name,'source':c['source'],'source_sha256':sha(ROOT/c['source']),'original_expected_status':0,'corrected_expected_status':4,'reason':why})
controls=[]
def add(name,source,status=0,**extra):
 p=ROOT/'exact_correction_controls'/f'{name}.wgsl';p.parent.mkdir(exist_ok=True);p.write_text(source+'\n')
 controls.append(dict(id=name,source=str(p.relative_to(ROOT)),source_sha256=sha(p),expected_status=status,**extra))
base='@group(0) @binding(1) var<storage,read_write> output:array<u32>;\nconst result=dot(vec2(2147483648,1),vec2(1,1));@compute @workgroup_size(1) fn main()'
add('wider_dot_f32_via_explicit_u32',base+'{output[0]=bitcast<u32>(f32(u32(result)));}',expected_options=[[0x4f000000]],output_words=8)
add('wider_dot_f32_via_af',base+'{output[0]=bitcast<u32>(f32(result+0.0));}',expected_options=[[0x4f000000]],output_words=8)
add('wider_dot_f32_context',base+'{let value:f32=result;output[0]=bitcast<u32>(value);}',expected_options=[[0x4f000000]],output_words=8)
add('runtime_sqrt_nan_inf_storage','@group(0) @binding(0) var<storage,read> input:array<u32>;@group(0) @binding(1) var<storage,read_write> output:array<u32>;@compute @workgroup_size(1) fn main(){let result=sqrt(bitcast<vec2f>(vec2u(input[0],input[1])));output[0]=bitcast<u32>(result.x);output[1]=bitcast<u32>(result.y);}', input_words=[0x7fc12345,0x7f800000],output_words=8,qualification='validation-only; nonfinite runtime values have no finite accuracy claim')
record={'phase':'After initial candidate observation; exact evaluator repairs only','candidate_manifest_sha256':'0c01ff844b4ad98ef88439bf5d0cdf4ac9ad8cd6f22143de52fdf77aa9381a7a','original_primary_seal':sha(ROOT/'CASE_SEAL.sha256'),'original_numeric_seal':sha(ROOT/'NUMERIC_SUPPLEMENT.sha256'),'original_results':{str(p.relative_to(ROOT)):sha(p) for p in [ROOT/'candidate1-full/results.json',ROOT/'candidate1-numeric-supplement/results.json']},'normative_sources':['https://www.w3.org/TR/2026/CRD-WGSL-20261021/#conversion-rank','https://www.w3.org/TR/2026/CRD-WGSL-20261021/#scalar-conversion-builtin','https://www.w3.org/TR/2026/CRD-WGSL-20261021/#floating-point-evaluation'],'corrections':corrections,'additional_cases':controls,'restriction':'No original source, expectation manifest, seal or first result edited. No original negative relaxed. New positive controls are separate and not retroactively pre-candidate.'}
(ROOT/'EXACT_EVALUATOR_CORRECTIONS.json').write_text(json.dumps(record,indent=2,sort_keys=True)+'\n')
paths=[ROOT/'prepare_exact_corrections.py',ROOT/'run_exact_corrections.py',ROOT/'EXACT_EVALUATOR_CORRECTIONS.json']+[ROOT/c['source'] for c in controls]
(ROOT/'EXACT_CORRECTIONS.sha256').write_text(''.join(sha(p)+'  '+str(p.relative_to(ROOT))+'\n' for p in paths))
print('exact corrections seal',sha(ROOT/'EXACT_CORRECTIONS.sha256'))
