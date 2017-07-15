enum_headers = files('libgduenums.h')

sources = files('gduutils.c')

enum = 'libgduenumtypes'

libgduenumtypes = gnome.mkenums(
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
  m_dep
]

if enable_libsystemd
  deps += libsystemd_dep
endif

cflags = [
  '-DPACKAGE_DATA_DIR="@0@"'.format(gdu_datadir),
  '-DG_LOG_DOMAIN="libgdu"',
  '-DGNOMELOCALEDIR="@0@"'.format(gdu_localedir)
]

libgdu = static_library(
  'gdu',
  sources: sources + libgduenumtypes,
  include_directories: top_inc,
  dependencies: deps,
  c_args: cflags
)

libgdu_dep = declare_dependency(
  link_with: libgdu,
  include_directories: include_directories('.'),
  dependencies: deps
)
