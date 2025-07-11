install_subdir(
  'icons/hicolor',
  install_dir: gdu_datadir / 'icons',
)

desktops = [
  'org.gnome.DiskUtility.desktop',
  'gnome-disk-image-mounter.desktop',
]

foreach desktop: desktops
  i18n.merge_file (
    type: 'desktop',
    input: desktop + '.in',
    output: desktop,
    po_dir: po_dir,
    install: true,
    install_dir: gdu_datadir / 'applications',
  )
endforeach


configure_file(
  input: 'org.gnome.DiskUtility.service.in',
  output: '@BASENAME@',
  configuration: {'bindir': gdu_prefix / gdu_bindir},
  install: true,
  install_dir: gdu_datadir / 'dbus-1/services',
)

install_data(
  'org.gnome.Disks.gschema.xml',
  install_dir: join_paths(gdu_datadir, 'glib-2.0', 'schemas')
)

info = 'org.gnome.DiskUtility.metainfo.xml'

metainfo_file = i18n.merge_file(
  input: info + '.in',
  output: info,
  po_dir: po_dir,
  install: true,
  install_dir: gdu_datadir / 'metainfo',
)

# Validate AppStream metadata
appstreamcli = find_program('appstreamcli', required: false)
if (appstreamcli.found())
  test('validate-metainfo',
    appstreamcli,
    args: ['validate', '--no-net', '--explain', metainfo_file],
    workdir: meson.current_build_dir()
  )
endif
