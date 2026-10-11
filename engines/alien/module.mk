MODULE := engines/alien

MODULE_OBJS = \
	alien.o \
	anim.o \
	animfont.o \
	anims.o \
	charanim.o \
	chat.o \
	clock.o \
	console.o \
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
	play.o \
	playrun.o \
	render.o \
	resources.o \
	room.o \
	roominit.o \
	roomplate.o \
	roomscripts.o \
	roomtick.o \
	s3m.o \
	saveload.o \
	scale.o \
	script.o \
	sfx.o \
	sweeps.o \
	tables.o \
	tal.o \
	text.o \
	transitions.o \
	video.o \
	walk.o \
	walkgeom.o \
	rooms/basement.o \
	rooms/bedroom.o \
	rooms/hallway.o \
	rooms/lab.o \
	rooms/library.o \
	rooms/lift.o \
	rooms/living.o \
	rooms/opening.o \
	rooms/sewer.o \

# This module can be built as a plugin
ifeq ($(ENABLE_ALIEN), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
