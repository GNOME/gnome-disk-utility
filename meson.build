project(
  'gnome-disk-utility', 'c',
  version: '44.0',
  license: 'GPL2.0',
  default_options: 'buildtype=debugoptimized',
  meson_version: '>= 0.59.0',
)

gdu_name = meson.project_name()
gdu_version = meson.project_version()

gdu_prefix = get_option('prefix')
gdu_bindir = get_option('bindir')
gdu_datadir = get_option('datadir')
gdu_libexecdir = get_option('libexecdir')
gdu_localedir = get_option('localedir')
gdu_mandir = get_option('mandir')
gdu_sysconfdir = get_option('sysconfdir')

gnome = import('gnome')
i18n = import('i18n')

po_dir = meson.current_source_dir() / 'po'

top_inc = include_directories('.', 'src')

cc = meson.get_compiler('c')

config_h = configuration_data()
config_h.set_quoted('PACKAGE_VERSION', gdu_version)
config_h.set_quoted('GETTEXT_PACKAGE', gdu_name)

# compiler flags
compiler_flags = []

if get_option('buildtype').contains('debug')
  compiler_flags += cc.get_supported_arguments([
    '-fno-strict-aliasing',
    '-Wcast-align',
    '-Wdeclaration-after-statement',
    '-Wformat=2',
    '-Winit-self',
    '-Winline',
    '-Wmissing-declarations',
    '-Wmissing-format-attribute',
    '-Wmissing-include-dirs',
    '-Wmissing-noreturn',
    '-Wmissing-prototypes',
    '-Wnested-externs',
    '-Wno-error=missing-field-initializers',
    '-Wno-error=unused-parameter',
    '-Wno-missing-field-initializers',
    '-Wno-unused-parameter',
    '-Wold-style-definition',
    '-Wpacked',
    '-Wredundant-decls',
    '-Wshadow',
    '-Wsign-compare',
    '-Wstrict-aliasing',
    '-Wstrict-prototypes',
    '-Wswitch-default',
    '-Wswitch-enum',
    '-Wundef',
    '-Wunused-but-set-variable',
    '-Wwrite-strings',
  ])
endif

add_project_arguments(compiler_flags, language: 'c')

dvdread_dep = dependency('dvdread', version: '>= 4.2.0')
gio_unix_dep = dependency('gio-unix-2.0', version: '>= 2.31.0')
gmodule_dep = dependency('gmodule-2.0')
gtk_dep = dependency('gtk4', version: '>= 4.12.0')
libadwaita_dep = dependency('libadwaita-1', version: '>= 1.4.alpha')
liblzma_dep = dependency('liblzma', version: '>= 5.0.5')
libnotify_dep = dependency('libnotify', version: '>= 0.7')
libsecret_dep = dependency('libsecret-1', version: '>= 0.7')
pwquality_dep = dependency('pwquality', version: '>= 1.0.0')
udisk_dep = dependency('udisks2', version: '>= 2.7.6')

m_dep = cc.find_library('m')

# *** Check for logind ***
logind = get_option('logind')
enable_logind = (logind != 'none')
if enable_logind
  logind_dep = dependency(logind, version: '>= 209')
endif
config_h.set('HAVE_LOGIND', enable_logind,
             description: 'Define to 1 if logind API is available')

subdir('src/libgdu')
subdir('src/disks')
subdir('src/disk-image-mounter')

# *** gnome-settings-daemon plug-in ***
enable_gsd_plugin = get_option('gsd_plugin')
if enable_gsd_plugin
  subdir('src/notify')
endif

subdir('data')
subdir('po')

enable_man = get_option('man')
if enable_man
  subdir('doc/man')
endif

configure_file(
  output: 'config.h',
  configuration: config_h,
)

gnome.post_install(
  gtk_update_icon_cache: true,
  glib_compile_schemas: true,
  update_desktop_database: true,
)

output = '\n        gnome-disk-utility ' + gdu_version + '\n'
output += '        =========================\n\n'
output += '        prefix:                     ' + gdu_prefix + '\n'
output += '        bindir:                     ' + gdu_bindir + '\n'
output += '        datadir:                    ' + gdu_datadir + '\n'
output += '        libexecdir:                 ' + gdu_libexecdir + '\n'
output += '        localedir:                  ' + gdu_localedir + '\n'
output += '        mandir:                     ' + gdu_mandir + '\n'
output += '        sysconfdir:                 ' + gdu_sysconfdir + '\n\n'
output += '        Use logind:                 ' + logind + '\n'
output += '        Build g-s-d plug-in:        ' + enable_gsd_plugin.to_string() + '\n\n'
output += '        compiler:                   ' + cc.get_id() + '\n'
output += '        cflags:                     ' + ' '.join(compiler_flags) + '\n\n'
output += '        (Change with: meson configure BUILDDIR -D logind=libsystemd|libelogind|none -D gsd_plugin=true|false)\n\n'
message(output)
