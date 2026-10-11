/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

// Which DL1 bank a room loads into which animation slot. The original passes
// both as immediates to MIDAS:load_anim_bank as the overlay opens, so this is
// those arguments; the manifest order overlay.cpp reads is not the slot order.
//
// A row whose flag is not zero is one the overlay assembles at run time: it
// loads the last name instead of the second while that flag holds the value
// given. Room 7's closet is the only one.
//
//   251 bank loads over 42 rooms.

#include "alien/anims.h"

namespace Alien {

static const AnimBank kAnimBanks[] = {
	{  0, "LAB_LANK.DL1", 0, 0, nullptr },	// Room 3: Uncle's lab
	{  1, "LAB_ROPE.DL1", 0, 0, nullptr },	// Room 3: Uncle's lab
	{  2, "KURSORI.DL1", 0, 0, nullptr },	// Room 3: Uncle's lab
	{  3, "FUSEBOX.DL1", 0, 0, nullptr },	// Room 3: Uncle's lab
	{  4, "TOASTER.DL1", 0, 0, nullptr },	// Room 3: Uncle's lab
	{  5, "LAB_PLAN.DL1", 0, 0, nullptr },	// Room 3: Uncle's lab
	{  6, "LAB_TKEY.DL1", 0, 0, nullptr },	// Room 3: Uncle's lab
	{  0, "KURPITSA.DL1", 0, 0, nullptr },	// Room 6: Upstairs landing
	{  1, "UP_DOOR1.DL1", 0, 0, nullptr },	// Room 6: Upstairs landing
	{  2, "UP_DOOR2.DL1", 0, 0, nullptr },	// Room 6: Upstairs landing
	{  3, "UP_DOOR3.DL1", 0, 0, nullptr },	// Room 6: Upstairs landing
	{  4, "KURP_TYH.DL1", 0, 0, nullptr },	// Room 6: Upstairs landing
	{  0, "WALKINC1.DL1", 0, 0, nullptr },	// Room 7: Bedroom
	{  0, "PILLOW.DL1", 0, 0, nullptr },	// Room 7: Bedroom
	{  1, "KAUKO.DL1", 0, 0, nullptr },	// Room 7: Bedroom
	{  2, "MAKDRAW.DL1", 0, 0, nullptr },	// Room 7: Bedroom
	{  3, "MAK_CLOS.DL1", 0xa700, 0, "MAK_CLO2.DL1" },	// Room 7: Bedroom
	{  4, "SOCK.DL1", 0, 0, nullptr },	// Room 7: Bedroom
	{  0, "SHELF_A.DL1", 0, 0, nullptr },	// Room 8: Library
	{  1, "SHELF_B.DL1", 0, 0, nullptr },	// Room 8: Library
	{  2, "SAFEOPEN.DL1", 0, 0, nullptr },	// Room 8: Library
	{  3, "IKKUNA1.DL1", 0, 0, nullptr },	// Room 8: Library
	{  5, "SAFEOPE1.DL1", 0, 0, nullptr },	// Room 8: Library
	{  6, "SAFEOPE2.DL1", 0, 0, nullptr },	// Room 8: Library
	{  7, "SAFEOPE3.DL1", 0, 0, nullptr },	// Room 8: Library
	{  8, "LIBSHELF.DL1", 0, 0, nullptr },	// Room 8: Library
	{  9, "SHELFLOC.DL1", 0, 0, nullptr },	// Room 8: Library
	{ 10, "LIB_LODR.DL1", 0, 0, nullptr },	// Room 8: Library
	{ 11, "LIB_SHPU.DL1", 0, 0, nullptr },	// Room 8: Library
	{ 12, "LIB_OWL.DL1", 0, 0, nullptr },	// Room 8: Library
	{ 13, "LIB_STET.DL1", 0, 0, nullptr },	// Room 8: Library
	{  0, "FRIDGE_1.DL1", 0, 0, nullptr },	// Room 10: Kitchen
	{  1, "FRIDGE_2.DL1", 0, 0, nullptr },	// Room 10: Kitchen
	{  2, "FRIDGE_3.DL1", 0, 0, nullptr },	// Room 10: Kitchen
	{  3, "FRIDGE_4.DL1", 0, 0, nullptr },	// Room 10: Kitchen
	{  4, "PICKMATC.DL1", 0, 0, nullptr },	// Room 10: Kitchen
	{  6, "PICKBRED.DL1", 0, 0, nullptr },	// Room 10: Kitchen
	{  7, "PICKFUSE.DL1", 0, 0, nullptr },	// Room 10: Kitchen
	{  8, "KIT_FAUC.DL1", 0, 0, nullptr },	// Room 10: Kitchen
	{  0, "LIV_TVBR.DL1", 0, 0, nullptr },	// Room 11: Living room
	{  1, "LIV_ARFR.DL1", 0, 0, nullptr },	// Room 11: Living room
	{  2, "SNOWTV.DL1", 0, 0, nullptr },	// Room 11: Living room
	{  3, "BLANKTV.DL1", 0, 0, nullptr },	// Room 11: Living room
	{  4, "SCISSORS.DL1", 0, 0, nullptr },	// Room 11: Living room
	{  5, "LIV_RADI.DL1", 0, 0, nullptr },	// Room 11: Living room
	{  6, "LIV_TV.DL1", 0, 0, nullptr },	// Room 11: Living room
	{  7, "LIV_CAGO.DL1", 0, 0, nullptr },	// Room 11: Living room
	{  0, "BAS_ELDO.DL1", 0, 0, nullptr },	// Room 13: Basement
	{  1, "ROPE.DL1", 0, 0, nullptr },	// Room 13: Basement
	{  2, "ECHO_ECH.DL1", 0, 0, nullptr },	// Room 13: Basement
	{  3, "ECHO_DEE.DL1", 0, 0, nullptr },	// Room 13: Basement
	{  4, "BAS_WATE.DL1", 0, 0, nullptr },	// Room 13: Basement
	{  5, "ALA_SMOK.DL1", 0, 0, nullptr },	// Room 13: Basement
	{  6, "BATTERY.DL1", 0, 0, nullptr },	// Room 13: Basement
	{  7, "BASEPIPE.DL1", 0, 0, nullptr },	// Room 13: Basement
	{  8, "FLUECLEA.DL1", 0, 0, nullptr },	// Room 13: Basement
	{  9, "BENPIPE.DL1", 0, 0, nullptr },	// Room 13: Basement
	{ 10, "BAS_ELUP.DL1", 0, 0, nullptr },	// Room 13: Basement
	{  0, "MAF_CHIM.DL1", 0, 0, nullptr },	// Room 14: Closet shaft / chimney
	{  0, "HALLDOO1.DL1", 0, 0, nullptr },	// Room 15: Entrance hall
	{  1, "HALLDOO2.DL1", 0, 0, nullptr },	// Room 15: Entrance hall
	{  2, "HAL_FRON.DL1", 0, 0, nullptr },	// Room 15: Entrance hall
	{  3, "HAL_VALO.DL1", 0, 0, nullptr },	// Room 15: Entrance hall
	{  0, "OBSODOOR.DL1", 0, 0, nullptr },	// Room 17: Observatory entrance
	{  1, "OBO_WATE.DL1", 0, 0, nullptr },	// Room 17: Observatory entrance
	{  0, "SPEAKER1.DL1", 0, 0, nullptr },	// Room 18: Sitting room
	{  1, "CABINET.DL1", 0, 0, nullptr },	// Room 18: Sitting room
	{  2, "HAMMER.DL1", 0, 0, nullptr },	// Room 18: Sitting room
	{  3, "UFOFLYBY.DL1", 0, 0, nullptr },	// Room 18: Sitting room
	{  4, "AQUARIUM.DL1", 0, 0, nullptr },	// Room 18: Sitting room
	{  5, "BEN_WALK.DL1", 0, 0, nullptr },	// Room 18: Sitting room
	{  6, "BEN_LSPE.DL1", 0, 0, nullptr },	// Room 18: Sitting room
	{  7, "BEN_RSPE.DL1", 0, 0, nullptr },	// Room 18: Sitting room
	{  8, "BEN_AMPL.DL1", 0, 0, nullptr },	// Room 18: Sitting room
	{  0, "OBSFUSE0.DL1", 0, 0, nullptr },	// Room 19: Observatory interior
	{  3, "BENSLIP.DL1", 0, 0, nullptr },	// Room 19: Observatory interior
	{  4, "OBSGOUP.DL1", 0, 0, nullptr },	// Room 19: Observatory interior
	{  5, "OBS_SWIT.DL1", 0, 0, nullptr },	// Room 19: Observatory interior
	{  6, "OBS_LIGH.DL1", 0, 0, nullptr },	// Room 19: Observatory interior
	{  0, "VESI.DL1", 0, 0, nullptr },	// Room 21: Yodle's tree hut
	{  1, "YODLEPIP.DL1", 0, 0, nullptr },	// Room 21: Yodle's tree hut
	{  2, "YODLEDOO.DL1", 0, 0, nullptr },	// Room 21: Yodle's tree hut
	{  3, "YODLE1.DL1", 0, 0, nullptr },	// Room 21: Yodle's tree hut
	{  4, "YOD_GOIN.DL1", 0, 0, nullptr },	// Room 21: Yodle's tree hut
	{  5, "YOD_CLER.DL1", 0, 0, nullptr },	// Room 21: Yodle's tree hut
	{  6, "YOD_THRO.DL1", 0, 0, nullptr },	// Room 21: Yodle's tree hut
	{  7, "YOD_PIP2.DL1", 0, 0, nullptr },	// Room 21: Yodle's tree hut
	{  0, "PAR_TELE.DL1", 0, 0, nullptr },	// Room 22: Parlor / teleport
	{  1, "PAR_BOCL.DL1", 0, 0, nullptr },	// Room 22: Parlor / teleport
	{  2, "PAR_BENP.DL1", 0, 0, nullptr },	// Room 22: Parlor / teleport
	{  3, "PAR_TOSH.DL1", 0, 0, nullptr },	// Room 22: Parlor / teleport
	{  4, "PAR_RAD1.DL1", 0, 0, nullptr },	// Room 22: Parlor / teleport
	{  5, "PAR_KIPI.DL1", 0, 0, nullptr },	// Room 22: Parlor / teleport
	{  6, "PAR_FROM.DL1", 0, 0, nullptr },	// Room 22: Parlor / teleport
	{  7, "PAR_PUMP.DL1", 0, 0, nullptr },	// Room 22: Parlor / teleport
	{  8, "PAR_PUM2.DL1", 0, 0, nullptr },	// Room 22: Parlor / teleport
	{  9, "PAR_WIRE.DL1", 0, 0, nullptr },	// Room 22: Parlor / teleport
	{ 10, "PAR_RAD2.DL1", 0, 0, nullptr },	// Room 22: Parlor / teleport
	{  0, "CROS_HIP.DL1", 0, 0, nullptr },	// Room 23: Crossroads
	{  1, "CROS_HCL.DL1", 0, 0, nullptr },	// Room 23: Crossroads
	{  0, "PAL_MAIL.DL1", 0, 0, nullptr },	// Room 25: Park / mailbox
	{  1, "PAL_BLOW.DL1", 0, 0, nullptr },	// Room 25: Park / mailbox
	{  0, "PARROT.DL1", 0, 0, nullptr },	// Room 26: Forest
	{  1, "YODLESAF.DL1", 0, 0, nullptr },	// Room 26: Forest
	{  2, "YODLELED.DL1", 0, 0, nullptr },	// Room 26: Forest
	{  3, "SQUIRREL.DL1", 0, 0, nullptr },	// Room 26: Forest
	{  4, "YOD_CAND.DL1", 0, 0, nullptr },	// Room 26: Forest
	{  5, "YOD_STAR.DL1", 0, 0, nullptr },	// Room 26: Forest
	{  0, "MANSDOOR.DL1", 0, 0, nullptr },	// Room 27: Mansion exterior
	{  1, "MAC_BEUP.DL1", 0, 0, nullptr },	// Room 27: Mansion exterior
	{  0, "TELESCOP.DL1", 0, 0, nullptr },	// Room 28: Telescope room
	{  1, "OBU_VIPY.DL1", 0, 0, nullptr },	// Room 28: Telescope room
	{  2, "OBU_VIPA.DL1", 0, 0, nullptr },	// Room 28: Telescope room
	{  3, "OBU_VIPF.DL1", 0, 0, nullptr },	// Room 28: Telescope room
	{  4, "OBU_GAME.DL1", 0, 0, nullptr },	// Room 28: Telescope room
	{  5, "OBU_DISK.DL1", 0, 0, nullptr },	// Room 28: Telescope room
	{  6, "OBU_SWIT.DL1", 0, 0, nullptr },	// Room 28: Telescope room
	{  7, "OBU_SCRE.DL1", 0, 0, nullptr },	// Room 28: Telescope room
	{  0, "ANT_SAC1.DL1", 0, 0, nullptr },	// Room 30: 24h antique store
	{  1, "ANT_POTI.DL1", 0, 0, nullptr },	// Room 30: 24h antique store
	{  2, "ANT_MONK.DL1", 0, 0, nullptr },	// Room 30: 24h antique store
	{  3, "ANT_SAC2.DL1", 0, 0, nullptr },	// Room 30: 24h antique store
	{  4, "ANT_SAC3.DL1", 0, 0, nullptr },	// Room 30: 24h antique store
	{  5, "ANT_SAC4.DL1", 0, 0, nullptr },	// Room 30: 24h antique store
	{  6, "ANT_SAC5.DL1", 0, 0, nullptr },	// Room 30: 24h antique store
	{  7, "ANT_SAC6.DL1", 0, 0, nullptr },	// Room 30: 24h antique store
	{  0, "BEN_UP.DL1", 0, 0, nullptr },	// Room 31: Cliff
	{  1, "BEN_DOWN.DL1", 0, 0, nullptr },	// Room 31: Cliff
	{  2, "CEF_BAT1.DL1", 0, 0, nullptr },	// Room 31: Cliff
	{  3, "CEF_BAT2.DL1", 0, 0, nullptr },	// Room 31: Cliff
	{  0, "CEM_LADO.DL1", 0, 0, nullptr },	// Room 32: Cemetery
	{  1, "CEM_LAOP.DL1", 0, 0, nullptr },	// Room 32: Cemetery
	{  2, "CEM_OPLO.DL1", 0, 0, nullptr },	// Room 32: Cemetery
	{  3, "CEM_BEIN.DL1", 0, 0, nullptr },	// Room 32: Cemetery
	{  4, "CEM_DOBU.DL1", 0, 0, nullptr },	// Room 32: Cemetery
	{  5, "CEM_RCAN.DL1", 0, 0, nullptr },	// Room 32: Cemetery
	{  6, "CEM_LCA1.DL1", 0, 0, nullptr },	// Room 32: Cemetery
	{  7, "CEM_LCA2.DL1", 0, 0, nullptr },	// Room 32: Cemetery
	{  0, "TOW_DOOR.DL1", 0, 0, nullptr },	// Room 33: Town
	{  1, "TOWNSIGN.DL1", 0, 0, nullptr },	// Room 33: Town
	{  2, "TOW_GETU.DL1", 0, 0, nullptr },	// Room 33: Town
	{  0, "PAL_SMOK.DL1", 0, 0, nullptr },	// Room 34: Hippie / pal area
	{  1, "PAL_ANIM.DL1", 0, 0, nullptr },	// Room 34: Hippie / pal area
	{  0, "SEW_PLAN.DL1", 0, 0, nullptr },	// Room 35: Sewer
	{  1, "SEW_CLIM.DL1", 0, 0, nullptr },	// Room 35: Sewer
	{  2, "SEW_FULL.DL1", 0, 0, nullptr },	// Room 35: Sewer
	{  3, "SEW_WATE.DL1", 0, 0, nullptr },	// Room 35: Sewer
	{  4, "SEW_FLUS.DL1", 0, 0, nullptr },	// Room 35: Sewer
	{  5, "SEW_HAND.DL1", 0, 0, nullptr },	// Room 35: Sewer
	{  6, "SEW_WALL.DL1", 0, 0, nullptr },	// Room 35: Sewer
	{  0, "MAE_WATF.DL1", 0, 0, nullptr },	// Room 41: Shore / underwater entry
	{  1, "MAE_TOR1.DL1", 0, 0, nullptr },	// Room 41: Shore / underwater entry
	{  2, "MAE_TOR2.DL1", 0, 0, nullptr },	// Room 41: Shore / underwater entry
	{  3, "MAE_TOR3.DL1", 0, 0, nullptr },	// Room 41: Shore / underwater entry
	{  4, "MAE_WATE.DL1", 0, 0, nullptr },	// Room 41: Shore / underwater entry
	{  5, "MAE_THRO.DL1", 0, 0, nullptr },	// Room 41: Shore / underwater entry
	{  6, "MAE_RIGH.DL1", 0, 0, nullptr },	// Room 41: Shore / underwater entry
	{  7, "MAE_LEFT.DL1", 0, 0, nullptr },	// Room 41: Shore / underwater entry
	{  8, "MAE_DOO1.DL1", 0, 0, nullptr },	// Room 41: Shore / underwater entry
	{  9, "MAE_SLIP.DL1", 0, 0, nullptr },	// Room 41: Shore / underwater entry
	{ 10, "MAE_DIVE.DL1", 0, 0, nullptr },	// Room 41: Shore / underwater entry
	{ 11, "MAE_SHOR.DL1", 0, 0, nullptr },	// Room 41: Shore / underwater entry
	{  0, "MAZ_TOR1.DL1", 0, 0, nullptr },	// Rooms 43, 44, 45: Maze + crystal entry
	{  2, "CRY_DMOR.DL1", 0, 0, nullptr },	// Rooms 43, 44, 45: Maze + crystal entry
	{  0, "MAZ_TOR1.DL1", 0, 0, nullptr },	// Rooms 43, 44, 45: Maze + crystal entry
	{  2, "MA2_AXE.DL1", 0, 0, nullptr },	// Rooms 43, 44, 45: Maze + crystal entry
	{  0, "CRY_ENT1.DL1", 0, 0, nullptr },	// Rooms 43, 44, 45: Maze + crystal entry
	{  4, "ENT_NOTE.DL1", 0, 0, nullptr },	// Rooms 43, 44, 45: Maze + crystal entry
	{  5, "ENT_BENB.DL1", 0, 0, nullptr },	// Rooms 43, 44, 45: Maze + crystal entry
	{  6, "ENT_DROP.DL1", 0, 0, nullptr },	// Rooms 43, 44, 45: Maze + crystal entry
	{  0, "DIV_SADE.DL1", 0, 0, nullptr },	// Room 46: Diving area / underwater
	{  0, "DIV_PROP.DL1", 0, 0, nullptr },	// Room 46: Diving area / underwater
	{  1, "DIV_CHES.DL1", 0, 0, nullptr },	// Room 46: Diving area / underwater
	{  0, "PUD_TORC.DL1", 0, 0, nullptr },	// Room 48: Alien ship upper level
	{  1, "PUD_DOOR.DL1", 0, 0, nullptr },	// Room 48: Alien ship upper level
	{  2, "PUD_DROP.DL1", 0, 0, nullptr },	// Room 48: Alien ship upper level
	{  3, "PUD_PUTO.DL1", 0, 0, nullptr },	// Room 48: Alien ship upper level
	{  4, "PUD_GOSU.DL1", 0, 0, nullptr },	// Room 48: Alien ship upper level
	{  0, "ENG_GOUP.DL1", 0, 0, nullptr },	// Room 49: Engine room
	{  1, "ENG_MACH.DL1", 0, 0, nullptr },	// Room 49: Engine room
	{  2, "ENG_SMAL.DL1", 0, 0, nullptr },	// Room 49: Engine room
	{  3, "ENG_PATA.DL1", 0, 0, nullptr },	// Room 49: Engine room
	{  4, "ENG_SWIT.DL1", 0, 0, nullptr },	// Room 49: Engine room
	{  5, "ENG_BLOW.DL1", 0, 0, nullptr },	// Room 49: Engine room
	{  0, "STE_STEA.DL1", 0, 0, nullptr },	// Room 50: Steam room
	{  1, "STE_BEVI.DL1", 0, 0, nullptr },	// Room 50: Steam room
	{  2, "STE_LADD.DL1", 0, 0, nullptr },	// Room 50: Steam room
	{  0, "LOB_CAM1.DL1", 0, 0, nullptr },	// Room 51: Alien lobby
	{  1, "LOB_CAM2.DL1", 0, 0, nullptr },	// Room 51: Alien lobby
	{  2, "LOB_CAM3.DL1", 0, 0, nullptr },	// Room 51: Alien lobby
	{  3, "LOB_DOOR.DL1", 0, 0, nullptr },	// Room 51: Alien lobby
	{  4, "LOB_DOO2.DL1", 0, 0, nullptr },	// Room 51: Alien lobby
	{  5, "LOB_LIGH.DL1", 0, 0, nullptr },	// Room 51: Alien lobby
	{  0, "SEC_MONI.DL1", 0, 0, nullptr },	// Room 52: Security scanner
	{  1, "SEC_DOO1.DL1", 0, 0, nullptr },	// Room 52: Security scanner
	{  2, "SEC_DOO2.DL1", 0, 0, nullptr },	// Room 52: Security scanner
	{  3, "SEC_ROEY.DL1", 0, 0, nullptr },	// Room 52: Security scanner
	{  4, "SEC_ROMO.DL1", 0, 0, nullptr },	// Room 52: Security scanner
	{  5, "SEC_NORO.DL1", 0, 0, nullptr },	// Room 52: Security scanner
	{  6, "SEC_ROBO.DL1", 0, 0, nullptr },	// Room 52: Security scanner
	{  0, "HAL2_WIN.DL1", 0, 0, nullptr },	// Rooms 53, 57: Hallways
	{  2, "HAL2_MSP.DL1", 0, 0, nullptr },	// Rooms 53, 57: Hallways
	{  3, "HAL2_LID.DL1", 0, 0, nullptr },	// Rooms 53, 57: Hallways
	{  4, "HAL2_DOO.DL1", 0, 0, nullptr },	// Rooms 53, 57: Hallways
	{  0, "WAIT_AL1.DL1", 0, 0, nullptr },	// Room 54: Waiting room
	{  1, "WAIT_AL2.DL1", 0, 0, nullptr },	// Room 54: Waiting room
	{  2, "WAI_MACH.DL1", 0, 0, nullptr },	// Room 54: Waiting room
	{  4, "WAI_BUTT.DL1", 0, 0, nullptr },	// Room 54: Waiting room
	{  5, "WAI_FAN.DL1", 0, 0, nullptr },	// Room 54: Waiting room
	{  6, "WAI_RIDO.DL1", 0, 0, nullptr },	// Room 54: Waiting room
	{  7, "WAI_BROK.DL1", 0, 0, nullptr },	// Room 54: Waiting room
	{  8, "WAI_RING.DL1", 0, 0, nullptr },	// Room 54: Waiting room
	{  9, "WAI_NUMB.DL1", 0, 0, nullptr },	// Room 54: Waiting room
	{ 10, "WAI_MONS.DL1", 0, 0, nullptr },	// Room 54: Waiting room
	{  0, "COR_DOOR.DL1", 0, 0, nullptr },	// Room 55: Corridor
	{  1, "COR_LIDO.DL1", 0, 0, nullptr },	// Room 55: Corridor
	{  2, "COR_REDL.DL1", 0, 0, nullptr },	// Room 55: Corridor
	{  3, "COR_LED1.DL1", 0, 0, nullptr },	// Room 55: Corridor
	{  4, "COR_LED2.DL1", 0, 0, nullptr },	// Room 55: Corridor
	{  5, "COR_LED3.DL1", 0, 0, nullptr },	// Room 55: Corridor
	{  0, "TRA_GUAR.DL1", 0, 0, nullptr },	// Room 56: Transporter chamber
	{  1, "TRA_DOOR.DL1", 0, 0, nullptr },	// Room 56: Transporter chamber
	{  2, "TRA_CARD.DL1", 0, 0, nullptr },	// Room 56: Transporter chamber
	{  3, "TRA_CHAM.DL1", 0, 0, nullptr },	// Room 56: Transporter chamber
	{  4, "TRA_CHA2.DL1", 0, 0, nullptr },	// Room 56: Transporter chamber
	{  5, "TRA_PIPE.DL1", 0, 0, nullptr },	// Room 56: Transporter chamber
	{  6, "TRA_SHAF.DL1", 0, 0, nullptr },	// Room 56: Transporter chamber
	{  0, "HALW_DOO.DL1", 0, 0, nullptr },	// Rooms 53, 57: Hallways
	{  1, "HALW_LED.DL1", 0, 0, nullptr },	// Rooms 53, 57: Hallways
	{  2, "HALW_WIN.DL1", 0, 0, nullptr },	// Rooms 53, 57: Hallways
	{  3, "HAL_SPEA.DL1", 0, 0, nullptr },	// Rooms 53, 57: Hallways
	{  4, "HAL1_AL1.DL1", 0, 0, nullptr },	// Rooms 53, 57: Hallways
	{  5, "HAL1_AL2.DL1", 0, 0, nullptr },	// Rooms 53, 57: Hallways
	{  6, "HAL1_AL3.DL1", 0, 0, nullptr },	// Rooms 53, 57: Hallways
	{  7, "HAL1_AL4.DL1", 0, 0, nullptr },	// Rooms 53, 57: Hallways
	{  8, "HAL1_AL5.DL1", 0, 0, nullptr },	// Rooms 53, 57: Hallways
	{  9, "HAL1_AL6.DL1", 0, 0, nullptr },	// Rooms 53, 57: Hallways
	{ 10, "HAL_PUMP.DL1", 0, 0, nullptr },	// Rooms 53, 57: Hallways
	{  0, "JAIL_YOD.DL1", 0, 0, nullptr },	// Room 58: Jail
	{  1, "JAIL_UNC.DL1", 0, 0, nullptr },	// Room 58: Jail
	{  2, "JAIL_WAL.DL1", 0, 0, nullptr },	// Room 58: Jail
	{  3, "JAI_DOOR.DL1", 0, 0, nullptr },	// Room 58: Jail
	{  4, "JAI_BESH.DL1", 0, 0, nullptr },	// Room 58: Jail
	{  5, "JAIL_LCD.DL1", 0, 0, nullptr },	// Room 58: Jail
	{  6, "JAI_CARD.DL1", 0, 0, nullptr },	// Room 58: Jail
	{  0, "ESC_DOOR.DL1", 0, 0, nullptr },	// Room 59: Escape pod
	{  1, "ESC_SECU.DL1", 0, 0, nullptr },	// Room 59: Escape pod
	{  2, "ESC_LELI.DL1", 0, 0, nullptr },	// Room 59: Escape pod
	{  3, "ESC_RILI.DL1", 0, 0, nullptr },	// Room 59: Escape pod
	{  4, "ESC_UNCL.DL1", 0, 0, nullptr },	// Room 59: Escape pod
	{  6, "ESC_CHAM.DL1", 0, 0, nullptr },	// Room 59: Escape pod
};

// Room number, first bank, count. A room absent from this table loads no bank
// of its own -- either it has no overlay, or its overlay names its banks from a
// register.
struct AnimBankRoom {
	byte room;
	uint16 first;
	uint16 count;
};

static const AnimBankRoom kAnimBankRooms[] = {
	{  3,   0,  7 },
	{  6,   7,  5 },
	{  7,  12,  6 },
	{  8,  18, 13 },
	{ 10,  31,  8 },
	{ 11,  39,  8 },
	{ 13,  47, 11 },
	{ 14,  58,  1 },
	{ 15,  59,  4 },
	{ 17,  63,  2 },
	{ 18,  65,  9 },
	{ 19,  74,  5 },
	{ 21,  79,  8 },
	{ 22,  87, 11 },
	{ 23,  98,  2 },
	{ 25, 100,  2 },
	{ 26, 102,  6 },
	{ 27, 108,  2 },
	{ 28, 110,  8 },
	{ 30, 118,  8 },
	{ 31, 126,  4 },
	{ 32, 130,  8 },
	{ 33, 138,  3 },
	{ 34, 141,  2 },
	{ 35, 143,  7 },
	{ 41, 150, 12 },
	{ 43, 162,  2 },
	{ 44, 164,  2 },
	{ 45, 166,  4 },
	{ 46, 170,  3 },
	{ 48, 173,  5 },
	{ 49, 178,  6 },
	{ 50, 184,  3 },
	{ 51, 187,  6 },
	{ 52, 193,  7 },
	{ 53, 200,  4 },
	{ 54, 204, 10 },
	{ 55, 214,  6 },
	{ 56, 220,  7 },
	{ 57, 227, 11 },
	{ 58, 238,  7 },
	{ 59, 245,  6 },
};

const AnimBank *animBanksForRoom(int room, uint &count) {
	count = 0;
	for (uint i = 0; i < ARRAYSIZE(kAnimBankRooms); i++) {
		if (kAnimBankRooms[i].room != room)
			continue;
		count = kAnimBankRooms[i].count;
		return &kAnimBanks[kAnimBankRooms[i].first];
	}
	return nullptr;
}

// The slots each room's tick keeps looping, from its MIDAS:snd_func_112d calls.
// `flag` is the [0xa53a] byte the call sits behind, or 0 where it is made every
// frame unconditionally.
//
//   81 looping slots over 35 rooms.

static const AnimLoop kAnimLoops[] = {
	{  2, 0x0000 },	// Room 3: Uncle's lab
	{  4, 0xa6fa },	// Room 7: Bedroom
	{  8, 0x0000 },	// Room 10: Kitchen
	{  2, 0xa725 },	// Room 11: Living room
	{  5, 0x0000 },	// Room 13: Basement
	{  7, 0xa6f0 },	// Room 13: Basement
	{  3, 0xa53d },	// Room 15: Entrance hall
	{  4, 0x0000 },	// Room 18: Sitting room
	{  6, 0xa540 },	// Room 18: Sitting room
	{  7, 0xa541 },	// Room 18: Sitting room
	{  3, 0xa52d },	// Room 21: Yodle's tree hut
	{  0, 0x293c },	// Room 22: Parlor / teleport
	{  0, 0xa52a },	// Room 23: Crossroads
	{  0, 0xa752 },	// Room 26: Forest
	{  3, 0x0000 },	// Room 26: Forest
	{  4, 0x0000 },	// Room 26: Forest
	{  5, 0x0000 },	// Room 26: Forest
	{  7, 0xa541 },	// Room 28: Telescope room
	{  1, 0x0000 },	// Room 30: 24h antique store
	{  2, 0x0000 },	// Room 30: 24h antique store
	{  4, 0xa53e },	// Room 30: 24h antique store
	{  7, 0xa541 },	// Room 30: 24h antique store
	{  0, 0x0000 },	// Room 32: Cemetery
	{  2, 0x0000 },	// Room 32: Cemetery
	{  4, 0x0000 },	// Room 32: Cemetery
	{  5, 0x0000 },	// Room 32: Cemetery
	{  6, 0x0000 },	// Room 32: Cemetery
	{  7, 0x0000 },	// Room 32: Cemetery
	{  1, 0xa76c },	// Room 33: Town
	{  0, 0x0000 },	// Room 34: Hippie / pal area
	{  1, 0xa52b },	// Room 34: Hippie / pal area
	{  3, 0x0000 },	// Room 35: Sewer
	{  0, 0xa53a },	// Room 40: Cave entrance
	{  0, 0x0000 },	// Room 41: Shore / underwater entry
	{  1, 0x0000 },	// Room 41: Shore / underwater entry
	{  2, 0x0000 },	// Room 41: Shore / underwater entry
	{  3, 0x0000 },	// Room 41: Shore / underwater entry
	{  4, 0x0000 },	// Room 41: Shore / underwater entry
	{  0, 0x0000 },	// Rooms 43, 44, 45: Maze + crystal entry
	{  0, 0x0000 },	// Rooms 43, 44, 45: Maze + crystal entry
	{  1, 0x0000 },	// Rooms 43, 44, 45: Maze + crystal entry
	{  0, 0xa784 },	// Rooms 43, 44, 45: Maze + crystal entry
	{  6, 0x0000 },	// Rooms 43, 44, 45: Maze + crystal entry
	{  0, 0x0000 },	// Room 46: Diving area / underwater
	{  0, 0x0000 },	// Room 48: Alien ship upper level
	{  2, 0x0000 },	// Room 48: Alien ship upper level
	{  1, 0xa798 },	// Room 49: Engine room
	{  2, 0x0000 },	// Room 49: Engine room
	{  3, 0x0000 },	// Room 49: Engine room
	{  0, 0x0000 },	// Room 50: Steam room
	{  5, 0x0000 },	// Room 51: Alien lobby
	{  0, 0x0000 },	// Room 52: Security scanner
	{  3, 0x0000 },	// Room 52: Security scanner
	{  4, 0x0000 },	// Room 52: Security scanner
	{  0, 0x0000 },	// Rooms 53, 57: Hallways
	{  2, 0xa53c },	// Rooms 53, 57: Hallways
	{  0, 0x0000 },	// Room 54: Waiting room
	{  1, 0x0000 },	// Room 54: Waiting room
	{  2, 0xa53c },	// Room 54: Waiting room
	{  3, 0x0000 },	// Room 54: Waiting room
	{  4, 0xa53e },	// Room 54: Waiting room
	{  5, 0x0000 },	// Room 54: Waiting room
	{  8, 0xa542 },	// Room 54: Waiting room
	{  7, 0x0000 },	// Room 54: Waiting room
	{  2, 0x0000 },	// Room 55: Corridor
	{  3, 0x0000 },	// Room 55: Corridor
	{  4, 0x0000 },	// Room 55: Corridor
	{  5, 0x0000 },	// Room 55: Corridor
	{  0, 0x0000 },	// Room 56: Transporter chamber
	{  3, 0x0000 },	// Room 56: Transporter chamber
	{  4, 0x0000 },	// Room 56: Transporter chamber
	{  5, 0x0000 },	// Room 56: Transporter chamber
	{  2, 0x0000 },	// Rooms 53, 57: Hallways
	{  3, 0xa53d },	// Rooms 53, 57: Hallways
	{  1, 0xa53b },	// Room 58: Jail
	{  0, 0x0000 },	// Room 58: Jail
	{  5, 0x0000 },	// Room 58: Jail
	{  1, 0x0000 },	// Room 59: Escape pod
	{  2, 0x0000 },	// Room 59: Escape pod
	{  3, 0x0000 },	// Room 59: Escape pod
	{  4, 0xa53e },	// Room 59: Escape pod
};

static const AnimBankRoom kAnimLoopRooms[] = {
	{  3,   0,  1 },
	{  7,   1,  1 },
	{ 10,   2,  1 },
	{ 11,   3,  1 },
	{ 13,   4,  2 },
	{ 15,   6,  1 },
	{ 18,   7,  3 },
	{ 21,  10,  1 },
	{ 22,  11,  1 },
	{ 23,  12,  1 },
	{ 26,  13,  4 },
	{ 28,  17,  1 },
	{ 30,  18,  4 },
	{ 32,  22,  6 },
	{ 33,  28,  1 },
	{ 34,  29,  2 },
	{ 35,  31,  1 },
	{ 40,  32,  1 },
	{ 41,  33,  5 },
	{ 43,  38,  1 },
	{ 44,  39,  2 },
	{ 45,  41,  2 },
	{ 46,  43,  1 },
	{ 48,  44,  2 },
	{ 49,  46,  3 },
	{ 50,  49,  1 },
	{ 51,  50,  1 },
	{ 52,  51,  3 },
	{ 53,  54,  2 },
	{ 54,  56,  8 },
	{ 55,  64,  4 },
	{ 56,  68,  4 },
	{ 57,  72,  2 },
	{ 58,  74,  3 },
	{ 59,  77,  4 },
};

const AnimLoop *animLoopsForRoom(int room, uint &count) {
	count = 0;
	for (uint i = 0; i < ARRAYSIZE(kAnimLoopRooms); i++) {
		if (kAnimLoopRooms[i].room != room)
			continue;
		count = kAnimLoopRooms[i].count;
		return &kAnimLoops[kAnimLoopRooms[i].first];
	}
	return nullptr;
}

} // End of namespace Alien
