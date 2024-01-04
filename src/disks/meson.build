name = 'GNOME-Disks'

enum_headers = files('gduenums.h')

sources = files(
  'gdu-application.c',
  'gdu-benchmark-dialog.c',
  'gdu-change-passphrase-dialog.c',
  'gdu-create-confirm-page.c',
  'gdu-create-filesystem-page.c',
  'gdu-create-other-page.c',
  'gdu-create-partition-page.c',
  'gdu-create-password-page.c',
  'gdu-create-disk-image-dialog.c',
  'gdu-edit-filesystem-dialog.c',
  'gdu-edit-partition-dialog.c',
  'gdu-encryption-options-dialog.c',
  'gdu-format-disk-dialog.c',
  'gdu-format-volume-dialog.c',
  'gdu-mount-options-dialog.c',
  'gdu-new-disk-image-dialog.c',
  'gdu-restore-disk-image-dialog.c',
  'gdu-window.c',
  'gdu-item.c',
  'gdu-block.c',
  'gdu-drive.c',
  'gdu-manager.c',
  'gdu-block-row.c',
  'gdu-drive-row.c',
  'gdu-drive-view.c',
  'gduatasmartdialog.c',
  'gdudevicetreemodel.c',
  'gdudisksettingsdialog.c',
  'gdudvdsupport.c',
  'gduestimator.c',
  'gdulocaljob.c',
  'gdupasswordstrengthwidget.c',
  'gdu-resize-volume-dialog.c',
  'gduunlockdialog.c',
  'gduxzdecompressor.c',
  'gdu-log.c',
  'main.c',
)

resource_data = files(
  'ui/about-dialog.ui',
  'ui/gdu-benchmark-dialog.ui',
  'ui/gdu-change-passphrase-dialog.ui',
  'ui/gdu-create-confirm-page.ui',
  'ui/gdu-create-disk-image-dialog.ui',
  'ui/gdu-create-filesystem-page.ui',
  'ui/gdu-format-volume-dialog.ui',
  'ui/gdu-create-other-page.ui',
  'ui/gdu-create-partition-page.ui',
  'ui/gdu-create-password-page.ui',
  'ui/disk-settings-dialog.ui',
  'ui/gdu-edit-filesystem-dialog.ui',
  'ui/gdu-encryption-options-dialog.ui',
  'ui/gdu-mount-options-dialog.ui',
  'ui/gdu-edit-partition-dialog.ui',
  'ui/erase-multiple-disks-dialog.ui',
  'ui/gdu-format-disk-dialog.ui',
  'ui/gdu.css',
  'ui/gdu-new-disk-image-dialog.ui',
  'ui/gdu-resize-volume-dialog.ui',
  'ui/gdu-restore-disk-image-dialog.ui',
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
  libgdu_dep,
  libadwaita_dep,
  liblzma_dep,
  libsecret_dep,
  m_dep,
  pwquality_dep,
]

cflags = [
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
