// Utility from https://github.com/FineFindus/udisks-rs/blob/feat/gettext/src/gettext.rs

/// Similar to [`gettextrs::pgettext`], but with support for formatted strings.
///
/// Unlike the provided macro, this function is compatible with gettext string extraction tools.
///
/// # Example
///
/// ```rust, compile_fail
/// let formatted_string = pgettext_f("hello-world", "Hello, {}!", ["world"]);
/// assert_eq!(formatted_string, "Hello, world!");
/// ```
pub fn pgettext_f(
    msgctxt: &str,
    format: &str,
    args: impl IntoIterator<Item = impl AsRef<str>>,
) -> String {
    // map Rust style string formatting to C style formatting
    let s = gettextrs::pgettext(msgctxt, format.replace("{}", "%s"));
    arg_replace(s, args)
}

/// Similar to [`gettextrs::gettext`], but with support for formatted strings.
///
/// Unlike the provided macro, this function is compatible with gettext string extraction tools.
///
/// # Example
///
/// ```rust, compile_fail
/// let formatted_string = gettext_f("Hello, {}!", ["world"]);
/// assert_eq!(formatted_string, "Hello, world!");
/// ```
pub fn gettext_f(format: &str, args: impl IntoIterator<Item = impl AsRef<str>>) -> String {
    // map Rust style string formatting to C style formatting
    let s = gettextrs::gettext(format.replace("{}", "%s"));
    arg_replace(s, args)
}

fn arg_replace(mut s: String, args: impl IntoIterator<Item = impl AsRef<str>>) -> String {
    for arg in args {
        s = s.replacen("%s", arg.as_ref(), 1);
    }
    s
}
