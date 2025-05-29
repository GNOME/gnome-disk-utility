use std::{
    collections::HashMap,
    ffi::c_char,
    rc::Rc,
    sync::{LazyLock, Mutex},
};

use gtk::glib::{self, translate::FromGlibPtrBorrow};
use udisks::zbus::zvariant::OwnedObjectPath;

use crate::{GduRestoreDiskImageDialog, localjob::LocalJob};

//FIXME: move this to Gdu application once ported
// GTK is single threaded
thread_local! {
    static GLOBAL_MAP: LazyLock<Mutex<HashMap<OwnedObjectPath, Rc<LocalJob>>>> =
    LazyLock::new(|| Mutex::new(HashMap::new()));
}

pub fn create_local_job(object: &udisks::Object) -> Rc<LocalJob> {
    let local_job = Rc::new(LocalJob::new(object.clone()));
    GLOBAL_MAP.with(|map| {
        let mut map = map.lock().expect("poisoned lock");
        map.entry(object.object_path().clone())
            .or_insert_with(|| local_job)
            .clone()
    })
}

pub fn destroy_local_job(job: Rc<LocalJob>) {
    GLOBAL_MAP.with(|map| {
        let mut map = map.lock().expect("poisoned lock");
        let _ = map.remove(job.object().unwrap().0.object_path());
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn gdu_rs_has_local_jobs() -> bool {
    GLOBAL_MAP.with(|map| {
        let map = map.lock().expect("poisoned lock");
        map.is_empty()
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn gdu_rs_local_jobs_clear() {
    GLOBAL_MAP.with(|map| {
        let mut map = map.lock().expect("poisoned lock");
        map.clear();
    })
}

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
    assert!(!window_ptr.is_null(), "`window_ptr` must be non-null");

    //SAFETY: the C side has already initialized GTK
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
