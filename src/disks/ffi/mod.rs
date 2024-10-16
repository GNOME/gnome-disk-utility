use std::ffi::c_char;

use gtk::glib::{self, translate::FromGlibPtrBorrow};

use crate::GduRestoreDiskImageDialog;

#[no_mangle]
pub extern "C" fn gdu_rs_restore_disk_image_dialog_show(
    window_ptr: *mut gtk::ffi::GtkWindow,
    disk_image_filename: *const c_char,
) {
    //SAFETY: the C side has already initialized gtk
    unsafe { gtk::set_initialized() }
    let parent_window = unsafe { gtk::Window::from_glib_borrow(window_ptr) };
    let disk_image_filename = unsafe { std::ffi::CStr::from_ptr(disk_image_filename) }.to_owned();

    glib::MainContext::default().spawn_local(async move {
        let client = udisks::Client::new()
            .await
            .expect("Failed to create udisks client");

        GduRestoreDiskImageDialog::show(
            parent_window.as_ref(),
            None,
            client,
            disk_image_filename.to_str().ok(),
        )
        .await;
    });
}
