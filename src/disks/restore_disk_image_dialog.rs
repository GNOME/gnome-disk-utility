use std::collections::HashMap;
use std::io::{ErrorKind, Read, Write};
use std::ops::Sub;
use std::os::fd::{AsFd, AsRawFd};

use adw::prelude::*;
use gettextrs::{gettext, pgettext};
use gtk::glib::property::PropertySet;
use gtk::subclass::prelude::*;
use gtk::{gio, glib};
use libgdu::gettext::gettext_f;
use libgdu::ConfirmationDialogResponse;

use crate::estimator::{self, GduEstimator};

mod imp {
    use std::cell::{Cell, RefCell};

    use adw::subclass::window::AdwWindowImpl;

    use crate::config;

    use super::*;

    #[derive(Debug, Default, gtk::CompositeTemplate)]
    #[template(file = "ui/gdu-restore-disk-image-dialog.ui")]
    pub struct GduRestoreDiskImageDialog {
        pub(super) restore_file: RefCell<Option<gio::File>>,
        pub(super) block_size: Cell<u64>,
        pub(super) client: RefCell<Option<udisks::Client>>,
        pub(super) object: RefCell<Option<udisks::Object>>,
        pub(super) block: RefCell<Option<udisks::block::BlockProxy<'static>>>,
        pub(super) drive: RefCell<Option<udisks::drive::DriveProxy<'static>>>,
        pub(super) inhibit_cookie: Cell<Option<u32>>,
        pub(super) destination_drives: RefCell<Vec<udisks::Object>>,

        #[template_child]
        pub(super) size_row: TemplateChild<adw::ActionRow>,
        #[template_child]
        pub(super) start_restore_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub(super) image_row: TemplateChild<adw::ActionRow>,
        #[template_child]
        pub(super) file_chooser_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub(super) destination_row: TemplateChild<adw::ComboRow>,
        #[template_child]
        pub(super) error_banner: TemplateChild<adw::Banner>,
        #[template_child]
        pub(super) warning_banner: TemplateChild<adw::Banner>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for GduRestoreDiskImageDialog {
        const NAME: &'static str = "GduRestoreDiskImageDialog";
        type Type = super::GduRestoreDiskImageDialog;
        type ParentType = adw::Window;

        fn class_init(klass: &mut Self::Class) {
            klass.bind_template();
            klass.bind_template_instance_callbacks();
        }

        fn instance_init(obj: &glib::subclass::InitializingObject<Self>) {
            obj.init_template();
        }
    }

    impl ObjectImpl for GduRestoreDiskImageDialog {
        fn constructed(&self) {
            self.parent_constructed();
            let obj = self.obj();

            // Devel Profile
            if config::PROFILE == "Devel" {
                obj.add_css_class("devel");
            }
        }

        fn dispose(&self) {
            self.dispose_template();
        }
    }

    impl WidgetImpl for GduRestoreDiskImageDialog {}
    impl WindowImpl for GduRestoreDiskImageDialog {}
    impl AdwWindowImpl for GduRestoreDiskImageDialog {}
}

glib::wrapper! {
    pub struct GduRestoreDiskImageDialog(ObjectSubclass<imp::GduRestoreDiskImageDialog>)
        @extends gtk::Widget, gtk::Window, adw::Window,
        @implements gio::ActionMap, gio::ActionGroup, gtk::Root;
}

#[gtk::template_callbacks]
impl GduRestoreDiskImageDialog {
    pub async fn show(
        parent_window: &impl IsA<gtk::Window>,
        object: Option<&udisks::Object>,
        client: udisks::Client,
        //TODO: use a path
        disk_image_filename: Option<&str>,
    ) {
        let dialog: Self = glib::Object::builder()
            .property("application", parent_window.application())
            .property("transient-for", parent_window)
            .build();
        let imp = dialog.imp();
        imp.client.replace(Some(client));
        dialog.set_destination_object(object.cloned()).await;
        if let Some(disk_image_filename) = disk_image_filename {
            let file = gio::File::for_commandline_arg(disk_image_filename);
            imp.restore_file.set(Some(file));
            //Image: show label if image is known, otherwise show a filechooser button
            let subtitle = libgdu::unfuse_path(disk_image_filename);
            imp.image_row.set_subtitle(&subtitle);
            imp.file_chooser_button.set_visible(false);
        }

        // Destination: Show label if device is known, otherwise show a combobox
        if let Some(object) = object {
            let info = dialog.client().object_info(object).await;
            imp.destination_row
                .set_subtitle(&info.one_liner.unwrap_or_default())
        } else {
            imp.destination_row.remove_css_class("property");
            dialog.populate_destination_combobox().await;
        }
        dialog.update();

        dialog.present();
    }

    async fn set_destination_object(&self, object: Option<udisks::Object>) {
        let imp = self.imp();
        //TODO: add a eq impl in udisks
        if imp.object.borrow().as_ref().map(|v| v.object_path())
            != object.as_ref().map(|v| v.object_path())
        {
            imp.block_size.set(0);
            //TODO: take ownership of object
            if let Some(object) = object {
                let block = object.block().await.expect("`block` should be Ok");
                let drive = self.client().drive_for_block(&block).await;
                imp.object.replace(Some(object));
                imp.drive.replace(drive.ok());

                //TODO: use a method call for this so it works on e.g. floppy drives where e.g. we don't know the size
                imp.block_size.set(block.size().await.unwrap_or_default());
                imp.block.replace(Some(block));
            }
        }
    }

    fn update(&self) -> Option<()> {
        let imp = self.imp();
        let Some(file) = &*imp.restore_file.borrow() else {
            //TODO: figure out why this check was placed here
            //|| imp.block_size.get() == 0 {
            imp.start_restore_button.set_sensitive(false);
            return None;
        };
        let mut restore_warning = None;
        let mut restore_error = None;

        let info = file
            .query_info(
                &format!(
                    "{},{},{}",
                    gio::FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                    gio::FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                    gio::FILE_ATTRIBUTE_STANDARD_SIZE
                ),
                gio::FileQueryInfoFlags::empty(),
                gio::Cancellable::NONE,
            )
            .ok()?;
        let name = info.display_name();

        let is_xz_compressed = info.content_type()?.ends_with("-xz-compressed");
        let size = if is_xz_compressed {
            let filestream = file.read(gio::Cancellable::NONE).ok()?.into_read();
            liblzma::uncompressed_size(filestream).unwrap_or_else(|_| {
                restore_error = Some(gettext("File does not appear to be XY compressed"));
                0
            })
        } else {
            info.size() as u64
        };

        // Translators: When shown for a compressed disk image in the "Size" field.
        // The %s is the uncompressed size as a long string e.g. "4.2 MB (4,300,123 bytes)".
        let size_str = gettext_f(
            if is_xz_compressed {
                "{} when decompressed"
            } else {
                "{}"
            },
            [self.client().size_for_display(size, false, true)],
        );

        let block_left_over_size = imp.block_size.get() as i64 - size as i64;

        // if size is 0, error may be set already..
        if size == 0 && restore_error.is_none() {
            restore_error = Some(gettext("Cannot restore image of size 0"));
        } else if block_left_over_size > 1000 * 1000 {
            // Only complain if slack is bigger than 1MB
            let size_str =
                self.client()
                    .size_for_display(block_left_over_size.unsigned_abs(), false, false);
            restore_warning = Some(gettext_f(
                "The disk image is {} smaller than the target device",
                [size_str],
            ));
        } else if size > imp.block_size.get() {
            let size_str =
                self.client()
                    .size_for_display(size - imp.block_size.get(), false, false);
            restore_error = Some(gettext_f(
                "The disk image is {} bigger than the target device",
                [size_str],
            ));
        }

        let imp = self.imp();

        if let Some(error) = &restore_error {
            imp.error_banner.set_title(error);
        }
        if let Some(warning) = &restore_warning {
            imp.warning_banner.set_title(warning);
        }
        imp.error_banner.set_revealed(restore_error.is_some());
        imp.warning_banner.set_revealed(restore_warning.is_some());
        imp.start_restore_button
            .set_sensitive(restore_error.is_none());
        imp.image_row.set_subtitle(&name);
        imp.size_row.set_subtitle(if size_str.is_empty() {
            "—"
        } else {
            &size_str
        });

        Some(())
    }

    async fn populate_destination_combobox(&self) {
        let mut drives: Vec<udisks::Object> = Vec::new();
        let drive_names = gtk::StringList::default();

        let client = self.client();
        for object in client
            .object_manager()
            .get_managed_objects()
            .await
            .unwrap_or_default()
            .into_iter()
            .filter_map(|(object_path, _)| client.object(object_path).ok())
        {
            let Ok(drive): udisks::Result<udisks::drive::DriveProxy> = object.drive().await else {
                continue;
            };
            let info = client.object_info(&object).await;
            drive_names.append(&info.one_liner.unwrap());
            if let Some(block) = client.block_for_drive(&drive, false).await {
                let object = client.object(block.inner().path().to_owned()).unwrap();
                drives.push(object);
            }
        }
        //TODO: names are truncated
        self.imp().destination_row.set_model(Some(&drive_names));
        self.imp().destination_drives.replace(drives);
    }

    #[template_callback]
    async fn destination_row_selected_cb(
        &self,
        _param: &glib::ParamSpec,
        combo_row: &adw::ComboRow,
    ) {
        let imp = self.imp();
        let selected_drive = imp.destination_drives.borrow()[combo_row.selected() as usize].clone();
        self.set_destination_object(Some(selected_drive)).await;
        self.update();
    }

    fn client(&self) -> udisks::Client {
        self.imp().client.borrow().clone().unwrap()
    }

    fn play_complete_sound(&self) {
        // Translators: A descriptive string for the 'complete' sound, see CA_PROP_EVENT_DESCRIPTION
        let _sound_message = gettext("Disk image copying complete");
        /* gtk4 todo : Find a replacement for this
        ca_gtk_play_for_widget (GTK_WIDGET (self->dialog), 0,
                                CA_PROP_EVENT_ID, "complete",
                                CA_PROP_EVENT_DESCRIPTION, sound_message,
                                NULL);
        */
    }

    #[template_callback]
    async fn on_start_restore_button_clicked_cb(&self, _button: &gtk::Button) {
        let object = self.imp().object.borrow().clone().unwrap();
        let objects = [&object];
        let affected_devices_widget =
            libgdu::create_widget_from_objects(&self.client(), &objects).await;
        let confirmation_dialog = libgdu::ConfirmationDialog {
            message: gettext("Are you sure you want to write the disk image to the device?"),
            description: gettext("All existing data will be lost"),
            reponse_verb: gettext("Restore"),
            reponse_appearance: adw::ResponseAppearance::Destructive,
        };

        let response = confirmation_dialog
            .show(self, Some(&affected_devices_widget))
            .await;

        if response == ConfirmationDialogResponse::Cancel {
            return;
        }

        let Ok(_) = libgdu::ensure_unused(&self.client(), self, &object).await else {
            return;
        };
        self.restore_disk_image().await;
    }

    async fn restore_disk_image(&self) -> Option<()> {
        let imp = self.imp();
        let file = imp.restore_file.borrow().clone()?;
        let info = match file.query_info(
            &format!(
                "{},{}",
                gio::FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                gio::FILE_ATTRIBUTE_STANDARD_SIZE
            ),
            gio::FileQueryInfoFlags::empty(),
            gio::Cancellable::NONE,
        ) {
            Ok(info) => info,
            Err(err) => {
                libgdu::show_error(
                    self,
                    &gettext("Error determining size of file"),
                    Box::new(err),
                );
                return None;
            }
        };

        let mut input_size = info.size() as u64;
        let mut input_stream = match file.read(gio::Cancellable::NONE) {
            Ok(stream) => stream.into_read(),
            Err(err) => {
                libgdu::show_error(
                    self,
                    &gettext("Error opening file for reading"),
                    Box::new(err),
                );
                return None;
            }
        };

        let is_xz_compressed = info.content_type().unwrap().ends_with("-xz-compressed");
        let mut input_stream: &mut dyn Read = if is_xz_compressed {
            input_size = liblzma::uncompressed_size(&mut input_stream).ok()?;
            &mut liblzma::read::XzDecoder::new(input_stream)
        } else {
            &mut input_stream
        };

        let application = self.application().expect("`application` should be set");
        let inhibit_cookie = application.inhibit(
            self.native().and_downcast_ref::<gtk::Window>(),
            gtk::ApplicationInhibitFlags::SUSPEND | gtk::ApplicationInhibitFlags::LOGOUT,
            // Translators: Reason why suspend/logout is being inhibited
            Some(&pgettext(
                "restore-inhibit-message",
                "Copying disk image to device",
            )),
        );
        self.imp().inhibit_cookie.set(Some(inhibit_cookie));

        //TODO: create udisks job
        // self.local_job =

        let block = self.imp().block.take().unwrap();
        let res = self
            .copy_disk_image(block, &mut input_stream, input_size)
            .await;
        self.play_complete_sound();
        //TODO: update job
        application.uninhibit(inhibit_cookie);
        if let Err(err) = res {
            libgdu::show_error(self, &gettext("Error restoring disk image"), err);
        } else {
            // sucessfully written image to device
            self.update_job(None, true);
        }
        //TODO: set completed
        self.set_visible(false);
        self.close();
        None
    }

    async fn copy_disk_image(
        &self,
        block: udisks::block::BlockProxy<'static>,
        input_stream: &mut impl std::io::Read,
        input_size: u64,
        // we return a boxed error so we can return different error types
        // we don't use anyhow here, as the show error function expects a box
    ) -> Result<(), Box<dyn std::error::Error>> {
        let fd = block
            .open_for_restore(udisks::standard_options(false))
            .await
            .map_err(Box::new)?;

        // We can't use udisks_block_get_size() because the media may have
        // changed and udisks may not have noticed. TODO: maybe have a
        // Block.GetSize() method instead...

        // https://github.com/topjohnwu/Magisk/blob/33f70f8f6df24f66f7da9ad855cd4f7fe72c37a9/native/src/base/files.rs#L908
        #[cfg(target_pointer_width = "32")]
        const BLKGETSIZE64: u64 = 0x80041272;
        #[cfg(target_pointer_width = "64")]
        const BLKGETSIZE64: u64 = 0x80081272;

        //TODO: use safe version
        let mut block_device_size = 0;
        if unsafe { libc::ioctl(fd.as_raw_fd(), BLKGETSIZE64, &mut block_device_size) } != 0 {
            log::error!("Error determining size of device");
            return Err(Box::new(std::io::Error::from(
                std::io::ErrorKind::InvalidData,
            )));
        }

        if block_device_size == 0 {
            log::error!("Device is size 0");
            return Err(Box::new(std::io::Error::from(
                std::io::ErrorKind::InvalidData,
            )));
        }
        self.imp().block_size.set(block_device_size as u64);

        // default to 1 MiB blocks
        const BUFFER_SIZE: usize = 1024 * 1024;
        let mut page_buffer = PageAlignedBuffer::new(BUFFER_SIZE);
        let buffer = page_buffer.as_mut_slice();

        let estimator = estimator::GduEstimator::new(input_size);

        // Read huge (e.g. 1 MiB) blocks and write it to the output device even if it was only
        // partially read
        let mut bytes_completed = 0;
        let update_interval = std::time::Duration::from_millis(200);
        // set initial timer back by the update interval, so the UI is refreshed on the first cycle
        let update_timer = std::time::Instant::now().sub(update_interval);
        let mut device = std::fs::File::from(fd.as_fd().try_clone_to_owned().unwrap());
        let copy_result = loop {
            // Update GUI - but only every 200ms and if the last update isn't peding
            if update_timer.elapsed() >= update_interval {
                if bytes_completed > 0 {
                    estimator.add_sample(bytes_completed);
                }
                //TODO: add a progress bar?
                self.update_job(Some(&estimator), false)
            }

            //TODO: check if using kernel calls like std's (file) copy does is faster
            //or using BufWriter
            let read_bytes = match input_stream.read(buffer) {
                // we finished reading all bytes
                Ok(0) => break Ok(()),
                Ok(n) => n,
                Err(err) if err.kind() == ErrorKind::Interrupted => continue,
                Err(err) => break Err(Box::new(err)),
            };

            if let Err(err) = device.write_all(&buffer[..read_bytes]) {
                log::error!("Error writing to device: {}", err);
                break Err(Box::new(err));
            }
            bytes_completed += read_bytes as u64;
        };

        if copy_result.is_err() {
            //TODO: the C version checks for udisks (we handle them above) and GIO cancelled errors, but i'm not sure how
            //that's possible
            if let Err(err) = block.format("empty", HashMap::new()).await {
                log::error!("Error wiping device on error path: {err}");
            }
        }

        // request that the OS / kernel rescans the device
        if let Err(err) = block.rescan(HashMap::new()).await {
            log::error!("Error rescanning device: {}", err);
        };

        Ok(copy_result?)
    }

    fn update_job(&self, estimator: Option<&GduEstimator>, done: bool) {
        let (bytes_per_sec, usec_remaining, completed_bytes, target_bytes) =
            if let Some(estimator) = estimator {
                (
                    estimator.bytes_per_sec(),
                    estimator.usec_remaining(),
                    estimator.completed_bytes(),
                    estimator.target_bytes(),
                )
            } else {
                (0, 0, 0, 0)
            };
        //TODO: update job
    }

    #[template_callback]
    async fn on_file_chooser_button_clicked_cb(&self, _button: &gtk::Button) {
        let file_dialog = gtk::FileDialog::builder()
            .title(gettext("Choose a disk image to restore."))
            .build();
        if let Ok(file) = file_dialog.open_future(Some(self)).await {
            self.imp().restore_file.set(Some(file));
            self.update();
        }
    }
}

use sealed::PageAlignedBuffer;
// hide private struct fields
//TODO: possibly move to different file
mod sealed {
    /// A RAII buffer that is aligned to the page size of the system.
    pub struct PageAlignedBuffer {
        /// Memory layout of the buffer.
        layout: std::alloc::Layout,
        /// Reference to the page aligned memory.
        buffer: &'static mut [u8],
    }

    impl PageAlignedBuffer {
        /// Allocates a new buffer with aligned to the page size of the operating system.
        /// The allocated memory will be zeroed.
        #[must_use]
        pub fn new(size: usize) -> Self {
            unsafe {
                let page_size = libc::sysconf(libc::_SC_PAGESIZE) as usize;
                let layout = std::alloc::Layout::from_size_align(size, page_size)
                    .expect("Failed to create layout for buffer");
                let buffer_ptr = std::alloc::alloc_zeroed(layout);
                assert!(!buffer_ptr.is_null(), "Failed to alloc buffer memory");
                // SAFETY: we know that the pointer is valid, correctly aligned and has space for `BUFFER_SIZE`
                // bytes
                let buffer = std::slice::from_raw_parts_mut(buffer_ptr, size);
                Self { layout, buffer }
            }
        }

        /// Returns a mutable slice of the allocated buffer.
        pub fn as_mut_slice(&mut self) -> &mut [u8] {
            self.buffer
        }
    }

    impl Drop for PageAlignedBuffer {
        fn drop(&mut self) {
            unsafe {
                // SAFETY: as we only ever give out an exlcusive reference to the buffer and it's not
                // possible to clone, we can be sure that there exists no other reference to the
                // buffer.
                // As the buffer cannot be swapped out, it was created from self.layout.
                std::alloc::dealloc(self.buffer.as_mut_ptr(), self.layout);
            }
        }
    }
}
