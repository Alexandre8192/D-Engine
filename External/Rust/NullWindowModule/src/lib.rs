#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]

use core::ffi::c_char;
use core::ffi::c_void;
use core::mem::{align_of, size_of};
use core::panic::UnwindSafe;
use core::ptr;
use std::panic::catch_unwind;

pub type DNG_ABI_CALL = extern "C" fn;
pub type dng_u8 = u8;
pub type dng_u32 = u32;
pub type dng_u64 = u64;

pub type dng_status_v1 = dng_u32;
pub const DNG_STATUS_OK: dng_status_v1 = 0;
pub const DNG_STATUS_FAIL: dng_status_v1 = 1;
pub const DNG_STATUS_INVALID_ARG: dng_status_v1 = 2;
pub const DNG_STATUS_OUT_OF_MEMORY: dng_status_v1 = 3;
pub const DNG_STATUS_UNSUPPORTED: dng_status_v1 = 4;

pub const DNG_ABI_VERSION_V1: dng_u32 = 1;

pub type dng_bool_v1 = dng_u8;

#[repr(C)]
pub struct dng_abi_header_v1 {
    pub struct_size: dng_u32,
    pub abi_version: dng_u32,
}

#[repr(C)]
pub struct dng_str_view_v1 {
    pub data: *const c_char,
    pub size: dng_u32,
}

pub type dng_window_handle_v1 = dng_u64;

#[repr(C)]
pub struct dng_window_desc_v1 {
    pub width: dng_u32,
    pub height: dng_u32,
    pub title: dng_str_view_v1,
    pub flags: dng_u32,
}

#[repr(C)]
pub struct dng_window_size_v1 {
    pub width: dng_u32,
    pub height: dng_u32,
}

#[repr(C)]
pub struct dng_host_api_v1 {
    pub header: dng_abi_header_v1,
    pub user: *mut c_void,
    pub log: Option<extern "C" fn(*mut c_void, dng_u32, dng_str_view_v1)>,
    pub alloc: Option<extern "C" fn(*mut c_void, dng_u64, dng_u64) -> *mut c_void>,
    pub free: Option<extern "C" fn(*mut c_void, *mut c_void, dng_u64, dng_u64)>,
}

#[repr(C)]
pub struct dng_window_api_v1 {
    pub header: dng_abi_header_v1,
    pub ctx: *mut c_void,
    pub create: Option<extern "C" fn(*mut c_void, *const dng_window_desc_v1, *mut dng_window_handle_v1) -> dng_status_v1>,
    pub destroy: Option<extern "C" fn(*mut c_void, dng_window_handle_v1) -> dng_status_v1>,
    pub poll: Option<extern "C" fn(*mut c_void) -> dng_status_v1>,
    pub get_size: Option<extern "C" fn(*mut c_void, dng_window_handle_v1, *mut dng_window_size_v1) -> dng_status_v1>,
    pub set_title: Option<extern "C" fn(*mut c_void, dng_window_handle_v1, dng_str_view_v1) -> dng_status_v1>,
}

#[repr(C)]
pub struct dng_module_api_v1 {
    pub header: dng_abi_header_v1,
    pub module_name: dng_str_view_v1,
    pub module_version_major: dng_u32,
    pub module_version_minor: dng_u32,
    pub module_version_patch: dng_u32,
    pub window: dng_window_api_v1,
    pub shutdown: Option<extern "C" fn(*mut c_void, *const dng_host_api_v1) -> dng_status_v1>,
}

#[repr(C)]
struct NullWindowCtx {
    host: *const dng_host_api_v1,
    handle: dng_window_handle_v1,
    size: dng_window_size_v1,
    title: *mut c_char,
    title_size: dng_u32,
}

unsafe fn log_message(host: *const dng_host_api_v1, level: dng_u32, msg: &'static [u8]) {
    if host.is_null() {
        return;
    }
    let h = &*host;
    if let Some(log_fn) = h.log {
        let view = dng_str_view_v1 { data: msg.as_ptr() as *const c_char, size: msg.len() as dng_u32 };
        log_fn(h.user, level, view);
    }
}

unsafe fn free_title(ctx: &mut NullWindowCtx) {
    if !ctx.title.is_null() {
        if let Some(free_fn) = (*ctx.host).free {
            free_fn((*ctx.host).user, ctx.title as *mut c_void, ctx.title_size as dng_u64, 1);
        }
        ctx.title = ptr::null_mut();
        ctx.title_size = 0;
    }
}

unsafe fn alloc_copy_title(ctx: &mut NullWindowCtx, title: dng_str_view_v1) -> dng_status_v1 {
    if title.size == 0 {
        return DNG_STATUS_OK;
    }
    if title.data.is_null() {
        return DNG_STATUS_INVALID_ARG;
    }
    let alloc_fn = match (*ctx.host).alloc {
        Some(f) => f,
        None => return DNG_STATUS_INVALID_ARG,
    };
    let size = title.size as dng_u64;
    let mem = alloc_fn((*ctx.host).user, size, 1);
    if mem.is_null() {
        return DNG_STATUS_OUT_OF_MEMORY;
    }
    ptr::copy_nonoverlapping(title.data, mem as *mut c_char, title.size as usize);
    ctx.title = mem as *mut c_char;
    ctx.title_size = title.size;
    DNG_STATUS_OK
}

fn catch_unwind_status<F: FnOnce() -> dng_status_v1 + UnwindSafe>(f: F) -> dng_status_v1 {
    match catch_unwind(f) {
        Ok(s) => s,
        Err(_) => DNG_STATUS_FAIL,
    }
}

extern "C" fn window_create(raw_ctx: *mut c_void, desc: *const dng_window_desc_v1, out_handle: *mut dng_window_handle_v1) -> dng_status_v1 {
    catch_unwind_status(|| unsafe {
        if raw_ctx.is_null() || desc.is_null() || out_handle.is_null() {
            return DNG_STATUS_INVALID_ARG;
        }
        let ctx = &mut *(raw_ctx as *mut NullWindowCtx);
        if ctx.handle != 0 {
            return DNG_STATUS_FAIL;
        }
        let d = &*desc;
        if d.flags != 0 {
            return DNG_STATUS_INVALID_ARG;
        }
        if d.title.size > 0 && d.title.data.is_null() {
            return DNG_STATUS_INVALID_ARG;
        }
        ctx.size.width = d.width;
        ctx.size.height = d.height;
        free_title(ctx);
        let title_status = alloc_copy_title(ctx, d.title);
        if title_status != DNG_STATUS_OK {
            return title_status;
        }
        ctx.handle = 1;
        *out_handle = ctx.handle;
        DNG_STATUS_OK
    })
}

extern "C" fn window_destroy(raw_ctx: *mut c_void, handle: dng_window_handle_v1) -> dng_status_v1 {
    catch_unwind_status(|| unsafe {
        if raw_ctx.is_null() || handle == 0 {
            return DNG_STATUS_INVALID_ARG;
        }
        let ctx = &mut *(raw_ctx as *mut NullWindowCtx);
        if ctx.handle != handle {
            return DNG_STATUS_INVALID_ARG;
        }
        free_title(ctx);
        ctx.handle = 0;
        ctx.size.width = 0;
        ctx.size.height = 0;
        DNG_STATUS_OK
    })
}

extern "C" fn window_poll(raw_ctx: *mut c_void) -> dng_status_v1 {
    catch_unwind_status(|| unsafe {
        if raw_ctx.is_null() {
            return DNG_STATUS_INVALID_ARG;
        }
        DNG_STATUS_OK
    })
}

extern "C" fn window_get_size(raw_ctx: *mut c_void, handle: dng_window_handle_v1, out_size: *mut dng_window_size_v1) -> dng_status_v1 {
    catch_unwind_status(|| unsafe {
        if raw_ctx.is_null() || out_size.is_null() || handle == 0 {
            return DNG_STATUS_INVALID_ARG;
        }
        let ctx = &*(raw_ctx as *mut NullWindowCtx);
        if ctx.handle != handle {
            return DNG_STATUS_INVALID_ARG;
        }
        ptr::write(out_size, ctx.size);
        DNG_STATUS_OK
    })
}

extern "C" fn window_set_title(raw_ctx: *mut c_void, handle: dng_window_handle_v1, title: dng_str_view_v1) -> dng_status_v1 {
    catch_unwind_status(|| unsafe {
        if raw_ctx.is_null() || handle == 0 {
            return DNG_STATUS_INVALID_ARG;
        }
        let ctx = &mut *(raw_ctx as *mut NullWindowCtx);
        if ctx.handle != handle {
            return DNG_STATUS_INVALID_ARG;
        }
        if title.size > 0 && title.data.is_null() {
            return DNG_STATUS_INVALID_ARG;
        }
        free_title(ctx);
        alloc_copy_title(ctx, title)
    })
}

extern "C" fn module_shutdown(raw_ctx: *mut c_void, host: *const dng_host_api_v1) -> dng_status_v1 {
    catch_unwind_status(|| unsafe {
        if raw_ctx.is_null() || host.is_null() {
            return DNG_STATUS_INVALID_ARG;
        }
        let ctx = &mut *(raw_ctx as *mut NullWindowCtx);
        free_title(ctx);
        let free_fn = match (*host).free {
            Some(f) => f,
            None => return DNG_STATUS_INVALID_ARG,
        };
        free_fn((*host).user, raw_ctx, size_of::<NullWindowCtx>() as dng_u64, align_of::<NullWindowCtx>() as dng_u64);
        DNG_STATUS_OK
    })
}

#[no_mangle]
pub extern "C" fn dngModuleGetApi_v1(host: *const dng_host_api_v1, out_api: *mut dng_module_api_v1) -> dng_status_v1 {
    catch_unwind_status(|| unsafe {
        if host.is_null() || out_api.is_null() {
            return DNG_STATUS_INVALID_ARG;
        }
        let h = &*host;
        if h.header.struct_size != size_of::<dng_host_api_v1>() as dng_u32 || h.header.abi_version != DNG_ABI_VERSION_V1 {
            return DNG_STATUS_UNSUPPORTED;
        }
        if h.alloc.is_none() || h.free.is_none() {
            return DNG_STATUS_INVALID_ARG;
        }
        let alloc_fn = h.alloc.unwrap();
        let ctx_mem = alloc_fn(h.user, size_of::<NullWindowCtx>() as dng_u64, align_of::<NullWindowCtx>() as dng_u64);
        if ctx_mem.is_null() {
            return DNG_STATUS_OUT_OF_MEMORY;
        }
        let ctx = &mut *(ctx_mem as *mut NullWindowCtx);
        ctx.host = host;
        ctx.handle = 0;
        ctx.size.width = 0;
        ctx.size.height = 0;
        ctx.title = ptr::null_mut();
        ctx.title_size = 0;

        let module_name_bytes: &[u8] = b"RustNullWindow";
        let mut api = dng_module_api_v1 {
            header: dng_abi_header_v1 { struct_size: size_of::<dng_module_api_v1>() as dng_u32, abi_version: DNG_ABI_VERSION_V1 },
            module_name: dng_str_view_v1 { data: module_name_bytes.as_ptr() as *const c_char, size: module_name_bytes.len() as dng_u32 },
            module_version_major: 1,
            module_version_minor: 0,
            module_version_patch: 0,
            window: dng_window_api_v1 {
                header: dng_abi_header_v1 { struct_size: size_of::<dng_window_api_v1>() as dng_u32, abi_version: DNG_ABI_VERSION_V1 },
                ctx: ctx as *mut NullWindowCtx as *mut c_void,
                create: Some(window_create),
                destroy: Some(window_destroy),
                poll: Some(window_poll),
                get_size: Some(window_get_size),
                set_title: Some(window_set_title),
            },
            shutdown: Some(module_shutdown),
        };

        ptr::write(out_api, api);
        DNG_STATUS_OK
    })
}
