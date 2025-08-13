use std::collections::HashMap;
use std::io::{ErrorKind, Read};
use std::ops::Sub;
use std::os::fd::{AsRawFd, OwnedFd};

use adw::prelude::*;
use async_std::io::{ReadExt, WriteExt};
use gettextrs::{gettext, pgettext};
use gtk::glib::property::PropertySet;
use gtk::subclass::prelude::*;
use gtk::{gio, glib};
use libgdu::ConfirmationDialogResponse;
use libgdu::gettext::gettext_f;

use crate::estimator::{self, Estimator};
use crate::ffi;
use crate::page_aligned_buffer::PageAlignedBuffer;

/// Device size in bytes of the block device from `fd`.
///
/// # Errors
///
/// Returns an error, if the given file descriptor is not for a block device.
fn device_size(fd: &OwnedFd) -> std::io::Result<u64> {
    // Defined in Linux/fs.h
    const BLKGETSIZE64_CODE: u8 = 0x12;
    const BLKGETSIZE64_SEQ: u8 = 114;
    nix::ioctl_read!(blkgetsize64, BLKGETSIZE64_CODE, BLKGETSIZE64_SEQ, u64);

    let mut block_device_size = 0;
    if unsafe { blkgetsize64(fd.as_raw_fd(), &mut block_device_size) } != Ok(0) {
        log::error!("Error determining size of device");
        return Err(std::io::Error::from(std::io::ErrorKind::InvalidInput));
    }

    Ok(block_device_size)
}

mod imp {
    use std::{
        cell::{Cell, RefCell},
        rc::Rc,
    };

    use adw::subclass::dialog::AdwDialogImpl;

    use crate::{config, gdu_combo_row::GduComboRow, localjob::LocalJob};

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
        pub(super) local_job: RefCell<Option<Rc<LocalJob>>>,

        #[template_child]
        pub(super) size_row: TemplateChild<adw::ActionRow>,
        #[template_child]
        pub(super) start_restore_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub(super) image_row: TemplateChild<adw::ActionRow>,
        #[template_child]
        pub(super) file_chooser_button: TemplateChild<gtk::Button>,
        #[template_child]
        pub(super) destination_row: TemplateChild<GduComboRow>,
        #[template_child]
        pub(super) error_banner: TemplateChild<adw::Banner>,
        #[template_child]
        pub(super) warning_banner: TemplateChild<adw::Banner>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for GduRestoreDiskImageDialog {
        const NAME: &'static str = "GduRestoreDiskImageDialog";
        type Type = super::GduRestoreDiskImageDialog;
        type ParentType = adw::Dialog;

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
            if config::PROFILE != "release" {
                obj.add_css_class("devel");
            }
        }

        fn dispose(&self) {
            self.dispose_template();

            if let Some(cookie) = self.inhibit_cookie.take() {
                self.obj()
                    .window()
                    .expect("`window` should be available")
                    .application()
                    .expect("`application` should be set")
                    .uninhibit(cookie);
            }

            if let Some(job) = self.local_job.take() {
                ffi::destroy_local_job(job);
            }
        }
    }

    impl WidgetImpl for GduRestoreDiskImageDialog {}
    impl AdwDialogImpl for GduRestoreDiskImageDialog {}
}

glib::wrapper! {
    pub struct GduRestoreDiskImageDialog(ObjectSubclass<imp::GduRestoreDiskImageDialog>)
        @extends gtk::Widget, adw::Dialog,
        @implements gio::ActionMap, gio::ActionGroup, gtk::Root;
}

#[gtk::template_callbacks]
impl GduRestoreDiskImageDialog {
    pub async fn show(
        parent_window: &impl IsA<gtk::Widget>,
        object: Option<&udisks::Object>,
        client: udisks::Client,
        disk_image_filename: Option<&str>,
    ) -> Self {
        let dialog: Self = glib::Object::new();
        let imp = dialog.imp();
        imp.client.replace(Some(client));

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
                .set_subtitle(&info.one_liner.unwrap_or_default());
            dialog.set_destination_object(object.clone()).await;
        } else {
            imp.destination_row.remove_css_class("property");
            dialog.populate_destination_combobox().await;
        }

        dialog.display_size_warning();
        dialog.present(parent_window);
        dialog
    }

    /// Sets the [`destination`] drive to the given object.
    async fn set_destination_object(&self, object: udisks::Object) {
        let imp = self.imp();
        if imp.object.borrow().as_ref().map(|v| v.object_path()) == Some(object.object_path()) {
            // object is the same as is currently displayed, nothing to update
            return;
        }

        let block = object.block().await.expect("`block` should be Ok");
        let drive = self.client().drive_for_block(&block).await;
        imp.object.replace(Some(object));
        imp.drive.replace(drive.ok());

        //TODO: use a method call for this so it works on e.g. floppy drives where e.g. we don't know the size
        imp.block_size.set(block.size().await.unwrap_or_default());
        imp.block.replace(Some(block));
    }

    /// Returns a the [`gtk::Window`] of the dialog.
    fn window(&self) -> Option<gtk::Window> {
        self.ancestor(gtk::Window::static_type()).and_downcast()
    }

    /// Displays an error banner, if the disk image is too large,
    /// or a warning banner if the disk image is much smaller, than the target device.
    fn display_size_warning(&self) -> Option<()> {
        let imp = self.imp();
        let client = self.client();
        let mut restore_warning = None;
        let mut restore_error = None;

        let Some(file) = &*imp.restore_file.borrow() else {
            imp.start_restore_button.set_sensitive(false);
            return None;
        };

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
                restore_error = Some(gettext("File does not appear to be XZ compressed"));
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
            [client.size_for_display(size, false, true)],
        );

        let block_left_over_size = imp.block_size.get() as i64 - size as i64;
        if size == 0 {
            restore_error = Some(gettext("Cannot restore image of size 0"));
        } else if block_left_over_size > 1000 * 1000 {
            // Only complain if slack is bigger than 1MB
            restore_warning = Some(gettext_f(
                "The disk image is {} smaller than the target device",
                [client.size_for_display(block_left_over_size.unsigned_abs(), false, false)],
            ));
        } else if size > imp.block_size.get() {
            restore_error = Some(gettext_f(
                "The disk image is {} bigger than the target device",
                [client.size_for_display(size - imp.block_size.get(), false, false)],
            ));
        }

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
        imp.size_row.set_subtitle(&size_str);

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
            //TODO: clean this up once if-let chains get stabilized
            let block = if let Ok(drive) = object.drive().await {
                let Some(block) = client.block_for_drive(&drive, false).await else {
                    continue;
                };
                block
            } else if let Ok(block) = object.block().await {
                if self.should_display(&object).await == Ok(true) {
                    block
                } else {
                    continue;
                }
            } else {
                continue;
            };

            let info = client.object_info(&object).await;
            drive_names.append(&info.one_liner.unwrap());
            let object = client.object(block.inner().path().to_owned()).unwrap();
            drives.push(object);
        }

        //TODO: names are truncated
        self.imp().destination_row.set_model(Some(&drive_names));
        self.imp().destination_drives.replace(drives);
    }

    //NOTE: this should be kept in sync with `src/disks/gdu-manager.c`
    async fn should_display(&self, object: &udisks::Object) -> udisks::Result<bool> {
        let block = object.block().await?;
        if libgdu::has_userspace_mount_option(&block, "x-gdu.hide").await {
            return Ok(false);
        }

        // skip RAM devices
        let device = block.device().await?;
        if device.starts_with(b"/dev/ram") || device.starts_with(b"/dev/zram") {
            return Ok(false);
        }

        // skip if loop is zero-sized (unused)
        // Note that we _do_ want to show any other device of size 0 since
        // that's a good hint that the system may be misconfigured and
        // attention is needed.
        let size = block.size().await?;
        let loop_proxy = object.r#loop().await;
        if size == 0 && loop_proxy.is_ok() {
            return Ok(false);
        }

        // only include devices that are top-level
        if object.partition().await.is_ok() {
            return Ok(false);
        }

        // skip if already shown as a drive
        let drive = block.drive().await?;
        if drive.as_str() != "/" {
            return Ok(false);
        }

        // skip if already shown as an unlocked device
        let crypto_backing_device = block.crypto_backing_device().await?;
        if crypto_backing_device.as_str() != "/" {
            return Ok(false);
        }

        Ok(true)
    }

    #[template_callback]
    async fn destination_row_selected_cb(
        &self,
        _param: &glib::ParamSpec,
        combo_row: &adw::ComboRow,
    ) {
        let selected_drive =
            self.imp().destination_drives.borrow()[combo_row.selected() as usize].clone();
        self.set_destination_object(selected_drive).await;
        self.display_size_warning();
    }

    /// Returns the [udisks::Client].
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
        let imp = self.imp();
        let object = imp.object.borrow().clone().unwrap();
        let affected_devices_widget =
            libgdu::create_widget_from_objects(&self.client(), &[&object]).await;

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

        self.restore_disk_image(&object).await;
    }

    /// Restores the disk image to the selected destination drive.
    async fn restore_disk_image(&self, object: &udisks::Object) -> Option<()> {
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
                libgdu::show_error(self, &gettext("Error determining size of file"), err.into())
                    .await;
                return None;
            }
        };

        let mut input_size = info.size() as u64;
        let mut input_stream = match file.read(gio::Cancellable::NONE) {
            Ok(stream) => stream.into_read(),
            Err(err) => {
                libgdu::show_error(self, &gettext("Error opening file for reading"), err.into())
                    .await;
                return None;
            }
        };

        let is_xz_compressed = info.content_type()?.ends_with("-xz-compressed");
        let input_stream: &mut dyn Read = if is_xz_compressed {
            input_size = liblzma::uncompressed_size(&mut input_stream).ok()?;
            &mut liblzma::read::XzDecoder::new(input_stream)
        } else {
            &mut input_stream
        };

        let application = self
            .window()?
            .application()
            .expect("`application` should be set");
        let inhibit_cookie = application.inhibit(
            self.native().and_downcast_ref::<gtk::Window>(),
            gtk::ApplicationInhibitFlags::SUSPEND | gtk::ApplicationInhibitFlags::LOGOUT,
            // Translators: Reason why suspend/logout is being inhibited
            Some(&pgettext(
                "restore-inhibit-message",
                "Copying disk image to device",
            )),
        );
        imp.inhibit_cookie.set(Some(inhibit_cookie));

        libgdu::ensure_unused(&self.client(), &self.window()?, object)
            .await
            .ok()?;

        let local_job = ffi::create_local_job(object);
        local_job.set_operation("x-gdu-restore-disk-image");
        // Translators: this is the description of the job
        local_job.set_description(gettext("Restoring Disk Image"));
        local_job.set_progress_valid(true);
        local_job.set_cancelable(true);
        imp.local_job.replace(Some(local_job));

        let block = imp.block.borrow().clone()?;
        let res = self
            .copy_disk_image(
                block,
                &mut futures::io::AllowStdIo::new(input_stream),
                input_size,
            )
            .await;

        self.play_complete_sound();
        application.uninhibit(imp.inhibit_cookie.take()?);

        if let Err(err) = res {
            libgdu::show_error(self, &gettext("Error restoring disk image"), err).await;
        } else {
            // successfully written image to device
            self.update_job(None, true);
            // clear job
            if let Some(job) = imp.local_job.take() {
                ffi::destroy_local_job(job);
            }
        }

        self.set_visible(false);
        self.close();
        Some(())
    }

    /// Copies the disk image from the `input_stream` to the given block device.
    async fn copy_disk_image(
        &self,
        block: udisks::block::BlockProxy<'static>,
        input_stream: &mut (impl async_std::io::Read + std::marker::Unpin),
        input_size: u64,
        // we return a boxed error so we can return different error types
        // we don't use anyhow here, as the show error function expects a box
    ) -> Result<(), Box<dyn std::error::Error>> {
        let fd: std::os::fd::OwnedFd = block
            .open_for_restore(udisks::standard_options(false))
            .await?
            .into();

        // We can't use udisks_block_get_size() because the media may have
        // changed and udisks may not have noticed. TODO: maybe have a
        // Block.GetSize() method instead...
        let block_device_size = device_size(&fd)?;

        if block_device_size == 0 {
            log::error!("Device is size 0");
            return Err(std::io::Error::from(std::io::ErrorKind::InvalidData).into());
        }
        self.imp().block_size.set(block_device_size as u64);

        // default to 1 MiB blocks
        const BUFFER_SIZE: usize = 1024 * 1024;
        let mut page_buffer = PageAlignedBuffer::new(BUFFER_SIZE);
        let buffer_slice = page_buffer.as_mut_slice();

        let estimator = estimator::Estimator::new(input_size);

        // Read huge (e.g. 1 MiB) blocks and write it to the output device even if it was only
        // partially read
        let mut bytes_completed = 0;
        let update_interval = std::time::Duration::from_millis(200);
        // set initial timer back by the update interval, so the UI is refreshed on the first cycle
        let update_timer = std::time::Instant::now().sub(update_interval);
        let mut device = async_std::fs::File::from(std::fs::File::from(fd));
        let copy_result: Result<(), std::io::Error> = loop {
            // update GUI
            if update_timer.elapsed() >= update_interval {
                estimator.add_sample(bytes_completed);
                //TODO: add a progress bar?
                self.update_job(Some(&estimator), false)
            }

            //TODO: check if using kernel calls like std's (file) copy does is faster
            //or using BufWriter
            let read_bytes = match input_stream.read(buffer_slice).await {
                // we finished reading all bytes
                Ok(0) => break Ok(()),
                Ok(n) => n,
                Err(err) if err.kind() == ErrorKind::Interrupted => continue,
                Err(err) => break Err(err),
            };

            if let Err(err) = device.write_all(&buffer_slice[..read_bytes]).await {
                log::error!("Error writing to device: {}", err);
                break Err(err);
            }
            bytes_completed += read_bytes as u64;
        };

        if copy_result.is_err() {
            if let Err(err) = block.format("empty", HashMap::new()).await {
                log::error!("Error wiping device on error path: {err}");
            }
        }

        // request that the OS / kernel re-scans the device
        if let Err(err) = block.rescan(HashMap::new()).await {
            log::error!("Error rescanning device: {}", err);
        };

        Ok(copy_result?)
    }

    fn update_job(&self, estimator: Option<&Estimator>, done: bool) {
        let Some(ref mut job) = *self.imp().local_job.borrow_mut() else {
            return;
        };

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

        job.set_bytes(target_bytes);
        job.set_rate(bytes_per_sec);

        let progress = if done {
            1.0
        } else if target_bytes != 0 {
            completed_bytes as f64 / target_bytes as f64
        } else {
            0.0
        };
        job.set_progress(progress);

        let end_time = if usec_remaining == 0 {
            0
        } else {
            usec_remaining + glib::real_time() as u64
        };
        job.set_expected_end_time(end_time);
    }

    #[template_callback]
    async fn on_file_chooser_button_clicked_cb(&self, _button: &gtk::Button) {
        let file_dialog = gtk::FileDialog::builder()
            .title(gettext("Choose a disk image to restore."))
            .build();
        if let Ok(file) = file_dialog.open_future(self.window().as_ref()).await {
            self.imp().restore_file.set(Some(file));
            self.display_size_warning();
        }
    }
}
