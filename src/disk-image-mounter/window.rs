use gtk::prelude::FileExt;
use gtk::subclass::prelude::*;
use gtk::{gio, glib};

use crate::application::ImageMounterApplication;

#[derive(Debug, Default, Clone, Copy, glib::Enum)]
#[enum_type(name = "Action")]
#[repr(i32)]
enum Action {
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
            let _obj = self.obj();

            // Devel Profile
            if config::PROFILE == "Devel" {
                // TODO: investigate
                // disabled, causes a GTK Criticial
                // obj.add_css_class("devel");
            }
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

    #[template_callback]
    fn on_continue_button(&self, _button: &gtk::Button) {
        unimplemented!()
    }
}
