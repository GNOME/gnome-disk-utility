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
  libadwaita_dep,
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

# Build a Rust version

configure_file(
  input: 'config.rs.in',
  output: 'config.rs',
  configuration: conf_rs,
)

build_root = meson.project_build_root()
source_root = meson.project_source_root()

# Copy the config.rs output to the source directory.
run_command(
  'cp',
  build_root / 'src/libgdu' / 'config.rs',
  source_root / 'src/libgdu' / 'config.rs',
  check: true,
)

cargo_options = ['--manifest-path', source_root / 'Cargo.toml']
cargo_options += ['--target-dir', build_root / 'src']
cargo_options += ['--lib']

if enable_logind
  cargo_options += ['--features', 'logind']
endif

if get_option('profile') != 'release'
  rust_target = 'debug'
  message('Building in debug mode')
else
  cargo_options += ['--release']
  rust_target = 'release'
  message('Building in release mode')
endif

cargo_env = {'CARGO_HOME': build_root / 'cargo-home'}

cargo_build = custom_target(
  'cargo-build',
  build_by_default: true,
  build_always_stale: true,
  output: 'src',
  console: true,
  env: cargo_env,
  command: [
    cargo,
    'build',
    cargo_options,
  ],
)

test(
  'cargo-test',
  cargo,
  args: [
    'test',
    cargo_options,
    '--',
    '--test-threads=1',
  ],
  is_parallel: false,
  timeout: 1800,
)
