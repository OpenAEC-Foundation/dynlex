use std::cell::RefCell;

use naga::back::glsl;
use naga::back::wgsl::WriterFlags;
use naga::front::spv;
use naga::valid::{Capabilities, ValidationFlags, Validator};
use naga::ShaderStage;

mod glsl_bindings;
mod glsl_samplers;
mod switches;

#[cfg(test)]
#[path = "../tests/cases/glsl_samplers.rs"]
mod sampler_tests;

thread_local! {
    static RESULT: RefCell<Vec<u8>> = const { RefCell::new(Vec::new()) };
    static GLSL_RESULT: RefCell<Vec<u8>> = const { RefCell::new(Vec::new()) };
    static GLSL_BINDINGS: RefCell<Vec<u8>> = RefCell::new(b"[]".to_vec());
    static ERROR: RefCell<Vec<u8>> = const { RefCell::new(Vec::new()) };
}

fn set_bytes(slot: &'static std::thread::LocalKey<RefCell<Vec<u8>>>, bytes: Vec<u8>) {
    slot.with(|value| *value.borrow_mut() = bytes);
}

fn reset_translation() {
    set_bytes(&RESULT, Vec::new());
    set_bytes(&GLSL_RESULT, Vec::new());
    set_bytes(&GLSL_BINDINGS, b"[]".to_vec());
    set_bytes(&ERROR, Vec::new());
}

struct GlslTranslation {
    source: String,
    bindings: String,
}

fn parse_and_validate(bytes: &[u8]) -> Result<(naga::Module, naga::valid::ModuleInfo), String> {
    let mut module = spv::parse_u8_slice(bytes, &spv::Options::default())
        .map_err(|error| format!("SPIR-V parse failed: {error}"))?;
    let mut info = Validator::new(ValidationFlags::all(), Capabilities::empty())
        .validate(&module)
        .map_err(|error| format!("WebGPU shader validation failed: {error}"))?;
    if switches::lower(&mut module) {
        info = Validator::new(ValidationFlags::all(), Capabilities::empty())
            .validate(&module)
            .map_err(|error| format!("Shader switch lowering failed validation: {error}"))?;
    }
    Ok((module, info))
}

fn translate(bytes: &[u8]) -> Result<String, String> {
    let (module, info) = parse_and_validate(bytes)?;
    naga::back::wgsl::write_string(&module, &info, WriterFlags::empty())
        .map_err(|error| format!("WGSL generation failed: {error}"))
}

fn write_glsl(
    module: &naga::Module,
    info: &naga::valid::ModuleInfo,
    stage: ShaderStage,
    origins: &glsl_samplers::BindingOrigins,
) -> Result<GlslTranslation, String> {
    let options = glsl::Options {
        version: glsl::Version::new_gles(300),
        ..Default::default()
    };
    let pipeline_options = glsl::PipelineOptions {
        shader_stage: stage,
        entry_point: "main".into(),
        multiview: None,
    };
    let mut source = String::new();
    let reflection = glsl::Writer::new(
        &mut source,
        &module,
        &info,
        &options,
        &pipeline_options,
        naga::proc::BoundsCheckPolicies::default(),
    )
    .map_err(|error| format!("GLSL writer initialization failed: {error}"))?
    .write()
    .map_err(|error| format!("GLSL generation failed: {error}"))?;
    Ok(GlslTranslation {
        source,
        bindings: glsl_bindings::texture_bindings(module, &reflection, origins)?,
    })
}

fn translate_glsl(bytes: &[u8], stage: ShaderStage) -> Result<GlslTranslation, String> {
    let (module, info) = parse_and_validate(bytes)?;
    write_glsl_input(bytes, &module, &info, stage)
}

fn write_glsl_input(
    bytes: &[u8],
    module: &naga::Module,
    info: &naga::valid::ModuleInfo,
    stage: ShaderStage,
) -> Result<GlslTranslation, String> {
    match glsl_samplers::lower(bytes)? {
        Some(lowered) => {
            let (module, info) = parse_and_validate(&lowered.bytes)?;
            write_glsl(&module, &info, stage, &lowered.origins)
        }
        None => write_glsl(module, info, stage, &glsl_samplers::BindingOrigins::new()),
    }
}

fn shader_stage(stage: u32) -> Result<ShaderStage, String> {
    match stage {
        0 => Ok(ShaderStage::Vertex),
        1 => Ok(ShaderStage::Fragment),
        _ => Err("Shader stage is invalid".into()),
    }
}

#[no_mangle]
pub extern "C" fn dynlex_wgsl_allocate(length: usize) -> *mut u8 {
    let bytes = vec![0_u8; length].into_boxed_slice();
    Box::into_raw(bytes) as *mut u8
}

/// # Safety
///
/// `pointer` and `length` must describe a slice returned by `dynlex_wgsl_allocate`
/// that has not already been released.
#[no_mangle]
pub unsafe extern "C" fn dynlex_wgsl_deallocate(pointer: *mut u8, length: usize) {
    if length == 0 {
        return;
    }
    let slice = std::ptr::slice_from_raw_parts_mut(pointer, length);
    drop(Box::from_raw(slice));
}

/// # Safety
///
/// `pointer` must reference `length` readable bytes in this module's linear memory.
#[no_mangle]
pub unsafe extern "C" fn dynlex_wgsl_translate(pointer: *const u8, length: usize) -> i32 {
    reset_translation();
    if pointer.is_null() || length == 0 {
        set_bytes(&ERROR, b"SPIR-V input is empty".to_vec());
        return 0;
    }
    let bytes = std::slice::from_raw_parts(pointer, length);
    match translate(bytes) {
        Ok(source) => {
            set_bytes(&RESULT, source.into_bytes());
            1
        }
        Err(error) => {
            set_bytes(&ERROR, error.into_bytes());
            0
        }
    }
}

/// # Safety
///
/// `pointer` must reference `length` readable bytes in this module's linear memory.
#[no_mangle]
pub unsafe extern "C" fn dynlex_glsl_translate(
    pointer: *const u8,
    length: usize,
    stage: u32,
) -> i32 {
    reset_translation();
    if pointer.is_null() || length == 0 {
        set_bytes(&ERROR, b"SPIR-V input is empty".to_vec());
        return 0;
    }
    let shader_stage = match shader_stage(stage) {
        Ok(shader_stage) => shader_stage,
        Err(error) => {
            set_bytes(&ERROR, error.into_bytes());
            return 0;
        }
    };
    let bytes = std::slice::from_raw_parts(pointer, length);
    match translate_glsl(bytes, shader_stage) {
        Ok(source) => {
            set_bytes(&RESULT, source.source.into_bytes());
            set_bytes(&GLSL_BINDINGS, source.bindings.into_bytes());
            1
        }
        Err(error) => {
            set_bytes(&ERROR, error.into_bytes());
            0
        }
    }
}

/// # Safety
///
/// `pointer` must reference `length` readable bytes in this module's linear memory.
#[no_mangle]
pub unsafe extern "C" fn dynlex_shader_translate(
    pointer: *const u8,
    length: usize,
    stage: u32,
) -> i32 {
    reset_translation();
    if pointer.is_null() || length == 0 {
        set_bytes(&ERROR, b"SPIR-V input is empty".to_vec());
        return 0;
    }
    let shader_stage = match shader_stage(stage) {
        Ok(shader_stage) => shader_stage,
        Err(error) => {
            set_bytes(&ERROR, error.into_bytes());
            return 0;
        }
    };
    let bytes = std::slice::from_raw_parts(pointer, length);
    let translated = parse_and_validate(bytes).and_then(|(module, info)| {
        let wgsl = naga::back::wgsl::write_string(&module, &info, WriterFlags::empty())
            .map_err(|error| format!("WGSL generation failed: {error}"))?;
        let glsl = write_glsl_input(bytes, &module, &info, shader_stage)?;
        Ok((wgsl, glsl))
    });
    match translated {
        Ok((wgsl, glsl)) => {
            set_bytes(&RESULT, wgsl.into_bytes());
            set_bytes(&GLSL_RESULT, glsl.source.into_bytes());
            set_bytes(&GLSL_BINDINGS, glsl.bindings.into_bytes());
            1
        }
        Err(error) => {
            set_bytes(&ERROR, error.into_bytes());
            0
        }
    }
}

#[no_mangle]
pub extern "C" fn dynlex_wgsl_result_pointer() -> *const u8 {
    RESULT.with(|result| result.borrow().as_ptr())
}

#[no_mangle]
pub extern "C" fn dynlex_wgsl_result_length() -> usize {
    RESULT.with(|result| result.borrow().len())
}

#[no_mangle]
pub extern "C" fn dynlex_glsl_result_pointer() -> *const u8 {
    GLSL_RESULT.with(|result| result.borrow().as_ptr())
}

#[no_mangle]
pub extern "C" fn dynlex_glsl_result_length() -> usize {
    GLSL_RESULT.with(|result| result.borrow().len())
}

#[no_mangle]
pub extern "C" fn dynlex_glsl_bindings_pointer() -> *const u8 {
    GLSL_BINDINGS.with(|result| result.borrow().as_ptr())
}

#[no_mangle]
pub extern "C" fn dynlex_glsl_bindings_length() -> usize {
    GLSL_BINDINGS.with(|result| result.borrow().len())
}

#[no_mangle]
pub extern "C" fn dynlex_wgsl_error_pointer() -> *const u8 {
    ERROR.with(|error| error.borrow().as_ptr())
}

#[no_mangle]
pub extern "C" fn dynlex_wgsl_error_length() -> usize {
    ERROR.with(|error| error.borrow().len())
}
