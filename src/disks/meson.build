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
  build_root / 'src/disks' / 'config.rs',
  source_root / 'src/disks' / 'config.rs',
  check: true,
)

cargo_options = ['--manifest-path', source_root / 'Cargo.toml']
cargo_options += ['--target-dir', build_root / 'src']
cargo_options += ['--lib']

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

# meson expects the .so file to be in the disks build directory
libgnome_disks = custom_target(
  'libgnome_disks',
  depends: cargo_build,
  build_always_stale: true,
  build_by_default: true,
  output: 'libgnome_disks.so',
  command: [
    'cp',
    '-a', build_root / 'src' / rust_target / 'libgnome_disks.so',
    meson.current_build_dir() / 'libgnome_disks.so',
  ],
)

libgnome_disks_dep = declare_dependency(
  sources: libgnome_disks,
  link_args: [build_root / 'src' / 'disks' / 'libgnome_disks.so'],
)

# Build C version
name = 'GNOME-Disks'

enum_headers = files('gduenums.h')

sources = files(
  'gdu-application.c',
  'gdu-ata-smart-dialog.c',
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
  'gdu-window.c',
  'gdu-item.c',
  'gdu-block.c',
  'gdu-drive.c',
  'gdu-manager.c',
  'gdu-block-row.c',
  'gdu-disk-settings-dialog.c',
  'gdu-drive-header.c',
  'gdu-drive-row.c',
  'gdu-drive-view.c',
  'gdudvdsupport.c',
  'gduestimator.c',
  'gdulocaljob.c',
  'gdu-space-allocation-bar.c',
  'gdu-resize-volume-dialog.c',
  'gdu-unlock-dialog.c',
  'gduxzdecompressor.c',
  'gdu-log.c',
  'main.c',
)

resource_data = files(
  'ui/gdu-ata-smart-dialog.ui',
  'ui/gdu-benchmark-dialog.ui',
  'ui/gdu-change-passphrase-dialog.ui',
  'ui/gdu-create-confirm-page.ui',
  'ui/gdu-create-disk-image-dialog.ui',
  'ui/gdu-create-filesystem-page.ui',
  'ui/gdu-format-volume-dialog.ui',
  'ui/gdu-create-other-page.ui',
  'ui/gdu-create-partition-page.ui',
  'ui/gdu-create-password-page.ui',
  'ui/gdu-disk-settings-dialog.ui',
  'ui/gdu-edit-filesystem-dialog.ui',
  'ui/gdu-encryption-options-dialog.ui',
  'ui/gdu-mount-options-dialog.ui',
  'ui/gdu-edit-partition-dialog.ui',
  'ui/erase-multiple-disks-dialog.ui',
  'ui/gdu-format-disk-dialog.ui',
  'ui/gdu-unlock-dialog.ui',
  'ui/style.css',
  'ui/style-dark.css',
  'ui/gdu-new-disk-image-dialog.ui',
  'ui/gdu-resize-volume-dialog.ui',
  'ui/gdu-restore-disk-image-dialog.ui',
  'ui/gdu-take-ownership-dialog.ui',
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
  # rust version
  libgnome_disks_dep,
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
