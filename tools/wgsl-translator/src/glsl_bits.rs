// GLSL ES 3.00 has integer shifts but no bitfieldReverse. LLVM may recognize
// ordinary bit arithmetic as OpBitReverse; legalize that opcode for this target.
// Keep operations unsigned so signed inputs also use logical right shifts.
use std::collections::HashMap;

#[derive(Clone, Copy)]
struct Integer {
    lanes: u32,
    width: u32,
}

fn instruction(output: &mut Vec<u32>, opcode: u32, operands: &[u32]) {
    output.push(((operands.len() as u32 + 1) << 16) | opcode);
    output.extend_from_slice(operands);
}

struct Builder {
    bound: u32,
    definitions: Vec<u32>,
    unsigned: HashMap<u32, u32>,
    constants: HashMap<(u32, u32), u32>,
}

impl Builder {
    fn id(&mut self) -> Result<u32, String> {
        let result = self.bound;
        self.bound = result
            .checked_add(1)
            .ok_or("Shader identifier limit exceeded")?;
        Ok(result)
    }

    fn unsigned_type(&mut self, lanes: u32) -> Result<u32, String> {
        if let Some(&kind) = self.unsigned.get(&lanes) {
            return Ok(kind);
        }
        let scalar = if lanes == 1 {
            None
        } else {
            Some(self.unsigned_type(1)?)
        };
        let kind = self.id()?;
        match scalar {
            None => instruction(&mut self.definitions, 21, &[kind, 32, 0]), // OpTypeInt
            Some(scalar) => instruction(&mut self.definitions, 23, &[kind, scalar, lanes]),
        }
        self.unsigned.insert(lanes, kind);
        Ok(kind)
    }

    fn constant(&mut self, lanes: u32, value: u32) -> Result<u32, String> {
        if let Some(&constant) = self.constants.get(&(lanes, value)) {
            return Ok(constant);
        }
        let kind = self.unsigned_type(lanes)?;
        let scalar = if lanes == 1 {
            None
        } else {
            Some(self.constant(1, value)?)
        };
        let constant = self.id()?;
        match scalar {
            None => instruction(&mut self.definitions, 43, &[kind, constant, value]),
            Some(scalar) => {
                let mut operands = vec![kind, constant];
                operands.extend(std::iter::repeat_n(scalar, lanes as usize));
                instruction(&mut self.definitions, 44, &operands); // OpConstantComposite
            }
        }
        self.constants.insert((lanes, value), constant);
        Ok(constant)
    }

    fn binary(
        &mut self,
        output: &mut Vec<u32>,
        opcode: u32,
        kind: u32,
        left: u32,
        right: u32,
    ) -> Result<u32, String> {
        let result = self.id()?;
        instruction(output, opcode, &[kind, result, left, right]);
        Ok(result)
    }
}

// The caller validates the input before running target-specific legalization.
pub(crate) fn lower(bytes: &[u8]) -> Result<Option<Vec<u8>>, String> {
    let words: Vec<u32> = bytes
        .chunks_exact(4)
        .map(|chunk| u32::from_le_bytes(chunk.try_into().unwrap()))
        .collect();
    let mut positions = Vec::new();
    let mut position = 5;
    while position < words.len() {
        let count = (words[position] >> 16) as usize;
        assert!(
            count > 0 && position + count <= words.len(),
            "Validated SPIR-V lost its instruction boundaries"
        );
        positions.push(position);
        position += count;
    }
    if !positions
        .iter()
        .any(|&position| words[position] & 0xffff == 204)
    {
        return Ok(None);
    }
    let mut types = HashMap::new();
    let mut vectors = Vec::new();
    let mut builder = Builder {
        bound: words[3],
        definitions: Vec::new(),
        unsigned: HashMap::new(),
        constants: HashMap::new(),
    };
    for &position in &positions {
        match words[position] & 0xffff {
            21 => {
                let width = words[position + 2];
                types.insert(words[position + 1], Integer { lanes: 1, width });
                if width == 32 && words[position + 3] == 0 {
                    builder.unsigned.insert(1, words[position + 1]);
                }
            }
            23 => vectors.push((
                words[position + 1],
                words[position + 2],
                words[position + 3],
            )),
            _ => {}
        }
    }
    for (kind, scalar, lanes) in vectors {
        if let Some(&integer) = types.get(&scalar) {
            types.insert(
                kind,
                Integer {
                    lanes,
                    width: integer.width,
                },
            );
            if builder.unsigned.get(&1) == Some(&scalar) {
                builder.unsigned.insert(lanes, kind);
            }
        }
    }
    let mut output = words[..5].to_vec();
    let mut definitions_at = None;
    for position in positions {
        let count = (words[position] >> 16) as usize;
        let source = &words[position..position + count];
        if source[0] & 0xffff == 54 && definitions_at.is_none() {
            // OpFunction
            definitions_at = Some(output.len());
        }
        if source[0] & 0xffff != 204 {
            output.extend_from_slice(source);
            continue;
        }
        let shape = types
            .get(&source[1])
            .ok_or("Bit reversal has no integer type")?;
        if shape.width != 32 {
            return Err("WebGL bit reversal requires 32-bit integers".into());
        }
        let kind = builder.unsigned_type(shape.lanes)?;
        let mut bits = source[3];
        if source[1] != kind {
            bits = builder.id()?;
            instruction(&mut output, 124, &[kind, bits, source[3]]); // OpBitcast
        }
        for (shift, mask) in [
            (1, 0x55555555),
            (2, 0x33333333),
            (4, 0x0f0f0f0f),
            (8, 0x00ff00ff),
            (16, 0x0000ffff),
        ] {
            let shift = builder.constant(shape.lanes, shift)?;
            let mask = builder.constant(shape.lanes, mask)?;
            let right = builder.binary(&mut output, 194, kind, bits, shift)?; // logical shift
            let right = builder.binary(&mut output, 199, kind, right, mask)?;
            let left = builder.binary(&mut output, 199, kind, bits, mask)?;
            let left = builder.binary(&mut output, 196, kind, left, shift)?;
            bits = builder.binary(&mut output, 197, kind, left, right)?;
        }
        instruction(
            &mut output,
            if source[1] == kind { 83 } else { 124 },
            &[source[1], source[2], bits],
        );
    }
    let position = definitions_at.expect("Validated bit reversal has no function");
    output.splice(position..position, builder.definitions);
    output[3] = builder.bound;
    Ok(Some(
        output.into_iter().flat_map(u32::to_le_bytes).collect(),
    ))
}
