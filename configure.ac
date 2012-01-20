AC_INIT([GNOME Disk Utility],[3.3.90],[https://bugzilla.gnome.org/enter_bug.cgi?product=gnome-disk-utility],[gnome-disk-utility])

AM_INIT_AUTOMAKE([1.9 foreign dist-bzip2 no-dist-gzip])

AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADERS([config.h])

AM_MAINTAINER_MODE

AC_PROG_CC
AC_ISC_POSIX
AC_HEADER_STDC
AC_PROG_LIBTOOL

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# *****
# GNOME
# *****

GNOME_COMMON_INIT
GNOME_DEBUG_CHECK
GNOME_COMPILE_WARNINGS([maximum])
GNOME_MAINTAINER_MODE_DEFINES

# ***************************
# Check for required packages
# ***************************

GLIB2_REQUIRED=2.31.0
UDISKS2_REQUIRED=1.90
GTK3_REQUIRED=3.3.0

PKG_CHECK_MODULES(GLIB2, [gio-unix-2.0 >= $GLIB2_REQUIRED])
PKG_CHECK_MODULES(UDISKS2, [udisks2 >= $UDISKS2_REQUIRED])
PKG_CHECK_MODULES(GTK3, [gtk+-3.0 >= $GTK3_REQUIRED])

# ********************
# Internationalization
# ********************

IT_PROG_INTLTOOL([$INTLTOOL_REQUIRED])

GETTEXT_PACKAGE=gnome-disk-utility
AC_SUBST([GETTEXT_PACKAGE])
AM_GLIB_GNU_GETTEXT
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[gettext domain])

AC_SUBST([AM_CPPFLAGS])
AC_SUBST([AM_CFLAGS])
AC_SUBST([AM_CXXFLAGS])
AC_SUBST([AM_LDFLAGS])

AC_CONFIG_FILES([
Makefile
src/Makefile
src/palimpsest/Makefile
po/Makefile.in
data/Makefile
data/icons/Makefile
data/icons/16x16/Makefile
data/icons/22x22/Makefile
data/icons/24x24/Makefile
data/icons/32x32/Makefile
data/icons/48x48/Makefile
data/icons/256x256/Makefile
data/icons/scalable/Makefile
data/ui/Makefile
])

AC_OUTPUT

echo "
                  gnome-disk-utility $VERSION
                =============================

        prefix:                     ${prefix}
        libdir:                     ${libdir}
        libexecdir:                 ${libexecdir}
        bindir:                     ${bindir}
        sbindir:                    ${sbindir}
        datadir:                    ${datadir}
        sysconfdir:                 ${sysconfdir}
        localstatedir:              ${localstatedir}

        compiler:                   ${CC}
        cflags:                     ${CFLAGS}
        cppflags:                   ${CPPFLAGS}

        Maintainer mode:            ${USE_MAINTAINER_MODE}
"
