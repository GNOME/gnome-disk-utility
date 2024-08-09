use gtk::glib;

#[glib::flags(name = "MyFlags")]
pub enum GduFormatDurationFlags {
    None = 0,
    SubSecondPrecision = (1 << 0),
    NoSeconds = (1 << 1),
}

// from libblockdev
#[glib::flags(name = "MyFlags")]
pub enum ResizeFlags {
    OfflineShrink = 1 << 1,
    OfflineGrow = 1 << 2,
    OnlineShrink = 1 << 3,
    OnlineGrow = 1 << 4,
}

#[derive(Debug, Clone, Copy)]
#[repr(u32)]
pub enum UnitSize {
    Byte,
    KByte,
    MByte,
    GByte,
    TByte,
    PByte,
    KiByte,
    MiByte,
    GiByte,
    TiByte,
    PiByte,
}

impl UnitSize {
    // Keep in sync with Glade file
    pub fn size_in_bytes(self) -> u64 {
        match self {
            UnitSize::Byte => 1,
            UnitSize::KByte => 1_000,
            UnitSize::MByte => 1_000_000,
            UnitSize::GByte => 1_000_000_000,
            UnitSize::TByte => 1_000_000_000_000,
            UnitSize::PByte => 1_000_000_000_000_000,
            UnitSize::KiByte => 1 << 10,
            UnitSize::MiByte => 1 << 20,
            UnitSize::GiByte => 1 << 30,
            UnitSize::TiByte => 1 << 40,
            UnitSize::PiByte => 1 << 50,
        }
    }
}
