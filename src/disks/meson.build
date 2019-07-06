name = 'GNOME-Disks'

enum_headers = files('gduenums.h')

sources = files(
  'gduapplication.c',
  'gduatasmartdialog.c',
  'gdubenchmarkdialog.c',
  'gduchangepassphrasedialog.c',
  'gducreateconfirmpage.c',
  'gducreatediskimagedialog.c',
  'gducreatefilesystempage.c',
  'gducreateformatdialog.c',
  'gducreateotherpage.c',
  'gducreatepartitionpage.c',
  'gducreatepasswordpage.c',
  'gducrypttabdialog.c',
  'gdudevicetreemodel.c',
  'gdudisksettingsdialog.c',
  'gdudvdsupport.c',
  'gduestimator.c',
  'gdufilesystemdialog.c',
  'gduformatdiskdialog.c',
  'gdufstabdialog.c',
  'gdulocaljob.c',
  'gdunewdiskimagedialog.c',
  'gdupartitiondialog.c',
  'gdupasswordstrengthwidget.c',
  'gduresizedialog.c',
  'gdurestorediskimagedialog.c',
  'gduunlockdialog.c',
  'gduvolumegrid.c',
  'gduwindow.c',
  'gduxzdecompressor.c',
  'main.c',
)

resource_data = files(
  'ui/about-dialog.ui',
  'ui/app-menu.ui',
  'ui/benchmark-dialog.ui',
  'ui/change-passphrase-dialog.ui',
  'ui/create-confirm-page.ui',
  'ui/create-disk-image-dialog.ui',
  'ui/create-filesystem-page.ui',
  'ui/create-format.ui',
  'ui/create-other-page.ui',
  'ui/create-partition-page.ui',
  'ui/create-password-page.ui',
  'ui/disk-settings-dialog.ui',
  'ui/disks.ui',
  'ui/edit-crypttab-dialog.ui',
  'ui/edit-dos-partition-dialog.ui',
  'ui/edit-gpt-partition-dialog.ui',
  'ui/edit-filesystem-dialog.ui',
  'ui/edit-fstab-dialog.ui',
  'ui/edit-partition-dialog.ui',
  'ui/erase-multiple-disks-dialog.ui',
  'ui/format-disk-dialog.ui',
  'ui/gdu.css',
  'ui/new-disk-image-dialog.ui',
  'ui/resize-dialog.ui',
  'ui/restore-disk-image-dialog.ui',
  'ui/smart-dialog.ui',
  'ui/unlock-device-dialog.ui',
)

sources += gnome.compile_resources(
  'resources',
  'gnome-disks.gresource.xml',
  dependencies: resource_data,
  export: true,
)

enum = 'gduenumtypes'

sources += gnome.mkenums(
  enum,
  sources: enum_headers,
  c_template: enum + '.c.template',
  h_template: enum + '.h.template',
)

deps = [
  dvdread_dep,
  gio_unix_dep,
  gmodule_dep,
  libcanberra_dep,
  libgdu_dep,
  liblzma_dep,
  libsecret_dep,
  m_dep,
  pwquality_dep,
]

cflags = [
  '-DG_LOG_DOMAIN="@0@"'.format(name),
  '-DGNOMELOCALEDIR="@0@"'.format(gdu_prefix / gdu_localedir),
]

if enable_logind
  deps += logind_dep
endif

executable(
  name.to_lower(),
  sources,
  include_directories: top_inc,
  dependencies: deps,
  c_args: cflags,
  install: true,
)
