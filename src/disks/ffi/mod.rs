use std::ffi::c_char;

use gtk::glib::{self, translate::FromGlibPtrBorrow};

use crate::GduRestoreDiskImageDialog;

/// Reads a C string and converts it to a Rust String.
///
/// Opposed to using [CStr], this also handles `NULL`.
fn read_nullable_cstr(cstr_ptr: *const c_char) -> Option<String> {
    if cstr_ptr.is_null() {
        return None;
    }
    //SAFETY: we ensure that the pointer is non-null and otherwise assume that it is a valid c
    //string
    let cstr = unsafe { std::ffi::CStr::from_ptr(cstr_ptr) }.to_owned();
    Some(cstr.to_str().ok()?.to_string())
}

#[unsafe(no_mangle)]
pub extern "C" fn gdu_rs_restore_disk_image_dialog_show(
    window_ptr: *mut gtk::ffi::GtkWindow,
    object_path: *const c_char,
    disk_image_filename: *const c_char,
) {
    //SAFETY: the C side has already initialized gtk
    unsafe { gtk::set_initialized() }
    let parent_window = unsafe { gtk::Window::from_glib_borrow(window_ptr) };
    let disk_image_filename = read_nullable_cstr(disk_image_filename);
    let object_path = read_nullable_cstr(object_path);

    glib::MainContext::default().spawn_local(async move {
        let client = udisks::Client::new()
            .await
            .expect("Failed to create udisks client");
        let object = object_path.and_then(|p| client.object(p).ok());

        GduRestoreDiskImageDialog::show(
            parent_window.as_ref(),
            object.as_ref(),
            client,
            disk_image_filename.as_deref(),
        )
        .await;
    });
}
