dnl
dnl Contains the following macros
dnl   SANE_SET_AM_CFLAGS(is_release)
dnl   SANE_CHECK_MISSING_HEADERS
dnl   SANE_SET_AM_LDFLAGS
dnl   SANE_CHECK_DLL_LIB
dnl   SANE_EXTRACT_LDFLAGS(LIBS, LDFLAGS)
dnl   SANE_CHECK_JPEG
dnl   SANE_CHECK_IEEE1284
dnl   SANE_CHECK_PTHREAD
dnl   SANE_CHECK_LOCKING
dnl   JAPHAR_GREP_AM_CFLAGS(flag, cmd_if_missing, cmd_if_present)
dnl   SANE_CHECK_U_TYPES
dnl   SANE_CHECK_GPHOTO2
dnl   SANE_CHECK_IPV6
dnl   SANE_CHECK_BACKENDS
dnl   SANE_PROTOTYPES
dnl   AC_PROG_LIBTOOL
dnl

# SANE_SET_AM_CFLAGS(is_release)
# Set default AM_CFLAGS if gcc is used.  Enable/disable additional
# compilation warnings.  The extra warnings are enabled by default
# during the development cycle but disabled for official releases.
# The argument is_release is either yes or no.
AC_DEFUN([SANE_SET_AM_CFLAGS],
[
if test "${ac_cv_c_compiler_gnu}" = "yes"; then
  DEFAULT_WARNINGS="\
      -Wall"
  EXTRA_WARNINGS="\
      -Wextra \
      -pedantic"

  for flag in $DEFAULT_WARNINGS; do
    JAPHAR_GREP_AM_CFLAGS($flag, [ AM_CFLAGS="$AM_CFLAGS $flag" ])
    JAPHAR_GREP_AM_CXXFLAGS($flag, [ AM_CXXFLAGS="$AM_CXXFLAGS $flag" ])
  done

  AC_ARG_ENABLE(warnings,
    AS_HELP_STRING([--enable-warnings],
                   [turn on tons of compiler warnings (GCC only)]),
    [
      if eval "test x$enable_warnings = xyes"; then
        for flag in $EXTRA_WARNINGS; do
          JAPHAR_GREP_AM_CFLAGS($flag, [ AM_CFLAGS="$AM_CFLAGS $flag" ])
          JAPHAR_GREP_AM_CXXFLAGS($flag, [ AM_CXXFLAGS="$AM_CXXFLAGS $flag" ])
        done
      fi
    ],
    [if test x$1 = xno; then
       # Warnings enabled by default (development)
       for flag in $EXTRA_WARNINGS; do
         JAPHAR_GREP_AM_CFLAGS($flag, [ AM_CFLAGS="$AM_CFLAGS $flag" ])
         JAPHAR_GREP_AM_CXXFLAGS($flag, [ AM_CXXFLAGS="$AM_CXXFLAGS $flag" ])
       done
    fi])
fi # ac_cv_c_compiler_gnu
])

dnl SANE_CHECK_MISSING_HEADERS
dnl Do some sanity checks. It doesn't make sense to proceed if those headers
dnl aren't present.
AC_DEFUN([SANE_CHECK_MISSING_HEADERS],
[
  MISSING_HEADERS=
  if test "${ac_cv_header_fcntl_h}" != "yes" ; then
    MISSING_HEADERS="${MISSING_HEADERS}\"fcntl.h\" "
  fi
  if test "${ac_cv_header_sys_time_h}" != "yes" ; then
    MISSING_HEADERS="${MISSING_HEADERS}\"sys/time.h\" "
  fi
  if test "${ac_cv_header_unistd_h}" != "yes" ; then
    MISSING_HEADERS="${MISSING_HEADERS}\"unistd.h\" "
  fi
  if test "${MISSING_HEADERS}" != "" ; then
    echo "*** The following essential header files couldn't be found:"
    echo "*** ${MISSING_HEADERS}"
    echo "*** Maybe the compiler isn't ANSI C compliant or not properly installed?"
    echo "*** For details on what went wrong see config.log."
    AC_MSG_ERROR([Exiting now.])
  fi
])

# SANE_SET_AM_LDFLAGS
# Add special AM_LDFLAGS
AC_DEFUN([SANE_SET_AM_LDFLAGS],
[
  # Define stricter linking policy on GNU systems.  This is not
  # added to global LDFLAGS because we may want to create convenience
  # libraries that don't require such strick linking.
  if test "$GCC" = yes; then
    case ${host_os} in
    linux* | solaris*)
      STRICT_LDFLAGS="-Wl,-z,defs"
      ;;
    esac
  fi
  AC_SUBST(STRICT_LDFLAGS)
  case "${host_os}" in
    aix*) #enable .so libraries, disable archives
      AM_LDFLAGS="$AM_LDFLAGS -Wl,-brtl"
      ;;
    darwin*) #include frameworks
      LIBS="$LIBS -framework CoreFoundation -framework IOKit"
      ;;
  esac
])

# SANE_CHECK_DLL_LIB
# Find dll library
AC_DEFUN([SANE_CHECK_DLL_LIB],
[
  dnl Checks for dll libraries: dl
  DL_LIBS=""
  if test "${enable_dynamic}" = "auto"; then
      # default to disabled unless library found.
      enable_dynamic=no
      # dlopen
      AC_CHECK_HEADERS(dlfcn.h,
      [AC_CHECK_LIB(dl, dlopen, DL_LIBS=-ldl)
       saved_LIBS="${LIBS}"
       LIBS="${LIBS} ${DL_LIBS}"
       AC_CHECK_FUNCS(dlopen, enable_dynamic=yes,)
       LIBS="${saved_LIBS}"
      ],)
      # HP/UX DLL handling
      AC_CHECK_HEADERS(dl.h,
      [AC_CHECK_LIB(dld,shl_load, DL_LIBS=-ldld)
       saved_LIBS="${LIBS}"
       LIBS="${LIBS} ${DL_LIBS}"
       AC_CHECK_FUNCS(shl_load, enable_dynamic=yes,)
       LIBS="${saved_LIBS}"
      ],)
      if test -z "$DL_LIBS" ; then
      # old Mac OS X/Darwin (without dlopen)
      AC_CHECK_HEADERS(mach-o/dyld.h,
      [AC_CHECK_FUNCS(NSLinkModule, enable_dynamic=yes,)
      ],)
      fi
  fi
  AC_SUBST(DL_LIBS)

  DYNAMIC_FLAG=
  if test "${enable_dynamic}" = yes ; then
    DYNAMIC_FLAG=-module
  fi
  AC_SUBST(DYNAMIC_FLAG)
])

#
# Separate LIBS from LDFLAGS to link correctly on HP/UX (and other
# platforms who care about the order of params to ld.  It removes all
# non '-l..'-params from passed in $1(LIBS), and appends them to $2(LDFLAGS).
#
# Use like this: SANE_EXTRACT_LDFLAGS(PKGNAME_LIBS, PKGNAME_LDFLAGS)
 AC_DEFUN([SANE_EXTRACT_LDFLAGS],
[
    tmp_LIBS=""
    for param in ${$1}; do
        case "${param}" in
	-l*)
         tmp_LIBS="${tmp_LIBS} ${param}"
	 ;;
	 *)
         $2="${$2} ${param}"
	 ;;
	 esac
     done
     $1="${tmp_LIBS}"
     unset tmp_LIBS
     unset param
])

#
#
# Checks for ieee1284 library, needed for canon_pp backend.
AC_DEFUN([SANE_CHECK_IEEE1284],
[
  AC_CHECK_HEADER(ieee1284.h, [
    AC_CACHE_CHECK([for libieee1284 >= 0.1.5], sane_cv_use_libieee1284, [
      AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <ieee1284.h>]], [[
	struct parport p; char *buf;
	ieee1284_nibble_read(&p, 0, buf, 1);
	]])],
        [sane_cv_use_libieee1284="yes"; IEEE1284_LIBS="-lieee1284"
      ],[sane_cv_use_libieee1284="no"])
    ],)
  ],)
  if test "$sane_cv_use_libieee1284" = "yes" ; then
    AC_DEFINE(HAVE_LIBIEEE1284,1,[Define to 1 if you have the `ieee1284' library (-lcam).])
  fi
  AC_SUBST(IEEE1284_LIBS)
])

#
# Checks for pthread support
AC_DEFUN([SANE_CHECK_PTHREAD],
[

  case "${host_os}" in
  linux* | darwin* | mingw*) # enabled by default on Linux, MacOS X and MINGW
    use_pthread=yes
    ;;
  *)
    use_pthread=no
  esac
  have_pthread=no

  #
  # now that we have the systems preferences, we check
  # the user

  AC_ARG_ENABLE([pthread],
    AS_HELP_STRING([--enable-pthread],
                   [use pthread instead of fork (default=yes for Linux/MacOS X/MINGW, no for everything else)]),
    [
      if test $enableval = yes ; then
        use_pthread=yes
      else
        use_pthread=no
      fi
    ])

  if test $use_pthread = yes ; then
  AC_CHECK_HEADERS(pthread.h,
    [
       AC_CHECK_LIB(pthread, pthread_create, PTHREAD_LIBS="-lpthread")
       have_pthread=yes
       save_LIBS="$LIBS"
       LIBS="$LIBS $PTHREAD_LIBS"
       AC_CHECK_FUNCS([pthread_create pthread_kill pthread_join pthread_detach pthread_cancel pthread_testcancel],
	,[ have_pthread=no; use_pthread=no ])
       LIBS="$save_LIBS"
    ],[ have_pthread=no; use_pthread=no ])
  fi

  # Based on a similar test for pthread_key_t from the Python project.
  # See https://bugs.python.org/review/25658/patch/19209/75870
  AC_MSG_CHECKING(whether pthread_t is integer)
  AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([[#include <pthread.h>]], [[pthread_t k; k * 1;]])],
    [ac_pthread_t_is_integer=yes],
    [ac_pthread_t_is_integer=no]
  )
  AC_MSG_RESULT($ac_pthread_t_is_integer)
  if test "$ac_pthread_t_is_integer" = yes ; then
    AC_DEFINE(PTHREAD_T_IS_INTEGER, 1,
              [Define if pthread_t is integer.])
  else
    case "$host_os" in
      darwin*)
        # Always use pthreads on macOS
        use_pthread=yes
        ;;
      *)
        # Until the sanei_thread implementation is fixed.
        use_pthread=no
        ;;
    esac
  fi

  if test "$have_pthread" = "yes" ; then
    AM_CPPFLAGS="${AM_CPPFLAGS} -D_REENTRANT"
  fi
  AC_SUBST(PTHREAD_LIBS)

  if test $use_pthread = yes ; then
    AC_DEFINE_UNQUOTED(USE_PTHREAD, "$use_pthread",
                   [Define if pthreads should be used instead of forked processes.])
    SANEI_THREAD_LIBS=$PTHREAD_LIBS
  else
    SANEI_THREAD_LIBS=""
  fi
  AC_SUBST(SANEI_THREAD_LIBS)
  AC_MSG_CHECKING([whether to enable pthread support])
  AC_MSG_RESULT([$have_pthread])
  AC_MSG_CHECKING([whether to use pthread instead of fork])
  AC_MSG_RESULT([$use_pthread])
])

#
# Checks for jpeg library >= v6B (61), needed for DC210,  DC240,
# GPHOTO2 and dell1600n_net backends.
AC_DEFUN([SANE_CHECK_JPEG],
[
  AC_ARG_WITH(libjpeg,
    AS_HELP_STRING([--without-libjpeg], [build without libjpeg]))
  if test "$with_libjpeg" != "no" ; then
    AC_CHECK_LIB(jpeg,jpeg_start_decompress,
    [
      AC_CHECK_HEADER(jconfig.h,
      [
        AC_MSG_CHECKING([for jpeglib - version >= 61 (6a)])
        AC_EGREP_CPP(sane_correct_jpeg_lib_version_found,
        [
          #include <jpeglib.h>
          #if JPEG_LIB_VERSION >= 61
            sane_correct_jpeg_lib_version_found
          #endif
        ], [sane_cv_use_libjpeg="yes"; JPEG_LIBS="-ljpeg";
        AC_MSG_RESULT(yes)],[AC_MSG_RESULT(no)])
      ],)
    ],)
  fi
  if test "$sane_cv_use_libjpeg" = "yes" ; then
    AC_DEFINE(HAVE_LIBJPEG,1,[Define to 1 if you have the libjpeg library.])
  elif test "$with_libjpeg" = "yes" ; then
    AC_MSG_ERROR([libjpeg requested but not found])
  fi
  AC_SUBST(JPEG_LIBS)
])

# Checks for tiff library dell1600n_net backend.
AC_DEFUN([SANE_CHECK_TIFF],
[
  AC_ARG_WITH(libtiff,
    AS_HELP_STRING([--without-libtiff], [build without libtiff]))
  if test "$with_libtiff" != "no" ; then
    AC_CHECK_LIB(tiff,TIFFFdOpen,
    [
      AC_CHECK_HEADER(tiffio.h,
      [sane_cv_use_libtiff="yes"; TIFF_LIBS="-ltiff"],)
    ],)
  fi
  if test "$sane_cv_use_libtiff" = "yes" ; then
     AC_DEFINE(HAVE_LIBTIFF,1,[Define to 1 if you have the libtiff library.])
  elif test "$with_libtiff" = "yes" ; then
    AC_MSG_ERROR([libtiff requested but not found])
  fi
  AC_SUBST(TIFF_LIBS)
])

# Check for OpenSSL 1.1 or higher (optional)
AC_DEFUN([SANE_CHECK_SSL], [

  SSL_LIBS=""
  # Check for libssl
  AC_CHECK_LIB([ssl], [SSL_CTX_new], [
    have_ssl_lib="yes"
  ], [
    have_ssl_lib="no"
    AC_MSG_WARN([libssl not found. SSL support will be disabled.])
  ])

  # Check for headers
  AC_CHECK_HEADER([openssl/ssl.h], [
    have_ssl_h="yes"
  ], [
    have_ssl_h="no"
    AC_MSG_WARN([Header <openssl/ssl.h> not found. SSL support will be disabled.])
  ])

  AC_CHECK_HEADER([openssl/crypto.h], [
    have_crypto_h="yes"
  ], [
    have_crypto_h="no"
    AC_MSG_WARN([Header <openssl/crypto.h> not found. SSL support will be disabled.])
  ])

  # If all basic checks passed, check version with pkg-config
  if test "x$have_ssl_lib" = "xyes" && \
     test "x$have_ssl_h" = "xyes" && \
     test "x$have_crypto_h" = "xyes"; then

    PKG_CHECK_MODULES([openssl], [openssl >= 1.1], [
      SSL_LIBS="-lssl -lcrypto"
      AC_DEFINE([HAVE_OPENSSL], [1], [Define to 1 if OpenSSL is available])
    ], [
      AC_MSG_WARN([OpenSSL 1.1 or higher not found. SSL support will be disabled.])
    ])
  fi

  AC_SUBST([SSL_LIBS])
])

AC_DEFUN([SANE_CHECK_PNG],
[
  AC_ARG_WITH(libpng,
    AS_HELP_STRING([--without-libpng], [build without libpng]))
  if test "$with_libpng" != "no" ; then
    AC_CHECK_LIB(png,png_init_io,
    [
      AC_CHECK_HEADER(png.h,
      [sane_cv_use_libpng="yes"; PNG_LIBS="-lpng"],)
    ],)
  fi
  if test "$sane_cv_use_libpng" = "yes" ; then
    AC_DEFINE(HAVE_LIBPNG,1,[Define to 1 if you have the libpng library.])
  elif test "$with_libpng" = "yes" ; then
    AC_MSG_ERROR([libpng requested but not found])
  fi
  AC_SUBST(PNG_LIBS)
])

#
# Checks for device locking support
AC_DEFUN([SANE_CHECK_LOCKING],
[
  use_locking=yes
  case "${host_os}" in
    os2* )
      use_locking=no
      ;;
  esac

  #
  # we check the user
  AC_ARG_ENABLE( [locking],
    AS_HELP_STRING([--enable-locking],
                   [activate device locking (default=yes, but only used by some backends)]),
    [
      if test $enableval = yes ; then
        use_locking=yes
      else
        use_locking=no
      fi
    ])
  if test $use_locking = yes ; then
    INSTALL_LOCKPATH=install-lockpath
    AC_DEFINE([ENABLE_LOCKING], 1,
              [Define to 1 if device locking should be enabled.])
  else
    INSTALL_LOCKPATH=
  fi
  AC_MSG_CHECKING([whether to enable device locking])
  AC_MSG_RESULT([$use_locking])
  AC_SUBST(INSTALL_LOCKPATH)
])

dnl
dnl JAPHAR_GREP_AM_CFLAGS(flag, cmd_if_missing, cmd_if_present)
dnl
dnl From Japhar.  Report changes to japhar@hungry.com
dnl
AC_DEFUN([JAPHAR_GREP_AM_CFLAGS],
[case "$AM_CFLAGS" in
"$1" | "$1 "* | *" $1" | *" $1 "* )
  ifelse($#, 3, [$3], [:])
  ;;
*)
  $2
  ;;
esac
])

dnl
dnl JAPHAR_GREP_AM_CXXFLAGS(flag, cmd_if_missing, cmd_if_present)
dnl
AC_DEFUN([JAPHAR_GREP_AM_CXXFLAGS],
[case "$AM_CXXFLAGS" in
"$1" | "$1 "* | *" $1" | *" $1 "* )
  ifelse($#, 3, [$3], [:])
  ;;
*)
  $2
  ;;
esac
])

dnl
dnl   SANE_CHECK_U_TYPES
dnl
AC_DEFUN([SANE_CHECK_U_TYPES],
[
dnl Use new style of check types that doesn't take default to use.
dnl The old style would add an #undef of the type check on platforms
dnl that defined that type... That is not portable to platform that
dnl define it as a #define.
AC_CHECK_TYPES([u_char, u_short, u_int, u_long],,,)
])

#
# Checks for gphoto2 libs, needed by gphoto2 backend
AC_DEFUN([SANE_CHECK_GPHOTO2],
[
  AC_ARG_WITH(gphoto2,
	      AS_HELP_STRING([--with-gphoto2],
			     [include the gphoto2 backend @<:@default=yes@:>@]),
	      [# If --with-gphoto2=no or --without-gphoto2, disable backend
               # as "$with_gphoto2" will be set to "no"])

  # If --with-gphoto2=yes (or not supplied), first check if
  # pkg-config exists, then use it to check if libgphoto2 is
  # present.  If all that works, then see if we can actually link
  # a program.   And, if that works, then add the -l flags to
  # GPHOTO2_LIBS and any other flags to GPHOTO2_LDFLAGS to pass to
  # sane-config.
  if test "$with_gphoto2" != "no" ; then
    AC_CHECK_TOOL(HAVE_GPHOTO2, pkg-config, false)

    if test ${HAVE_GPHOTO2} != "false" ; then
      if pkg-config --exists libgphoto2 ; then
        with_gphoto2="`pkg-config --modversion libgphoto2`"
	GPHOTO2_CPPFLAGS="`pkg-config --cflags libgphoto2`"
        GPHOTO2_LIBS="`pkg-config --libs libgphoto2`"

        saved_CPPFLAGS="${CPPFLAGS}"
        CPPFLAGS="${GPHOTO2_CPPFLAGS}"
	saved_LIBS="${LIBS}"
	LIBS="${LIBS} ${GPHOTO2_LIBS}"
 	# Make sure we an really use the library
        AC_CHECK_FUNCS(gp_camera_init, HAVE_GPHOTO2=true, HAVE_GPHOTO2=false)
	if test "${HAVE_GPHOTO2}" = "true"; then
	  AC_CHECK_FUNCS(gp_port_info_get_path)
	fi
	CPPFLAGS="${saved_CPPFLAGS}"
        LIBS="${saved_LIBS}"
      else
        HAVE_GPHOTO2=false
      fi
      if test "${HAVE_GPHOTO2}" = "false"; then
        GPHOTO2_CPPFLAGS=""
        GPHOTO2_LIBS=""
      else
        SANE_EXTRACT_LDFLAGS(GPHOTO2_LIBS, GPHOTO2_LDFLAGS)
        if pkg-config --atleast-version=2.5.0 libgphoto2; then
          AC_DEFINE([GPLOGFUNC_NO_VARGS], [1],
                    [Define if GPLogFunc does not take a va_list.])
        fi
      fi
    fi
  fi
  AC_SUBST(GPHOTO2_CPPFLAGS)
  AC_SUBST(GPHOTO2_LIBS)
  AC_SUBST(GPHOTO2_LDFLAGS)
])

#
# Check for AF_INET6, determines whether or not to enable IPv6 support
# Check for ss_family member in struct sockaddr_storage
AC_DEFUN([SANE_CHECK_IPV6],
[
  AC_MSG_CHECKING([whether to enable IPv6])
  AC_ARG_ENABLE(ipv6,
    AS_HELP_STRING([--disable-ipv6],[disable IPv6 support]),
      [  if test "$enableval" = "no" ; then
         AC_MSG_RESULT([no, manually disabled])
         ipv6=no
         fi
      ])

  if test "$ipv6" != "no" ; then
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
	#define INET6
	#include <stdlib.h>
	#include <sys/types.h>
	#include <sys/socket.h> ]], [[
	 /* AF_INET6 available check */
 	if (socket(AF_INET6, SOCK_STREAM, 0) < 0)
   	  exit(1);
 	else
   	  exit(0);
      ]])],[
        AC_MSG_RESULT(yes)
        AC_DEFINE([ENABLE_IPV6], 1, [Define to 1 if the system supports IPv6])
        ipv6=yes
      ],[
        AC_MSG_RESULT([no (couldn't compile test program)])
        ipv6=no
      ])
  fi

  if test "$ipv6" != "no" ; then
    AC_MSG_CHECKING([whether struct sockaddr_storage has an ss_family member])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
	#define INET6
	#include <stdlib.h>
	#include <sys/types.h>
	#include <sys/socket.h> ]], [[
	/* test if the ss_family member exists in struct sockaddr_storage */
	struct sockaddr_storage ss;
	ss.ss_family = AF_INET;
	exit (0);
    ]])], [
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAS_SS_FAMILY], 1, [Define to 1 if struct sockaddr_storage has an ss_family member])
    ], [
		AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
		#define INET6
		#include <stdlib.h>
		#include <sys/types.h>
		#include <sys/socket.h> ]], [[
		/* test if the __ss_family member exists in struct sockaddr_storage */
		struct sockaddr_storage ss;
		ss.__ss_family = AF_INET;
		exit (0);
	  ]])], [
		AC_MSG_RESULT([no, but __ss_family exists])
		AC_DEFINE([HAS___SS_FAMILY], 1, [Define to 1 if struct sockaddr_storage has __ss_family instead of ss_family])
	  ], [
		AC_MSG_RESULT([no])
		ipv6=no
    	  ])
    ])
  fi
])

#
# Verifies that values in $BACKENDS and updates FILTERED_BACKEND
# with either backends that can be compiled or fails the script.
AC_DEFUN([SANE_CHECK_BACKENDS],
[
if test "${user_selected_backends}" = "yes"; then
  DISABLE_MSG="aborting"
else
  DISABLE_MSG="disabling"
fi

FILTERED_BACKENDS=""
for be in ${BACKENDS}; do
  backend_supported="yes"
  case $be in
    plustek_pp)
    case "$host_os" in
      gnu*)
      echo "*** $be backend not supported on GNU/Hurd - $DISABLE_MSG"
      backend_supported="no"
      ;;
    esac
    ;;

    dc210|dc240|pixma)
    if test "${sane_cv_use_libjpeg}" != "yes"; then
      echo "*** $be backend requires JPEG library - $DISABLE_MSG"
      backend_supported="no"
    fi
    ;;

    canon_pp|hpsj5s)
    if test "${sane_cv_use_libieee1284}" != "yes"; then
      echo "*** $be backend requires libieee1284 library - $DISABLE_MSG"
      backend_supported="no"
    fi
    ;;

    genesys)
    if test "${HAVE_CXX11}" != "1"; then
      echo "*** $be backend requires C++11 support - $DISABLE_MSG"
      backend_supported="no"
    fi
    ;;

    mustek_pp)
    if test "${sane_cv_use_libieee1284}" != "yes" && test "${enable_parport_directio}" != "yes"; then
      echo "*** $be backend requires libieee1284 or parport-directio libraries - $DISABLE_MSG"
      backend_supported="no"
    fi
    ;;

    dell1600n_net)
    if test "${sane_cv_use_libjpeg}" != "yes" || test "${sane_cv_use_libtiff}" != "yes"; then
      echo "*** $be backend requires JPEG and TIFF library - $DISABLE_MSG"
      backend_supported="no"
    fi
    ;;

    epsonds)
    if test "${sane_cv_use_libjpeg}" != "yes"; then
      echo "*** $be backend requires JPEG library - $DISABLE_MSG"
      backend_supported="no"
    fi
    ;;

    escl)
    if test "x${with_avahi}" != "xyes"; then
      echo "*** $be backend requires AVAHI library - $DISABLE_MSG"
      backend_supported="no"
    fi
    if test "x${with_libcurl}" != "xyes"; then
      echo "*** $be backend requires cURL library - $DISABLE_MSG"
      backend_supported="no"
    fi
    if test "x${have_libxml}" != "xyes"; then
      echo "*** $be backend requires XML library - $DISABLE_MSG"
      backend_supported="no"
    fi
    # FIXME: Remove when PNG and/or PDF support have been added.
    if test "x${sane_cv_use_libjpeg}" != "xyes"; then
      echo "*** $be backend currently requires JPEG library - $DISABLE_MSG"
      backend_supported="no"
    else
      if test "x${ac_cv_func_jpeg_crop_scanline}"  != "xyes" \
      || test "x${ac_cv_func_jpeg_skip_scanlines}" != "xyes"; then
        echo "*** $be backend requires a newer JPEG library - $DISABLE_MSG"
        backend_supported="no"
      fi
    fi

    ;;

    gphoto2)
    if test "${HAVE_GPHOTO2}" != "true" \
      || test "${sane_cv_use_libjpeg}" != "yes"; then
      echo "*** $be backend requires gphoto2 and JPEG libraries - $DISABLE_MSG"
      backend_supported="no"
    fi
    ;;

    pint)
    if test "${ac_cv_header_sys_scanio_h}" = "no"; then
      echo "*** $be backend requires sys/scanio.h - $DISABLE_MSG"
      backend_supported="no"
    fi
    ;;

    qcam)
    if ( test "${ac_cv_func_ioperm}" = "no" || test "${sane_cv_have_sys_io_h_with_inb_outb}" = "no" )\
      && test "${ac_cv_func__portaccess}" = "no"; then
      echo "*** $be backend requires (ioperm, inb and outb) or portaccess functions - $DISABLE_MSG"
      backend_supported="no"
    fi
    ;;

    v4l)
    if test "${have_linux_ioctl_defines}" != "yes" \
      || test "${have_libv4l1}" != "yes"; then
      echo "*** $be backend requires v4l libraries - $DISABLE_MSG"
      backend_supported="no"
    fi
    ;;

    net)
    if test "${ac_cv_header_sys_socket_h}" = "no"; then
      echo "*** $be backend requires sys/socket.h - $DISABLE_MSG"
      backend_supported="no"
    fi
    ;;

    mustek_usb2|kvs40xx)
    if test "${have_pthread}" != "yes"; then
      echo "*** $be backend requires pthread library - $DISABLE_MSG"
      backend_supported="no"
    fi
    ;;
  esac
  if test "${backend_supported}" = "no"; then
    if test "${user_selected_backends}" = "yes"; then
      exit 1
    fi
  else
    FILTERED_BACKENDS="${FILTERED_BACKENDS} $be"
  fi
done
])

#
# Generate prototypes for functions not available on the system
AC_DEFUN([SANE_PROTOTYPES],
[
AH_BOTTOM([

#if defined(__MINGW32__)
#define _BSDTYPES_DEFINED
#endif

#ifndef HAVE_U_CHAR
#define u_char unsigned char
#endif
#ifndef HAVE_U_SHORT
#define u_short unsigned short
#endif
#ifndef HAVE_U_INT
#define u_int unsigned int
#endif
#ifndef HAVE_U_LONG
#define u_long unsigned long
#endif

/* Prototype for getenv */
#ifndef HAVE_GETENV
#define getenv sanei_getenv
char * getenv(const char *name);
#endif

/* Prototype for inet_ntop */
#ifndef HAVE_INET_NTOP
#define inet_ntop sanei_inet_ntop
#include <sys/types.h>
const char * inet_ntop (int af, const void *src, char *dst, size_t cnt);
#endif

/* Prototype for inet_pton */
#ifndef HAVE_INET_PTON
#define inet_pton sanei_inet_pton
int inet_pton (int af, const char *src, void *dst);
#endif

/* Prototype for sigprocmask */
#ifndef HAVE_SIGPROCMASK
#define sigprocmask sanei_sigprocmask
int sigprocmask (int how, int *new, int *old);
#endif

/* Prototype for snprintf */
#ifndef HAVE_SNPRINTF
#define snprintf sanei_snprintf
#include <sys/types.h>
int snprintf (char *str,size_t count,const char *fmt,...);
#endif

/* Prototype for strcasestr */
#ifndef HAVE_STRCASESTR
#define strcasestr sanei_strcasestr
char * strcasestr (const char *phaystack, const char *pneedle);
#endif

/* Prototype for strdup */
#ifndef HAVE_STRDUP
#define strdup sanei_strdup
char *strdup (const char * s);
#endif

/* Prototype for strndup */
#ifndef HAVE_STRNDUP
#define strndup sanei_strndup
#include <sys/types.h>
char *strndup(const char * s, size_t n);
#endif

/* Prototype for strsep */
#ifndef HAVE_STRSEP
#define strsep sanei_strsep
char *strsep(char **stringp, const char *delim);
#endif

/* Prototype for usleep */
#ifndef HAVE_USLEEP
#define usleep sanei_usleep
unsigned int usleep (unsigned int useconds);
#endif

/* Prototype for vsyslog */
#ifndef HAVE_VSYSLOG
#include <stdarg.h>
void vsyslog(int priority, const char *format, va_list args);
#endif
])
])

m4_include([m4/byteorder.m4])
