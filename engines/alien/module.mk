MODULE := engines/alien

MODULE_OBJS = \
	alien.o \
	anim.o \
	anims.o \
	charanim.o \
	chat.o \
	cutscenes.o \
	cutsceneplay.o \
	basement.o \
	bedroom.o \
	dl1.o \
	ending.o \
	fade.o \
	hallway.o \
	font.o \
	hotspots.o \
	inventory.o \
	lighting.o \
	metaengine.o \
	occlusion.o \
	opening.o \
	overlay.o \
	pack.o \
	plates.o \
	play.o \
	resources.o \
	roominit.o \
	roomplate.o \
	roomscripts.o \
	roomtick.o \
	script.o \
	s3m.o \
	scale.o \
	saveload.o \
	lab.o \
	library.o \
	sewer.o \
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
