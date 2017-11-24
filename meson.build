project(
  'gnome-disk-utility', 'c',
  version: '3.27.4',
  license: 'GPL2.0',
  default_options: 'buildtype=debugoptimized',
  meson_version: '>= 0.43.0'
)

gdu_name = 'gnome-disk-utility'
gdu_version = meson.project_version()

gdu_prefix = get_option('prefix')
gdu_bindir = join_paths(gdu_prefix, get_option('bindir'))
gdu_datadir = join_paths(gdu_prefix, get_option('datadir'))
gdu_libexecdir = join_paths(gdu_prefix, get_option('libexecdir'))
gdu_localedir = join_paths(gdu_prefix, get_option('localedir'))
gdu_mandir = join_paths(gdu_prefix, get_option('mandir'))
gdu_sysconfdir = join_paths(gdu_prefix, get_option('sysconfdir'))

cc = meson.get_compiler('c')

config_h = configuration_data()
config_h.set_quoted('PACKAGE_VERSION', gdu_version)
config_h.set_quoted('GETTEXT_PACKAGE', gdu_name)

message('Checking for valid fallocate() function')

fallocate_src = '''
  #include <unistd.h>
  #include <sys/types.h>
  #include <linux/falloc.h>
  #include <fcntl.h>
  int main() {
    long ret;

    ret = fallocate(0, FALLOC_FL_KEEP_SIZE, 0xfffffffful, 0xfffffffful);
    if (ret != 0) {
      return 1;
    }
  };
'''
config_h.set('HAVE_FALLOCATE', cc.run(fallocate_src).returncode())

# compiler flags
compiler_flags = []

if get_option('buildtype').contains('debug')
  test_cflags = [
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
    '-Wwrite-strings'
  ]

  compiler_flags += cc.get_supported_arguments(test_cflags)
endif

add_project_arguments(compiler_flags, language: 'c')

gmodule_dep = dependency('gmodule-2.0')
gio_unix_dep = dependency('gio-unix-2.0', version: '>= 2.31.0')
udisk_dep = dependency('udisks2', version: '>= 2.1.1')
gtk_dep = dependency('gtk+-3.0', version: '>= 3.16.0')
libsecret_dep = dependency('libsecret-1', version: '>= 0.7')
pwquality_dep = dependency('pwquality', version: '>= 1.0.0')
libcanberra_dep = dependency('libcanberra-gtk3', version: '>= 0.1')
dvdread_dep = dependency('dvdread', version: '>= 4.2.0')
libnotify_dep = dependency('libnotify', version: '>= 0.7')
liblzma_dep = dependency('liblzma', version: '>= 5.0.5')
m_dep = cc.find_library('m')

# *** Check for recent UDisks ***
udisks2_7_2_dep = dependency('udisks2', version: '>= 2.7.2', required: false)
config_h.set('HAVE_UDISKS2_7_2', udisks2_7_2_dep.found(),
             description: 'Define to 1 if UDisks version >= 2.7.2 for resize, repair, check')

udisks2_7_6_dep = dependency('udisks2', version: '>= 2.7.6', required: false)
config_h.set('HAVE_UDISKS2_7_6', udisks2_7_6_dep.found(),
             description: 'Define to 1 if UDisks version >= 2.7.6 for userspace mount options')

# *** Check for libsystemd ***
enable_libsystemd = get_option('libsystemd')
if enable_libsystemd
  libsystemd_dep = dependency('libsystemd', version: '>= 209', required: false)
  config_h.set('HAVE_LIBSYSTEMD', libsystemd_dep.found(),
               description: 'Define to 1 if libsystemd is available')
endif

gnome = import('gnome')
i18n = import('i18n')

po_dir = join_paths(meson.source_root(), 'po')

top_inc = include_directories('.', 'src')

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
subdir('doc/man')

configure_file(
  output: 'config.h',
  configuration: config_h
)

meson.add_install_script(
  'meson_post_install.py',
  gdu_datadir
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
output += '        Use libsystemd:             ' + enable_libsystemd.to_string() + '\n'
output += '        Build g-s-d plug-in:        ' + enable_gsd_plugin.to_string() + '\n\n'
output += '        compiler:                   ' + cc.get_id() + '\n'
output += '        cflags:                     ' + ' '.join(compiler_flags) + '\n\n'
output += '        (Change with: meson configure BUILDDIR -D libsystemd=true|false -D gsd_plugin=true|false)\n\n'
message(output)
