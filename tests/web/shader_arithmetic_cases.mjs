import fs from 'node:fs/promises';
const cases=JSON.parse(await fs.readFile(new URL('./shader_arithmetic_cases.json',import.meta.url),'utf8'));
const f32='@intrinsic("type", "float", 32)';
export const shaderArithmeticCases=cases.map(record=>{
  const scalar=`@intrinsic("type", "${{sint32:'int',uint32:'uint',float32:'float'}[record.scalar]}", 32)`;
  const vector=`@intrinsic("vector", 2, ${scalar})`;
  const uniform=`@intrinsic("shader uniform", "data", 3, ${vector})`;
  const first=`@intrinsic("extract element", ${uniform}, 0)`,second=`@intrinsic("extract element", ${uniform}, 1)`;
  let operation;
  if(record.operation==='cast') operation=`@intrinsic("cast", ${first}, @intrinsic("type", "${record.conversion}", 32))`;
  else if(['negate','abs'].includes(record.operation)) operation=`@intrinsic("${record.operation}", ${first})`;
  else operation=`@intrinsic("${record.operation}", ${first}, ${second})`;
  let denominator=`@intrinsic("cast", ${Math.abs(record.divisor)}.0, ${f32})`;
  if(record.divisor<0) denominator=`@intrinsic("negate", ${denominator})`;
  const component=`@intrinsic("divide", @intrinsic("cast", ${operation}, ${f32}), ${denominator})`;
  const source=`@intrinsic("shader output", @intrinsic("construct", @intrinsic("vector", 4, ${f32}), ${component}, 0.0, 0.0, 1.0))\n`;
  return {...record,source,type:{scalar:record.scalar,rows:1,columns:2},samples:[...record.samples,0,0],red:0.25};
});
