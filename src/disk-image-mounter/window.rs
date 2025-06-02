use std::collections::HashMap;
use std::ffi::CString;
use std::fs::OpenOptions;
use std::os::fd::AsFd;
use std::path::PathBuf;

use adw::prelude::AdwDialogExt;
use anyhow::anyhow;
use anyhow::Context;
use gettextrs::gettext;
use gtk::prelude::FileExt;
use gtk::prelude::GtkWindowExt;
use gtk::prelude::WidgetExt;
use gtk::subclass::prelude::*;
use gtk::{gio, glib};
use udisks::zbus;
use udisks::zbus::zvariant::{OwnedObjectPath, Value};
use zbus::export::futures_util::StreamExt;

use crate::application::ImageMounterApplication;

/// Action to perform for the image.
#[derive(Debug, Default, Clone, Copy, glib::Enum)]
#[enum_type(name = "Action")]
pub enum Action {
    /// Open the image in Nautilus.
    #[default]
    #[enum_value(name = "Open in Files", nick = "open-in-files")]
    OpenInFiles,
    /// Open the image in Nautilus with write access.
    #[enum_value(
        name = "Open in Files with write access",
        nick = "open-in-files-writable"
    )]
    OpenInFilesWritable,
    /// Unmount the mounted image.
    #[enum_value(name = "Unmount the mounted image", nick = "unmount")]
    Unmount,
    /// Opens Disks to write the image.
    #[enum_value(name = "Open Disks to write", nick = "write")]
    Write,
    /// Opens Disk to inspect the image.
    #[enum_value(name = "Open Disks to inspect", nick = "inspect")]
    Inspect,
}

mod imp {
    use std::cell::{Cell, RefCell};

    use adw::{prelude::ActionRowExt, subclass::prelude::AdwApplicationWindowImpl};
    use gtk::{prelude::ObjectExt, prelude::WidgetExt};

    use crate::config;

    use super::*;

    #[derive(Debug, Default, gtk::CompositeTemplate, glib::Properties)]
    #[template(file = "window.ui")]
    #[properties(wrapper_type = super::ImageMounterWindow)]
    pub struct ImageMounterWindow {
        #[template_child]
        pub(super) status_page: TemplateChild<adw::StatusPage>,
        #[template_child]
        pub(super) open_files_row: TemplateChild<adw::ActionRow>,
        #[template_child]
        pub(super) unmount_row: TemplateChild<adw::ActionRow>,
        #[template_child]
        pub(super) open_files_edit_row: TemplateChild<adw::ActionRow>,
        #[template_child]
        pub(super) toast_overlay: TemplateChild<adw::ToastOverlay>,

        /// File of the disk image.
        #[property(get, set, construct_only)]
        pub(super) file: RefCell<Option<gio::File>>,
        /// Selected action to perform.
        #[property(get, set, builder(Action::default()))]
        pub(super) continue_action: Cell<Action>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for ImageMounterWindow {
        const NAME: &'static str = "ImageMounterWindow";
        type Type = super::ImageMounterWindow;
        type ParentType = adw::ApplicationWindow;

        fn class_init(klass: &mut Self::Class) {
            klass.bind_template();
            klass.bind_template_instance_callbacks();

            klass.install_property_action("win.action", "continue-action");
        }

        fn instance_init(obj: &glib::subclass::InitializingObject<Self>) {
            obj.init_template();
        }
    }

    #[glib::derived_properties]
    impl ObjectImpl for ImageMounterWindow {
        fn constructed(&self) {
            self.parent_constructed();
            let obj = self.obj();

            // Devel Profile
            if config::PROFILE != "release" {
                obj.add_css_class("devel");
            }

            let main_context = glib::MainContext::default();
            main_context.spawn_local(
                glib::clone!(@weak self as window => @default-return None, async move {
                    if !window.obj().mountable(window.file.borrow().as_ref()) {
                        window.status_page.set_description(Some(&gettext("Compressed image files are not mountable")));
                        window.obj().set_continue_action(Action::Write);
                        return None;
                    }

                    let object = window.obj().mounted_file_object()
                        .await?;
                    window.unmount_row.set_visible(true);
                    window.open_files_row.set_subtitle(&gettext("Open the mounted image"));

                    if object.block().await.ok()?.read_only().await.ok()? {
                        window.status_page.set_description(Some(&gettext("Already mounted read-only")));
                        window.open_files_edit_row.set_sensitive(false);
                    } else {
                        window.status_page.set_description(Some(&gettext("Already mounted")));
                        window.obj().set_continue_action(Action::Unmount);
                        window.open_files_row.set_sensitive(false);
                    }
                    None::<()>
                }),
            );
        }
    }

    impl WidgetImpl for ImageMounterWindow {}
    impl WindowImpl for ImageMounterWindow {}
    impl ApplicationWindowImpl for ImageMounterWindow {}
    impl AdwApplicationWindowImpl for ImageMounterWindow {}
}

glib::wrapper! {
    pub struct ImageMounterWindow(ObjectSubclass<imp::ImageMounterWindow>)
        @extends gtk::Widget, gtk::Window, gtk::ApplicationWindow, adw::ApplicationWindow,
        @implements gio::ActionMap, gio::ActionGroup, gtk::Root;
}

#[gtk::template_callbacks]
impl ImageMounterWindow {
    /// Creates a new [`ImageMounterWindow`] for the given disk image `file`.
    pub fn new(app: &ImageMounterApplication, file: &gio::File) -> Self {
        glib::Object::builder()
            .property("application", app)
            .property("file", file)
            .build()
    }

    /// Returns the button label, which should be displayed for the given `action`.
    #[template_callback]
    fn button_label(&self, action: Action) -> String {
        gettext(match action {
            Action::OpenInFiles => "Open in Files",
            Action::OpenInFilesWritable => "Edit Image",
            Action::Unmount => "Unmount",
            Action::Write => "Write to Drive…",
            Action::Inspect => "Inspect",
        })
    }

    /// Returns the display name of the given `file`.
    #[template_callback]
    fn window_title(&self, file: Option<&gio::File>) -> Option<String> {
        let info = file?
            .query_info(
                gio::FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                gio::FileQueryInfoFlags::empty(),
                gio::Cancellable::NONE,
            )
            .ok()?;
        Some(info.display_name().into())
    }

    /// Returns `true` when the given file is (directly) mountable.
    #[template_callback]
    fn mountable(&self, file: Option<&gio::File>) -> bool {
        let Some(file) = file else { return false };
        // may return wrong type, see https://github.com/gtk-rs/gtk-rs-core/issues/1257
        let (content_type, _uncertain) = gio::content_type_guess(file.path().as_ref(), &[]);
        // explicitly deny mime types, rather than allowing them, as some may be reported wrong
        // and we want to allow mounting obfuscated VeraCrypt images
        content_type != "application/x-raw-disk-image-xz-compressed"
    }

    /// Returns the [`udisks::Object`] corresponding to [`Self::file`].
    ///
    /// If the file is not mounted as a loopback device, None is returned.
    async fn mounted_file_object(&self) -> Option<udisks::Object> {
        let path = self.file().and_then(|file| file.path())?;
        let client = udisks::Client::new().await.ok()?;

        for object in client
            .object_manager()
            .get_managed_objects()
            .await
            .ok()?
            .into_iter()
            .filter_map(|(object_path, _)| client.object(object_path).ok())
        {
            let Ok(loop_proxy) = object.r#loop().await else {
                continue;
            };
            if loop_proxy
                .backing_file()
                .await
                .into_iter()
                .filter_map(|data| CString::from_vec_with_nul(data).ok())
                .filter_map(|data| data.into_string().ok())
                .map(PathBuf::from)
                .any(|mount_point| mount_point == path)
            {
                log::debug!("Found mounted file at {:?}", object.object_path());
                return Some(object);
            }
        }
        None
    }

    /// Reads the device filepath of the given [`udisks::Object`].
    async fn read_device(&self, object: &udisks::Object) -> anyhow::Result<String> {
        let block = object.block().await?;
        Ok(CString::from_vec_with_nul(block.device().await?)?
            .to_str()?
            .to_owned())
    }

    /// Returns a user-visible error message based on the action.
    fn user_visible_error_msg(&self) -> String {
        gettext(match self.continue_action() {
            Action::OpenInFiles | Action::OpenInFilesWritable => "Failed to mount file",
            Action::Unmount => "Failed to unmount file",
            Action::Write => "Failed to write image",
            Action::Inspect => "Failed to inspect image",
        })
    }

    #[template_callback]
    async fn on_continue_button(&self, button: &gtk::Button) {
        button.set_sensitive(false);
        let action_result = match self.continue_action() {
            Action::OpenInFiles => self.open_in_files(true).await,
            Action::OpenInFilesWritable => self.open_in_files(false).await,
            Action::Unmount => self.unmount().await,
            Action::Write => self.write_image().await,
            Action::Inspect => self.inspect().await,
        };

        button.set_sensitive(true);
        match action_result {
            Ok(_) => self.close(),
            Err(err) => {
                log::error!("{}", err);
                self.imp()
                    .toast_overlay
                    .add_toast(adw::Toast::new(&self.user_visible_error_msg()));
            }
        };
    }

    /// Opens the image in [Files](https://apps.gnome.org/Nautilus/).
    ///
    /// If the image is not yet mounted, it will be mounted first.
    async fn open_in_files(&self, read_only: bool) -> anyhow::Result<()> {
        let mut object = if let Some(object) = self.mounted_file_object().await {
            object
        } else {
            log::debug!("File not yet mounted, mounting first");
            self.mount(read_only).await?
        };

        if let Ok(encrypted) = object.encrypted().await {
            if encrypted.cleartext_device().await?.to_string() == "/" {
                // the encrypted file is already mounted, but not unlocked,
                // so we re-mount to prompt the system to show the unlock dialog again
                log::debug!(
                    "{} is already mounted, but not unlocked",
                    object.object_path()
                );
                self.unmount().await?;
                object = self.mount(read_only).await?;
            }
        }

        let client = udisks::Client::new().await?;
        let (Some(filesystem), _encrypted, _last) =
            libgdu::is_in_full_use(&client, &object, false).await?
        else {
            return Err(anyhow!("Failed to find filesystem"));
        };

        for mount_point in filesystem
            .mount_points()
            .await?
            .into_iter()
            .filter_map(|mount_point| CString::from_vec_with_nul(mount_point).ok())
            .filter_map(|mount_point| mount_point.to_str().map(|p| p.to_string()).ok())
        {
            log::debug!("Opening Files at {}", mount_point);
            let file = gio::File::for_path(&mount_point);
            let file_launcher = gtk::FileLauncher::new(Some(&file));
            if file_launcher
                .launch_future(gtk::Window::NONE)
                .await
                .is_err()
            {
                log::error!("Failed to open Files at {}", mount_point);
                continue;
            }
        }

        Ok(())
    }

    /// Opens the image in disks,
    /// allowing the users to see information about the image.
    ///
    /// If the image is not yet mounted, it will be mounted first.
    async fn inspect(&self) -> anyhow::Result<()> {
        let object = if let Some(object) = self.mounted_file_object().await {
            object
        } else {
            log::debug!("File not yet mounted, mounting first");
            self.mount(false).await?
        };
        let device = self.read_device(&object).await?;
        self.open_in_disks(device).await
    }

    /// Unmounts the disk image.
    async fn unmount(&self) -> anyhow::Result<()> {
        let mounted_object = self
            .mounted_file_object()
            .await
            .context("Failed to find mounted_object")?;

        let client = udisks::Client::new().await?;

        if let Err((err, _msg)) = libgdu::unuse_data_iterate(&client, &mounted_object).await {
            log::error!("Failed to unmount: {}", err);
            return Err(err.into());
        }
        log::info!("Successfully unmounted");

        mounted_object
            .r#loop()
            .await?
            .delete(udisks::standard_options(false))
            .await?;
        log::info!("Successfully deleted loop device");

        Ok(())
    }

    /// Mounts the disk image as a loop device.
    async fn mount(&self, read_only: bool) -> anyhow::Result<udisks::Object> {
        let client = udisks::Client::new().await?;
        let manager = client.manager();

        let path = self
            .file()
            .and_then(|file| file.path())
            .context("Failed to find file path")?;

        let file = OpenOptions::new()
            .read(true)
            .write(!read_only)
            .open(&path)?;

        let options = HashMap::from([("read-only", read_only.into())]);
        let object_path = manager.loop_setup(file.as_fd().into(), options).await?;
        log::info!("Mounted {} at {}", path.display(), object_path);

        // safe to unwrap, since the given path is already an object path
        let object = client.object(object_path).unwrap();

        if let Ok(encrypted) = object.encrypted().await {
            // wait until encrypted block has been unlocked, i.e. there is a cleartext device
            let mut cleartext_dev_changes = encrypted.receive_cleartext_device_changed().await;
            while let Some(dev) = cleartext_dev_changes.next().await {
                if dev.get().await.is_ok_and(|clear| clear.to_string() != "/") {
                    log::debug!("Encrypted device has been unlocked");
                    break;
                }
            }
        }
        Ok(object)
    }

    /// Writes the image to a disk.
    ///
    /// Opens the restore dialog from GNOME Disks, allowing the users to restore the contents of the
    /// disk image to a disk device.
    async fn write_image(&self) -> anyhow::Result<()> {
        let path = self
            .file()
            .and_then(|file| file.path())
            .context("Failed to find file path")?;
        let client = udisks::Client::new().await?;

        let dialog =
            gnome_disks::GduRestoreDiskImageDialog::show(self, None, client, path.to_str()).await;

        let (rx, tx) = async_channel::bounded(1);
        dialog.connect_closed(move |_dialog| {
            let rx = rx.clone();
            glib::spawn_future_local(async move {
                let _ = rx.send(()).await;
            });
        });
        tx.recv().await?;

        Ok(())
    }

    /// Opens the given `device` in [Disks](https://apps.gnome.org/DiskUtility/).
    async fn open_in_disks(&self, device: String) -> anyhow::Result<()> {
        let connection = zbus::Connection::session().await?;

        const EMPTY_ARR: &[&[u8]] = &[];

        // ('/org/gnome/DiskUtility', [], {'options': <{'block-device': <'/dev/loop0'>}>})
        connection
            .call_method(
                Some("org.gnome.DiskUtility"),
                "/org/gnome/DiskUtility",
                Some("org.gtk.Application"),
                "CommandLine",
                &(
                    OwnedObjectPath::try_from("/org/gnome/DiskUtility").unwrap(),
                    &EMPTY_ARR,
                    HashMap::<&str, &Value>::from([(
                        "options",
                        &Value::new(HashMap::<&str, Value>::from([(
                            "block-device",
                            device.into(),
                        )])),
                    )]),
                ),
            )
            .await?;
        Ok(())
    }
}
