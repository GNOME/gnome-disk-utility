name = 'GNOME-Disks'

enum_headers = files('gduenums.h')

sources = files(
  'gduapplication.c',
  'gduatasmartdialog.c',
  'gdubenchmarkdialog.c',
  'gduchangepassphrasedialog.c',
  'gducreatediskimagedialog.c',
  'gducreatefilesystemwidget.c',
  'gducreatepartitiondialog.c',
  'gducrypttabdialog.c',
  'gdudevicetreemodel.c',
  'gdudisksettingsdialog.c',
  'gdudvdsupport.c',
  'gduestimator.c',
  'gdufilesystemdialog.c',
  'gduformatdiskdialog.c',
  'gduformatvolumedialog.c',
  'gdufstabdialog.c',
  'gdulocaljob.c',
  'gdunewdiskimagedialog.c',
  'gdupartitiondialog.c',
  'gdupasswordstrengthwidget.c',
  'gdurestorediskimagedialog.c',
  'gduunlockdialog.c',
  'gduvolumegrid.c',
  'gduwindow.c',
  'gduxzdecompressor.c',
  'main.c'
)

if config_h.get('HAVE_UDISKS2_7_2')
  sources += files('gduresizedialog.c')
endif

resource_data = files(
  'ui/about-dialog.ui',
  'ui/app-menu.ui',
  'ui/benchmark-dialog.ui',
  'ui/change-passphrase-dialog.ui',
  'ui/create-disk-image-dialog.ui',
  'ui/create-partition-dialog.ui',
  'ui/disk-settings-dialog.ui',
  'ui/disks.ui',
  'ui/edit-crypttab-dialog.ui',
  'ui/edit-dos-partition-dialog.ui',
  'ui/edit-gpt-partition-dialog.ui',
  'ui/edit-filesystem-dialog.ui',
  'ui/edit-fstab-dialog.ui',
  'ui/edit-partition-dialog.ui',
  'ui/erase-multiple-disks-dialog.ui',
  'ui/filesystem-create.ui',
  'ui/format-disk-dialog.ui',
  'ui/format-volume-dialog.ui',
  'ui/gdu.css',
  'ui/new-disk-image-dialog.ui',
  'ui/resize-dialog.ui',
  'ui/restore-disk-image-dialog.ui',
  'ui/smart-dialog.ui',
  'ui/unlock-device-dialog.ui'
)

sources += gnome.compile_resources(
  'resources',
  'gnome-disks.gresource.xml',
  source_dir: '.',
  dependencies: resource_data,
  export: true
)

enum = 'gduenumtypes'

sources += gnome.mkenums(
  enum,
  sources: enum_headers,
  c_template: enum + '.c.template',
  h_template: enum + '.h.template'
)

deps = [
  gmodule_dep,
  gio_unix_dep,
  udisk_dep,
  gtk_dep,
  libsecret_dep,
  pwquality_dep,
  libcanberra_dep,
  dvdread_dep,
  liblzma_dep,
  libgdu_dep,
  m_dep
]

cflags = [
  '-DPACKAGE_DATA_DIR="@0@"'.format(gdu_datadir),
  '-DG_LOG_DOMAIN="@0@"'.format(name),
  '-DGNOMELOCALEDIR="@0@"'.format(gdu_localedir)
]

executable(
  name.to_lower(),
  sources,
  include_directories: top_inc,
  dependencies: deps,
  c_args: cflags,
  install: true,
  install_dir: gdu_bindir
)
