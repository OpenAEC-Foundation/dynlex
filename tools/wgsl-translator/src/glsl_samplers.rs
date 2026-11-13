// GLSL combines images and samplers into uniforms. Give each additional pairing
// a virtual image binding; reflection maps it back to the original GPU image.
use naga::ResourceBinding;
use std::collections::{HashMap, HashSet};

pub(crate) type BindingOrigins = HashMap<ResourceBinding, ResourceBinding>;

pub(crate) struct Lowered {
    pub bytes: Vec<u8>,
    pub origins: BindingOrigins,
}

fn id(bound: &mut u32) -> Result<u32, String> {
    let result = *bound;
    *bound = bound
        .checked_add(1)
        .ok_or("Shader identifier limit exceeded")?;
    Ok(result)
}

fn name_instruction(identifier: u32, name: &str) -> Vec<u32> {
    let mut bytes = name.as_bytes().to_vec();
    bytes.push(0);
    while bytes.len() % 4 != 0 {
        bytes.push(0);
    }
    let mut words = vec![(((bytes.len() / 4 + 2) as u32) << 16) | 5, identifier];
    words.extend(
        bytes
            .chunks_exact(4)
            .map(|chunk| u32::from_le_bytes(chunk.try_into().unwrap())),
    );
    words
}

// The caller first validates the original SPIR-V. DynLex resource operands are
// direct loads of statically bound globals, including samples inside helpers.
pub(crate) fn lower(bytes: &[u8]) -> Result<Option<Lowered>, String> {
    let words: Vec<u32> = bytes
        .chunks_exact(4)
        .map(|chunk| u32::from_le_bytes(chunk.try_into().unwrap()))
        .collect();
    let mut instructions = Vec::new();
    let mut position = 5;
    while position < words.len() {
        let count = (words[position] >> 16) as usize;
        assert!(
            count > 0 && position + count <= words.len(),
            "Validated SPIR-V lost its instruction boundaries"
        );
        instructions.push(position);
        position += count;
    }
    let mut groups = HashMap::new();
    let mut bindings = HashMap::new();
    let mut variables = HashMap::new();
    let mut loads = HashMap::new();
    for &position in &instructions {
        let count = (words[position] >> 16) as usize;
        let instruction = &words[position..position + count];
        match instruction[0] & 0xffff {
            71 if instruction.len() == 4 => match instruction[2] {
                33 => {
                    bindings.insert(instruction[1], instruction[3]);
                }
                34 => {
                    groups.insert(instruction[1], instruction[3]);
                }
                _ => {}
            },
            59 if instruction[3] == 0 => {
                variables.insert(instruction[2], instruction[1]);
            }
            61 => {
                loads.insert(instruction[2], instruction.to_vec());
            }
            _ => {}
        }
    }
    let binding = |variable: u32| -> Result<ResourceBinding, String> {
        Ok(ResourceBinding {
            group: *groups
                .get(&variable)
                .ok_or("A sampled resource has no descriptor set")?,
            binding: *bindings
                .get(&variable)
                .ok_or("A sampled resource has no binding")?,
        })
    };
    let mut used: HashSet<ResourceBinding> = bindings
        .keys()
        .map(|&variable| binding(variable))
        .collect::<Result<_, _>>()?;
    let mut first = HashMap::new();
    let mut aliases = HashMap::new();
    let mut origins = HashMap::new();
    let mut replacements = HashMap::new();
    let mut names = Vec::new();
    let mut decorations = Vec::new();
    let mut globals = Vec::new();
    let mut bound = words[3];
    for &position in &instructions {
        if words[position] & 0xffff != 86 {
            continue;
        }
        let image_load = loads
            .get(&words[position + 3])
            .ok_or("GLSL sampling requires a statically bound image")?;
        let sampler_load = loads
            .get(&words[position + 4])
            .ok_or("GLSL sampling requires a statically bound sampler")?;
        let image = image_load[3];
        let sampler = sampler_load[3];
        if *first.entry(image).or_insert(sampler) == sampler {
            continue;
        }
        let alias = if let Some(&alias) = aliases.get(&(image, sampler)) {
            alias
        } else {
            let original = binding(image)?;
            let mut alias_binding = ResourceBinding {
                group: original.group,
                binding: 0,
            };
            while !used.insert(alias_binding) {
                alias_binding.binding = alias_binding
                    .binding
                    .checked_add(1)
                    .ok_or("Shader binding limit exceeded")?;
            }
            let alias = id(&mut bound)?;
            let pointer = *variables
                .get(&image)
                .ok_or("A sampled image is not a global resource")?;
            globals.extend([4 << 16 | 59, pointer, alias, 0]);
            decorations.extend([
                4 << 16 | 71,
                alias,
                34,
                alias_binding.group,
                4 << 16 | 71,
                alias,
                33,
                alias_binding.binding,
            ]);
            names.extend(name_instruction(
                alias,
                &format!("dynlex_glsl_image_{image}_sampler_{sampler}"),
            ));
            origins.insert(alias_binding, original);
            aliases.insert((image, sampler), alias);
            alias
        };
        let mut load = image_load.clone();
        load[2] = id(&mut bound)?;
        load[3] = alias;
        replacements.insert(position, load);
    }
    if aliases.is_empty() {
        return Ok(None);
    }
    let mut output = words[..5].to_vec();
    output[3] = bound;
    let mut named = false;
    let mut decorated = false;
    let mut declared = false;
    for &position in &instructions {
        let count = (words[position] >> 16) as usize;
        let opcode = words[position] & 0xffff;
        if !named && (opcode == 71 || opcode == 72 || (19..=39).contains(&opcode)) {
            output.extend(&names);
            named = true;
        }
        if !decorated && (19..=39).contains(&opcode) {
            output.extend(&decorations);
            decorated = true;
        }
        if !declared && opcode == 54 {
            output.extend(&globals);
            declared = true;
        }
        if opcode == 15 {
            let count = u16::try_from(count + aliases.len())
                .map_err(|_| "Shader entry-point interface is too large")?;
            output.push((u32::from(count) << 16) | 15);
            output.extend(&words[position + 1..position + (words[position] >> 16) as usize]);
            let mut identifiers: Vec<_> = aliases.values().copied().collect();
            identifiers.sort();
            output.extend(identifiers);
        } else if let Some(load) = replacements.get(&position) {
            output.extend(load);
            output.extend(&words[position..position + 3]);
            output.push(load[2]);
            output.push(words[position + 4]);
        } else {
            output.extend(&words[position..position + count]);
        }
    }
    assert!(
        named && decorated && declared,
        "GLSL sampler lowering lost its declaration sections"
    );
    let bytes = output.into_iter().flat_map(u32::to_le_bytes).collect();
    Ok(Some(Lowered { bytes, origins }))
}
