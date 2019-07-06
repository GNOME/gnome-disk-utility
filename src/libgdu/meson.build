enum_headers = files('libgduenums.h')

sources = files('gduutils.c')

enum = 'libgduenumtypes'

enum_sources = gnome.mkenums(
  enum,
  sources: enum_headers,
  c_template: enum + '.c.template',
  h_template: enum + '.h.template',
)

private_deps = [m_dep]

deps = [
  gtk_dep,
  udisk_dep,
]

if enable_logind
  private_deps += logind_dep
endif

cflags = [
  '-DG_LOG_DOMAIN="libgdu"',
  '-DGNOMELOCALEDIR="@0@"'.format(gdu_prefix / gdu_localedir),
]

libgdu = static_library(
  'gdu',
  sources: sources + enum_sources,
  include_directories: top_inc,
  dependencies: deps + private_deps,
  c_args: cflags,
)

libgdu_dep = declare_dependency(
  sources: enum_sources[1],
  link_with: libgdu,
  include_directories: '.',
  dependencies: deps,
)
