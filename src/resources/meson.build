fs = import('fs')

ui_sources = files(
  'ui/gdu-benchmark-dialog.blp',
  'ui/gdu-block-row.blp',
  'ui/gdu-change-passphrase-dialog.blp',
  'ui/gdu-create-confirm-page.blp',
  'ui/gdu-create-disk-image-dialog.blp',
  'ui/gdu-create-filesystem-page.blp',
  'ui/gdu-create-other-page.blp',
  'ui/gdu-create-partition-page.blp',
  'ui/gdu-create-password-page.blp',
  'ui/gdu-disk-settings-dialog.blp',
  'ui/gdu-drive-header.blp',
  'ui/gdu-drive-row.blp',
  'ui/gdu-drive-view.blp',
  'ui/gdu-edit-filesystem-dialog.blp',
  'ui/gdu-edit-partition-dialog.blp',
  'ui/gdu-encryption-options-dialog.blp',
  'ui/gdu-format-disk-dialog.blp',
  'ui/gdu-format-volume-dialog.blp',
  'ui/gdu-image-mounter-window.blp',
  'ui/gdu-mount-options-dialog.blp',
  'ui/gdu-new-disk-image-dialog.blp',
  'ui/gdu-resize-volume-dialog.blp',
  'ui/gdu-restore-disk-image-dialog.blp',
  'ui/gdu-take-ownership-dialog.blp',
  'ui/gdu-unlock-dialog.blp',
  'ui/gdu-window.blp',
  'ui/shortcuts-dialog.blp',
)

ui_compiled = []
foreach ui_source: ui_sources
  ui_compiled += fs.name(fs.replace_suffix(ui_source, '.ui'))
endforeach

blueprints = custom_target('blueprints',
  input: ui_sources,
  output: ui_compiled,
  command: [blueprint_cmd, 'batch-compile', '--minify', '@OUTDIR@', '@CURRENT_SOURCE_DIR@', '@INPUT@'],
)

resource_data = files(
  'ui/gdu-ata-smart-dialog.ui',
  'ui/erase-multiple-disks-dialog.ui',
  'style.css',
)

gdu_resources = gnome.compile_resources(
  'resources',
  'gnome-disks.gresource.xml',
  dependencies: [blueprints, resource_data],
  source_dir: [meson.current_build_dir(), meson.current_source_dir()],
  export: true,
)
