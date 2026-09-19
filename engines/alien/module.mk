MODULE := engines/alien

MODULE_OBJS = \
	alien.o \
	anim.o \
	animfont.o \
	anims.o \
	charanim.o \
	chat.o \
	cliff.o \
	living.o \
	mailbox.o \
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
	lift.o \
	library.o \
	sewer.o \
	sluggs.o \
	hippie.o \
	store.o \
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
