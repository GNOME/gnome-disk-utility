sources = files(
  'gdusdmonitor.c',
  'main.c',
)

deps = [
  gmodule_dep,
  gtk_dep,
  libnotify_dep,
  udisk_dep,
]

executable(
  'gnome-disk-utility-notify',
  sources,
  include_directories: top_inc,
  dependencies: deps,
  install: true,
  install_dir: gdu_libexecdir,
)


configure_file(
  input: 'gnome-disk-utility-notify.service.in',
  output: '@BASENAME@',
  configuration: {'libexecdir': gdu_prefix / gdu_libexecdir},
  install: true,
  install_dir: systemd_user_unit_dir,
)
