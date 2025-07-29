//! An Adw.ComboRow that does not truncate the items in the popover.

use gtk::glib;
use gtk::prelude::*;
use gtk::subclass::prelude::*;

mod imp {
    use super::*;
    use adw::{
        prelude::ComboRowExt,
        subclass::{
            combo_row::ComboRowImpl,
            prelude::{ActionRowImpl, PreferencesRowImpl},
        },
    };

    #[derive(Default, Debug)]
    pub struct GduComboRow;

    #[glib::object_subclass]
    impl ObjectSubclass for GduComboRow {
        const NAME: &'static str = "GduComboRow";
        type ParentType = adw::ComboRow;
        type Type = super::GduComboRow;
    }

    impl ObjectImpl for GduComboRow {
        fn constructed(&self) {
            self.parent_constructed();

            let factory = gtk::SignalListItemFactory::new();
            // https://gitlab.gnome.org/GNOME/libadwaita/-/blob/ad446167acf3e6d1ee693f98ca636268be8592a1/src/adw-combo-row.c#L280
            factory.connect_setup(
                glib::clone!(@weak self as row =>  move |_factory, list_item| {
                    let list_item = list_item
                        .downcast_ref::<gtk::ListItem>()
                        .expect("`list_item` should be a valid GTK.ListItem");

                    row.on_factory_setup(_factory, list_item);
                }),
            );

            // https://gitlab.gnome.org/GNOME/libadwaita/-/blob/ad446167acf3e6d1ee693f98ca636268be8592a1/src/adw-combo-row.c#L341
            factory.connect_bind(
                glib::clone!(@weak self as row => move |factory, list_item| {
                    let list_item = list_item
                        .downcast_ref::<gtk::ListItem>()
                        .expect("`list_item` should be a valid GTK.ListItem");
                    row.on_factory_bind(factory, list_item);
                }),
            );

            self.obj().set_factory(Some(&factory));
        }
    }

    impl WidgetImpl for GduComboRow {}
    impl ListBoxRowImpl for GduComboRow {}
    impl PreferencesRowImpl for GduComboRow {}
    impl ActionRowImpl for GduComboRow {}
    impl ComboRowImpl for GduComboRow {}

    impl GduComboRow {
        fn on_factory_setup(
            &self,
            _factory: &gtk::SignalListItemFactory,
            list_item: &gtk::ListItem,
        ) {
            let box_ = gtk::Box::new(gtk::Orientation::Horizontal, 0);

            let label = gtk::Label::new(None);
            label.set_xalign(0.0);
            label.set_ellipsize(gtk::pango::EllipsizeMode::End);
            label.set_max_width_chars(20);
            label.set_width_chars(1);
            label.set_valign(gtk::Align::Center);
            box_.append(&label);

            let icon: gtk::Image = glib::Object::builder()
                .property("accessible-role", gtk::AccessibleRole::Presentation)
                .property("icon-name", "object-select-symbolic")
                .build();
            box_.append(&icon);

            list_item.set_child(Some(&box_));
        }

        fn on_factory_bind(&self, _: &gtk::SignalListItemFactory, list_item: &gtk::ListItem) {
            let item = list_item
                .item()
                .and_dynamic_cast::<gtk::StringObject>()
                .unwrap();
            let box_ = list_item.child().and_downcast::<gtk::Box>().unwrap();

            let label = box_.first_child().and_downcast::<gtk::Label>().unwrap();
            label.set_label(&item.string());

            self.obj().connect_selected_item_notify(
                glib::clone!(@weak list_item => move |row: &super::GduComboRow| {
                    row.imp().on_item_selected(&list_item);
                }),
            );
            self.on_item_selected(list_item);

            // https://gitlab.gnome.org/GNOME/gnome-control-center/-/blob/5cbf3f952b69d56c5a7276742f898d36dd9c083c/panels/sound/cc-device-combo-row.c#L51
            box_.connect_root_notify(glib::clone!(@weak self as row => move |box_: &gtk::Box| {
                row.on_item_root_changed(box_);
            }));
            self.on_item_root_changed(&box_);
        }

        fn on_item_selected(&self, list_item: &gtk::ListItem) {
            let box_ = list_item.child().unwrap();
            let icon = box_.last_child().unwrap();
            icon.set_opacity(if self.obj().selected_item() == list_item.item() {
                1.0
            } else {
                0.0
            });
        }

        fn on_item_root_changed(&self, box_: &gtk::Box) {
            let label = box_.first_child().and_downcast::<gtk::Label>().unwrap();
            let selected_icon = box_.last_child().unwrap();
            let box_popover = box_.ancestor(gtk::Popover::static_type());
            let is_in_combo_popover = box_popover.is_some_and(|popover| {
                &popover.ancestor(adw::ComboRow::static_type()).unwrap()
                    == self.obj().upcast_ref::<gtk::Widget>()
            });

            // selection icon should only be visible when in the popover
            selected_icon.set_visible(is_in_combo_popover);
            // allow fully expanded text whilst in popover
            label.set_max_width_chars(if is_in_combo_popover { -1 } else { 20 });
        }
    }
}

glib::wrapper! {
    pub struct GduComboRow(ObjectSubclass<imp::GduComboRow>)
    @extends gtk::Widget, adw::ActionRow, adw::ComboRow;
}
