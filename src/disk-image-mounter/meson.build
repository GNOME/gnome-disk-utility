name = 'gnome-disk-image-mounter'

deps = [
  gio_unix_dep,
  libgdu_dep,
]

cflags = [
  '-DG_LOG_DOMAIN="@0@"'.format(name),
  '-DGNOMELOCALEDIR="@0@"'.format(gdu_prefix / gdu_localedir),
]

executable(
  name,
  'main.c',
  include_directories: top_inc,
  dependencies: deps,
  c_args: cflags,
  install: true,
)
