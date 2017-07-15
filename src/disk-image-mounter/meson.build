name = 'gnome-disk-image-mounter'

deps = [
  gmodule_dep,
  gio_unix_dep,
  udisk_dep,
  gtk_dep,
  libgdu_dep
]

cflags = [
  '-DPACKAGE_DATA_DIR="@0@"'.format(gdu_datadir),
  '-DG_LOG_DOMAIN="@0@"'.format(name),
  '-DGNOMELOCALEDIR="@0@"'.format(gdu_localedir)
]

executable(
  name,
  ['main.c']  + libgduenumtypes,
  include_directories: top_inc,
  dependencies: deps,
  c_args: cflags,
  install: true,
  install_dir: gdu_bindir
)
