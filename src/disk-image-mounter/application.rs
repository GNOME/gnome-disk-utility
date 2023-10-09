use gtk::prelude::*;
use gtk::subclass::prelude::*;
use gtk::{gio, glib};

use crate::config::{PKGDATADIR, PROFILE, VERSION};
use crate::window::ImageMounterWindow;

mod imp {

    use crate::window::ImageMounterWindow;

    use super::*;
    use glib::WeakRef;
    use std::cell::OnceCell;

    #[derive(Debug, Default)]
    pub struct ImageMounterApplication {
        pub window: OnceCell<WeakRef<ImageMounterWindow>>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for ImageMounterApplication {
        const NAME: &'static str = "ImageMounterApplication";
        type Type = super::ImageMounterApplication;
        type ParentType = gtk::Application;
    }

    impl ObjectImpl for ImageMounterApplication {}

    impl ApplicationImpl for ImageMounterApplication {
        fn activate(&self) {
            log::debug!("GtkApplication<ImageMounterApplication>::activate");
            self.parent_activate();
            let app = self.obj();

            if let Some(window) = self.window.get() {
                let window = window.upgrade().unwrap();
                window.present();
                return;
            }

            let window = ImageMounterWindow::new(&app);
            self.window
                .set(window.downgrade())
                .expect("Window already set.");

            app.main_window().present();
        }

        fn open(&self, files: &[gio::File], _hint: &str) {
            files
                .iter()
                .for_each(|file| log::debug!("Path: {:?}", file.peek_path()))
        }

        fn startup(&self) {
            log::debug!("GtkApplication<ImageMounterApplication>::startup");
            self.parent_startup();
            let app = self.obj();

            gtk::Window::set_default_icon_name("drive-removable-media");

            app.setup_gactions();
            app.setup_accels();
            app.activate();
        }
    }

    impl GtkApplicationImpl for ImageMounterApplication {}
}

glib::wrapper! {
    pub struct ImageMounterApplication(ObjectSubclass<imp::ImageMounterApplication>)
        @extends gio::Application, gtk::Application,
        @implements gio::ActionMap, gio::ActionGroup;
}

impl ImageMounterApplication {
    fn main_window(&self) -> ImageMounterWindow {
        self.imp().window.get().unwrap().upgrade().unwrap()
    }

    fn setup_gactions(&self) {
        let action_quit = gio::ActionEntry::builder("quit")
            .activate(move |app: &Self, _, _| {
                app.main_window().close();
                app.quit();
            })
            .build();

        self.add_action_entries([action_quit]);
    }

    fn setup_accels(&self) {
        self.set_accels_for_action("app.quit", &["<Control>q"]);
        self.set_accels_for_action("window.close", &["<Control>w"]);
    }

    pub fn run(&self) -> glib::ExitCode {
        log::info!("Disk Image Mounter");
        log::info!("Version: {} ({})", VERSION, PROFILE);
        log::info!("Datadir: {}", PKGDATADIR);

        ApplicationExtManual::run(self)
    }
}

impl Default for ImageMounterApplication {
    fn default() -> Self {
        glib::Object::builder()
            .property("resource-base-path", "/org/gnome/gnome-disk-utility/")
            .property("flags", gio::ApplicationFlags::HANDLES_OPEN)
            .build()
    }
}
