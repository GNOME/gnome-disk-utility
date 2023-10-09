use gtk::subclass::prelude::*;
use gtk::{gio, glib};

use crate::application::ImageMounterApplication;
use crate::config::PROFILE;

mod imp {
    use adw::subclass::prelude::AdwApplicationWindowImpl;

    use super::*;

    #[derive(Debug, Default, gtk::CompositeTemplate)]
    #[template(file = "window.ui")]
    pub struct ImageMounterWindow {
        #[template_child]
        pub(super) icon: TemplateChild<gtk::Image>,
        #[template_child]
        pub(super) file_name_label: TemplateChild<gtk::Label>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for ImageMounterWindow {
        const NAME: &'static str = "ImageMounterWindow";
        type Type = super::ImageMounterWindow;
        type ParentType = adw::ApplicationWindow;

        fn class_init(klass: &mut Self::Class) {
            klass.bind_template();
            klass.bind_template_instance_callbacks();
        }

        fn instance_init(obj: &glib::subclass::InitializingObject<Self>) {
            obj.init_template();
        }
    }

    impl ObjectImpl for ImageMounterWindow {
        fn constructed(&self) {
            self.parent_constructed();
            let _obj = self.obj();

            // Devel Profile
            if PROFILE == "Devel" {
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
    pub fn new(app: &ImageMounterApplication) -> Self {
        glib::Object::builder().property("application", app).build()
    }
}
