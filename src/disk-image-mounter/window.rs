use std::collections::HashMap;
use std::ffi::CString;
use std::fs::OpenOptions;
use std::os::fd::AsFd;
use std::path::PathBuf;

use gtk::prelude::FileExt;
use gtk::prelude::GtkWindowExt;
use gtk::subclass::prelude::*;
use gtk::{gio, glib};
use udisks::zbus;
use udisks::zbus::zvariant::{OwnedObjectPath, Value};

use crate::application::ImageMounterApplication;

#[derive(Debug, Default, Clone, Copy, glib::Enum)]
#[enum_type(name = "Action")]
#[repr(i32)]
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
    use gtk::prelude::ObjectExt;

    use crate::config;

    use super::*;

    #[derive(Debug, Default, gtk::CompositeTemplate, glib::Properties)]
    #[template(file = "window.ui")]
    #[properties(wrapper_type = super::ImageMounterWindow)]
    pub struct ImageMounterWindow {
        #[template_child]
        pub(super) status_page: TemplateChild<adw::StatusPage>,
        #[property(get, set)]
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

            // Devel Profile
            if config::PROFILE == "Devel" {
                // TODO: investigate
                // disabled, causes a GTK Criticial
                // obj.add_css_class("devel");
            }

            let main_context = glib::MainContext::default();
            main_context.spawn_local(
                glib::clone!(@weak self as window => @default-return None, async move {
                    let object = window.obj().mounted_file_object_path()
                        .await?;
                    let description = if object.block().await.ok()?.read_only().await.ok()? {
                        gettextrs::gettext("Already mounted read-only")
                    } else {
                        gettextrs::gettext("Already mounted")
                    };
                    window.status_page.set_description(Some(&description));
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
        gettextrs::gettext(match action {
            Action::OpenInFiles => "Open in Files",
            Action::OpenInFilesWritable => "Edit Image",
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

    async fn mounted_file_object_path(&self) -> Option<udisks::Object> {
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
                return Some(object);
            }
        }
        None
    }

    #[template_callback]
    fn on_continue_button(&self, _button: &gtk::Button) {
        let main_context = glib::MainContext::default();
        main_context.spawn_local(glib::clone!(@weak self as window => async move {
            let action = window.continue_action();
            match action {
                Action::OpenInFiles => {
                    window.mount(true).await.expect("Failed to read-only mount");
                }
                Action::OpenInFilesWritable => {
                    window.mount(false).await.expect("Failed to mount");
                }
                Action::Write => {unimplemented!()}
                Action::Inspect => {
                    unimplemented!()
                }
            };
            window.close();
        }));
    }

    async fn mount(&self, read_only: bool) -> Result<String, Box<dyn std::error::Error>> {
        let client = udisks::Client::new().await?;
        let manager = client.manager();

        let path = self
            .file()
            .and_then(|file| file.path())
            .ok_or("Failed to open file")?;
        let file = OpenOptions::new()
            .read(true)
            .write(!read_only)
            .open(&path)?;

        let options = HashMap::from([("read-only", read_only.into())]);
        let obj_path = manager.loop_setup(file.as_fd().into(), options).await?;
        log::info!("Mounted {}", path.display());

        let block = client.object(obj_path)?.block().await?;
        let device = CString::from_vec_with_nul(block.device().await?)?
            .to_str()?
            .to_owned();

        Ok(device)
    }
}
