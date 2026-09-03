MODULE := engines/alien

MODULE_OBJS = \
	alien.o \
	anim.o \
	anims.o \
	charanim.o \
	cutscenes.o \
	cutsceneplay.o \
	bedroom.o \
	dl1.o \
	ending.o \
	font.o \
	hotspots.o \
	inventory.o \
	metaengine.o \
	occlusion.o \
	opening.o \
	overlay.o \
	play.o \
	resources.o \
	roominit.o \
	roomscripts.o \
	roomtick.o \
	script.o \
	s3m.o \
	saveload.o \
	sfx.o \
	tables.o \
	tal.o \
	transitions.o \
	video.o \
	walk.o \
	walkgeom.o

# This module can be built as a plugin
ifeq ($(ENABLE_ALIEN), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
