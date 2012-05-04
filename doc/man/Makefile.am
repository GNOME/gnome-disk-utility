
NULL =

man_MANS = 				\
	gnome-disks.1			\
	gnome-disk-image-mounter.1	\
	$(NULL)

gnome-disks.1 : gnome-disks.xml
	$(XSLTPROC) -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

gnome-disk-image-mounter.1 : gnome-disk-image-mounter.xml
	$(XSLTPROC) -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<


EXTRA_DIST= 				\
	gnome-disks.xml			\
	gnome-disk-image-mounter.xml    \
	$(NULL)

clean-local:
	rm -f *~ *.1
