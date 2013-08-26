NULL =

SUBDIRS = src data po doc

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

# TODO: remove --disable-gsd-plugin when the g-s-d ABI settles again
#
DISTCHECK_CONFIGURE_FLAGS=                                      \
	--disable-gsd-plugin					\
        $(NULL)

clean-local :
	rm -f *~ po/*~
