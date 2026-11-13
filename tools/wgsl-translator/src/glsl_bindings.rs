use crate::glsl_samplers::BindingOrigins;
use naga::back::glsl::ReflectionInfo;
use std::fmt::Write;

fn quoted(value: &str) -> String {
    let mut result = String::from("\"");
    for character in value.chars() {
        match character {
            '"' => result.push_str("\\\""),
            '\\' => result.push_str("\\\\"),
            '\n' => result.push_str("\\n"),
            '\r' => result.push_str("\\r"),
            '\t' => result.push_str("\\t"),
            value if value < '\u{20}' => write!(result, "\\u{:04x}", value as u32).unwrap(),
            value => result.push(value),
        }
    }
    result.push('"');
    result
}

fn binding(value: &naga::ResourceBinding) -> String {
    format!(
        "{{\"group\":{},\"binding\":{}}}",
        value.group, value.binding
    )
}

pub(crate) fn texture_bindings(
    module: &naga::Module,
    reflection: &ReflectionInfo,
    origins: &BindingOrigins,
) -> Result<String, String> {
    let mut records = Vec::new();
    for (name, mapping) in &reflection.texture_mapping {
        let texture = module.global_variables[mapping.texture]
            .binding
            .as_ref()
            .ok_or("A sampled GLSL texture has no resource binding")?;
        let sampler = match mapping.sampler {
            Some(handle) => binding(
                module.global_variables[handle]
                    .binding
                    .as_ref()
                    .ok_or("A GLSL sampler has no resource binding")?,
            ),
            None => String::from("null"),
        };
        records.push((
            name,
            format!(
                "{{\"name\":{},\"texture\":{},\"sampler\":{}}}",
                quoted(name),
                binding(origins.get(texture).unwrap_or(texture)),
                sampler
            ),
        ));
    }
    records.sort_by(|left, right| left.0.cmp(right.0));
    Ok(format!(
        "[{}]",
        records
            .into_iter()
            .map(|(_, record)| record)
            .collect::<Vec<_>>()
            .join(",")
    ))
}

#[cfg(test)]
mod tests {
    use super::quoted;

    #[test]
    fn source_names_use_json_escapes() {
        assert_eq!(
            quoted("ink\"\\\n\r\t\u{1} 🌽"),
            "\"ink\\\"\\\\\\n\\r\\t\\u0001 🌽\""
        );
    }
}
