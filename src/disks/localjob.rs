use glib::Object;
use glib::prelude::*;
use glib::subclass::prelude::*;
use gtk::glib;

/// Wrapper type around [`udisks::Object`] to allow using it in G-OBJECT code.
#[derive(Clone, Debug, glib::Boxed)]
#[boxed_type(name = "BoxedUdisksObject", nullable)]
pub struct BoxedUdisksObject(pub udisks::Object);

mod imp {
    use std::{
        cell::{Cell, RefCell},
        sync::OnceLock,
    };

    use glib::subclass::{prelude::ObjectImpl, types::ObjectSubclass};
    use gtk::glib::subclass::Signal;

    use super::*;

    #[derive(Default, Debug, glib::Properties)]
    #[properties(wrapper_type = super::LocalJob)]
    pub struct LocalJob {
        #[property(get, set)]
        description: RefCell<String>,
        #[property(get, set)]
        extra_markup: RefCell<String>,
        #[property(get, set, nullable)]
        object: RefCell<Option<BoxedUdisksObject>>,

        // UDisksJob properties
        #[property(get, set)]
        operation: RefCell<String>,
        #[property(get, set)]
        bytes: Cell<u64>,
        #[property(get, set)]
        progress_valid: Cell<bool>,
        #[property(get, set)]
        cancelable: Cell<bool>,
        #[property(get, set)]
        expected_end_time: Cell<u64>,
        #[property(get, set)]
        progress: Cell<f64>,
        #[property(get, set)]
        rate: Cell<u64>,
        #[property(get, set)]
        start_time: Cell<u64>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for LocalJob {
        const NAME: &'static str = "LocalJob";
        type Type = super::LocalJob;
    }

    #[glib::derived_properties]
    impl ObjectImpl for LocalJob {
        fn signals() -> &'static [Signal] {
            static SIGNALS: OnceLock<Vec<Signal>> = OnceLock::new();
            SIGNALS.get_or_init(|| vec![Signal::builder("canceled").build()])
        }
    }
}

glib::wrapper! {
    pub struct LocalJob(ObjectSubclass<imp::LocalJob>);
}

impl LocalJob {
    pub fn new(object: udisks::Object) -> Self {
        Object::builder()
            .property("object", BoxedUdisksObject(object))
            .build()
    }
}
