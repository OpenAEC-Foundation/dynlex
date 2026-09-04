use std::cell::RefCell;

use naga::back::wgsl::WriterFlags;
use naga::front::spv;
use naga::valid::{Capabilities, ValidationFlags, Validator};

thread_local! {
    static RESULT: RefCell<Vec<u8>> = const { RefCell::new(Vec::new()) };
    static ERROR: RefCell<Vec<u8>> = const { RefCell::new(Vec::new()) };
}

fn set_bytes(slot: &'static std::thread::LocalKey<RefCell<Vec<u8>>>, bytes: Vec<u8>) {
    slot.with(|value| *value.borrow_mut() = bytes);
}

fn translate(bytes: &[u8]) -> Result<String, String> {
    let module = spv::parse_u8_slice(bytes, &spv::Options::default())
        .map_err(|error| format!("SPIR-V parse failed: {error}"))?;
    let info = Validator::new(ValidationFlags::all(), Capabilities::empty())
        .validate(&module)
        .map_err(|error| format!("WebGPU shader validation failed: {error}"))?;
    naga::back::wgsl::write_string(&module, &info, WriterFlags::empty())
        .map_err(|error| format!("WGSL generation failed: {error}"))
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
    set_bytes(&RESULT, Vec::new());
    set_bytes(&ERROR, Vec::new());
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

#[no_mangle]
pub extern "C" fn dynlex_wgsl_result_pointer() -> *const u8 {
    RESULT.with(|result| result.borrow().as_ptr())
}

#[no_mangle]
pub extern "C" fn dynlex_wgsl_result_length() -> usize {
    RESULT.with(|result| result.borrow().len())
}

#[no_mangle]
pub extern "C" fn dynlex_wgsl_error_pointer() -> *const u8 {
    ERROR.with(|error| error.borrow().as_ptr())
}

#[no_mangle]
pub extern "C" fn dynlex_wgsl_error_length() -> usize {
    ERROR.with(|error| error.borrow().len())
}
