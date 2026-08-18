MODULE := engines/alien

MODULE_OBJS = \
	alien.o \
	anim.o \
	anims.o \
	charanim.o \
	dl1.o \
	font.o \
	hotspots.o \
	inventory.o \
	metaengine.o \
	overlay.o \
	resources.o \
	roominit.o \
	roomscripts.o \
	script.o \
	sfx.o \
	tables.o \
	tal.o \
	transitions.o \
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
