#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)
CFLAGS += -DCS_PLATFORM=3 \
	-DMG_DISABLE_DIRECTORY_LISTING=1 \
	-DMG_DISABLE_DAV=1 \
	-DMG_DISABLE_CGI=1 \
	-DMG_DISABLE_FILESYSTEM=1 \
	-DMG_LWIP=1 \
	-DMG_ENABLE_BROADCAST \
	-DBOOTWIFI_OVERRIDE_GPIO=0 \
	-DSTATUS_LED_GPIO=2

