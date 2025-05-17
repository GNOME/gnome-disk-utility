use glib::Object;
use gtk::glib;
use gtk::prelude::*;
use gtk::subclass::prelude::*;

const MAX_SAMPLES: usize = 50;

#[derive(Debug, Default, Clone, Copy)]
pub struct Sample {
    pub time_usec: u64,
    pub value: u64,
}

mod imp {
    use std::cell::{Cell, RefCell};

    use gtk::glib::ffi::G_USEC_PER_SEC;
    use itertools::Itertools;

    use super::*;

    #[derive(Debug, glib::Properties)]
    #[properties(wrapper_type = super::Estimator)]
    pub struct GduEstimator {
        #[property(get, construct_only)]
        target_bytes: Cell<u64>,
        #[property(get)]
        completed_bytes: Cell<u64>,
        #[property(get)]
        bytes_per_sec: Cell<u64>,
        #[property(get)]
        usec_remaining: Cell<u64>,
        samples: RefCell<Vec<Sample>>,
    }

    impl Default for GduEstimator {
        fn default() -> Self {
            Self {
                target_bytes: Default::default(),
                completed_bytes: Default::default(),
                bytes_per_sec: Default::default(),
                usec_remaining: Default::default(),
                samples: RefCell::new(Vec::with_capacity(MAX_SAMPLES)),
            }
        }
    }

    #[glib::object_subclass]
    impl ObjectSubclass for GduEstimator {
        const NAME: &'static str = "GduEstimator";
        type Type = super::Estimator;
    }

    #[glib::derived_properties]
    impl ObjectImpl for GduEstimator {}

    impl GduEstimator {
        pub fn add_sample(&self, completed_bytes: u64) {
            if completed_bytes >= self.completed_bytes.get() {
                return;
            }
            self.completed_bytes.set(completed_bytes);
            let mut samples = self.samples.borrow_mut();
            if samples.len() == MAX_SAMPLES {
                samples.rotate_left(1);
                samples.pop();
            }
            samples.push(Sample {
                time_usec: std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .expect("`now()` should be after `UNIX_EPOCH`")
                    .as_secs(),
                value: completed_bytes,
            });
            self.update();
        }

        fn update(&self) {
            let (num_speeds, sum_of_speeds) = self.samples.borrow().iter().tuple_windows().fold(
                (0, 0.0),
                |(num_speeds, sum_of_speeds), (a, b)| {
                    let speed = (b.value - a.value) as f64
                        / ((b.time_usec - a.time_usec) as f64 / glib::ffi::G_USEC_PER_SEC as f64);
                    (num_speeds + 1, sum_of_speeds + speed)
                },
            );

            self.bytes_per_sec.set(0);
            self.usec_remaining.set(0);
            if num_speeds > 0 {
                let speed = (sum_of_speeds / num_speeds as f64) as u64;
                self.bytes_per_sec.set(speed);
                if speed > 0 {
                    let remaining_bytes = self.target_bytes.get() - self.completed_bytes.get();
                    self.usec_remaining
                        .set(G_USEC_PER_SEC as u64 * remaining_bytes / self.bytes_per_sec.get());
                }
            }

            {
                // freezes notifications until the guard is dropped at the end of the scope
                let _notify_guard = self.obj().freeze_notify();
                self.obj().notify("bytes-per-sec");
                self.obj().notify("usec-remaining");
            }
        }
    }
}

glib::wrapper! {
    pub struct Estimator(ObjectSubclass<imp::GduEstimator>);
}
impl Estimator {
    pub fn new(target_bytes: u64) -> Self {
        Object::builder()
            .property("target-bytes", target_bytes)
            .build()
    }
}
impl Estimator {
    pub fn add_sample(&self, completed_bytes: u64) {
        self.imp().add_sample(completed_bytes);
    }
}
