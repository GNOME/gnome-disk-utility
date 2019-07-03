xsltproc_cmd = [
  find_program('xsltproc'),
  '--output', '@OUTPUT@',
  '--nonet',
  'http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl',
  '@INPUT@',
]

mans = [
  'gnome-disks',
  'gnome-disk-image-mounter',
]

foreach man: mans
  output = man + '.1'

  custom_target(
    output,
    input: man + '.xml',
    output: output,
    command: xsltproc_cmd,
    install: true,
    install_dir: gdu_mandir / 'man1',
  )
endforeach
