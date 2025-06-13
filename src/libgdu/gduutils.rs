//! Rust version of [./libgdu.h]
// NOTE: Keep this file in sync with gduutils.c
use std::collections::HashMap;
use std::ffi::CString;
use std::mem::MaybeUninit;
use std::sync::OnceLock;

use adw::prelude::*;
use async_recursion::async_recursion;
use futures::StreamExt;
use gettextrs::{dngettext, gettext, pgettext};
use gtk::{
    gio,
    glib::{self, object::IsA},
};
use itertools::Itertools;
use tokio::sync::Mutex;
use udisks::zbus::{
    self,
    zvariant::{self, OwnedValue},
};

use crate::config::GETTEXT_PACKAGE;
use crate::enums::{GduFormatDurationFlags, ResizeFlags, UnitSize};
use crate::gettext::pgettext_f;

pub const PARTITION_COLORS: [&str; 7] = [
    "blue", "green", "yellow", "orange", "red", "purple", "brown",
];

pub async fn has_configuration(
    block: &udisks::block::BlockProxy<'static>,
    config_type: &str,
) -> udisks::Result<(bool, bool)> {
    let config = block.configuration().await?;

    let mut has_passphrase = false;
    for (ty, config_details) in config {
        if ty != config_type {
            continue;
        }

        if config_type == "crypttab" {
            has_passphrase = config_details
                .get("passphrase-path")
                .and_then(string_from_array)
                .is_some_and(|path| !path.is_empty() && !path.starts_with("/dev"));
        }
        return Ok((true, has_passphrase));
    }
    Ok((false, has_passphrase))
}

pub async fn has_userspace_mount_option(
    block: &udisks::block::BlockProxy<'static>,
    option: &str,
) -> bool {
    block
        .userspace_mount_options()
        .await
        .iter()
        .flatten()
        .any(|val| val == option)
}

pub async fn configure_file_dialog_for_disks_images(
    file_dialog: gtk::FileDialog,
    set_file_types: bool,
    allow_compressed: bool,
) {
    // safe to unwrap as it should fallback to $HOME/Desktop if Documents have not been set up
    let folder =
        gio::File::for_path(glib::user_special_dir(glib::UserDirectory::Documents).unwrap());
    file_dialog.set_initial_folder(Some(&folder));

    if set_file_types {
        let filters = gio::ListStore::new::<gtk::FileFilter>();

        let filter = gtk::FileFilter::new();
        filter.set_name(Some(&gettext("All Files")));
        filter.add_pattern("*");
        filters.append(&filter);

        let filter = gtk::FileFilter::new();
        filter.set_name(Some(&gettext(if allow_compressed {
            "Disk Images (*.img, *.img.xz, *.iso)"
        } else {
            "Disk Images (*.img, *.iso)"
        })));
        filter.add_mime_type("application/x-raw-disk-image");
        if allow_compressed {
            filter.add_mime_type("application/x-raw-disk-image-xz-compressed");
        }
        filter.add_mime_type("application/x-cd-image");
        filters.append(&filter);

        file_dialog.set_filters(Some(&filters));
    }
}

pub fn file_chooser_for_disk_images_set_default_folder(folder: gio::File) {
    let folder_uri = folder.uri();
    // TODO: use a constant for schema_id
    let settings = gio::Settings::new("org.gnome.Disks");
    let _ = settings.set_string("image-dir-uri", &folder_uri);
}

pub fn unfuse_path<P>(path: &P) -> String
where
    P: AsRef<std::path::Path> + ?Sized,
{
    // Map GVfs FUSE paths to GVfs URIs
    let file = gio::File::for_path(path);
    let uri = file.uri();
    let mut ret = if uri.starts_with("file:") {
        path.as_ref()
            .to_str()
            .expect("`path` should be valid UTF-8")
            .into()
    } else {
        glib::Uri::unescape_string(&uri, None).unwrap()
    };

    // replace $HOME with ~
    let home = glib::home_dir();
    if let Some(path) = ret.strip_prefix(home.to_str().unwrap()) {
        let path = path.strip_prefix('/').unwrap_or(path);
        ret = format!("~/{}", path).into();
    }
    ret.to_string()
}

pub fn has_option(
    options_entry: &impl IsA<gtk::Editable>,
    option: &str,
    check_prefix: bool,
) -> (bool, Option<String>) {
    let text = options_entry.text();
    for option_iter in text.split(',') {
        if check_prefix {
            if let Some(val) = option_iter.strip_prefix(option) {
                return (true, Some(val.to_string()));
            }
        } else if option_iter == option {
            return (true, None);
        }
    }
    (false, None)
}

pub fn add_option(
    options_entry: &impl IsA<gtk::Editable>,
    prefix: &str,
    option: &str,
    add_to_front: bool,
) {
    let text = options_entry.text();
    let (front, suffix) = if add_to_front {
        (option, text.as_str())
    } else {
        (text.as_str(), option)
    };
    options_entry.set_text(&format!(
        "{}{}{}{}",
        front,
        if text.is_empty() { "" } else { "," },
        prefix,
        suffix
    ));
}

pub fn remove_option(options_entry: &impl IsA<gtk::Editable>, option: &str, check_prefix: bool) {
    let str = options_entry
        .text()
        .split(',')
        .filter(|&option_iter| {
            (check_prefix && option_iter.starts_with(option)) || option_iter == option
        })
        .join(",");
    options_entry.set_text(&str);
}

pub fn update_check_option(
    options_entry: &impl IsA<gtk::Editable>,
    option: &str,
    widget: &impl IsA<gtk::Widget>,
    check_button: &impl IsA<adw::SwitchRow>,
    negate: bool,
    _add_to_front: bool,
) {
    let (opts, _) = has_option(options_entry, option, false);
    let ui = check_button.as_ref().is_active();
    if (!negate && (opts != ui)) || (negate && (opts == ui)) {
        if std::ptr::addr_eq(widget.to_glib_none().0, check_button.to_glib_none().0) {
            if (!negate && ui) || (negate && !ui) {
                add_option(options_entry, "", option, _add_to_front);
            } else {
                remove_option(options_entry, option, false);
            }
        } else if negate {
            check_button.as_ref().set_active(!opts);
        } else {
            check_button.as_ref().set_active(opts);
        }
    }
}

pub fn update_entry_option(
    options_entry: &impl IsA<gtk::Editable>,
    option: &str,
    widget: &impl IsA<gtk::Widget>,
    entry: &impl IsA<gtk::Editable>,
) {
    let opts = has_option(options_entry, option, true)
        .1
        .unwrap_or_default();
    let ui = entry.text();
    let ui_escaped = glib::Uri::escape_string(&ui, None, true);

    if opts != ui_escaped {
        if std::ptr::addr_eq(entry.to_glib_none().0, widget.to_glib_none().0) {
            if !ui_escaped.is_empty() {
                remove_option(options_entry, option, true);
                add_option(options_entry, option, &ui, false)
            } else {
                remove_option(options_entry, option, true);
            }
        } else {
            let opts_unescaped = glib::Uri::unescape_string(&opts, None).unwrap_or_default();
            entry.set_text(&opts_unescaped);
        }
    }
}

#[cfg(feature = "logind")]
pub fn seat() -> &'static Option<String> {
    use std::sync::OnceLock;

    static SEAT: OnceLock<Option<String>> = OnceLock::new();
    SEAT.get_or_init(|| {
        if let Ok(session) = systemd::login::get_session(Some(std::process::id() as i32)) {
            // no more memory leak, yay :)
            return systemd::login::get_seat(session).ok();
        }
        None
    })
}

#[cfg(not(feature = "logind"))]
pub fn seat() -> &'static Option<String> {
    &None
}

fn years_to_string(value: u32) -> String {
    // Translators: Used for number of years
    dngettext(GETTEXT_PACKAGE, "%d year", "%d years", value).replace("%d", &value.to_string())
}

fn months_to_string(value: u32) -> String {
    // Translators: Used for number of months
    dngettext(GETTEXT_PACKAGE, "%d month", "%d months", value).replace("%d", &value.to_string())
}

fn days_to_string(value: u32) -> String {
    // Translators: Used for number of days
    dngettext(GETTEXT_PACKAGE, "%d day", "%d days", value).replace("%d", &value.to_string())
}

fn hours_to_string(value: u32) -> String {
    // Translators: Used for number of hours
    dngettext(GETTEXT_PACKAGE, "%d hour", "%d hours", value).replace("%d", &value.to_string())
}

fn minutes_to_string(value: u32) -> String {
    // Translators: Used for number of minutes
    dngettext(GETTEXT_PACKAGE, "%d minute", "%d minutes", value).replace("%d", &value.to_string())
}

fn seconds_to_string(value: u32) -> String {
    // Translators: Used for number of seconds
    dngettext(GETTEXT_PACKAGE, "%d second", "%d seconds", value).replace("%d", &value.to_string())
}

fn milliseconds_to_string(value: u32) -> String {
    // Translators: Used for number of milli-seconds
    dngettext(
        GETTEXT_PACKAGE,
        "%d milli-second",
        "%d milli-seconds",
        value,
    )
    .replace("%d", &value.to_string())
}

/// Number of microseconds in one year
const USEC_PER_YEAR: f64 = (USEC_PER_SEC * 60 * 60 * 24) as f64 * 365.25;
/// Number of microseconds in one month
const USEC_PER_MONTH: f64 = (USEC_PER_SEC * 60 * 60 * 24) as f64 * 365.25 / 12.0;
/// Number of microseconds in one day (86.4 billion)
const USEC_PER_DAY: u64 = USEC_PER_SEC * 60 * 60 * 24;
/// Number of microseconds in one hour (360 million)
const USEC_PER_HOUR: u64 = USEC_PER_SEC * 60 * 60;
/// Number of microseconds in one minute (60 million)
const USEC_PER_MINUTE: u64 = USEC_PER_SEC * 60;
/// Number of microseconds in one second (1 million)
const USEC_PER_SEC: u64 = 1_000_000;
/// Number of microseconds in one milli-second (1000)
const MSEC_PER_USEC: u32 = 1_000;

pub fn format_duration_usec(usec: u64, flags: GduFormatDurationFlags) -> String {
    let mut t = usec;
    //TODO: check if math is equivalent to C version
    let years = (t as f64 / USEC_PER_YEAR).floor() as u64;
    t -= years * USEC_PER_YEAR as u64;
    let months = (t as f64 / USEC_PER_MONTH).floor() as u64;
    t -= months * USEC_PER_MONTH as u64;
    let days = t / USEC_PER_DAY;
    t -= days * USEC_PER_DAY;
    let hours = t / USEC_PER_HOUR;
    t -= hours * USEC_PER_HOUR;
    let minutes = t / USEC_PER_MINUTE;
    t -= minutes * USEC_PER_MINUTE;
    let seconds = t / USEC_PER_SEC;
    t -= seconds * USEC_PER_SEC;
    let milliseconds = t / MSEC_PER_USEC as u64;

    let years_str = years_to_string(years as u32);
    let months_str = months_to_string(months as u32);
    let days_str = days_to_string(days as u32);
    let hours_str = hours_to_string(hours as u32);
    let minutes_str = minutes_to_string(minutes as u32);
    let seconds_str = seconds_to_string(seconds as u32);
    let milliseconds_str = milliseconds_to_string(milliseconds as u32);

    if years > 0 {
        /* Translators: Used for duration greater than one year. First %s is number of years, second %s is months, third %s is days */
        pgettext_f(
            "duration-year-to-inf",
            "%s, %s and %s",
            [years_str, months_str, days_str],
        )
    } else if months > 0 {
        /* Translators: Used for durations less than one year but greater than one month. First %s is number of months, second %s is days */
        pgettext_f(
            "duration-months-to-year",
            "%s and %s",
            [months_str, days_str],
        )
    } else if days > 0 {
        /* Translators: Used for durations less than one month but greater than one day. First %s is number of days, second %s is hours */
        pgettext_f("duration-day-to-month", "%s and %s", [days_str, hours_str])
    } else if hours > 0 {
        /* Translators: Used for durations less than one day but greater than one hour. First %s is number of hours, second %s is minutes */
        pgettext_f(
            "duration-hour-to-day",
            "%s and %s",
            [hours_str, minutes_str],
        )
    } else if minutes > 0 {
        if flags.contains(GduFormatDurationFlags::NoSeconds) {
            minutes_str
        } else {
            /* Translators: Used for durations less than one hour but greater than one minute. First %s is number of minutes, second %s is seconds */
            pgettext_f(
                "duration-minute-to-hour",
                "%s and %s",
                [minutes_str, seconds_str],
            )
        }
    } else if seconds > 0
        || !flags.contains(GduFormatDurationFlags::SubSecondPrecision)
        || flags.contains(GduFormatDurationFlags::NoSeconds)
    {
        if flags.contains(GduFormatDurationFlags::NoSeconds) {
            pgettext("duration", "Less than a minute")
        } else {
            /* Translators: Used for durations less than one minute byte greater than one second. First %s is number of seconds */
            pgettext_f("duration-second-to-minute", "%s", [seconds_str])
        }
    } else {
        /* Translators: Used for durations less than one second. First %s is number of milli-seconds */
        pgettext_f("duration-zero-to-second", "%s", [milliseconds_str])
    }
}

pub async fn is_flash(drive: &udisks::drive::DriveProxy<'static>) -> bool {
    drive
        .media_compatibility()
        .await
        .iter()
        .flatten()
        .any(|compat| compat.starts_with("flash"))
}

pub async fn count_primary_dos_partitions(
    client: &udisks::Client,
    table: &udisks::partitiontable::PartitionTableProxy<'static>,
) -> usize {
    futures::stream::iter(client.partitions(table).await.iter())
        .filter(|&partition| async move { partition.is_contained().await.is_ok_and(|v| !v) })
        .count()
        .await
}

pub async fn have_dos_extended(
    client: &udisks::Client,
    table: &udisks::partitiontable::PartitionTableProxy<'static>,
) -> bool {
    futures::stream::iter(client.partitions(table).await.iter())
        .any(|partition| async move { partition.is_container().await.is_ok_and(|v| v) })
        .await
}

pub async fn is_inside_dos_extended(
    client: &udisks::Client,
    table: &udisks::partitiontable::PartitionTableProxy<'static>,
    offset: u64,
) -> bool {
    for partition in client.partitions(table).await {
        if partition.is_container().await.is_ok_and(|v| v) {
            let partition_offset = partition.offset().await.unwrap_or(0);
            let size = partition.size().await.unwrap_or(0);
            if offset >= partition_offset && offset < partition_offset + size {
                return true;
            }
        }
    }
    false
}

pub async fn show_message_dialog(
    title: &str,
    message: &str,
    parent_window: &impl IsA<gtk::Widget>,
) {
    let dialog = adw::AlertDialog::builder()
        .heading(title)
        .body(message)
        .close_response("close")
        .default_response("close")
        .build();
    dialog.add_response("close", &gettext("_Close"));

    dialog.choose_future(parent_window).await;
}

pub async fn show_error(
    parent_window: &impl IsA<gtk::Widget>,
    title: &str,
    //TODO: potentially replace with anyhow
    error: Box<dyn std::error::Error>,
) {
    // Never show an error if it's because the user dismissed the
    // authentication dialog themself
    //
    // ... or if the user canceled the operation
    if matches!(
        error.downcast_ref::<udisks::Error>(),
        Some(udisks::Error::NotAuthorizedDismissed) | Some(udisks::Error::Cancelled)
    ) {
        return;
    }

    // TODO: probably provide the error-domain / error-code / D-Bus error name
    // in a GtkExpander.
    let message = format!("{}", error);

    show_message_dialog(title, &message, parent_window).await;
}

pub async fn widget_for_object(client: &udisks::Client, object: &udisks::Object) -> gtk::Widget {
    let row = adw::ActionRow::new();
    let info = client.object_info(object).await;

    // probably safe to unwrap since it gets set when the object is created
    row.set_title(&info.description.unwrap());
    row.set_subtitle(&info.name.unwrap());

    row.upcast::<gtk::Widget>()
}

pub async fn create_widget_from_objects(
    client: &udisks::Client,
    objects: &[&udisks::Object],
) -> gtk::Widget {
    let group = adw::PreferencesGroup::new();
    for object in objects {
        let widget = widget_for_object(client, object).await;
        group.add(&widget);
    }

    group.upcast::<gtk::Widget>()
}

#[derive(Debug, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub enum ConfirmationDialogResponse {
    Cancel,
    Confirm,
    Custom(String),
}

impl From<glib::GString> for ConfirmationDialogResponse {
    fn from(value: glib::GString) -> Self {
        match value.as_str() {
            ConfirmationDialog::RESPONSE_CANCEL => Self::Cancel,
            ConfirmationDialog::RESPONSE_CONFIRM => Self::Confirm,
            v => Self::Custom(v.to_owned()),
        }
    }
}

pub struct ConfirmationDialog {
    pub message: String,
    pub description: String,
    pub reponse_verb: String,
    pub reponse_appearance: adw::ResponseAppearance,
}

impl ConfirmationDialog {
    /// Response id when the dialog is canceled.
    pub(super) const RESPONSE_CANCEL: &'static str = "cancel";

    /// Response id when the user chooses to confirm the dialog action.
    pub(super) const RESPONSE_CONFIRM: &'static str = "confirm";

    pub async fn show(
        self,
        parent_window: &impl IsA<gtk::Widget>,
        extra_child: Option<&impl IsA<gtk::Widget>>,
    ) -> ConfirmationDialogResponse {
        // TODO: this api is so confusing
        let mut dialog_builder = adw::AlertDialog::builder()
            .heading(self.message)
            .body(self.description)
            .close_response(Self::RESPONSE_CANCEL)
            .default_response(
                if self.reponse_appearance == adw::ResponseAppearance::Suggested {
                    Self::RESPONSE_CONFIRM
                } else {
                    Self::RESPONSE_CANCEL
                },
            );

        if let Some(widget) = extra_child {
            dialog_builder = dialog_builder.extra_child(widget);
        }
        let dialog = dialog_builder.build();

        dialog.add_response(Self::RESPONSE_CANCEL, &gettext("Cancel"));
        dialog.add_response(Self::RESPONSE_CONFIRM, &self.reponse_verb);
        dialog.set_response_appearance(Self::RESPONSE_CONFIRM, self.reponse_appearance);
        dialog.choose_future(parent_window).await.into()
    }
}

struct CacheEntry {
    available: bool,
    missing_util: String,
    mode: ResizeFlags,
}

impl From<(bool, u64, String)> for CacheEntry {
    fn from((available, mode, missing_util): (bool, u64, String)) -> Self {
        Self {
            available,
            missing_util,
            mode: ResizeFlags::from_bits(mode as u32).unwrap(),
        }
    }
}

impl From<(bool, String)> for CacheEntry {
    fn from((available, missing_util): (bool, String)) -> Self {
        Self {
            available,
            missing_util,
            mode: ResizeFlags::empty(),
        }
    }
}

//TODO: the can_* methods are nearly identical, write a common base for them

/// Uses an internal cache, set flush to rebuild it first
pub async fn can_resize(
    client: &udisks::Client,
    fstype: &str,
    flush: bool,
) -> Option<(bool, ResizeFlags, String)> {
    // utility for creating a static HashMap, working around the fact that HashMap::new is
    // non-const
    let cache = {
        static HASHMAP: OnceLock<Mutex<HashMap<String, CacheEntry>>> = OnceLock::new();
        HASHMAP.get_or_init(|| Mutex::new(HashMap::new()))
    };

    //TODO: why do we need a mutex for the HashMap?
    let mut lock = cache.lock().await;
    if flush {
        // drop previous cache
        lock.clear();
    }

    if lock.is_empty() {
        for type_ in client
            .manager()
            .supported_filesystems()
            .await
            .iter()
            .flatten()
        {
            if let Ok(data) = client.manager().can_resize(type_).await {
                lock.insert(type_.to_string(), CacheEntry::from(data));
            }
        }
    }

    // TODO: are we just converting the type back into a tuple?
    lock.get(fstype)
        .map(|entry| (entry.available, entry.mode, entry.missing_util.clone()))
}

pub async fn can_repair(
    client: &udisks::Client,
    fstype: &str,
    flush: bool,
) -> Option<(bool, String)> {
    // utility for creating a static HashMap, working around the fact that HashMap::new is
    // non-const
    let cache = {
        static HASHMAP: OnceLock<Mutex<HashMap<String, CacheEntry>>> = OnceLock::new();
        HASHMAP.get_or_init(|| Mutex::new(HashMap::new()))
    };

    //TODO: why do we need a mutex for the HashMap?
    let mut lock = cache.lock().await;
    if flush {
        // drop previous cache
        lock.clear();
    }

    if lock.is_empty() {
        for type_ in client
            .manager()
            .supported_filesystems()
            .await
            .iter()
            .flatten()
        {
            if let Ok(data) = client.manager().can_repair(type_).await {
                lock.insert(type_.to_string(), CacheEntry::from(data));
            }
        }
    }

    // TODO: are we just converting the type back into a tuple?
    lock.get(fstype)
        .map(|entry| (entry.available, entry.missing_util.clone()))
}

pub async fn can_format(
    client: &udisks::Client,
    fstype: &str,
    flush: bool,
) -> Option<(bool, String)> {
    // utility for creating a static HashMap, working around the fact that HashMap::new is
    // non-const
    let cache = {
        static HASHMAP: OnceLock<Mutex<HashMap<String, CacheEntry>>> = OnceLock::new();
        HASHMAP.get_or_init(|| Mutex::new(HashMap::new()))
    };

    //TODO: why do we need a mutex for the HashMap?
    let mut lock = cache.lock().await;
    if flush {
        // drop previous cache
        lock.clear();
    }

    if lock.is_empty() {
        for type_ in client
            .manager()
            .supported_filesystems()
            .await
            .iter()
            .flatten()
        {
            if let Ok(data) = client.manager().can_format(type_).await {
                lock.insert(type_.to_string(), CacheEntry::from(data));
            }
        }
    }

    // TODO: are we just converting the type back into a tuple?
    lock.get(fstype)
        .map(|entry| (entry.available, entry.missing_util.clone()))
}

pub fn can_take_ownership(fstype: &str) -> bool {
    !matches!(fstype, "ntfs" | "vfat" | "exfat")
}

pub async fn can_check(
    client: &udisks::Client,
    fstype: &str,
    flush: bool,
) -> Option<(bool, String)> {
    // utility for creating a static HashMap, working around the fact that HashMap::new is
    // non-const
    let cache = {
        static HASHMAP: OnceLock<Mutex<HashMap<String, CacheEntry>>> = OnceLock::new();
        HASHMAP.get_or_init(|| Mutex::new(HashMap::new()))
    };

    //TODO: why do we need a mutex for the HashMap?
    let mut lock = cache.lock().await;
    if flush {
        // drop previous cache
        lock.clear();
    }

    if lock.is_empty() {
        for type_ in client
            .manager()
            .supported_filesystems()
            .await
            .iter()
            .flatten()
        {
            if let Ok(data) = client.manager().can_check(type_).await {
                lock.insert(type_.to_string(), CacheEntry::from(data));
            }
        }
    }

    // TODO: are we just converting the type back into a tuple?
    lock.get(fstype)
        .map(|entry| (entry.available, entry.missing_util.clone()))
}

pub fn max_label_length(fstype: &str) -> u32 {
    match fstype {
        "exfat" => 15,
        "vfat" => 11,
        _ => u32::MAX,
    }
}

pub async fn is_same_size(blocks: &[udisks::block::BlockProxy<'static>]) -> (bool, u64) {
    if blocks.is_empty() {
        return (false, u64::MAX);
    }
    let mut min_size = u64::MIN;
    let mut max_size = 0;

    for block in blocks {
        let block_size = block.size().await.unwrap_or(0);
        if block_size > max_size {
            max_size = block_size;
        }
        if block_size < min_size {
            min_size = block_size;
        }
    }

    // bail if there is more than 1% difference and at least 1MiB
    if max_size - min_size > min_size * 101 / 100 && max_size - min_size > 1048576 {
        return (false, min_size);
    }

    (true, min_size)
}

pub fn pretty_uri(_file: gio::File) -> Option<String> {
    todo!("Duplicate of unfuse_path")
}

async fn all_contained_objects(
    client: &udisks::Client,
    object: &udisks::Object,
) -> Vec<udisks::Object> {
    let mut objects_to_check = vec![];

    let block = if let Ok(drive) = object.drive().await {
        client.block_for_drive(&drive, false).await
    } else {
        object.block().await.ok()
    };

    if let Some(block) = block {
        let Ok(block_object) = client.object(block.inner().path().clone());
        objects_to_check.push(block_object.clone());
        // if we're a partitioned block device, add all partitions
        if let Ok(partition_table) = block_object.partition_table().await {
            objects_to_check.extend(
                client
                    .partitions(&partition_table)
                    .await
                    .iter()
                    .filter_map(|partition| client.object(partition.inner().path().clone()).ok()),
            );
        }
    }

    // add LUKS objects
    let mut i = 0;
    while i < objects_to_check.len() {
        let object_iter = &objects_to_check[i];
        if let Ok(block_for_object) = object_iter.block().await {
            if let Some(cleartext) = client.cleartext_block(&block_for_object).await {
                let Ok(cleartext_object) = client.object(cleartext.inner().path().clone());
                objects_to_check.push(cleartext_object);
            }
        }
        i += 1;
    }
    objects_to_check
}

pub async fn is_in_full_use(
    client: &udisks::Client,
    object: &udisks::Object,
    last_out: bool,
) -> udisks::Result<(
    Option<udisks::filesystem::FilesystemProxy<'static>>,
    Option<udisks::encrypted::EncryptedProxy<'static>>,
    bool,
)> {
    let objects_to_check = all_contained_objects(client, object).await;

    let mut filesystem_to_unmount = None;
    let mut encrypted_to_lock = None;
    let mut ret = false;
    let mut last = true;

    // check in reverse order, e.g. cleartext before LUKS, partitions before the main block device
    for object_iter in objects_to_check.iter().rev() {
        let Ok(block_for_object) = object_iter.block().await else {
            continue;
        };
        if let Ok(filesystem_for_object) = object_iter.filesystem().await {
            let mount_points = filesystem_for_object.mount_points().await?;
            if mount_points.iter().flatten().count() > 0 {
                if ret {
                    last = false;
                    break;
                }
                filesystem_to_unmount = Some(filesystem_for_object);
                ret = true;
            }
        }

        if let Ok(encrypted_for_object) = object_iter.encrypted().await {
            if client.cleartext_block(&block_for_object).await.is_some() {
                if ret {
                    last = false;
                    break;
                }
                encrypted_to_lock = Some(encrypted_for_object);
                ret = true;
            }
        }

        if ret && !last_out {
            break;
        }
    }

    Ok((filesystem_to_unmount, encrypted_to_lock, last))
}

pub async fn is_in_use(client: &udisks::Client, object: &udisks::Object) -> bool {
    is_in_full_use(client, object, false).await.is_ok()
}

#[async_recursion]
pub async fn unuse_data_iterate(
    client: &udisks::Client,
    object: &udisks::Object,
) -> Result<(), (udisks::Error, String)> {
    let (filesystem_to_unmount, encrypted_to_lock, _last) = is_in_full_use(client, object, false)
        .await
        .map_err(|err| (err, gettext("Failed to find filesystem")))?;

    let block = object.block().await;

    if block.is_ok() && (filesystem_to_unmount.is_some() || encrypted_to_lock.is_some()) {
        if let Ok(loop_) = client.loop_for_block(&block.unwrap()).await {
            if loop_.autoclear().await.is_ok_and(|res| res) {
                let loop_object = client.object(loop_.inner().path().clone()).unwrap();
                let (_fs, _enc, last) = is_in_full_use(client, &loop_object, true).await.unwrap();
                if last {
                    loop_
                        .set_autoclear(false, HashMap::new())
                        .await
                        .map_err(|err| {
                            (err, gettext("Error disabling autoclear for loop device"))
                        })?;
                    unuse_data_iterate(client, object).await?;
                    return Ok(());
                }
            }
        }
    }

    if let Some(filesystem_to_unmount) = filesystem_to_unmount {
        filesystem_to_unmount
            .unmount(HashMap::new())
            .await
            .map_err(|err| (err, gettext("Error unmounting filesystem")))?;
        unuse_data_iterate(client, object).await?;
    } else if let Some(encrypted_to_lock) = encrypted_to_lock {
        encrypted_to_lock
            .lock(HashMap::new())
            .await
            .map_err(|err| (err, gettext("Error locking device")))?;
        unuse_data_iterate(client, object).await?;
    }
    Ok(())
}

pub async fn ensure_unused_list(
    client: &udisks::Client,
    parent_window: &impl IsA<gtk::Widget>,
    objects: &[&udisks::Object],
) -> udisks::Result<()> {
    for object in objects {
        if let Err((err, err_msg)) = unuse_data_iterate(client, object).await {
            show_error(parent_window, &err_msg, Box::new(err.clone())).await;
            return Err(err);
        }
    }
    Ok(())
}

pub async fn ensure_unused(
    client: &udisks::Client,
    parent_window: &impl IsA<gtk::Widget>,
    object: &udisks::Object,
) -> udisks::Result<()> {
    ensure_unused_list(client, parent_window, &[object]).await
}

pub async fn calc_space_to_grow(
    client: &udisks::Client,
    table: &udisks::partitiontable::PartitionTableProxy<'static>,
    partition: &udisks::partition::PartitionProxy<'static>,
) -> udisks::Result<u64> {
    let partion_number = partition.number().await?;
    let table_object = client.object(table.inner().path().clone())?;
    let mut next_pos = table_object.block().await?.size().await?;
    let current_end = partition.offset().await? + partition.size().await?;
    for partition_tmp in client.partitions(table).await {
        if Ok(partion_number) == partition_tmp.number().await {
            continue;
        }

        let start = partition_tmp.offset().await?;
        let end = start + partition_tmp.size().await?;
        if end > current_end && (end < next_pos) {
            next_pos = end;
        }
        if start >= current_end && (start < next_pos) {
            next_pos = start;
        }
    }
    Ok(next_pos - partition.offset().await?)
}

pub async fn calc_space_to_shrink_extended(
    client: &udisks::Client,
    table: &udisks::partitiontable::PartitionTableProxy<'static>,
    partition: &udisks::partition::PartitionProxy<'static>,
) -> udisks::Result<u64> {
    let partion_number = partition.number().await?;
    assert!(partition.is_container().await.unwrap());

    let mut minimum = partition.offset().await? + 1;
    let maximum = minimum + partition.size().await?;
    for partition_tmp in client.partitions(table).await {
        if Ok(partion_number) == partition_tmp.number().await {
            continue;
        }

        let end = partition_tmp.offset().await? + partition_tmp.size().await?;
        if end > minimum && end <= maximum {
            minimum = end;
        }
    }
    Ok(minimum - partition.offset().await?)
}

pub async fn unused_for_block(
    client: &udisks::Client,
    block: &udisks::block::BlockProxy<'static>,
) -> udisks::Result<u64> {
    let object = client.object(block.inner().path().clone())?;
    // TODO: Look at UDisksFilesystem property set from the udev db populated by blkid(8)
    let filesystem = object.filesystem().await?;
    let mount_points = filesystem.mount_points().await?;

    let Some(mount_point) = mount_points.first() else {
        // TODO: use own error type
        return Err(udisks::Error::Zbus(zbus::Error::Failure(
            "No mount points".to_string(),
        )));
    };

    let path = CString::from_vec_with_nul(mount_point.clone()).unwrap();

    //TODO: switch to function/crate
    let mut stat: MaybeUninit<libc::statvfs> = MaybeUninit::zeroed();

    if unsafe { libc::statvfs(path.as_ptr() as *const _, stat.as_mut_ptr()) } != 0 {
        // TODO: use own error type
        return Err(udisks::Error::Zbus(zbus::Error::Failure(
            "Failed to read file metadata".to_string(),
        )));
    }

    let stat = unsafe { stat.assume_init() };
    Ok(stat.f_bfree * stat.f_bsize)
}

pub fn default_unit(size: u64) -> u32 {
    //TODO: use loop
    if size > UnitSize::TByte.size_in_bytes() * 10 {
        // size > 10TB -> TB
        UnitSize::TByte as u32
    } else if size > UnitSize::GByte.size_in_bytes() * 10 {
        // 10TB > size > 10GB -> GB
        UnitSize::GByte as u32
    } else if size > UnitSize::MByte.size_in_bytes() * 10 {
        // 10GB > size > 10MB -> MB
        UnitSize::MByte as u32
    } else if size > UnitSize::KByte.size_in_bytes() * 10 {
        // 10MB > size > 10KB -> KB
        UnitSize::KByte as u32
    } else {
        // 10kB > size > 0 -> bytes
        UnitSize::Byte as u32
    }
}

//TODO: probably move this to a utils file
fn string_from_array(data: &OwnedValue) -> Option<String> {
    let array = data.downcast_ref::<zvariant::Array>().ok()?;
    let mut bytes = Vec::with_capacity(array.len());
    for byte in array.inner() {
        bytes.push(byte.downcast_ref::<u8>().ok()?);
    }
    let cstr = CString::from_vec_with_nul(bytes).ok()?;
    Some(cstr.to_str().ok()?.to_string())
}

#[cfg(test)]
mod unfuse_path_tests {
    use super::*;

    #[test]
    fn unfuse_path_home() {
        let path = file!();
        let home = glib::home_dir();
        assert_eq!(path.replace(home.to_str().unwrap(), "~"), unfuse_path(path));
    }

    #[test]
    fn unfuse_path_without_home() {
        let path = "/root";
        assert_eq!(path, unfuse_path(path));
    }
}
