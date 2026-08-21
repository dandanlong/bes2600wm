include $(APPDIR)/Make.defs

ifneq ($(CONFIG_DAILE_BTSINK),)

PROGNAME  = $(CONFIG_DAILE_BTSINK_PROGNAME)
PRIORITY  = $(CONFIG_DAILE_BTSINK_PRIORITY)
STACKSIZE = $(CONFIG_DAILE_BTSINK_STACKSIZE)
MODULE    = $(CONFIG_DAILE_BTSINK)

MAINSRC = daile_btsink.c

CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/frameworks/connectivity/bluetooth/framework/include

endif

include $(APPDIR)/Application.mk
