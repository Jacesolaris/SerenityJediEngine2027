/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2005 - 2015, ioquake3 contributors
Copyright (C) 2013 - 2015, SerenityJediEngine2027 contributors

This file is part of the SerenityJediEngine2027 source code.

SerenityJediEngine2027 is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

#ifndef __TR_TYPES_H
#define __TR_TYPES_H

#include "../game/ghoul2_shared.h"
#include <qcommon\q_shared.h>

#define	MAX_DLIGHTS		32			// can't be increased, because bit flags are used on surfaces
constexpr auto REFENTITYNUM_BITS = 11; // can't be increased without changing drawsurf bit packing;
#define	REFENTITYNUM_MASK	((1<<REFENTITYNUM_BITS) - 1)
// the last N-bit number (2^REFENTITYNUM_BITS - 1) is reserved for the special world refentity,
//  and this is reflected by the value of MAX_REFENTITIES (which therefore is not a power-of-2)
#define	MAX_REFENTITIES		((1<<REFENTITYNUM_BITS) - 1)
#define	REFENTITYNUM_WORLD	((1<<REFENTITYNUM_BITS) - 1)

#define	MAX_MINI_ENTITIES	1024

// renderfx flags
constexpr auto RF_MINLIGHT = 0x00001; // allways have some light (viewmodel, some items);
constexpr auto RF_THIRD_PERSON = 0x00002; // don't draw through eyes, only mirrors (player bodies, chat sprites);
constexpr auto RF_FIRST_PERSON = 0x00004; // only draw through eyes (view weapon, damage blood blob);
constexpr auto RF_DEPTHHACK = 0x00008; // for view weapon Z crunching;
constexpr auto RF_NODEPTH = 0x00010; // No depth at all (seeing through walls);

constexpr auto RF_VOLUMETRIC = 0x00020; // fake volumetric shading;

constexpr auto RF_NOSHADOW = 0x00040; // don't add stencil shadows;

constexpr auto RF_LIGHTING_ORIGIN = 0x00080; // use refEntity->lightingOrigin instead of refEntity->origin;
// for lighting.  This allows entities to sink into the floor
// with their origin going solid, and allows all parts of a
// player to get the same lighting
constexpr auto RF_SHADOW_PLANE = 0x00100; // use refEntity->shadowPlane;
constexpr auto RF_WRAP_FRAMES = 0x00200; // mod the model frames by the maxframes to allow continuous;
// animation without needing to know the frame count
constexpr auto RF_CAP_FRAMES = 0x00400; // cap the model frames by the maxframes for one shot anims;

constexpr auto RF_ALPHA_FADE = 0x00800; // hacks blend mode and uses whatever the set alpha is.;
constexpr auto RF_PULSATE = 0x01000; // for things like a dropped saber, where we want to add an extra visual clue;
constexpr auto RF_RGB_TINT = 0x02000; // overrides ent RGB color to the specified color;

constexpr auto RF_FORKED = 0x04000; // override lightning to have forks;
constexpr auto RF_TAPERED = 0x08000; // lightning tapers;
constexpr auto RF_GROW = 0x10000; // lightning grows from start to end during its life;

constexpr auto RF_SETANIMINDEX = 0x20000; //use backEnd.currentEntity->e.skinNum for R_BindAnimatedImage;

constexpr auto RF_DISINTEGRATE1 = 0x40000; // does a procedural hole-ripping thing.;
constexpr auto RF_DISINTEGRATE2 = 0x80000; // does a procedural hole-ripping thing with scaling at the ripping point;

constexpr auto RF_G2MINLOD = 0x100000; // force Lowest lod on g2;

constexpr auto RF_SHADOW_ONLY = 0x200000; //add surfs for shadowing but don't draw them normally -rww;

constexpr auto RF_DISTORTION = 0x400000; //area distortion effect -rww;

constexpr auto RF_FORCE_ENT_ALPHA = 0x800000; // override shader alpha settings;

constexpr auto RF_ALPHA_DEPTH = 0x1000000; //depth write on alpha model

constexpr auto RF_MORELIGHT = 0x2000000; // allways have some light (viewmodel, some items) | SP version which is different from MP

// Saber slice (Jedi Survivor style cuts): see refEntity_t::slice*
#define RF_SABERSLICE		0x8000000	// ghoul2 model is cut by refEntity_t::slicePlane
#define MAX_SABER_SLICES	4

// Saber slice cut slots. A cut is tied to a slot (sliceBone low byte, the game's bit in saberSliceBones).
// Each slot lists bone segments for the skeletons we know (bone -> child, an optional twist bone that goes with
// the bone, the body part's radius at model scale 1). The first segment whose bone and child are both in the
// skeleton is used: the game, cgame and renderers all pick it the same way, by bone name (DH 2026-10-31,
// every skeleton in G:\Claude\work\skeletons\skeletons.json). Slots 0-11 are the humanoid ones.
enum
{
	SABERSLICE_BONE_LOWER_LUMBAR, // = 0
	SABERSLICE_BONE_UPPER_LUMBAR,
	SABERSLICE_BONE_THORACIC,
	SABERSLICE_BONE_CERVICAL,
	SABERSLICE_BONE_RHUMERUS,
	SABERSLICE_BONE_RRADIUS,
	SABERSLICE_BONE_LHUMERUS,
	SABERSLICE_BONE_LRADIUS,
	SABERSLICE_BONE_RFEMUR,
	SABERSLICE_BONE_RTIBIA,
	SABERSLICE_BONE_LFEMUR,
	SABERSLICE_BONE_LTIBIA,
	SABERSLICE_BONE_NECK2,
	SABERSLICE_BONE_TAIL1,
	SABERSLICE_BONE_TAIL2,
	SABERSLICE_BONE_EXTRA1,
	SABERSLICE_BONE_EXTRA2,
	SABERSLICE_BONE_EXTRA3,
	SABERSLICE_BONE_EXTRA4,
	SABERSLICE_BONE_EXTRA5,
	SABERSLICE_BONE_EXTRA6,
	SABERSLICE_BONE_EXTRA7,
	SABERSLICE_BONE_EXTRA8,
	SABERSLICE_SLOT_COUNT // at most 32 (bit masks)
};

// flags on refEntity_t::sliceBone (the slot is the low byte)
#define SABERSLICE_BONE_MASK	0xff
#define SABERSLICE_PLANE_LOCAL	0x100	// slicePlane is in the anchor bone's space: the renderer places it on the skeleton it draws
#define SABERSLICE_METAL		0x200	// metal body (droids): hot metal cut face, no flesh

// slot flags
#define SABERSLICE_SLOT_TORSO	1	// a cut through the body: the whole spine is cut by the plane, arms go with their shoulder's side
#define SABERSLICE_SLOT_NECK	2

typedef struct
{
	int slot;
	const char* bone;	// anchor bone: the cut is stored in its space
	const char* child;	// far end of the segment (the side that flies off)
	const char* extra;	// twist bone along the segment (cut by the plane too), or NULL
	float radius;
} saberSliceSeg_t;

// the cut table, NULL past the end
static inline const saberSliceSeg_t* SaberSlice_Seg(const int i)
{
	static const saberSliceSeg_t segs[] = {
		{ SABERSLICE_BONE_LOWER_LUMBAR, "lower_lumbar", "upper_lumbar", NULL, 8.0f },
		{ SABERSLICE_BONE_LOWER_LUMBAR, "lower_lumbar", "thoracic", NULL, 8.0f },
		{ SABERSLICE_BONE_LOWER_LUMBAR, "lower_lumbar1", "upper_lumbar1", NULL, 6.0f },
		{ SABERSLICE_BONE_LOWER_LUMBAR, "lower_spine", "mid_spine", NULL, 16.0f },
		{ SABERSLICE_BONE_LOWER_LUMBAR, "lower_spine", "upper_spine", NULL, 7.0f },
		{ SABERSLICE_BONE_LOWER_LUMBAR, "spinebone02", "spinebone03", NULL, 6.0f },
		{ SABERSLICE_BONE_LOWER_LUMBAR, "Spine_02", "Spine_03", NULL, 6.0f },
		{ SABERSLICE_BONE_LOWER_LUMBAR, "Spine05", "Spine06", NULL, 18.0f },
		{ SABERSLICE_BONE_LOWER_LUMBAR, "pelvis", "low_lumbar", NULL, 16.0f },
		{ SABERSLICE_BONE_LOWER_LUMBAR, "middle", "back", NULL, 9.0f },
		{ SABERSLICE_BONE_LOWER_LUMBAR, "lumbar2", "lumbar3", NULL, 3.0f },
		{ SABERSLICE_BONE_LOWER_LUMBAR, "pelvis", "thoracic", NULL, 8.0f },
		{ SABERSLICE_BONE_LOWER_LUMBAR, "pelvis", "cranium", NULL, 9.0f },
		{ SABERSLICE_BONE_LOWER_LUMBAR, "body", "m_leg", NULL, 9.0f },
		{ SABERSLICE_BONE_LOWER_LUMBAR, "body", "rtibia", NULL, 9.0f },
		{ SABERSLICE_BONE_LOWER_LUMBAR, "Bone_fin4", "Bone_fin1", NULL, 8.0f },
		{ SABERSLICE_BONE_LOWER_LUMBAR, "thoracic", "butt_bone", NULL, 3.0f },
		{ SABERSLICE_BONE_UPPER_LUMBAR, "upper_lumbar", "thoracic", NULL, 8.5f },
		{ SABERSLICE_BONE_UPPER_LUMBAR, "upper_lumbar", "cervical", NULL, 8.0f },
		{ SABERSLICE_BONE_UPPER_LUMBAR, "mid_spine", "upper_spine", NULL, 16.0f },
		{ SABERSLICE_BONE_UPPER_LUMBAR, "spinebone04", "spinebone05", NULL, 6.0f },
		{ SABERSLICE_BONE_UPPER_LUMBAR, "Spine_03", "Spine_04", NULL, 6.0f },
		{ SABERSLICE_BONE_UPPER_LUMBAR, "upper_lumbar1", "thoracic1", NULL, 6.0f },
		{ SABERSLICE_BONE_THORACIC, "thoracic", "cervical", NULL, 9.0f },
		{ SABERSLICE_BONE_THORACIC, "upper_spine", "neck_base", NULL, 16.0f },
		{ SABERSLICE_BONE_THORACIC, "thoracic1", "cranicum", NULL, 5.0f },
		{ SABERSLICE_BONE_CERVICAL, "cervical", "cranium", NULL, 3.5f },
		{ SABERSLICE_BONE_CERVICAL, "thoracic", "cranium", NULL, 4.0f },
		{ SABERSLICE_BONE_CERVICAL, "neck_mid", "head_bone", NULL, 9.0f },
		{ SABERSLICE_BONE_CERVICAL, "neck_bone", "head_bone", NULL, 3.0f },
		{ SABERSLICE_BONE_CERVICAL, "Neck_02", "Neck_03", NULL, 3.0f },
		{ SABERSLICE_BONE_CERVICAL, "cranicum", "cerv", NULL, 3.5f },
		{ SABERSLICE_BONE_CERVICAL, "neckb", "head", NULL, 3.5f },
		{ SABERSLICE_BONE_CERVICAL, "low_lumbar", "cranic", NULL, 10.0f },
		{ SABERSLICE_BONE_CERVICAL, "headbone01", "headbone02", NULL, 3.5f },
		{ SABERSLICE_BONE_CERVICAL, "Neck", "Head", NULL, 14.0f },
		{ SABERSLICE_BONE_CERVICAL, "lumbar7", "lumbar8", NULL, 3.0f },
		{ SABERSLICE_BONE_CERVICAL, "body", "cranium", NULL, 7.0f },
		{ SABERSLICE_BONE_RHUMERUS, "rhumerus", "rradius", "rhumerusX", 3.0f },
		{ SABERSLICE_BONE_RHUMERUS, "rhumerus1", "rradius1", NULL, 2.0f },
		{ SABERSLICE_BONE_RHUMERUS, "r_humerus", "r_radius", NULL, 3.0f },
		{ SABERSLICE_BONE_RHUMERUS, "R_upper_arm", "R_lower_arm", NULL, 12.0f },
		{ SABERSLICE_BONE_RHUMERUS, "R_Arm_Upper", "R_Arm_Lower", NULL, 2.5f },
		{ SABERSLICE_BONE_RHUMERUS, "r_humerous", "r_radius", NULL, 1.5f },
		{ SABERSLICE_BONE_RHUMERUS, "r_shoulderbone", "r_armbone", NULL, 3.0f },
		{ SABERSLICE_BONE_RHUMERUS, "r_humerus", "right_casing", NULL, 5.0f },
		{ SABERSLICE_BONE_RHUMERUS, "R_shoulder_bone", "R_upper_arm", NULL, 4.0f },
		{ SABERSLICE_BONE_RHUMERUS, "rfront_femur", "rfront_tibia", NULL, 3.0f },
		{ SABERSLICE_BONE_RHUMERUS, "right_arm", "right_arm1", NULL, 1.5f },
		{ SABERSLICE_BONE_RRADIUS, "rradius", "rhand", "rradiusX", 2.5f },
		{ SABERSLICE_BONE_RRADIUS, "rradius", "Wrist_Bone", NULL, 2.5f },
		{ SABERSLICE_BONE_RRADIUS, "rradius1", "rhand", NULL, 2.0f },
		{ SABERSLICE_BONE_RRADIUS, "r_radius", "rhand", NULL, 2.5f },
		{ SABERSLICE_BONE_RRADIUS, "R_lower_arm", "R_hand_bone", NULL, 10.0f },
		{ SABERSLICE_BONE_RRADIUS, "R_Arm_Lower", "R_Hand", NULL, 2.0f },
		{ SABERSLICE_BONE_RRADIUS, "R_Arm_Lower", "R_hand_bone", NULL, 2.0f },
		{ SABERSLICE_BONE_RRADIUS, "r_radius", "r_carpal", NULL, 1.2f },
		{ SABERSLICE_BONE_RRADIUS, "r_armbone", "r_gunbone01", NULL, 3.0f },
		{ SABERSLICE_BONE_RRADIUS, "r_radius", "r_hand", NULL, 5.0f },
		{ SABERSLICE_BONE_RRADIUS, "rfront_tibia", "rfront_foot", NULL, 2.5f },
		{ SABERSLICE_BONE_RRADIUS, "R_upper_arm", "R_hand_bone", NULL, 3.0f },
		{ SABERSLICE_BONE_LHUMERUS, "lhumerus", "lradius", "lhumerusX", 3.0f },
		{ SABERSLICE_BONE_LHUMERUS, "lhumerus1", "lradius1", NULL, 2.0f },
		{ SABERSLICE_BONE_LHUMERUS, "l_humerus", "l_radius", NULL, 3.0f },
		{ SABERSLICE_BONE_LHUMERUS, "L_upper_arm", "L_lower_arm", NULL, 12.0f },
		{ SABERSLICE_BONE_LHUMERUS, "L_Arm_Upper", "L_Arm_Lower", NULL, 2.5f },
		{ SABERSLICE_BONE_LHUMERUS, "l_humerous", "l_radius", NULL, 1.5f },
		{ SABERSLICE_BONE_LHUMERUS, "l_shoulderbone", "l_armbone", NULL, 3.0f },
		{ SABERSLICE_BONE_LHUMERUS, "l_humerus", "repeater1", NULL, 5.0f },
		{ SABERSLICE_BONE_LHUMERUS, "l_upper_arm", "lower_arm", NULL, 4.0f },
		{ SABERSLICE_BONE_LHUMERUS, "lfront_femur", "lfront_tibia", NULL, 3.0f },
		{ SABERSLICE_BONE_LHUMERUS, "left_arm", "left_arm1", NULL, 1.5f },
		{ SABERSLICE_BONE_LRADIUS, "lradius", "lhand", "lradiusX", 2.5f },
		{ SABERSLICE_BONE_LRADIUS, "lradius", "LeftWristY", NULL, 2.5f },
		{ SABERSLICE_BONE_LRADIUS, "lradius1", "lhand", NULL, 2.0f },
		{ SABERSLICE_BONE_LRADIUS, "l_radius", "lhand", NULL, 2.5f },
		{ SABERSLICE_BONE_LRADIUS, "L_lower_arm", "L_hand_bone", NULL, 10.0f },
		{ SABERSLICE_BONE_LRADIUS, "L_lower_arm", "L_Hand", NULL, 2.0f },
		{ SABERSLICE_BONE_LRADIUS, "L_Arm_Lower", "L_hand_bone", NULL, 2.0f },
		{ SABERSLICE_BONE_LRADIUS, "l_radius", "l_carpal", NULL, 1.2f },
		{ SABERSLICE_BONE_LRADIUS, "l_armbone", "l_gunbone01", NULL, 3.0f },
		{ SABERSLICE_BONE_LRADIUS, "l_radius", "l_hand", NULL, 5.0f },
		{ SABERSLICE_BONE_LRADIUS, "lfront_tibia", "lfront_foot", NULL, 2.5f },
		{ SABERSLICE_BONE_LRADIUS, "lower_arm", "L_hand_bone", NULL, 3.0f },
		{ SABERSLICE_BONE_RFEMUR, "rfemurYZ", "rtibia", "rfemurX", 4.5f },
		{ SABERSLICE_BONE_RFEMUR, "rfemur", "rtibia", NULL, 4.5f },
		{ SABERSLICE_BONE_RFEMUR, "rfemer", "rtibia", NULL, 4.5f },
		{ SABERSLICE_BONE_RFEMUR, "Right_Thigh_Bone", "rtibia", NULL, 4.5f },
		{ SABERSLICE_BONE_RFEMUR, "r_femur", "r_tibia", NULL, 5.0f },
		{ SABERSLICE_BONE_RFEMUR, "R_thigh", "R_calve", NULL, 10.0f },
		{ SABERSLICE_BONE_RFEMUR, "rfemurYZ1", "rfemurX", NULL, 3.5f },
		{ SABERSLICE_BONE_RFEMUR, "R_Upper_Leg", "R_Lower_Leg", NULL, 3.0f },
		{ SABERSLICE_BONE_RFEMUR, "R_leg_upper", "R_lower_leg", NULL, 3.0f },
		{ SABERSLICE_BONE_RFEMUR, "leg_right_bone01", "leg_right_bone02", NULL, 3.0f },
		{ SABERSLICE_BONE_RFEMUR, "rhind_femur", "rhind_femurb", NULL, 3.0f },
		{ SABERSLICE_BONE_RTIBIA, "rtibia", "rtalus", NULL, 3.2f },
		{ SABERSLICE_BONE_RTIBIA, "r_tibia", "r_talus", NULL, 4.0f },
		{ SABERSLICE_BONE_RTIBIA, "R_calve", "R_foot_bone", NULL, 9.0f },
		{ SABERSLICE_BONE_RTIBIA, "R_calve", "talus_4", NULL, 3.0f },
		{ SABERSLICE_BONE_RTIBIA, "R_Lower_Leg", "R_Foot", NULL, 2.0f },
		{ SABERSLICE_BONE_RTIBIA, "leg_right_bone02", "leg_right_bone03", NULL, 2.5f },
		{ SABERSLICE_BONE_RTIBIA, "rhind_tibia", "rhind_foot", NULL, 2.5f },
		{ SABERSLICE_BONE_RTIBIA, "r_tibia", "r_foot", NULL, 5.0f },
		{ SABERSLICE_BONE_LFEMUR, "lfemurYZ", "ltibia", "lfemurX", 4.5f },
		{ SABERSLICE_BONE_LFEMUR, "lfemur", "ltibia", NULL, 4.5f },
		{ SABERSLICE_BONE_LFEMUR, "lfemer", "ltibia", NULL, 4.5f },
		{ SABERSLICE_BONE_LFEMUR, "Left_Thigh_Bone", "ltibia", NULL, 4.5f },
		{ SABERSLICE_BONE_LFEMUR, "l_femur", "l_tibia", NULL, 5.0f },
		{ SABERSLICE_BONE_LFEMUR, "L_thigh", "L_calve", NULL, 10.0f },
		{ SABERSLICE_BONE_LFEMUR, "lfemurYZ1", "lfemurX", NULL, 3.5f },
		{ SABERSLICE_BONE_LFEMUR, "L_Upper_Leg", "L_Lower_Leg", NULL, 3.0f },
		{ SABERSLICE_BONE_LFEMUR, "leg_left_bone01", "leg_left_bone02", NULL, 3.0f },
		{ SABERSLICE_BONE_LFEMUR, "lhind_femur", "lhind_femurb", NULL, 3.0f },
		{ SABERSLICE_BONE_LTIBIA, "ltibia", "ltalus", NULL, 3.2f },
		{ SABERSLICE_BONE_LTIBIA, "l_tibia", "l_talus", NULL, 4.0f },
		{ SABERSLICE_BONE_LTIBIA, "L_calve", "L_foot_bone", NULL, 9.0f },
		{ SABERSLICE_BONE_LTIBIA, "L_calve", "talus_7", NULL, 3.0f },
		{ SABERSLICE_BONE_LTIBIA, "L_Lower_Leg", "L_Foot", NULL, 2.0f },
		{ SABERSLICE_BONE_LTIBIA, "leg_left_bone02", "leg_left_bone03", NULL, 2.5f },
		{ SABERSLICE_BONE_LTIBIA, "lhind_tibia", "lhind_foot", NULL, 2.5f },
		{ SABERSLICE_BONE_LTIBIA, "l_tibia", "l_foot", NULL, 5.0f },
		{ SABERSLICE_BONE_NECK2, "neck_base", "neck_mid", NULL, 10.0f },
		{ SABERSLICE_BONE_NECK2, "Neck_01", "Neck_02", NULL, 3.0f },
		{ SABERSLICE_BONE_NECK2, "neck", "neckb", NULL, 3.5f },
		{ SABERSLICE_BONE_NECK2, "lumbar4", "lumbar5", NULL, 3.0f },
		{ SABERSLICE_BONE_TAIL1, "base_tail", "mid_tail", NULL, 9.0f },
		{ SABERSLICE_BONE_TAIL1, "Tail_02", "Tail_03", NULL, 2.5f },
		{ SABERSLICE_BONE_TAIL1, "tail_pelv", "tail1", NULL, 4.0f },
		{ SABERSLICE_BONE_TAIL1, "tail", "tail2", NULL, 6.0f },
		{ SABERSLICE_BONE_TAIL1, "tail2", "tail3", NULL, 4.0f },
		{ SABERSLICE_BONE_TAIL1, "tail3", "tail5", NULL, 2.0f },
		{ SABERSLICE_BONE_TAIL2, "mid_tail", "end_tail", NULL, 6.0f },
		{ SABERSLICE_BONE_TAIL2, "Tail_06", "Tail_07", NULL, 2.0f },
		{ SABERSLICE_BONE_TAIL2, "Tail_03", "Tail_04", NULL, 2.0f },
		{ SABERSLICE_BONE_TAIL2, "tail2", "tailend", NULL, 4.0f },
		{ SABERSLICE_BONE_TAIL2, "tail4", "tail5", NULL, 3.0f },
		{ SABERSLICE_BONE_TAIL2, "tail7", "tail9", NULL, 1.5f },
		{ SABERSLICE_BONE_EXTRA1, "leg_back_bone01", "leg_back_bone02", NULL, 3.0f },
		{ SABERSLICE_BONE_EXTRA1, "b_femur", "b_tibia", NULL, 1.5f },
		{ SABERSLICE_BONE_EXTRA1, "arm_10", "arm_11", NULL, 2.0f },
		{ SABERSLICE_BONE_EXTRA1, "femur_1", "tibia_1", NULL, 4.0f },
		{ SABERSLICE_BONE_EXTRA1, "ltentacle1_bone2", "ltentacle1_bone3", NULL, 2.0f },
		{ SABERSLICE_BONE_EXTRA1, "right_wing1", "right_wing2", NULL, 3.0f },
		{ SABERSLICE_BONE_EXTRA2, "leg_back_bone02", "leg_back_bone03", NULL, 2.5f },
		{ SABERSLICE_BONE_EXTRA2, "b_tibia", "b_talus", NULL, 1.2f },
		{ SABERSLICE_BONE_EXTRA2, "arm_13", "arm_14", NULL, 2.0f },
		{ SABERSLICE_BONE_EXTRA2, "tibia_1", "talus_1", NULL, 3.0f },
		{ SABERSLICE_BONE_EXTRA2, "ltentacle2_bone2", "ltentacle2_bone3", NULL, 2.0f },
		{ SABERSLICE_BONE_EXTRA2, "left_wing1", "left_wing2", NULL, 3.0f },
		{ SABERSLICE_BONE_EXTRA3, "arm_16", "arm_17", NULL, 2.0f },
		{ SABERSLICE_BONE_EXTRA3, "femur_2", "tibia_2", NULL, 4.0f },
		{ SABERSLICE_BONE_EXTRA3, "ltentacle3_bone2", "ltentacle3_bone3", NULL, 2.0f },
		{ SABERSLICE_BONE_EXTRA3, "right_wing2", "right_wing3", NULL, 2.0f },
		{ SABERSLICE_BONE_EXTRA4, "arm_19", "arm_20", NULL, 2.0f },
		{ SABERSLICE_BONE_EXTRA4, "tibia_2", "talus_2", NULL, 3.0f },
		{ SABERSLICE_BONE_EXTRA4, "rtentacle1_bone2", "rtentacle1_bone3", NULL, 2.0f },
		{ SABERSLICE_BONE_EXTRA4, "left_wing2", "left_wing3", NULL, 2.0f },
		{ SABERSLICE_BONE_EXTRA5, "arm_22", "arm_23", NULL, 2.0f },
		{ SABERSLICE_BONE_EXTRA5, "femur_3", "tibia_3", NULL, 4.0f },
		{ SABERSLICE_BONE_EXTRA5, "rtentacle2_bone2", "rtentacle2_bone3", NULL, 2.0f },
		{ SABERSLICE_BONE_EXTRA6, "tibia_3", "talus_3", NULL, 3.0f },
		{ SABERSLICE_BONE_EXTRA6, "rtentacle3_bone2", "rtentacle3_bone3", NULL, 2.0f },
		{ SABERSLICE_BONE_EXTRA7, "femur_8", "tibia_8", NULL, 4.0f },
		{ SABERSLICE_BONE_EXTRA8, "tibia_8", "talus_8", NULL, 3.0f },
	};
	return i >= 0 && i < (int)(sizeof(segs) / sizeof(segs[0])) ? &segs[i] : NULL;
}

static inline int SaberSlice_SlotFlags(const int slot)
{
	switch (slot)
	{
	case SABERSLICE_BONE_LOWER_LUMBAR:
	case SABERSLICE_BONE_UPPER_LUMBAR:
	case SABERSLICE_BONE_THORACIC:
		return SABERSLICE_SLOT_TORSO;
	case SABERSLICE_BONE_CERVICAL:
	case SABERSLICE_BONE_NECK2:
		return SABERSLICE_SLOT_NECK;
	default:
		return 0;
	}
}

static inline int SaberSlice_IsTorso(const int slot)
{
	return (SaberSlice_SlotFlags(slot) & SABERSLICE_SLOT_TORSO) != 0;
}

static inline int SaberSlice_IsNeck(const int slot)
{
	return (SaberSlice_SlotFlags(slot) & SABERSLICE_SLOT_NECK) != 0;
}

// has(ctx, name): nonzero when the skeleton has a bone of that name
typedef int (*saberSliceHasBone_t)(void* ctx, const char* name);

// the segment a slot uses on this skeleton, NULL when the skeleton can't be cut there
static inline const saberSliceSeg_t* SaberSlice_FindSeg(const int slot, const saberSliceHasBone_t has, void* ctx)
{
	const saberSliceSeg_t* s;
	int i;
	for (i = 0; (s = SaberSlice_Seg(i)) != NULL; i++)
	{
		if (s->slot == slot && has(ctx, s->bone) && has(ctx, s->child))
		{
			return s;
		}
	}
	return NULL;
}

// world plane (normal, dist) -> plane in a bone's space. m = ghoul2 bolt matrix (rotation * scale | translation)
static inline void SaberSlice_PlaneToBone(const float m[3][4], const float* worldPlane, float* bonePlane)
{
	const float* n = worldPlane;
	for (int k = 0; k < 3; k++)
	{
		bonePlane[k] = m[0][k] * n[0] + m[1][k] * n[1] + m[2][k] * n[2];
	}
	bonePlane[3] = worldPlane[3] - (n[0] * m[0][3] + n[1] * m[1][3] + n[2] * m[2][3]);
}

// plane in a bone's space -> plane in the space m maps into (unit normal). Returns 0 if m is degenerate
static inline int SaberSlice_PlaneFromBone(const float m[3][4], const float* bonePlane, float* outPlane)
{
	const float a = m[0][0], b = m[0][1], c = m[0][2];
	const float d = m[1][0], e = m[1][1], f = m[1][2];
	const float g = m[2][0], h = m[2][1], i = m[2][2];
	const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	if (det > -1e-8f && det < 1e-8f)
	{
		return 0;
	}
	const float inv[3][3] = {
		{ (e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det },
		{ (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det },
		{ (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det }
	};
	float n[3];
	for (int k = 0; k < 3; k++)
	{
		n[k] = inv[0][k] * bonePlane[0] + inv[1][k] * bonePlane[1] + inv[2][k] * bonePlane[2];
	}
	float dist = bonePlane[3] + n[0] * m[0][3] + n[1] * m[1][3] + n[2] * m[2][3];
	const float len = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
	if (len < 1e-12f)
	{
		return 0;
	}
	const float il = 1.0f / (float)sqrt(len);
	outPlane[0] = n[0] * il;
	outPlane[1] = n[1] * il;
	outPlane[2] = n[2] * il;
	outPlane[3] = dist * il;
	return 1;
}

// refdef flags
constexpr auto RDF_NOWORLDMODEL = 1; // used for player configuration screen;
constexpr auto RDF_HYPERSPACE = 4; // teleportation effect;

constexpr auto RDF_SKYBOXPORTAL = 8;
constexpr auto RDF_DRAWSKYBOX = 16;
// the above marks a scene as being a 'portal sky'.  this flag says to draw it or not;

constexpr auto RDF_doLAGoggles = 32; // Light Amp goggles;
constexpr auto RDF_doFullbright = 64; // Light Amp goggles;
constexpr auto RDF_ForceSightOn = 128; // using force sight;

using rgbTintsNew_t = enum
{
	TINT_NEW_ENT,
	TINT_HILT1,
	TINT_HILT2,
	MAX_CVAR_TINT,
	TINT_BLADE1 = MAX_CVAR_TINT,
	TINT_BLADE2,
	MAX_NEW_ENT_RGB
};

extern int skyboxportal;
extern int drawskyboxportal;

using color4ub_t = byte[4];

using polyVert_t = struct
{
	vec3_t xyz;
	float st[2];
	byte modulate[4];
};

using poly_t = struct poly_s
{
	qhandle_t hShader;
	int numVerts;
	polyVert_t* verts;
};

using refEntityType_t = enum
{
	RT_MODEL,
	RT_POLY,
	RT_SPRITE,
	RT_ORIENTED_QUAD,
	RT_LINE,
	RT_ORIENTEDLINE,
	RT_ELECTRICITY,
	RT_CYLINDER,
	RT_LATHE,
	RT_BEAM,
	RT_SABER_GLOW,
	RT_PORTALSURFACE,
	// doesn't draw anything, just info for portals
	RT_CLOUDS,
	RT_LIGHTNING,
	RT_ENT_CHAIN,

	RT_MAX_MP_REF_ENTITY_TYPE,

	RT_MAX_SP_REF_ENTITY_TYPE,

	RT_MAX_REF_ENTITY_TYPE
};

using miniRefEntity_t = struct
{
	refEntityType_t		reType;
	int					renderfx;

	qhandle_t			hModel;				// opaque type outside refresh

	// most recent data
	matrix3_t			axis;			// rotation vectors
	qboolean			nonNormalizedAxes;	// axis are not normalized, i.e. they have scale
	vec3_t				origin;				// also used as MODEL_BEAM's "from"

	// previous data for frame interpolation
	vec3_t				oldorigin;			// also used as MODEL_BEAM's "to"

	// texturing
	qhandle_t			customShader;		// use one image for the entire thing

	// misc
	byte				shaderRGBA[4];		// colors used by rgbgen entity shaders
	vec2_t				shaderTexCoord;		// texture coordinates used by tcMod entity modifiers

	// extra sprite information
	float				radius;
	float				rotation;			// size 2 for RT_CYLINDER or number of verts in RT_ELECTRICITY

	// misc
	float		shaderTime;			// subtracted from refdef time to control effect start times
	int			frame;				// also used as MODEL_BEAM's diameter
};

using refEntity_t = struct
{
	refEntityType_t reType;
	int renderfx;

	qhandle_t hModel; // opaque type outside refresh

	// most recent data
	vec3_t lightingOrigin; // so multi-part models can be lit identically (RF_LIGHTING_ORIGIN)
	float shadowPlane; // projection shadows go here, stencils go slightly lower

	vec3_t axis[3]; // rotation vectors
	qboolean nonNormalizedAxes; // axis are not normalized, i.e. they have scale
	float origin[3]; // also used as MODEL_BEAM's "from"
	int frame; // also used as MODEL_BEAM's diameter

	// previous data for frame interpolation
	float oldorigin[3]; // also used as MODEL_BEAM's "to"
	int oldframe;
	float backlerp; // 0.0 = current, 1.0 = old

	// texturing
	int skinNum; // inline skin index

	qhandle_t customSkin; // NULL for default skin
	qhandle_t customShader; // use one image for the entire thing

	// misc
	byte shaderRGBA[4]; // colors used by colorSrc=vertex shaders
	float shaderTexCoord[2]; // texture coordinates used by tcMod=vertex modifiers
	float shaderTime; // subtracted from refdef time to control effect start times

	// extra sprite information
	float radius;

	// texturing
	union
	{
		//		int			skinNum;		// inline skin index
		//		ivec3_t		terxelCoords;	// coords of patch for RT_TERXELS
		struct
		{
			int miniStart;
			int miniCount;
		} uMini;
	} uRefEnt;

	// extra sprite information
	union
	{
		struct
		{
			float rotation;
			float radius;
			byte vertRGBA[4][4];
		} sprite;

		struct
		{
			float width;
			float width2;
			float stscale;
		} line;

		struct
			// that whole put-the-opening-brace-on-the-same-line-as-the-beginning-of-the-definition coding style is fecal
		{
			float width;
			vec3_t control1;
			vec3_t control2;
		} bezier;

		struct
		{
			float width;
			float width2;
			float stscale;
			float height;
			float bias;
			qboolean wrap;
		} cylinder;

		struct
		{
			float width;
			float deviation;
			float stscale;
			qboolean wrap;
			qboolean taper;
		} electricity;
	} data;

	// This doesn't have to be unioned, but it does make for more meaningful variable names :)
	union
	{
		float rotation;
		float endTime;
		float saberLength;
	};

	/*
	Ghoul2 Insert Start
	*/
	vec3_t angles; // rotation angles - used for Ghoul2

	vec3_t modelScale; // axis scale for models
	CGhoul2Info_v* ghoul2; // has to be at the end of the ref-ent in order for it to be created properly
	/*
	Ghoul2 Insert End
	*/
	byte newShaderRGBA[MAX_NEW_ENT_RGB][4]; // colors used by colorSrc=vertex shaders

	// Saber slice (Jedi Survivor style cuts at the saber contact point).
	// Only read by the renderer when renderfx has RF_SABERSLICE. Ghoul2 models only.
	int sliceCount; // 1..MAX_SABER_SLICES
	int sliceBone[MAX_SABER_SLICES]; // SABERSLICE_BONE_* anchor bone of each cut | SABERSLICE_PLANE_LOCAL / SABERSLICE_METAL
	float slicePlane[MAX_SABER_SLICES][4]; // world space normal + dist, the positive side is cut away
	int sliceInvert; // bit i set: show ONLY the cut-away side of slice i (the flying piece)
	float sliceHeat; // 1 = fresh glowing cut, 0 = cooled down
};

constexpr auto MAX_RENDER_STRINGS = 8;
constexpr auto MAX_RENDER_STRING_LENGTH = 32;

using refdef_t = struct
{
	int x, y, width, height;
	float fov_x, fov_y;
	vec3_t vieworg;
	vec3_t viewangles;
	vec3_t viewaxis[3]; // transformation matrix
	int viewContents; // world contents at vieworg

	// time in milliseconds for shader effects and other time dependent rendering issues
	int time;

	int rdflags; // RDF_NOWORLDMODEL, etc

	// 1 bits will prevent the associated area from rendering at all
	byte areamask[MAX_MAP_AREA_BYTES];

	// text messages for deform text shaders
	char text[MAX_RENDER_STRINGS][MAX_RENDER_STRING_LENGTH];
};

using stereoFrame_t = enum
{
	STEREO_CENTER,
	STEREO_LEFT,
	STEREO_RIGHT
};

/*
** glconfig_t
**
** Contains variables specific to the OpenGL configuration
** being run right now.  These are constant once the OpenGL
** subsystem is initialized.
*/
using textureCompression_t = enum
{
	TC_NONE,
	TC_S3TC,
	TC_S3TC_DXT,
	TC_S3TC_ARB
};

using glconfig_t = struct glconfig_s
{
	const char* renderer_string;
	const char* vendor_string;
	const char* version_string;
	const char* extensions_string;
	int maxTextureSize; // queried from GL
	int maxActiveTextures; // multitexture ability
	float maxTextureFilterAnisotropy;

	int colorBits, depthBits, stencilBits;

	qboolean deviceSupportsGamma;
	textureCompression_t textureCompression;
	qboolean textureEnvAddAvailable;
	qboolean textureFilterAnisotropicAvailable;
	qboolean clampToEdgeAvailable;

	int vidWidth, vidHeight;

	int displayFrequency;

	qboolean				doStencilShadowsInOneDrawcall;

	// synonymous with "does rendering consume the entire screen?", therefore
	// a Voodoo or Voodoo2 will have this set to TRUE, as will a Win32 ICD that
	// used CDS.
	qboolean isFullscreen;
	qboolean stereoEnabled;
};

#endif	// __TR_TYPES_H
