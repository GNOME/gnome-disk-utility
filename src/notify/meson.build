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
  'gsd-disk-utility-notify',
  sources,
  include_directories: top_inc,
  dependencies: deps,
  install: true,
  install_dir: gdu_libexecdir,
)


configure_file(
  input: 'org.gnome.SettingsDaemon.DiskUtilityNotify.desktop.in',
  output: '@BASENAME@',
  configuration: {'libexecdir': gdu_prefix / gdu_libexecdir},
  install: true,
  install_dir: gdu_sysconfdir / 'xdg/autostart',
)
