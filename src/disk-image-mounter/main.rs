use application::ImageMounterApplication;
use config::{GETTEXT_PACKAGE, LOCALEDIR};
use gettextrs::{gettext, LocaleCategory};
use gtk::glib;

pub mod application;
mod config;
pub mod window;

fn main() -> glib::ExitCode {
    env_logger::Builder::from_default_env()
        .format_timestamp_millis()
        .init();

    // Prepare i18n
    gettextrs::setlocale(LocaleCategory::LcAll, "");
    gettextrs::bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR).expect("Unable to bind the text domain");
    gettextrs::textdomain(GETTEXT_PACKAGE).expect("Unable to switch to the text domain");

    glib::set_application_name(&gettext("GNOME Disk Image Mounter"));

    let app = ImageMounterApplication::default();
    app.run()
}
