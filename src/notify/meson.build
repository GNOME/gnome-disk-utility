sources = files(
  'gdusdmonitor.c',
  'main.c'
)

deps = [
  gmodule_dep,
  gio_unix_dep,
  udisk_dep,
  gtk_dep,
  libnotify_dep
]

executable(
  'gsd-disk-utility-notify',
  sources,
  include_directories: top_inc,
  dependencies: deps,
  install: true,
  install_dir: gdu_libexecdir
)

desktop_conf = configuration_data()
desktop_conf.set('libexecdir', gdu_libexecdir)

desktop = 'org.gnome.SettingsDaemon.DiskUtilityNotify.desktop'

configure_file(
  input: desktop + '.in',
  output: desktop,
  configuration: desktop_conf,
  install: true,
  install_dir: join_paths(gdu_sysconfdir, 'xdg', 'autostart')
)
