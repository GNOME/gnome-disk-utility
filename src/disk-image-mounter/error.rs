use std::{fmt::Display, io};

use udisks::zbus;

/// Indicates a failure
#[derive(Debug)]
pub enum ImageMounterError {
    /// An error occured  while communicating via zbus.
    Zbus(zbus::Error),
    /// An error occured  while doing an IO operation.
    Io(io::Error),
    /// An error occured  while converting types.
    Conversion,
}

impl std::error::Error for ImageMounterError {}

impl Display for ImageMounterError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ImageMounterError::Zbus(err) => err.fmt(f),
            ImageMounterError::Io(err) => err.fmt(f),
            ImageMounterError::Conversion => write!(f, "Failed to convert value"),
        }
    }
}

impl From<zbus::Error> for ImageMounterError {
    fn from(value: zbus::Error) -> Self {
        Self::Zbus(value)
    }
}

impl From<io::Error> for ImageMounterError {
    fn from(value: io::Error) -> Self {
        Self::Io(value)
    }
}

impl From<std::ffi::FromVecWithNulError> for ImageMounterError {
    fn from(_value: std::ffi::FromVecWithNulError) -> Self {
        Self::Conversion
    }
}

impl From<std::str::Utf8Error> for ImageMounterError {
    fn from(_value: std::str::Utf8Error) -> Self {
        Self::Conversion
    }
}
