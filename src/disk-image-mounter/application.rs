use gtk::prelude::*;
use gtk::subclass::prelude::*;
use gtk::{gio, glib};

use crate::config;

mod imp {
    use super::*;
    use crate::window::ImageMounterWindow;
    use adw::subclass::prelude::AdwApplicationImpl;

    #[derive(Debug, Default)]
    pub struct ImageMounterApplication {}

    #[glib::object_subclass]
    impl ObjectSubclass for ImageMounterApplication {
        const NAME: &'static str = "ImageMounterApplication";
        type Type = super::ImageMounterApplication;
        type ParentType = adw::Application;
    }

    impl ObjectImpl for ImageMounterApplication {}

    impl ApplicationImpl for ImageMounterApplication {
        fn activate(&self) {
            log::debug!("AdwApplication<ImageMounterApplication>::activate");
        }

        fn open(&self, files: &[gio::File], _hint: &str) {
            for file in files {
                if !file.query_exists(gio::Cancellable::NONE) {
                    log::error!(
                        "File {:?} does not exists",
                        file.path().as_ref().map(|path| path.display())
                    );
                    continue;
                }
                ImageMounterWindow::new(&self.obj(), file).present();
            }
        }

        fn startup(&self) {
            log::debug!("AdwApplication<ImageMounterApplication>::startup");
            self.parent_startup();
            let app = self.obj();

            gtk::Window::set_default_icon_name("drive-removable-media");

            app.setup_gactions();
            app.setup_accels();
            app.activate();
        }
    }

    impl GtkApplicationImpl for ImageMounterApplication {}
    impl AdwApplicationImpl for ImageMounterApplication {}
}

glib::wrapper! {
    pub struct ImageMounterApplication(ObjectSubclass<imp::ImageMounterApplication>)
        @extends gio::Application, gtk::Application, adw::Application,
        @implements gio::ActionMap, gio::ActionGroup;
}

#[gtk::template_callbacks]
impl ImageMounterApplication {
    fn setup_gactions(&self) {
        let action_quit = gio::ActionEntry::builder("quit")
            .activate(move |app: &Self, _, _| {
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
        log::info!("Version: {} ({})", config::VERSION, config::PROFILE);
        log::info!("Datadir: {}", config::PKGDATADIR);

        ApplicationExtManual::run(self)
    }
}

impl Default for ImageMounterApplication {
    fn default() -> Self {
        glib::Object::builder()
            .property("application-id", config::APP_ID)
            .property(
                "flags",
                gio::ApplicationFlags::HANDLES_OPEN | gio::ApplicationFlags::NON_UNIQUE,
            )
            .build()
    }
}
