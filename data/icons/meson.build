icon_sizes = [
  '16x16',
  '22x22',
  '24x24',
  '32x32',
  '48x48',
  '256x256'
]

foreach icon_size: icon_sizes
  install_data(
    'hicolor/' + icon_size + '/gnome-disks.png',
    install_dir: join_paths(gdu_datadir, 'icons', 'hicolor', icon_size, 'apps')
  )
endforeach

scalable_icons = files(
  'hicolor/scalable/gnome-disks-state-standby-symbolic.svg',
  'hicolor/scalable/gnome-disks-symbolic.svg'
)

install_data(
  scalable_icons,
  install_dir: join_paths(gdu_datadir, 'icons', 'hicolor', 'scalable', 'apps')
)
