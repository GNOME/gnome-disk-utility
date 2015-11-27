AC_INIT([GNOME Disk Utility],[3.18.3.1],[https://bugzilla.gnome.org/enter_bug.cgi?product=gnome-disk-utility],[gnome-disk-utility])

AX_IS_RELEASE([git-directory])

AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC
AC_HEADER_STDC

AM_INIT_AUTOMAKE([1.11 foreign dist-xz no-dist-gzip tar-ustar])
AM_MAINTAINER_MODE([enable])
AM_SILENT_RULES([yes])

LT_PREREQ([2.2])
LT_INIT

# ***********
# Compilation
# ***********

AX_COMPILER_FLAGS([WARN_CFLAGS],[WARN_LDFLAGS])

AC_MSG_CHECKING([for valid fallocate() function])
AC_LINK_IFELSE([
AC_LANG_PROGRAM([[
#include <unistd.h>
#include <sys/types.h>
#include <linux/falloc.h>
#include <fcntl.h>
]],[[
   long ret;

   ret = fallocate(0, FALLOC_FL_KEEP_SIZE, 0xfffffffful, 0xfffffffful);

   if (ret != 0) {
      return 1;
   }
   ]])],[
AC_MSG_RESULT([yes])
AC_DEFINE([HAVE_FALLOCATE],[1],[Have valid fallocate() function])],[
AC_MSG_RESULT([no])])

# ***************************
# Check for required packages
# ***************************

GLIB2_REQUIRED=2.31.0
UDISKS2_REQUIRED=2.1.1
GTK3_REQUIRED=3.16.0
LIBSECRET1_REQUIRED=0.7
PWQUALITY_REQUIRED=1.0.0
CANBERRA_REQUIRED=0.1
LIBDVDREAD_REQUIRED=4.2.0
GSD_PLUGIN_REQUIRED=3.8
LIBNOTIFY_REQUIRED=0.7
LIBLZMA_REQUIRED=5.0.5

PKG_CHECK_MODULES([GLIB2], [gmodule-2.0 gio-unix-2.0 >= $GLIB2_REQUIRED])
PKG_CHECK_MODULES([UDISKS2], [udisks2 >= $UDISKS2_REQUIRED])
PKG_CHECK_MODULES([GTK3], [gtk+-3.0 >= $GTK3_REQUIRED])
PKG_CHECK_MODULES([LIBSECRET1], [libsecret-1 >= $LIBSECRET1_REQUIRED])
PKG_CHECK_MODULES([PWQUALITY], [pwquality >= $PWQUALITY_REQUIRED])
PKG_CHECK_MODULES([CANBERRA], [libcanberra-gtk3 >= $CANBERRA_REQUIRED])
PKG_CHECK_MODULES([LIBDVDREAD], [dvdread >= $LIBDVDREAD_REQUIRED])
PKG_CHECK_MODULES([LIBNOTIFY], [libnotify >= $LIBNOTIFY_REQUIRED])
PKG_CHECK_MODULES([LIBLZMA], [liblzma >= $LIBLZMA_REQUIRED])

gsd_plugindir='${libdir}/gnome-settings-daemon-3.0'
AC_SUBST([gsd_plugindir])

AX_REQUIRE_DEFINED([GLIB_GSETTINGS])
GLIB_GSETTINGS

AC_PATH_PROG([GLIB_COMPILE_RESOURCES], [glib-compile-resources])

dnl ****************************
dnl *** Check for libsystemd ***
dnl ****************************

AC_ARG_ENABLE([libsystemd], AS_HELP_STRING([--disable-libsystemd], [build without libsystemd]))
msg_libsystemd=no
LIBSYSTEMD_REQUIRED=209

if test "x$enable_libsystemd" != "xno"; then
  PKG_CHECK_EXISTS([libsystemd >= $LIBSYSTEMD_REQUIRED], [msg_libsystemd=yes])

  if test "x$msg_libsystemd" = "xyes"; then
    PKG_CHECK_MODULES([LIBSYSTEMD], [libsystemd >= $LIBSYSTEMD_REQUIRED])
    AC_DEFINE(HAVE_LIBSYSTEMD, 1, [Define to 1 if libsystemd is available])
  fi
fi

AM_CONDITIONAL([USE_LIBSYSTEMD], [test "$msg_libsystemd" = "yes"])

dnl *************************************
dnl *** gnome-settings-daemon plug-in ***
dnl *************************************

AC_ARG_ENABLE([gsd_plugin], AS_HELP_STRING([--disable-gsd-plugin],[don't build gnome-settings-daemon plug-in]))
msg_gsd_plugin=no
if test "x$enable_gsd_plugin" != "xno"; then
  msg_gsd_plugin=yes
  PKG_CHECK_MODULES([GSD_PLUGIN], [gnome-settings-daemon >= GSD_PLUGIN_REQUIRED])
fi
AM_CONDITIONAL([USE_GSD_PLUGIN], [test "$msg_gsd_plugin" = "yes"])

# *******
# Appdata
# *******

AX_REQUIRE_DEFINED([APPSTREAM_XML])
APPSTREAM_XML

# ********************
# Internationalization
# ********************

AC_PATH_PROG([XSLTPROC], [xsltproc])
if test -z "$XSLTPROC"; then
  AC_MSG_ERROR([xsltproc is needed])
fi

AX_REQUIRE_DEFINED([IT_PROG_INTLTOOL])
IT_PROG_INTLTOOL([0.50.2])

GETTEXT_PACKAGE=gnome-disk-utility
AC_SUBST([GETTEXT_PACKAGE])
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[gettext domain])

AC_SUBST([AM_CPPFLAGS])
AC_SUBST([AM_CFLAGS])
AC_SUBST([AM_CXXFLAGS])
AC_SUBST([AM_LDFLAGS])

AC_CONFIG_FILES([
Makefile
src/Makefile
src/libgdu/Makefile
src/disks/Makefile
src/disk-image-mounter/Makefile
src/notify/Makefile
po/Makefile.in
data/Makefile
data/icons/Makefile
data/icons/hicolor/Makefile
data/icons/hicolor/16x16/Makefile
data/icons/hicolor/22x22/Makefile
data/icons/hicolor/24x24/Makefile
data/icons/hicolor/32x32/Makefile
data/icons/hicolor/48x48/Makefile
data/icons/hicolor/256x256/Makefile
data/icons/hicolor/scalable/Makefile
doc/Makefile
doc/man/Makefile
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

        Use libsystemd:             ${msg_libsystemd}
        Build g-s-d plug-in:        ${msg_gsd_plugin}

        compiler:                   ${CC}
        cflags:                     ${CFLAGS}
        cppflags:                   ${CPPFLAGS}

        Maintainer mode:            ${USE_MAINTAINER_MODE}
"
