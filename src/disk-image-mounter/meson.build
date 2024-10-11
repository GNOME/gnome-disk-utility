name = 'gnome-disk-image-mounter'

mounter_conf = configuration_data()
mounter_conf.merge_from(conf_rs)
mounter_conf.set_quoted('APP_ID', 'org.gnome.DiskImageMounter')
configure_file(
    input: 'config.rs.in',
    output: 'config.rs',
    configuration: mounter_conf,
)

build_root = meson.project_build_root()
source_root = meson.project_source_root()

# Copy the config.rs output to the source directory.
run_command(
  'cp',
  build_root / 'src/disk-image-mounter' / 'config.rs',
  source_root / 'src/disk-image-mounter' / 'config.rs',
  check: true
)

cargo_options = [ '--manifest-path', source_root / 'Cargo.toml' ]
cargo_options += [ '--target-dir', build_root / 'src' ]
cargo_options += [ '--bin', name ]

if get_option('profile') != 'release'
  rust_target = 'debug'
  message('Building in debug mode')
else
  cargo_options += [ '--release' ]
  rust_target = 'release'
  message('Building in release mode')
endif

cargo_env = { 'CARGO_HOME': build_root / 'cargo-home' }

cargo_build = custom_target(
  'cargo-build',
  build_by_default: true,
  build_always_stale: true,
  output: 'src',
  console: true,
  env: cargo_env,
  command: [
    cargo, 'build',
    cargo_options,
  ]
)

copy_binary = custom_target(
  'cp-binary',
  depends: cargo_build,
  build_by_default: true,
  build_always_stale: true,
  install: true,
  install_dir: get_option('bindir'),
  output: name,
  command: [
    'cp',
    'src' / rust_target / name,
    '@OUTPUT@',
  ],
)

cargo_doc = custom_target(
  'cargo-doc',
  build_by_default: false,
  build_always_stale: true,
  output: 'doc',
  console: true,
  command: [
    cargo, 'doc',
    cargo_options + ['--no-deps', '--document-private-items'],
  ],
)
