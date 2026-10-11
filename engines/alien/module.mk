MODULE := engines/alien

MODULE_OBJS = \
	animfont.o \
	cutscenes.o \
	dl1.o \
	font.o \
	hotspots.o \
	inventory.o \
	mazetables.o \
	occlusion.o \
	overlay.o \
	pack.o \
	plates.o \
	resources.o \
	roominit.o \
	roomscripts.o \
	s3m.o \
	sfx.o \
	tables.o \
	transitions.o \
	video.o \
	walkgeom.o \

# This module can be built as a plugin
ifeq ($(ENABLE_ALIEN), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
