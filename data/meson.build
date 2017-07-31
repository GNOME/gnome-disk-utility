subdir('icons')

desktops = [
  'org.gnome.DiskUtility.desktop',
  'gnome-disk-image-mounter.desktop',
  'gnome-disk-image-writer.desktop'
]

foreach desktop: desktops
  i18n.merge_file (
    desktop,
    type: 'desktop',
    input: desktop + '.in',
    output: desktop,
    po_dir: po_dir,
    install: true,
    install_dir: join_paths(gdu_datadir, 'applications')
  )
endforeach

service_conf = configuration_data()
service_conf.set('bindir', gdu_bindir)

service = 'org.gnome.DiskUtility.service'

configure_file(
  input: service + '.in',
  output: service,
  install: true,
  install_dir: join_paths(gdu_datadir, 'dbus-1', 'services'),
  configuration: service_conf
)

install_data(
  'org.gnome.Disks.gschema.xml',
  install_dir: join_paths(gdu_datadir, 'glib-2.0', 'schemas')
)

info = 'org.gnome.DiskUtility.appdata.xml'

i18n.merge_file(
  info,
  input: 'org.gnome.DiskUtility.appdata.xml.in',
  output: info,
  po_dir: po_dir,
  install: true,
  install_dir: join_paths(gdu_datadir, 'metainfo')
)
