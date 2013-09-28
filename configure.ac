AC_INIT([GNOME Disk Utility],[3.10.0],[https://bugzilla.gnome.org/enter_bug.cgi?product=gnome-disk-utility],[gnome-disk-utility])

AM_INIT_AUTOMAKE([1.9 foreign dist-xz no-dist-gzip tar-ustar])

AC_CONFIG_MACRO_DIR([m4])
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

# ***********
# Compilation
# ***********

CC_CHECK_CFLAGS_APPEND([                          \
        -Waggregate-return                        \
        -Wall                                     \
        -W                                        \
        -Wextra                                   \
        -Wcast-align                              \
        -Wdeclaration-after-statement             \
        -Wendif-labels                            \
        -Wformat-security                         \
        -Wimplicit-function-declaration           \
        -Woverflow                                \
        -Wformat-nonliteral                       \
        -Wformat=2                                \
        -Wimplicit                                \
        -Winit-self                               \
        -Wlogical-op                              \
        -Wmissing-declarations                    \
        -Wmissing-format-attribute                \
        -Wmissing-include-dirs                    \
        -Wmissing-noreturn                        \
        -Wmissing-prototypes                      \
        -Wnested-externs                          \
        -Wno-inline                               \
        -Wno-long-long                            \
        -Wno-missing-field-initializers           \
        -Wno-overlength-strings                   \
        -Wno-switch-enum                          \
        -Wno-unused-parameter                     \
        -Wno-unused-result                        \
        -Wold-style-definition                    \
        -Wpacked                                  \
        -Wpointer-arith                           \
        -Wshadow                                  \
        -Wsign-compare                            \
        -Wstrict-aliasing=2                       \
        -Wstrict-prototypes                       \
        -Wundef                                   \
        -Wuninitialized                           \
        -Wmaybe-uninitialized                     \
        -Wunsafe-loop-optimizations               \
        -Wvla                                     \
        -Wwrite-strings                           \
        -fdata-sections                           \
        -fdiagnostics-show-option                 \
        -ffast-math                               \
        -ffunction-sections                       \
        -fno-common                               \
        -fno-strict-aliasing                      \
])

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
AC_DEFINE(HAVE_FALLOCATE,1,[Have valid fallocate() function])],[
AC_MSG_RESULT([no])])

# ***************************
# Check for required packages
# ***************************

GLIB2_REQUIRED=2.31.0
UDISKS2_REQUIRED=2.1.1
GTK3_REQUIRED=3.5.8
LIBSECRET1_REQUIRED=0.7
PWQUALITY_REQUIRED=1.0.0
CANBERRA_REQUIRED=0.1
LIBDVDREAD_REQUIRED=4.2.0
GSD_PLUGIN_REQUIRED=3.6
LIBNOTIFY_REQUIRED=0.7
LIBLZMA_REQUIRED=5.0.5

PKG_CHECK_MODULES(GLIB2, [gmodule-2.0 gio-unix-2.0 >= $GLIB2_REQUIRED])
PKG_CHECK_MODULES(UDISKS2, [udisks2 >= $UDISKS2_REQUIRED])
PKG_CHECK_MODULES(GTK3, [gtk+-3.0 >= $GTK3_REQUIRED])
PKG_CHECK_MODULES(LIBSECRET1, [libsecret-1 >= $LIBSECRET1_REQUIRED])
PKG_CHECK_MODULES(PWQUALITY, [pwquality >= $PWQUALITY_REQUIRED])
PKG_CHECK_MODULES(CANBERRA, [libcanberra-gtk3 >= $CANBERRA_REQUIRED])
PKG_CHECK_MODULES(LIBDVDREAD, [dvdread >= $LIBDVDREAD_REQUIRED])
PKG_CHECK_MODULES(GSD_PLUGIN, [gnome-settings-daemon >= $GSD_PLUGIN_REQUIRED])
PKG_CHECK_MODULES(LIBNOTIFY, [libnotify >= $LIBNOTIFY_REQUIRED])
PKG_CHECK_MODULES(LIBLZMA, [liblzma >= $LIBLZMA_REQUIRED])

gsd_plugindir='${libdir}/gnome-settings-daemon-3.0'
AC_SUBST([gsd_plugindir])

GLIB_GSETTINGS

dnl **********************************
dnl *** Check for libsystemd-login ***
dnl **********************************

AC_ARG_ENABLE(libsystemd_login, AS_HELP_STRING([--disable-libsystemd-login],[build without liblibsystemd-login]))
msg_libsystemd_login=no
LIBSYSTEMD_LOGIN_LIBS=
LIBSYSTEMD_LOGIN_CFLAGS=
LIBSYSTEMD_LOGIN_REQUIRED=44

if test "x$enable_libsystemd_login" != "xno"; then
  PKG_CHECK_EXISTS([libsystemd-login >= $LIBSYSTEMD_LOGIN_REQUIRED], msg_libsystemd_login=yes)

  if test "x$msg_libsystemd_login" = "xyes"; then
    PKG_CHECK_MODULES([LIBSYSTEMD_LOGIN],[libsystemd-login >= $LIBSYSTEMD_LOGIN_REQUIRED])
    AC_DEFINE(HAVE_LIBSYSTEMD_LOGIN, 1, [Define to 1 if liblibsystemd_login is available])
  fi
fi

AM_CONDITIONAL(USE_LIBSYSTEMD_LOGIN, [test "$msg_libsystemd_login" = "yes"])

dnl *************************************
dnl *** gnome-settings-daemon plug-in ***
dnl *************************************

AC_ARG_ENABLE(gsd_plugin, AS_HELP_STRING([--disable-gsd-plugin],[don't build gnome-settings-daemon plug-in]))
msg_gsd_plugin=no
if test "x$enable_gsd_plugin" != "xno"; then
   msg_gsd_plugin=yes
fi
AM_CONDITIONAL(USE_GSD_PLUGIN, [test "$msg_gsd_plugin" = "yes"])

# ********************
# Internationalization
# ********************

AC_PATH_PROG([XSLTPROC], [xsltproc])
if test -z "$XSLTPROC"; then
  AC_MSG_ERROR([xsltproc is needed])
fi

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
data/icons/HighContrast/Makefile
data/icons/HighContrast/16x16/Makefile
data/icons/HighContrast/22x22/Makefile
data/icons/HighContrast/24x24/Makefile
data/icons/HighContrast/32x32/Makefile
data/icons/HighContrast/48x48/Makefile
data/icons/HighContrast/256x256/Makefile
data/org.gnome.Disks.gschema.xml.in
data/org.gnome.settings-daemon.plugins.gdu-sd.gschema.xml.in
data/ui/Makefile
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

        Use libsystem-login:        ${msg_libsystemd_login}
        Build g-s-d plug-in:        ${msg_gsd_plugin}

        compiler:                   ${CC}
        cflags:                     ${CFLAGS}
        cppflags:                   ${CPPFLAGS}

        Maintainer mode:            ${USE_MAINTAINER_MODE}
"
