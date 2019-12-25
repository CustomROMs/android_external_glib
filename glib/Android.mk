LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

# config built with ./configure --host=arm-linux-androideabi --build=x86_64-linux-gnu --cache=android.cache
# (unfortunately I lost my original contents of android.cache, but they were for a few of the tests that can't be run
# when cross-compiling)

#	gregex.c \

LOCAL_SRC_FILES:= \
	garray.c \
	gmain.c \
	gtree.c \
	gpoll.c \
	gqueue.c \
	gdatetime.c \
	gconvert.c \
	gsequence.c \
	gtrashstack.c \
	gmem.c \
	gunidecomp.c \
	gfileutils.c \
	gstrfuncs.c \
	gvarianttype.c \
	gthread.c \
	docs.c \
	guniprop.c \
	gmessages.c \
	glist.c \
	gtestutils.c \
	gthread-posix.c \
	gprimes.c \
	gslice.c \
	gvariant-parser.c \
	gmarkup.c \
	gscanner.c \
	gerror.c \
	gwakeup.c \
	gdate.c \
	gtester.c \
	gatomic.c \
	gbacktrace.c \
	gversion.c \
	gvariant-serialiser.c \
	gvariant-core.c \
	giochannel.c \
	gvariant.c \
	gutf8.c \
	gpattern.c \
	gvarianttypeinfo.c \
	glib-init.c \
	gdir.c \
	gslist.c \
	gkeyfile.c \
	gurifuncs.c \
	gchecksum.c \
	gstring.c \
	gcharset.c \
	gstdio.c \
	genviron.c \
	gbytes.c \
	gdataset.c \
	giounix.c \
	gunibreak.c \
	gunicollate.c \
	deprecated/gcompletion.c \
	deprecated/gcache.c \
	deprecated/gthread-deprecated.c \
	deprecated/gallocator.c \
	deprecated/grel.c \
	glib-unix.c \
	gtimer.c \
	gthreadpool.c \
	gnulib/printf.c \
	gnulib/printf-parse.c \
	gnulib/vasnprintf.c \
	gnulib/asnprintf.c \
	gnulib/printf-args.c \
	ghash.c \
	gutils.c \
	gtimezone.c \
	gmappedfile.c \
	gstringchunk.c \
	gnode.c \
	ghook.c \
	gbookmarkfile.c \
	glib-private.c \
	gbitlock.c \
	gshell.c \
	ghmac.c \
	gprintf.c \
	ghostutils.c \
	gbase64.c \
	grand.c \
	gspawn.c \
	goption.c \
	gasyncqueue.c \
	gqsort.c \
	libcharset/localcharset.c \

LOCAL_C_INCLUDES:= $(LOCAL_PATH)/../ $(LOCAL_PATH)

LOCAL_CFLAGS:= \
        -Wno-error \
	-DANDROID_STUB \
	-DGLIB_COMPILATION \
	-DLIBICONV_PLUG \
	-DLIBDIR=\"/\"

LOCAL_SHARED_LIBRARIES := libiconv libpcre2

LOCAL_MODULE:=libglib

include $(BUILD_SHARED_LIBRARY)
