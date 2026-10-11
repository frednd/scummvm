MODULE := engines/alien

MODULE_OBJS = \
	animfont.o \
	cutscenes.o \
	dl1.o \
	font.o \
	overlay.o \
	pack.o \
	resources.o \
	roomscripts.o \
	s3m.o \
	sfx.o \
	tables.o \
	video.o \

# This module can be built as a plugin
ifeq ($(ENABLE_ALIEN), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
