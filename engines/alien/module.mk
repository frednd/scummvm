MODULE := engines/alien

MODULE_OBJS = \
	alien.o \
	anim.o \
	animfont.o \
	anims.o \
	charanim.o \
	clock.o \
	cutsceneplay.o \
	cutscenes.o \
	dl1.o \
	fade.o \
	font.o \
	hotspots.o \
	input.o \
	inventory.o \
	lighting.o \
	mazetables.o \
	media.o \
	occlusion.o \
	overlay.o \
	pack.o \
	plates.o \
	render.o \
	resources.o \
	room.o \
	roominit.o \
	roomplate.o \
	roomscripts.o \
	roomtick.o \
	s3m.o \
	scale.o \
	script.o \
	sfx.o \
	sweeps.o \
	tables.o \
	transitions.o \
	video.o \
	walk.o \
	walkgeom.o \

# This module can be built as a plugin
ifeq ($(ENABLE_ALIEN), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
