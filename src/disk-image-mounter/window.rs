use std::collections::HashMap;
use std::ffi::CString;
use std::fs::OpenOptions;
use std::os::fd::AsFd;
use std::path::PathBuf;

use gettextrs::gettext;
use gtk::prelude::FileExt;
use gtk::prelude::GtkWindowExt;
use gtk::subclass::prelude::*;
use gtk::{gio, glib};
use udisks::zbus;
use udisks::zbus::zvariant::{OwnedObjectPath, Value};

use crate::application::ImageMounterApplication;
use crate::config;
use crate::error::ImageMounterError;
use crate::unmount;

#[derive(Debug, Default, Clone, Copy, glib::Enum)]
#[enum_type(name = "Action")]
pub enum Action {
    /// Open the image in Nautilus.
    #[default]
    #[enum_value(name = "Open in Files", nick = "open-in-files")]
    OpenInFiles,
    /// Open the image in Nautilus with write access
    #[enum_value(
        name = "Open in Files with write access",
        nick = "open-in-files-writable"
    )]
    OpenInFilesWritable,
    /// Unmount the mounted image
    #[enum_value(name = "Unmount the mounted image", nick = "unmount")]
    Unmount,
    /// Opens Disks to write the image
    #[enum_value(name = "Open Disks to write", nick = "write")]
    Write,
    /// Opens Disk to inspect the image
    #[enum_value(name = "Open Disks to inspect", nick = "inspect")]
    Inspect,
}

mod imp {
    use std::cell::{Cell, RefCell};

    use adw::subclass::prelude::AdwApplicationWindowImpl;
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
        pub(super) unmount_row: TemplateChild<adw::ActionRow>,
        #[template_child]
        pub(super) toast_overlay: TemplateChild<adw::ToastOverlay>,
        #[property(get, set, construct_only)]
        pub(super) file: RefCell<Option<gio::File>>,
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
            if config::PROFILE == "Devel" {
                obj.add_css_class("devel");
            }

            let main_context = glib::MainContext::default();
            main_context.spawn_local(
                glib::clone!(@weak self as window => @default-return None, async move {
                    let object = window.obj().mounted_file_object()
                        .await?;
                    let description = if object.block().await.ok()?.read_only().await.ok()? {
                        gettext("Already mounted read-only")
                    } else {
                        gettext("Already mounted")
                    };
                    window.status_page.set_description(Some(&description));
                    window.unmount_row.set_visible(true);
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
    pub fn new(app: &ImageMounterApplication, file: &gio::File) -> Self {
        glib::Object::builder()
            .property("application", app)
            .property("file", file)
            .build()
    }

    #[template_callback]
    fn button_label(&self, action: Action) -> String {
        gettext(match action {
            Action::OpenInFiles => "Open in Files",
            Action::OpenInFilesWritable => "Edit Image",
            Action::Unmount => "Unmount",
            Action::Write => "Write to Drive",
            Action::Inspect => "Inspect",
        })
    }

    #[template_callback]
    fn window_title(&self, file: Option<&gio::File>) -> Option<String> {
        let info = file?
            .query_info(
                gio::FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                gio::FileQueryInfoFlags::empty(),
                gio::Cancellable::NONE,
            )
            .ok()?;
        Some(info.display_name().to_string())
    }

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

    async fn read_device(&self, object: udisks::Object) -> Result<String, ImageMounterError> {
        let block = object.block().await?;
        Ok(CString::from_vec_with_nul(block.device().await?)?
            .to_str()?
            .to_owned())
    }

    #[template_callback]
    async fn on_continue_button(&self, _button: &gtk::Button) {
        let error = match self.continue_action() {
            Action::OpenInFiles => self
                .mount(true)
                .await
                .map_err(|_| gettext("Failed to mount file"))
                .err(),
            Action::OpenInFilesWritable => self
                .mount(false)
                .await
                .map_err(|_| gettext("Failed to mount file"))
                .err(),
            Action::Unmount => self
                .unmount()
                .await
                .map_err(|_| gettext("Failed to unmount file"))
                .err(),
            Action::Write => self
                .write_image()
                .map_err(|_| gettext("Failed to write image"))
                .err(),
            Action::Inspect => self
                .inspect()
                .await
                .map_err(|_| gettext("Failed to inspect image"))
                .err(),
        };

        if let Some(error_msg) = error {
            self.imp()
                .toast_overlay
                .add_toast(adw::Toast::new(&error_msg))
        } else if config::PROFILE != "Devel" {
            self.close();
        }
    }

    async fn inspect(&self) -> Result<(), ImageMounterError> {
        let device = if let Some(object) = self.mounted_file_object().await {
            log::debug!("File already mounted, reading device");
            self.read_device(object).await?
        } else {
            log::debug!("File not yet mounted, mounting first");
            self.mount(false).await?
        };
        self.open_in_disks(device).await
    }

    async fn unmount(&self) -> Result<(), ImageMounterError> {
        let mounted_object = self
            .mounted_file_object()
            .await
            .ok_or(ImageMounterError::File)?;

        let client = udisks::Client::new().await?;
        if let Err(err) = unmount::unuse_data_iterate(&client, &mounted_object).await {
            log::error!("Failed to unmount: {}", err);
            return Err(ImageMounterError::Zbus(err));
        }
        log::info!("Succesfully unmounted");

        Ok(())
    }

    async fn mount(&self, read_only: bool) -> Result<String, ImageMounterError> {
        let client = udisks::Client::new().await?;
        let manager = client.manager();

        let path = self
            .file()
            .and_then(|file| file.path())
            .ok_or(ImageMounterError::File)?;

        let file = OpenOptions::new()
            .read(true)
            .write(!read_only)
            .open(&path)?;

        let options = HashMap::from([("read-only", read_only.into())]);
        let object_path = manager.loop_setup(file.as_fd().into(), options).await?;
        log::info!("Mounted {} at {}", path.display(), object_path);

        //safe to unwrap, since the given path is
        //already an oject path
        let object = client.object(object_path).unwrap();
        let device = self.read_device(object).await?;
        Ok(device)
    }

    fn write_image(&self) -> Result<(), ImageMounterError> {
        let path = self
            .file()
            .and_then(|file| file.path())
            .ok_or(ImageMounterError::File)?;

        let mut child = std::process::Command::new("gnome-disks")
            .args(["--restore-disk-image", path.to_str().unwrap()])
            .spawn()
            .map_err(|_| ImageMounterError::File)?;
        if !child.wait()?.success() {
            return Err(ImageMounterError::File);
        }
        Ok(())
    }

    async fn open_in_disks(&self, device: String) -> Result<(), ImageMounterError> {
        let connection = zbus::Connection::session().await?;

        const EMPTY_ARR: &[&[u8]] = &[];

        //('/org/gnome/DiskUtility', [], {'options': <{'block-device': <'/dev/loop0'>}>})
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
