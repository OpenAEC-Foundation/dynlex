use super::*;
use std::{
    fs,
    path::{Path, PathBuf},
    process::Command,
    time::{SystemTime, UNIX_EPOCH},
};

struct Workspace(PathBuf);

impl Workspace {
    fn new() -> Self {
        let unique = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let path = std::env::temp_dir().join(format!(
            "dynlex-glsl-samplers-{}-{unique}",
            std::process::id()
        ));
        fs::create_dir(&path).unwrap();
        Self(path)
    }
}

impl Drop for Workspace {
    fn drop(&mut self) {
        fs::remove_dir_all(&self.0).unwrap();
    }
}

fn run(command: &mut Command) {
    let output = command.output().unwrap();
    assert!(
        output.status.success(),
        "{command:?}\n{}\n{}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
}

fn compile(source: &Path, output: &Path, stage: &str, level: &str) -> Vec<u8> {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap()
        .parent()
        .unwrap();
    let compiler = std::env::var_os("DYNLEX_COMPILER")
        .map(PathBuf::from)
        .unwrap_or_else(|| root.join("build/dynlex"));
    run(Command::new(compiler)
        .arg(source)
        .arg("--emit-spirv")
        .arg(format!("--shader-stage={stage}"))
        .arg(level)
        .arg("-o")
        .arg(output)
        .current_dir(root));
    run(Command::new("spirv-val")
        .args(["--target-env", "vulkan1.3"])
        .arg(output));
    fs::read(output).unwrap()
}

#[test]
fn independent_sampler_pairs_validate_and_keep_one_image_binding() {
    let work = Workspace::new();
    let root = Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap()
        .parent()
        .unwrap();
    let source =
        fs::read_to_string(root.join("tests/runtime/graphics_shaders/multiple_samplers.dl"))
            .unwrap();
    // Exercise sampling in entry blocks and in separate fixed-resource helpers.
    let helper = "function:\n    patterns:\n        sample {@intrinsic(\"fix\", @intrinsic(\"type\", \"shader image\")):image} with {@intrinsic(\"fix\", @intrinsic(\"type\", \"shader sampler\")):filtering} at {uv}\n    execute:\n        @intrinsic(\"return\", @intrinsic(\"shader sample level\", image, filtering, uv, 0.0))\n\n";
    let helpers = format!(
        "{helper}{}",
        source
            .replace(
                "@intrinsic(\"shader sample level\", paint, pixel, uv, 0.0)",
                "sample paint with pixel at uv"
            )
            .replace(
                "@intrinsic(\"shader sample level\", paint, smooth, uv, 0.0)",
                "sample paint with smooth at uv"
            )
    );
    for (label, text) in [("direct", source), ("helpers", helpers)] {
        let input = work.0.join(format!("{label}.dl"));
        fs::write(&input, text).unwrap();
        for (stage, kind) in [
            ("vertex", ShaderStage::Vertex),
            ("fragment", ShaderStage::Fragment),
        ] {
            for level in ["-O0", "-O2"] {
                let binary = work.0.join(format!("{label}-{stage}-{level}.spv"));
                let bytes = compile(&input, &binary, stage, level);
                let (module, info) = parse_and_validate(&bytes).unwrap();
                assert!(
                    write_glsl(&module, &info, kind, &glsl_samplers::BindingOrigins::new())
                        .err()
                        .unwrap()
                        .contains("multiple samplers")
                );
                let lowered = glsl_samplers::lower(&bytes).unwrap().unwrap();
                assert_eq!(lowered.origins.len(), 1);
                assert_eq!(lowered.origins.values().next().unwrap().binding, 0);
                let lowered_path = binary.with_extension("glsl.spv");
                fs::write(&lowered_path, &lowered.bytes).unwrap();
                run(Command::new("spirv-val")
                    .args(["--target-env", "vulkan1.3"])
                    .arg(&lowered_path));
                let glsl = translate_glsl(&bytes, kind).unwrap();
                for binding in [1, 2] {
                    assert!(glsl.bindings.contains(&format!("\"texture\":{{\"group\":0,\"binding\":0}},\"sampler\":{{\"group\":0,\"binding\":{binding}}}")));
                }
                assert_eq!(glsl.bindings.matches("\"name\":").count(), 2);
                assert_eq!(glsl.source.matches("uniform highp sampler2D ").count(), 2);
                let wgsl = translate(&bytes).unwrap();
                assert_eq!(
                    wgsl.matches("texture_2d<f32>").count(),
                    1,
                    "WebGPU must keep the original image"
                );
            }
        }
    }
}

#[test]
fn every_translation_clears_other_results_and_resource_mappings() {
    let work = Workspace::new();
    let root = Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap()
        .parent()
        .unwrap();
    let bytes = compile(
        &root.join("tests/runtime/graphics_shaders/multiple_samplers.dl"),
        &work.0.join("samples.spv"),
        "fragment",
        "-O2",
    );
    unsafe {
        assert_eq!(dynlex_shader_translate(bytes.as_ptr(), bytes.len(), 1), 1);
        assert!(dynlex_glsl_result_length() > 0);
        assert!(dynlex_glsl_bindings_length() > 2);
        assert!(dynlex_glsl_uniforms_length() > 2);
        assert_eq!(dynlex_wgsl_translate(bytes.as_ptr(), bytes.len()), 1);
        assert_eq!(dynlex_glsl_result_length(), 0);
        GLSL_BINDINGS.with(|value| assert_eq!(*value.borrow(), b"[]"));
        GLSL_UNIFORMS.with(|value| assert_eq!(*value.borrow(), b"[]"));
        assert_eq!(dynlex_shader_translate(bytes.as_ptr(), bytes.len(), 99), 0);
        assert_eq!(dynlex_wgsl_result_length(), 0);
        assert_eq!(dynlex_glsl_result_length(), 0);
        GLSL_BINDINGS.with(|value| assert_eq!(*value.borrow(), b"[]"));
        GLSL_UNIFORMS.with(|value| assert_eq!(*value.borrow(), b"[]"));
    }
}

#[test]
fn optimized_if_chain_fallthrough_is_valid_for_both_writers() {
    let work = Workspace::new();
    let root = Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap()
        .parent()
        .unwrap();
    let input = root.join("tests/runtime/graphics_shaders/switch_fallthrough.dl");
    for (stage, kind) in [
        ("vertex", ShaderStage::Vertex),
        ("fragment", ShaderStage::Fragment),
    ] {
        for level in ["-O0", "-O2"] {
            let bytes = compile(&input, &work.0.join("switch.spv"), stage, level);
            let mut module = spv::parse_u8_slice(&bytes, &spv::Options::default()).unwrap();
            let info = Validator::new(ValidationFlags::all(), Capabilities::empty())
                .validate(&module)
                .unwrap();
            if level == "-O2" {
                assert!(module.functions.iter().any(|(_, function)| function.body.iter().any(|statement|
                matches!(statement, naga::Statement::Store {pointer, ..}
                    if matches!(function.expressions[*pointer], naga::Expression::GlobalVariable(_))))),
                "The shared shader output store was nested in only one if arm ({stage}, {level})");
            }
            let original = naga::back::wgsl::write_string(&module, &info, WriterFlags::empty());
            let changed = switches::lower(&mut module);
            assert_eq!(changed, original.is_err());
            if level == "-O2" {
                assert!(original
                    .err()
                    .unwrap()
                    .to_string()
                    .contains("fall-through switch"));
            }
            Validator::new(ValidationFlags::all(), Capabilities::empty())
                .validate(&module)
                .unwrap();
            assert!(
                !switches::lower(&mut module),
                "Switch lowering must finish in one pass"
            );
            translate(&bytes).unwrap();
            translate_glsl(&bytes, kind).unwrap();
        }
    }
}

#[test]
fn early_join_before_its_last_predecessor_keeps_the_shared_output() {
    let work = Workspace::new();
    let root = Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap()
        .parent()
        .unwrap();
    let binary = work.0.join("ordered.spv");
    let bytes = compile(
        &root.join("tests/runtime/graphics_shaders/switch_fallthrough.dl"),
        &binary,
        "fragment",
        "-O2",
    );
    let mut words: Vec<u32> = bytes
        .chunks_exact(4)
        .map(|chunk| u32::from_le_bytes(chunk.try_into().unwrap()))
        .collect();
    let mut position = 5;
    let mut accepted = None;
    let mut selected = None;
    let mut end = None;
    while position < words.len() {
        let count = (words[position] >> 16) as usize;
        match words[position] & 0xffff {
            250 if accepted.is_none() => accepted = Some(words[position + 2]),
            248 if Some(words[position + 1]) == accepted => selected = Some(position),
            248 | 56 if selected.is_some() => {
                end = Some(position);
                break;
            }
            _ => {}
        }
        position += count;
    }
    let removed: Vec<_> = words.drain(selected.unwrap()..end.unwrap()).collect();
    let mut position = 5;
    while words[position] & 0xffff != 56 {
        position += (words[position] >> 16) as usize;
    }
    words.splice(position..position, removed);
    let bytes: Vec<_> = words.into_iter().flat_map(u32::to_le_bytes).collect();
    fs::write(&binary, &bytes).unwrap();
    run(Command::new("spirv-val")
        .args(["--target-env", "vulkan1.3"])
        .arg(&binary));
    let (module, _) = parse_and_validate(&bytes).unwrap();
    assert!(
        module
            .functions
            .iter()
            .any(|(_, function)| function.body.iter().any(
                |statement| matches!(statement, naga::Statement::Store {pointer, ..}
            if matches!(function.expressions[*pointer], naga::Expression::GlobalVariable(_)))
            )),
        "The output store depends on which branch appears first in the SPIR-V binary"
    );
    translate(&bytes).unwrap();
    translate_glsl(&bytes, ShaderStage::Fragment).unwrap();
}

#[test]
fn switch_lowering_leaves_unaffected_modules_and_validation_types_unchanged() {
    let work = Workspace::new();
    let source = work.0.join("constant.dl");
    fs::write(&source, "@intrinsic(\"shader output\", 1.0)\n").unwrap();
    let bytes = compile(&source, &work.0.join("constant.spv"), "fragment", "-O2");
    let mut module = spv::parse_u8_slice(&bytes, &spv::Options::default()).unwrap();
    let before = format!("{module:?}");
    assert!(!switches::lower(&mut module));
    assert_eq!(format!("{module:?}"), before);
}
