NULL =

SUBDIRS = src data po doc

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

# TODO: remove --disable-gsd-plugin when the g-s-d ABI settles again
#
DISTCHECK_CONFIGURE_FLAGS=                                      \
	--disable-gsd-plugin					\
        $(NULL)

MAINTAINERCLEANFILES = \
	$(GITIGNORE_MAINTAINERCLEANFILES_TOPLEVEL)	\
	$(GITIGNORE_MAINTAINERCLEANFILES_MAKEFILE_IN)	\
	$(GITIGNORE_MAINTAINERCLEANFILES_M4_LIBTOOL)	\
	$(NULL)

GITIGNOREFILES = m4

clean-local :
	rm -f *~ po/*~

-include $(top_srcdir)/git.mk
