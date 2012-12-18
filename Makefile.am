NULL =

SUBDIRS = src data po doc

# TODO: remove --disable-gsd-plugin when the g-s-d ABI settles again
#
DISTCHECK_CONFIGURE_FLAGS=                                      \
	--disable-gsd-plugin					\
        $(NULL)

clean-local :
	rm -f *~ po/*~
