/*	FILE:	Oni_Weapon.c

	AUTHOR:	Quinn Dunki, Michael Evans, Chris Butcher

	CREATED: April 2, 1999

	PURPOSE: control of weapons in ONI

	Copyright 1998, 2000

*/

#include "bfw_math.h"

#include "BFW.h"
#include "BFW_Totoro.h"
#include "BFW_Motoko.h"
#include "BFW_TemplateManager.h"
#include "BFW_Physics.h"
#include "BFW_Object.h"
#include "BFW_ScriptLang.h"

#include "Oni_Weapon.h"
#include "Oni_ExperimentalLights.h"
#include "BFW_Particle3.h"
#include "BFW_EnvParticle.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "Oni.h"
#include "Oni_GameStatePrivate.h"
#include "Oni_Character_Animation.h"
#include "Oni_AI2_Knowledge.h"
#include "Oni_Object.h"
#include "Oni_InGameUI.h"
#include "Oni_Mechanics.h"
#include "Oni_ImpactEffect.h"
#include "Oni_Sound2.h"
#include "Oni_Persistance.h"

#define WEAPON_DEBUG_BLAST			0

// scales for how far objects fly when they are knocked away
#define WPcPistolKickbackScale	0.12f
#define WPcRifleKickbackScale	0.10f
#define WPcCannonKickbackScale	0.07f
#define WPcPowerupKickbackScale	0.055f

#define WPcFadeTime (10 * 60)
#define WPcFreeTime (80 * 60)
#define WPcOverhypeMultiplier 2

#define WPcWeaponStaticFriction			0.0025f
#define WPcWeaponDynamicFriction		0.75f
#define WPcPowerupStaticFriction		0.0025f
#define WPcPowerupDynamicFriction		0.75f

#define WPcImpact_MinVelocity			0.35f
#define WPcPowerup_MinSphereRadius		1.0f
#define WPcPowerupPhysicsTimer			120
#define WPcGlowTextureScale				1.1f

#define WPcItemLaunchSpeed				1.5f

#define WPcAI_Alert_Delay				30

#define WPcShieldDisplaySpeed			2

#define WPcWeaponNodeCount 16


struct WPtWeapon
{
	// general control
	UUtUns16			index;
	UUtUns16			flags;
	WPtWeaponClass		*weaponClass;
	ONtCharacter		*owner;

	// physics
	M3tMatrix4x3		matrix;
	PHtPhysicsContext	*physics;
	float				rotation;		// CB: determined at creation from weapon matrix, or by weapon placement tool

	// life cycle
	UUtUns16			fadeTime;
	UUtUns16			freeTime;
	UUtUns32			birthDate;

	// state variables
	UUtUns16			chamber_time;
	UUtUns16			chamber_time_total;
	UUtUns16			reload_delay_time;
	UUtUns16			reloadTime;
	UUtUns16			must_fire_time;
	UUtUns16			current_ammo;

	// attachments
	UUtUns16			attachments_started;
	UUtUns16			start_delay[WPcMaxParticleAttachments];
	P3tParticle *		particle[WPcMaxParticleAttachments];
	P3tParticleReference particle_ref[WPcMaxParticleAttachments];		// for knowing when particles get deleted

	// AI-related variables
	UUtUns32			ai_alert_timer;
	UUtUns32			ai_firingspread_index;
	UUtUns32			nearby_link;
	float				nearby_distance_squared;

	PHtSphereTree		weapon_sphere_tree;

	M3tPoint3D last_position; // S.S.
	UUtUns32 oct_tree_node_index[WPcWeaponNodeCount]; // S.S. length-prefixed list of oct tree nodes
};

const char *WPgPowerupName[WPcPowerup_NumTypes]
	= {"ammo", "cell", "hypo", "shield", "invis", "lsi"};

WPtWeapon WPgWeapons[WPcMaxWeapons];
WPtPowerup WPgPowerups[WPcMaxPowerups];
UUtUns16 WPgNumPowerups;

MAtImpactType WPgWeaponImpactType;
MAtImpactType WPgPowerupImpactType;

extern UUtBool OBgObjectsShowDebug;
UUtInt32 WPgHypoStrength;
UUtInt32 WPgFadeTime;
UUtBool WPgDisableFade;

UUtBool WPgDebugShowEvents = UUcFalse;
UUtBool WPgPlayerKicksWeapons = UUcFalse;
UUtBool WPgRecoilEdit = UUcFalse;
WPtRecoil WPgRecoil_Edited;
WPtRecoil WPgRecoil_Commited;
UUtBool WPgGatlingCheat = UUcFalse;
UUtBool WPgRegenerationCheat = UUcFalse;

static PHtPhysicsCallbacks gWeaponCallbacks;
static PHtPhysicsCallbacks gPowerupCallbacks;

static void WPrSetupCallbacks(void);
static void WPiPulseShooter(WPtWeapon *inWeapon, UUtUns16 inIndex, WPtParticleAttachment *inShooter);
static void WPiDoEvent(WPtWeapon *inWeapon, UUtUns16 inEvent, UUtUns16 inIndex);
static void WPiAlertAI(WPtWeapon *ioWeapon, UUtBool inAlternate);
static void WPiFindParticleClasses(WPtWeaponClass *inClass);
static UUtError
WPiCommand_GivePowerup(
	SLtErrorContext*	inErrorContext,
	UUtUns32			inParameterListLength,
	SLtParameter_Actual*		inParameterList,
	UUtUns32			*outTicksTillCompletion,
	UUtBool				*outStall,
	SLtParameter_Actual		*ioReturnValue);

static void WPrPowerup_AddPhysics(
	WPtPowerup *inPowerup);
static void WPrPowerup_RemovePhysics(
	WPtPowerup *inPowerup);

static void WPrNotThroughWall(ONtCharacter *inCharacter, M3tMatrix4x3 *ioMatrix);

UUtUns16 WPrGetAmmo(
	WPtWeapon *inWeapon)
{
	if ((inWeapon->owner != NULL) && (inWeapon->owner->flags & ONcCharacterFlag_InfiniteAmmo)) {
		WPrSetMaxAmmo(inWeapon);
	}

	return inWeapon->current_ammo;
}

UUtUns16 WPrMaxAmmo(
	WPtWeapon *inWeapon)
{
	return inWeapon->weaponClass->max_ammo;
}

static void FaceKnockdown(ONtCharacter *ioCharacter, M3tVector3D *inDirection, TRtAnimType front_type, TRtAnimType behind_type)
{
	UUtInt32 direction;

	direction = ONrCharacter_FaceAwayOrAtVector_Raw(ioCharacter, inDirection);

	if (1 == direction) {
		ONrCharacter_Knockdown(NULL, ioCharacter, behind_type);
	}
	else {
		ONrCharacter_Knockdown(NULL, ioCharacter, front_type);
	}

	return;
}

static UUtBool InflictDamage(ONtCharacter *inCharacter, float inRadius, M3tPoint3D *inHitPoint, M3tVector3D *inDirection,
						  P3tEnumDamageType inDamageType, UUtUns32 inDamage, UUtUns32 inStun, float inKnockback,
						  P3tParticleReference inParticleRef, UUtUns32 inOwner)
{
	ONtActiveCharacter *active_character = ONrForceActiveCharacter(inCharacter);
	ONtCharacter *attacker;
	UUtUns32 physical_damage;
	UUtBool super_shield;

	if (NULL == active_character) {
		COrConsole_Printf("failed to allocate active_character");

		return UUcFalse;
	}

	if (inParticleRef != P3cParticleReference_Null) {
		if (active_character->lastParticleDamageRef == inParticleRef) {
			// this particle cannot collide twice with the same character,
			// there is a multiple-collision problem and we should reject this damage.
			return UUcTrue;
		}

		active_character->lastParticleDamageRef = inParticleRef;
	}

	super_shield = ONrCharacter_HasSuperShield(inCharacter);

	if ((inCharacter->inventory.shieldRemaining > 0) || (inCharacter->flags & ONcCharacterFlag_BossShield) || super_shield) {
		UUtUns32 hit_mask = ONrCharacter_FindParts(inCharacter, inHitPoint, inRadius);
		ONrCharacter_Shield_HitParts(inCharacter, hit_mask /* eventually find a real mask */);
	}

	if (super_shield || (inCharacter->flags & ONcCharacterFlag_WeaponImmune)){
		// this character is totally immune to weapon damage
	}
	else if (inCharacter->inventory.shieldRemaining > 0) {
		if (inCharacter->inventory.shieldRemaining >= inDamage) {
			inCharacter->inventory.shieldRemaining -= (UUtUns16) inDamage;
		}
		else {
			inCharacter->inventory.shieldRemaining = 0;
		}

		ONrCharacter_DamageCompass(inCharacter, inDirection);
	}
	else {
		// apply character-class-specific resistances
		if ((inDamageType >= 0) && (inDamageType < P3cEnumDamageType_Max)) {
			float resistance = inCharacter->characterClass->damageResistance[inDamageType];

			if (resistance > 0) {
				resistance = UUmMax(0.0f, 1.0f - resistance);

				inStun = MUrUnsignedSmallFloat_To_Uns_Round(resistance * inStun);
				inKnockback *= resistance;
			}
		}

		if (inCharacter->flags & ONcCharacterFlag_BossShield) {
			float adjusted_damage, adjusted_stun;

			inKnockback *= (1.0f - inCharacter->characterClass->bossShieldProtectAmount);
			adjusted_damage = inDamage * (1.0f - inCharacter->characterClass->bossShieldProtectAmount);
			adjusted_stun = inStun * (1.0f - inCharacter->characterClass->bossShieldProtectAmount);

			// compute integer damage and stun
			inDamage = (UUtUns32) adjusted_damage;
			inStun = (UUtUns32) adjusted_stun;

			// account for fractional damage and stun using random chance
			if (UUrRandom() < (adjusted_damage - inDamage) * UUcMaxUns16) {
				inDamage += 1;
			}
			if (UUrRandom() < (adjusted_stun - inStun) * UUcMaxUns16) {
				inStun += 1;
			}
		}

		physical_damage = (inDamageType == P3cEnumDamageType_PickUp) ? 0 : inDamage;

		ONrCharacter_FightMode(inCharacter);
		ONrCharacter_TakeDamage(inCharacter, physical_damage, inKnockback, inHitPoint, inDirection, inOwner, ONcAnimType_None);

		// CB: unstoppable characters can't get blown up, stunned, etc
		if (!ONrCharacter_IsUnstoppable(inCharacter)) {
			switch(inDamageType)
			{
				case P3cEnumDamageType_Normal:
					ONrCharacter_MinorHitReaction(inCharacter);
				break;

				case P3cEnumDamageType_MinorStun:
					if (inStun > 0) {
						ONrCharacter_Knockdown(NULL, inCharacter, ONcAnimType_Stun);
						active_character->dizzyStun = (UUtUns16) UUmMax(inStun, active_character->dizzyStun);
					}
				break;

				case P3cEnumDamageType_MajorStun:
					if (inStun > 0) {
						switch (active_character->curAnimType)
						{
							case ONcAnimType_Stagger:
							case ONcAnimType_Stagger_Behind:
							case ONcAnimType_Stun:
							break;

							default:
								FaceKnockdown(inCharacter, inDirection, ONcAnimType_Stagger, ONcAnimType_Stagger_Behind);
						}

						active_character->staggerStun = 30;
						active_character->dizzyStun = (UUtUns16) UUmMax(inStun, active_character->dizzyStun);
					}
				break;

				case P3cEnumDamageType_MinorKnockdown:
					{
						float length_of_knockback_squared = MUmVector_GetLengthSquared(active_character->knockback);

						//COrConsole_Printf("knockback %2.2f %2.2f", length_of_knockback_squared, sqrt(length_of_knockback_squared));

						if (length_of_knockback_squared > UUmSQR(4.1f)) {
							if (!ONrAnimType_IsKnockdown(active_character->curAnimType)) {
								FaceKnockdown(inCharacter, inDirection, ONcAnimType_Knockdown_Head, ONcAnimType_Knockdown_Head_Behind);
							}
						}
						else if (length_of_knockback_squared > UUmSQR(2.2f)) {
							if (!ONrAnimType_IsVictimType(active_character->curAnimType)) {
								FaceKnockdown(inCharacter, inDirection, ONcAnimType_Stagger, ONcAnimType_Stagger_Behind);
							}

							active_character->staggerStun = 22;
						}
					}
				break;

				case P3cEnumDamageType_MajorKnockdown:
					FaceKnockdown(inCharacter, inDirection, ONcAnimType_Knockdown_Head, ONcAnimType_Knockdown_Head_Behind);
				break;

				case P3cEnumDamageType_Blownup:
					FaceKnockdown(inCharacter, inDirection, ONcAnimType_Blownup, ONcAnimType_Blownup_Behind);
				break;

				case P3cEnumDamageType_PickUp:
					{
						UUtBool pickup;

						if (active_character->inAirControl.pickupReleaseTimer > 0) {
							pickup = UUcTrue;
						} else {
							// check to see if we will get picked up
							active_character->pickup_percentage += inDamage;
							pickup = (active_character->pickup_percentage >= 100);
						}

						if (pickup) {
							attacker = WPrOwner_GetCharacter(inOwner);
							if (attacker != NULL) {
								// the character is being picked up by this damage
								active_character->inAirControl.pickupReleaseTimer += (UUtUns16) inStun;
								active_character->inAirControl.pickupReleaseTimer = UUmMin(active_character->inAirControl.pickupReleaseTimer, 60);
								active_character->inAirControl.pickupOwner = attacker;
							}
						}
					}
				break;
			}
		}
	}

	return UUcFalse;
}

UUtBool WPrHitChar(void *inHitChar, M3tPoint3D *inHitPoint, M3tVector3D *inDirection, M3tVector3D *inHitNormal,
				P3tEnumDamageType inDamageType, float inDamage, float inStunDmg, float inKnockback, UUtUns32 inOwner,
				P3tParticleReference inParticleRef, UUtBool inSelfImmune)
{
	ONtCharacter *character = (ONtCharacter *) inHitChar;
	M3tVector3D hit_dir, pelvis_pt;
	UUtUns32 in_damage_integer = MUrUnsignedSmallFloat_To_Uns_Round(inDamage);

	// environmental particle effects have no 'owner' field
	if (inOwner == 0) {
		inOwner = WPcDamageOwner_Environmental;
	} else if (inSelfImmune && WPrOwner_IsSelfDamage(inOwner, inHitChar)) {
		return UUcFalse;
	}


	UUmAssert((character >= ONgGameState->characters) &&
			  (character < &ONgGameState->characters[ONgGameState->numCharacters]));

	MUmVector_Copy(hit_dir, *inDirection);
	if (MUrVector_Normalize_ZeroTest(&hit_dir)) {
		// no input hit direction, we must build one
		ONrCharacter_GetPelvisPosition(character, &pelvis_pt);
		MUmVector_Subtract(hit_dir, pelvis_pt, *inHitPoint);
		if (MUrVector_Normalize_ZeroTest(&hit_dir)) {
			// we have been hit exactly at our pelvis, no hit direction is available -
			// build a hit direction from our front
			MUmVector_Set(hit_dir, -MUrSin(character->facing), 0, -MUrCos(character->facing));
		}
	}

	return InflictDamage(inHitChar, 4.f, inHitPoint, &hit_dir, inDamageType, MUrUnsignedSmallFloat_To_Uns_Round(inDamage),
						MUrUnsignedSmallFloat_To_Uns_Round(inStunDmg), inKnockback, inParticleRef, inOwner);
}

void WPrBlast(M3tPoint3D *inLocation, P3tEnumDamageType inDamageType, float inDamage, float inStunDmg, float inKnockback, float inRadius,
			  P3tEnumFalloff inFalloffType, UUtUns32 inOwner, void *inIgnoreChar, P3tParticleReference inParticleRef, UUtBool inSelfImmune)
{
	ONtCharacter *character;
	UUtUns16 itr;
	float distance;
	M3tVector3D blast_vector;
	ONtCharacter **active_character_list = ONrGameState_ActiveCharacterList_Get();
	UUtUns32 active_character_count = ONrGameState_ActiveCharacterList_Count();
	AKtEnvironment *environment = ONrGameState_GetEnvironment();

	// environmental particle effects have no 'owner' field
	if (inOwner == 0) {
		inOwner = WPcDamageOwner_Environmental;
	}

	for(itr = 0; itr < active_character_count; itr++)
	{
		character = active_character_list[itr];

		if (character->flags & ONcCharacterFlag_Dead) {
			continue;
		}

		if (character == (ONtCharacter *) inIgnoreChar) {
#if WEAPON_DEBUG_BLAST
			COrConsole_Printf("weapon blast skipping %s, specifically ignored", character->player_name);
#endif
			continue;
		}

		if (inSelfImmune && WPrOwner_IsSelfDamage(inOwner, character)) {
#if WEAPON_DEBUG_BLAST
			COrConsole_Printf("weapon blast skipping %s, self-immune and self-owner", character->player_name);
#endif
			continue;
		}

		ONrCharacter_GetPelvisPosition(character, &blast_vector);
		MUmVector_Subtract(blast_vector, blast_vector, *inLocation);
		distance = MUmVector_GetLength(blast_vector) / inRadius;

		if (distance <= 1.f) {
			UUtUns32 damage_integer;
			UUtUns32 stun_integer;
			float knockback;
			float percent_effect;

			// check that we have a line of sight to the blast center
			if (AKrCollision_Point(environment, inLocation, &blast_vector, AKcGQ_Flag_LOS_CanShoot_Skip_AI, 0)) {
				// blast damage is blocked by environment geometry
#if WEAPON_DEBUG_BLAST
				COrConsole_Printf("weapon blast skipping %s, no line of sight", character->player_name);
#endif
				continue;
			}

			switch(inFalloffType)
			{
				case P3cEnumFalloff_None:
					percent_effect = 1.0f;
				break;

				case P3cEnumFalloff_Linear:
					percent_effect = 1.0f - distance;
				break;

				case P3cEnumFalloff_InvSquare:
					// inverse-square, from 100% in middle to 25% at blast fringe
					percent_effect = 1.0f / UUmSQR(1.0f + distance);
				break;

				default:
					percent_effect = 1.0f;
			}

			MUrNormalize(&blast_vector);

			damage_integer = MUrUnsignedSmallFloat_To_Uns_Round(percent_effect * inDamage);
			stun_integer =  MUrUnsignedSmallFloat_To_Uns_Round(percent_effect * inStunDmg);
			knockback = inKnockback * percent_effect;

#if WEAPON_DEBUG_BLAST
			COrConsole_Printf("weapon blast hurting %s, distance fraction %f, damage fraction %f -> dmg %d stun %d knockback %f",
								character->player_name, distance, percent_effect, damage_integer, stun_integer, knockback);
#endif
			InflictDamage(character, inRadius, inLocation, &blast_vector, inDamageType, damage_integer,
						stun_integer, knockback, inParticleRef, inOwner);
		} else {
#if WEAPON_DEBUG_BLAST
			COrConsole_Printf("weapon blast skipping %s, outside radius (%f / %f)", character->player_name, MUmVector_GetLength(blast_vector), inRadius);
#endif
		}
	}

	return;
}

static void WPiGenerateFallImpact(
	PHtCollider *ioCollider,
	MAtImpactType inImpactType)
{
	P3tEffectData effect_data;
	AKtEnvironment *environment;
	AKtGQ_Render *render_gq;
	UUtUns32 gq_index, tex_index, tex_material;
	MAtMaterialType material_type;

	// set up the effect data
	UUrMemory_Clear(&effect_data, sizeof(effect_data));
	effect_data.time = ONrGameState_GetGameTime();

	// get the collision location
	MUmVector_Copy(effect_data.position, ioCollider->planePoint);
	effect_data.normal.x = ioCollider->plane.a;
	effect_data.normal.y = ioCollider->plane.b;
	effect_data.normal.z = ioCollider->plane.c;

	environment = ONrGameState_GetEnvironment();
	gq_index = ((AKtGQ_General *) ioCollider->data) - environment->gqGeneralArray->gqGeneral;
	UUmAssert((gq_index >= 0) && (gq_index < environment->gqGeneralArray->numGQs));

	effect_data.cause_type = P3cEffectCauseType_ParticleHitWall;
	effect_data.cause_data.particle_wall.gq_index = gq_index;

	// get the texture off this GQ
	render_gq = environment->gqRenderArray->gqRender + gq_index;
	tex_index = render_gq->textureMapIndex;
	material_type = MAcMaterial_Base;
	if (tex_index < environment->textureMapArray->numMaps) {
		// determine the material type for this texture
		tex_material = environment->textureMapArray->maps[tex_index]->materialType;
		if (tex_material < MArMaterials_GetNumTypes()) {
			material_type = (MAtMaterialType) tex_material;
		}
	}

	ONrImpactEffect(inImpactType, material_type, ONcIEModType_Any, &effect_data, 1.0f, NULL, NULL);
}


static void WPiWeaponPhysics_PostCollision_Callback(
	PHtPhysicsContext *ioContext,
	PHtCollider *ioColliders,
	UUtUns16 *ioNumColliders)
{
	/**************
	* Physics collision callback for weapons
	*/

	UUtUns32 i;
	PHtCollider *collider;
	PHtPhysicsContext *otherContext = NULL;
	WPtWeapon *weapon = (WPtWeapon *)ioContext->callbackData;

	UUmAssert(ioContext->callback->type == PHcCallback_Weapon);
	UUmAssert(ioContext->flags & PHcFlags_Physics_InUse);
	UUmAssert(!ioContext->sphereTree->child);
	UUmAssert(!ioContext->sphereTree->sibling);

	for (i=0; i<*ioNumColliders; i++)
	{
		collider = ioColliders + i;

		// Determine what we hit
		switch (collider->type)
		{
			case PHcCollider_Env:
				if ((float)fabs(ioContext->velocity.x * collider->plane.a +
							ioContext->velocity.y * collider->plane.b +
							ioContext->velocity.z * collider->plane.c) > WPcImpact_MinVelocity) {
					WPiGenerateFallImpact(collider, WPgWeaponImpactType);
				}
				break;

			case PHcCollider_Phy:
				otherContext = (PHtPhysicsContext *)collider->data;
				if (otherContext->callback->type == PHcCallback_Character) continue;
				break;
		}

		weapon->flags |= WPcWeaponFlag_HitGround;
	}
}

static UUtBool WPiWeaponPhysics_SkipPhyCollisions_Callback(
	const PHtPhysicsContext *inContext,
	const PHtPhysicsContext *inOtherContext)
{
	UUmAssert(inContext->callback->type == PHcCallback_Weapon);
	switch (inOtherContext->callback->type) {
		case PHcCallback_Weapon:
		{
			WPtWeapon *this_weapon, *other_weapon;

			this_weapon = (WPtWeapon *) inContext->callbackData;
			other_weapon = (WPtWeapon *) inOtherContext->callbackData;

			if ((this_weapon->flags & WPcWeaponFlag_NoWeaponCollision) ||
				(other_weapon->flags & WPcWeaponFlag_NoWeaponCollision)) {
				return UUcTrue;
			} else {
				return UUcFalse;
			}
		}

		default:
			return UUcFalse;
	}
}

static UUtBool WPiPowerupPhysics_SkipPhyCollisions_Callback(
	const PHtPhysicsContext *inContext,
	const PHtPhysicsContext *inOtherContext)
{
	UUmAssert(inContext->callback->type == PHcCallback_Powerup);
	switch (inOtherContext->callback->type) {
		case PHcCallback_Character:
			return UUcTrue;

		default:
			return UUcFalse;
	}
}

static void WPiPowerupPhysics_PostCollision_Callback(
	PHtPhysicsContext *ioContext,
	PHtCollider *ioColliders,
	UUtUns16 *ioNumColliders)
{
	/**************
	* Physics collision callback for powerups
	*/

	UUtBool played_impact = UUcFalse;
	UUtUns32 i;
	PHtCollider *collider;
	PHtPhysicsContext *otherContext = NULL;
	WPtPowerup *powerup = (WPtPowerup *)ioContext->callbackData;

	UUmAssert(ioContext->callback->type == PHcCallback_Powerup);
	UUmAssert(ioContext->flags & PHcFlags_Physics_InUse);
	UUmAssert(!ioContext->sphereTree->child);
	UUmAssert(!ioContext->sphereTree->sibling);
	UUmAssert(powerup->flags & WPcPowerupFlag_Moving);

	powerup->flags &= ~WPcPowerupFlag_OnGround;

	for (i=0; i<*ioNumColliders; i++)
	{
		collider = ioColliders + i;

		if (collider->type == PHcCollider_Phy) {
			otherContext = (PHtPhysicsContext *)collider->data;
			if (otherContext->callback->type == PHcCallback_Character) {
				// powerups do not collide with characters
				continue;
			}

		} else if (collider->type == PHcCollider_Env) {
			if ((!played_impact) &&
				((float)fabs(ioContext->velocity.x * collider->plane.a +
				 		ioContext->velocity.y * collider->plane.b +
				 		ioContext->velocity.z * collider->plane.c) > WPcImpact_MinVelocity)) {
				WPiGenerateFallImpact(collider, WPgPowerupImpactType);
				played_impact = UUcTrue;
			}

			if (collider->plane.b > ONcMinFloorNormalY) {
				powerup->flags |= (WPcPowerupFlag_OnGround | WPcPowerupFlag_HitGround);
			}
		}
	}
}

UUtError WPrRegisterTemplates(
	void)
{
	/*****************
	* Register weapon templates
	*/

	UUtError error;

	error = TMrTemplate_Register(WPcTemplate_WeaponClass, sizeof(WPtWeaponClass), TMcFolding_Allow);
	UUmError_ReturnOnError(error);

	return UUcError_None;
}

void WPrSetMaxAmmo(WPtWeapon *inWeapon)
{
	inWeapon->current_ammo = inWeapon->weaponClass->max_ammo;

	if ((inWeapon->owner != NULL) && (inWeapon->flags & WPcWeaponFlag_InHand)) {
		WPrStartBackgroundEffects(inWeapon);
	}

	return;
}

// CB: the weapon has been reloaded - give it max ammo. there is
// now a fixed delay before it can resume firing.
void WPrNotifyReload(WPtWeapon *inWeapon)
{
	UUmAssert(inWeapon);

	WPrStopChamberDelay(inWeapon);
	WPrSetMaxAmmo(inWeapon);
	inWeapon->reloadTime = inWeapon->weaponClass->reloadTime;
	inWeapon->chamber_time = 0;
	inWeapon->chamber_time_total = 0;
	inWeapon->must_fire_time = 0;

	inWeapon->flags &= ~(WPcWeaponFlag_DelayRelease);

	return;
}

static PHtSphereTree *WPiMakeSphereTree(PHtSphereTree *inData, WPtWeaponClass *inClass)
{
	/**************
	* Constructs a best-fit sphere tree for a weapon
	*/

	float best = -1e9f;
	UUtUns16 i;
	M3tPoint3D *p;
	PHtSphereTree *tree;

	for (i=0; i<inClass->geometry->pointArray->numPoints; i++)
	{
		p = inClass->geometry->pointArray->points + i;
		if ((float)fabs(p->x) > best) best = (float)fabs(p->x);
		if ((float)fabs(p->y) > best) best = (float)fabs(p->y);
		if ((float)fabs(p->z) > best) best = (float)fabs(p->z);

	}

	UUmAssert(best > 0);

	tree = PHrSphereTree_New(inData, NULL);

	tree->sphere.center.x = tree->sphere.center.y = tree->sphere.center.z = 0.0f;
	tree->sphere.radius = best/4.0f;	// This is a hack that assumes origin of weapon is roughly at
										// tail end of it (which is basically true for oblong one-handed
										// weapons). Someday this will be authored.


	return tree;
}

static void WPiWeaponPhysics_Callback_ReceiveForce(
	PHtPhysicsContext *ioContext,
	const PHtPhysicsContext *inPushingContext,
	const M3tVector3D *inForce)
{
	WPtWeapon *weapon = ioContext->callbackData;
	ONtCharacter *pushing_character;

	UUmAssert(weapon);
	UUmAssert(ioContext->callback->type == PHcCallback_Weapon);

	if ((inPushingContext->callback->type == PHcCallback_Character) && (!weapon->owner)) {
		// we're a free weapon being pushed by a character
		if ((weapon->flags & WPcWeaponFlag_HitGround) == 0) {
			// until a weapon hits the ground, don't get pushed
			return;
		}

		pushing_character = (ONtCharacter *) inPushingContext->callbackData;
		UUmAssertReadPtr(pushing_character, sizeof(ONtCharacter));

		if (pushing_character->charType == ONcChar_Player) {
			if (!WPgPlayerKicksWeapons) {
				// the player cannot kick weapons
				return;
			}
		} else if (pushing_character->charType == ONcChar_AI2) {
			if ((pushing_character->ai2State.currentGoal == AI2cGoal_Combat) &&
				(pushing_character->ai2State.currentState->state.combat.maneuver.primary_movement
						== AI2cPrimaryMovement_Gun) &&
				(pushing_character->ai2State.currentState->state.combat.maneuver.gun_target == weapon)) {
				// the AI is trying to pick us up, don't allow ourselves to be kicked
				return;
			}
		}
	}

	PHrPhysics_Accelerate(ioContext, inForce);

	return;
}

static void WPiPowerupPhysics_Callback_ReceiveForce(
	PHtPhysicsContext *ioContext,
	const PHtPhysicsContext *inPushingContext,
	const M3tVector3D *inForce)
{
	UUmAssert(ioContext->callback->type == PHcCallback_Powerup);

	if (inPushingContext->callback->type == PHcCallback_Character) {
		// powerups do not get pushed around by characters
		return;
	}

	PHrPhysics_Accelerate(ioContext, inForce);
}

// converts particle class names into pointers, and sets a flag to say so.
static void WPiFindParticleClasses(WPtWeaponClass *inClass)
{
	UUtUns16 itr;
	WPtParticleAttachment *attachment;

	if (inClass->flags & WPcWeaponClassFlag_ParticlesFound)
		return;

	for (itr = 0, attachment = inClass->attachment; itr < inClass->attachment_count; itr++, attachment++) {
		attachment->classptr = P3rGetParticleClass(attachment->classname);
	}

	inClass->flags |= WPcWeaponClassFlag_ParticlesFound;
}

// creates all particles for a gun's attachments
static void WPiNewParticles(WPtWeaponClass *inClass, WPtWeapon *ioWeapon)
{
	UUtUns32 itr;
	WPtParticleAttachment *attachment;
	P3tParticle *particle;
	P3tPrevPosition *p_prev_position;
	UUtUns32 creation_time, *p_texture, *p_lensframes;
	float float_time;
	M3tMatrix3x3 *p_orientation;
	M3tMatrix4x3 **p_dynamicmatrix;
	M3tPoint3D *p_position;
	M3tVector3D *p_velocity, *p_offset;
	AKtOctNodeIndexCache *p_envcache;
	P3tOwner *p_owner;
	UUtBool is_dead;

	// ensure that the weapon class's particle class pointers have been found
	WPiFindParticleClasses(inClass);

	creation_time = ONrGameState_GetGameTime();
	float_time = ((float) creation_time) / UUcFramesPerSecond;

	for (itr = 0, attachment = inClass->attachment; itr < inClass->attachment_count; itr++, attachment++) {
		if (attachment->classptr == NULL) {
			ioWeapon->particle[itr] = NULL;
			ioWeapon->particle_ref[itr] = P3cParticleReference_Null;
			continue;
		}

		particle = P3rCreateParticle(attachment->classptr, creation_time);
		if (particle == NULL) {
			ioWeapon->particle[itr] = NULL;
			ioWeapon->particle_ref[itr] = P3cParticleReference_Null;
			continue;
		}

		// position is the translation part of attachment->matrix
		p_position = P3rGetPositionPtr(attachment->classptr, particle, &is_dead);
		if (is_dead) {
			ioWeapon->particle[itr] = NULL;
			ioWeapon->particle_ref[itr] = P3cParticleReference_Null;
			continue;
		}

		MUrMatrix_GetCol(&attachment->matrix, 3, p_position);

		// orientation is the 3x3 matrix part of attachment->matrix
		p_orientation = P3rGetOrientationPtr(attachment->classptr, particle);
		if (p_orientation != NULL) {
			UUrMemory_MoveFast(&attachment->matrix, p_orientation, sizeof(M3tMatrix3x3));
		}

		// velocity and offset are zero if they exist
		p_velocity = P3rGetVelocityPtr(attachment->classptr, particle);
		if (p_velocity != NULL) {
			MUmVector_Set(*p_velocity, 0, 0, 0);
		}
		p_offset = P3rGetOffsetPosPtr(attachment->classptr, particle);
		if (p_offset != NULL) {
			MUmVector_Set(*p_offset, 0, 0, 0);
		}

		// dynamic matrix ensures that the particles stay attached to the weapon
		p_dynamicmatrix = P3rGetDynamicMatrixPtr(attachment->classptr, particle);
		if (p_dynamicmatrix != NULL) {
			*p_dynamicmatrix = &ioWeapon->matrix;
			particle->header.flags |= P3cParticleFlag_AlwaysUpdatePosition;
		} else {
			COrConsole_Printf("### particle class '%s' attached to weapon but no dynamic matrix", attachment->classname);
		}

		// the particle's owner is set up every time we send an event to it
		p_owner = P3rGetOwnerPtr(attachment->classptr, particle);
		if (p_owner != NULL) {
			*p_owner = 0;
		}

		// randomise texture start index
		p_texture = P3rGetTextureIndexPtr(attachment->classptr, particle);
		if (p_texture != NULL) {
			*p_texture = (UUtUns32) UUrLocalRandom();
		}

		// set texture time index to be now
		p_texture = P3rGetTextureTimePtr(attachment->classptr, particle);
		if (p_texture != NULL) {
			*p_texture = creation_time;
		}

		// no previous position
		p_prev_position = P3rGetPrevPositionPtr(attachment->classptr, particle);
		if (p_prev_position != NULL) {
			p_prev_position->time = 0;
		}

		// lensflares are not initially visible
		p_lensframes = P3rGetLensFramesPtr(attachment->classptr, particle);
		if (p_lensframes != NULL) {
			*p_lensframes = 0;
		}

		// set up the environment cache
		p_envcache = P3rGetEnvironmentCache(attachment->classptr, particle);
		if (p_envcache != NULL) {
			AKrCollision_InitializeSpatialCache(p_envcache);
		}

		// we can't have particles dying on us, because we need to keep referring to them.
		// so make sure they don't have a lifetime.
		particle->header.lifetime = 0;

		P3rSendEvent(attachment->classptr, particle, P3cEvent_Create, float_time);

		ioWeapon->particle[itr] = particle;
		ioWeapon->particle_ref[itr] = particle->header.self_ref;
	}

	WPrSendEvent(ioWeapon, (ioWeapon->flags & WPcWeaponFlag_BackgroundFX) ? P3cEvent_BackgroundStart : P3cEvent_BackgroundStop, -1);
}

// deletes all particles attached to a gun's attachments
static void WPiKillParticles(WPtWeaponClass *inClass, WPtWeapon *ioWeapon)
{
	UUtUns32 itr;
	WPtParticleAttachment *attachment;

	for (itr = 0, attachment = inClass->attachment; itr < inClass->attachment_count; itr++, attachment++) {
		if (ioWeapon->particle[itr] == NULL)
			continue;

		if (ioWeapon->particle[itr]->header.self_ref == ioWeapon->particle_ref[itr]) {
			// the particle is still alive, kill it
			UUmAssert(attachment->classptr != NULL);
			P3rKillParticle(attachment->classptr, ioWeapon->particle[itr]);
		}
		ioWeapon->particle[itr] = NULL;
		ioWeapon->particle_ref[itr] = P3cParticleReference_Null;
	}
}

static void WPiWeapon_CreatePhysics(WPtWeapon *ioWeapon)
{
	PHtPhysicsContext *physics;

	if (ioWeapon->physics) {
		goto exit;
	}

	physics = PHrPhysicsContext_Add();

	if (NULL == physics) {
		COrConsole_Printf("failed to allocate physics for weapon");
	}
	else {
		PHrPhysics_Init(physics);

		physics->level = PHcPhysics_Linear;
		physics->callbackData = ioWeapon;
		physics->flags |= PHcFlags_Physics_DontPopCharacters | PHcFlags_Physics_NoMidairFriction | PHcFlags_Physics_CharacterCollisionSkip;
		physics->callback = &gWeaponCallbacks;
		physics->staticFriction = WPcWeaponStaticFriction;
		physics->dynamicFriction = WPcWeaponDynamicFriction;

		// Set up bounding volumes
		M3rMinMaxBBox_To_BBox(&ioWeapon->weaponClass->geometry->pointArray->minmax_boundingBox, &physics->axisBox);

		physics->sphereTree = WPiMakeSphereTree(&ioWeapon->weapon_sphere_tree, ioWeapon->weaponClass);

		physics->position = MUrMatrix_GetTranslation(&ioWeapon->matrix);
		PHrPhysics_MaintainMatrix(physics);
	}

	ioWeapon->physics = physics;

exit:
	return;
}

static void WPiWeapon_ReleasePhysics(WPtWeapon *ioWeapon)
{
	if (ioWeapon->physics != NULL) {
		PHrPhysicsContext_Remove(ioWeapon->physics);
		ioWeapon->physics = NULL;
	}

	return;
}

static void WPiInit(
	WPtWeaponClass *inClass,
	WPtWeapon *ioWeapon,
	WPtPlacedWeapon inPlacedWeapon)
{
	/************
	* Initializes a new weapon for use by a character. This is
	* called only when a weapon is created
	*/

	// one time, and one time only we need to setup the callback structure
	{
		static UUtBool once = UUcTrue;

		if (once) {
			WPrSetupCallbacks();

			once = UUcFalse;
		}
	}


	if (!ioWeapon) return;
	UUrMemory_Clear(ioWeapon,sizeof(WPtWeapon));

	ioWeapon->index = ioWeapon - WPgWeapons;
	UUmAssert((ioWeapon->index >= 0) && (ioWeapon->index < WPcMaxWeapons));

	// Set up stats
	MUrMatrix_Identity(&ioWeapon->matrix);
	ioWeapon->weaponClass = inClass;
	WPrSetMaxAmmo(ioWeapon);
	ioWeapon->flags |= WPcWeaponFlag_InUse;
	ioWeapon->birthDate = ONgGameState->gameTime;
	ioWeapon->ai_alert_timer = 0;
	ioWeapon->ai_firingspread_index = (UUtUns32) -1;

	// Set up physics
	ioWeapon->physics = NULL;

	if (WPcPlacedWeapon != inPlacedWeapon) {
		WPiWeapon_CreatePhysics(ioWeapon);
		if (inPlacedWeapon == WPcCheatWeapon) {
			// does not collide with other weapons
			ioWeapon->flags |= WPcWeaponFlag_NoWeaponCollision;
		}
	}

	return;
}

// CB: called if all particles get nuked by debugging code
void
WPrRecreateParticles(void)
{
	UUtUns32 itr;
	WPtWeapon *weapon;

	for (itr = 0, weapon = WPgWeapons; itr < WPcMaxWeapons; itr++, weapon++) {
		if (weapon->flags & WPcWeaponFlag_HasParticles) {
			WPiNewParticles(WPrGetClass(weapon), weapon);
		}
	}
}

// enable a weapon's particles
static void WPiCreateParticles(WPtWeapon *ioWeapon)
{
	if (ioWeapon->flags & WPcWeaponFlag_HasParticles)
		return;

	WPiNewParticles(WPrGetClass(ioWeapon), ioWeapon);
	ioWeapon->flags |= WPcWeaponFlag_HasParticles;
}

// disable a weapon's particles
static void WPiDeleteParticles(WPtWeapon *ioWeapon)
{
	if ((ioWeapon->flags & WPcWeaponFlag_HasParticles) == 0)
		return;

	WPiKillParticles(WPrGetClass(ioWeapon), ioWeapon);
	ioWeapon->flags &= ~WPcWeaponFlag_HasParticles;
}

static WPtWeapon *WPiFindOldest(
	UUtBool inCountUsed)
{
	/****************8
	* Finds oldest powerup, ignoring in use ones if !inCountUsed
	*/

	UUtUns32 i,oldDate = UUcMaxUns32;
	WPtWeapon *weapon = NULL, *oldest=NULL;

	for (i=0; i<WPcMaxWeapons; i++)
	{
		weapon = WPgWeapons + i;

		if (!inCountUsed && (weapon->flags & WPcWeaponFlag_InUse)) { continue; }
		if (weapon->flags & WPcWeaponFlag_Untouchable) continue;

		if (weapon->birthDate < oldDate)
		{
			oldDate = weapon->birthDate;
			oldest = weapon;
		}
	}

	return oldest;
}

WPtWeaponClass*
WPrGetClass(
	const WPtWeapon			*inWeapon)
{
	UUmAssert(inWeapon);
	return inWeapon->weaponClass;
}

UUtUns16
WPrGetIndex(
	const WPtWeapon			*inWeapon)
{
	UUmAssert(inWeapon);
	return inWeapon->index;
}

M3tMatrix4x3*
WPrGetMatrix(
	WPtWeapon				*inWeapon)
{
	UUmAssert(inWeapon);
	return &inWeapon->matrix;
}

ONtCharacter*
WPrGetOwner(
	const WPtWeapon			*inWeapon)
{
	UUmAssert(inWeapon);
	return inWeapon->owner;
}

void
WPrGetPosition(
	const WPtWeapon			*inWeapon,
	M3tPoint3D				*outPosition)
{
	UUmAssert(inWeapon);
	UUmAssertWritePtr(outPosition, sizeof(M3tPoint3D));

	*outPosition = MUrMatrix_GetTranslation(&inWeapon->matrix);

	return;
}

void
WPrGetPickupPosition(
	const WPtWeapon			*inWeapon,
	M3tPoint3D				*outPosition)
{
	UUmAssert(inWeapon);
	UUmAssert(inWeapon->weaponClass);
	UUmAssertWritePtr(outPosition, sizeof(M3tPoint3D));

	MUmVector_Add(*outPosition, inWeapon->weaponClass->pickup_offset, MUrMatrix_GetTranslation(&inWeapon->matrix));

	return;
}

M3tGeometry*
WPrGetGeometry(
	const WPtWeapon			*inWeapon)
{
	UUmAssert(inWeapon);
	return inWeapon->weaponClass->geometry;
}

WPtWeapon*
WPrGetWeapon(
	UUtUns16				inIndex)
{
	UUmAssert(inIndex < WPcMaxWeapons);
	return WPgWeapons + inIndex;
}

UUtBool
WPrInUse(
	const WPtWeapon			*inWeapon)
{
	UUtBool					result = UUcFalse;

	if (inWeapon)
	{
		result = ((inWeapon->flags & WPcWeaponFlag_InUse) != 0);
	}

	return result;
}

WPtWeapon *WPrNew(WPtWeaponClass *inClass, WPtPlacedWeapon inPlacedWeapon)
{
	/*****************
	* Makes a new weapon
	*/

	UUtUns16 i;
	WPtWeapon *weapon;

	for (i = 0, weapon = WPgWeapons; i < WPcMaxWeapons; i++, weapon++)
	{
		if ((weapon->flags & WPcWeaponFlag_InUse) == 0)
			break;
	}

	if (i == WPcMaxWeapons)
	{
		// No weapons available. Blitz oldest one
		weapon = WPiFindOldest(UUcFalse);	// Try to get an unused one
		if (!weapon)
		{
			weapon = WPiFindOldest(UUcTrue);
		}
		UUmAssert(weapon);
	}

	WPiInit(inClass, weapon, inPlacedWeapon);

	return weapon;
}

WPtWeapon *WPrSpawnAtFlag(WPtWeaponClass *inClass, ONtFlag *inFlag)
{
	WPtWeapon				*weapon;
	M3tPoint3D				position;

	weapon = WPrNew(inClass, WPcNormalWeapon);
	if (weapon == NULL) {
		return weapon;
	}

	position = inFlag->location;
	position.y += 2.0f;				// don't embed weapons in floor

	WPrSetPosition(weapon, &position, inFlag->rotation);
	weapon->flags |= WPcWeaponFlag_Object;

	return weapon;
}

void WPrDelete(
	WPtWeapon *inWeapon)
{
	/*****************
	* Deletes a weapon
	*/

	UUtUns16 itr;
	P3tParticleClass *this_class;

	if ((inWeapon->flags & WPcWeaponFlag_InUse) == 0) { return; }

	// shut down the weapon
	WPrReleaseTrigger(inWeapon, UUcTrue);
	WPrStopChamberDelay(inWeapon);
	WPrStopBackgroundEffects(inWeapon);

	for (itr = 0; itr < inWeapon->weaponClass->attachment_count; itr++) {
		if (inWeapon->particle[itr]) {
			this_class = &P3gClassTable[inWeapon->particle[itr]->header.class_index];
			if (inWeapon->particle[itr]->header.self_ref == inWeapon->particle_ref[itr]) {
				// this particle is still around, kill it
				P3rKillParticle(this_class, inWeapon->particle[itr]);
			}
		}
	}

	// release the physics context
	if (inWeapon->physics) {
		PHrPhysicsContext_Remove(inWeapon->physics);
		inWeapon->physics = NULL;
	}

	// tell the AI that we've stopped firing
	if (inWeapon->ai_firingspread_index != (UUtUns32) -1) {
		AI2rKnowledge_DeleteDodgeFiringSpread(inWeapon->ai_firingspread_index);
		inWeapon->ai_firingspread_index = (UUtUns32) -1;
	}

	inWeapon->flags &=~WPcWeaponFlag_InUse;
}

// update a powerup for a game tick
static void WPiPowerup_Update(WPtPowerup *ioPowerup)
{
	UUmAssertReadPtr(ioPowerup, sizeof(WPtPowerup));

	if (ioPowerup->physics != NULL) {
		ioPowerup->position = ioPowerup->physics->sphereTree->sphere.center = ioPowerup->physics->position;

		// CB: this is a god-awful hack but necessary to stop powerups from being flung into the stratosphere
		// by the physics system. all hail the glory of the physics system   - september 26, 2000
		ioPowerup->physics->velocity.y = UUmMin(1.5f * WPcItemLaunchSpeed, ioPowerup->physics->velocity.y);

		if ((ioPowerup->flags & WPcPowerupFlag_OnGround) ||
			(MUmVector_GetLengthSquared(ioPowerup->physics->velocity) < UUmSQR(5.0f / UUcFramesPerSecond))) {
			// the powerup is stationary, or sliding along the ground
			ioPowerup->physicsRemoveTime++;

			if (ioPowerup->physicsRemoveTime >= WPcPowerupPhysicsTimer) {
				// delete the powerup's physics context, it has come to rest
				WPrPowerup_RemovePhysics(ioPowerup);
				ioPowerup->flags &= ~WPcPowerupFlag_Moving;
			}
		} else {
			// the powerup is moving
			ioPowerup->physicsRemoveTime = 0;
		}
	}


	if ((ioPowerup->flags & WPcPowerupFlag_Moving) == 0) {
		if (!WPgDisableFade) {
			if (ioPowerup->fadeTime > 0) {

				// Track fade-out
				ioPowerup->fadeTime--;
				if (0 == ioPowerup->fadeTime) {
					WPrPowerup_Delete(ioPowerup);
				}
			}

			if (ioPowerup->freeTime > 0) {

				// Track free time
				ioPowerup->freeTime--;
				if (0 == ioPowerup->freeTime) {
					ioPowerup->fadeTime = WPcFadeTime;
				}
			}
		}
	}
}

// update all the powerups for a game tick
void WPrPowerup_Update(void)
{
	UUtUns32 i;
	WPtPowerup *powerup;

	for (i = 0, powerup = WPgPowerups; i < WPgNumPowerups; i++, powerup++)
	{
		WPiPowerup_Update(powerup);
	}
}

static void WPiUpdate(WPtWeapon *ioWeapon)
{
	UUtUns16 itr;

	// apply deferred changes from this frame
	if (ioWeapon->flags & WPcWeaponFlag_DeferUpdate_StopFiring) {
		WPrReleaseTrigger(ioWeapon, UUcTrue);
		ioWeapon->flags &= ~WPcWeaponFlag_DeferUpdate_StopFiring;
	}
	if (ioWeapon->flags & WPcWeaponFlag_DeferUpdate_GoInactive) {
		WPrStopChamberDelay(ioWeapon);
		WPrStopBackgroundEffects(ioWeapon);
		ioWeapon->flags &= ~WPcWeaponFlag_DeferUpdate_GoInactive;
	}

	if (ioWeapon->owner)
	{
		if (ioWeapon->owner->inventory.weapons[0] == ioWeapon) {
			M3tMatrix4x3 *matrixptr;

			// Weapon being carried
			matrixptr = ONrCharacter_GetMatrix(ioWeapon->owner, ONcWeapon_Index);
			if (matrixptr == NULL) {
				MUrMatrix_SetTranslation(&ioWeapon->matrix, &ioWeapon->owner->actual_position);
				ioWeapon->matrix.m[3][1] += ioWeapon->owner->heightThisFrame;
			}
			else {
				ioWeapon->matrix = *matrixptr;
			}
			UUmAssert(ioWeapon->matrix.m[0][0] || ioWeapon->matrix.m[0][1] || ioWeapon->matrix.m[0][2]);

			if (ioWeapon->physics != NULL) {
				ioWeapon->physics->position = MUrMatrix_GetTranslation(&ioWeapon->matrix);
			}
		}

		// carried weapons must have particles
		// note: these are not created when the weapon is created because we need the matrix to be
		// set up first
		WPiCreateParticles(ioWeapon);

		// Track must-fire time
		if (ioWeapon->must_fire_time > 0) {
			ioWeapon->must_fire_time--;

			if ((ioWeapon->must_fire_time > 0) && (ioWeapon->weaponClass->flags & WPcWeaponClassFlag_Autofire)) {
				// we are doing an involuntary firing period and we're an autofire weapon - keep pressing the trigger
				WPrDepressTrigger(ioWeapon, UUcTrue, UUcFalse, NULL);
			}
		}

		if (ioWeapon->flags & WPcWeaponFlag_DelayRelease) {
			// check to see if it's safe to release the trigger yet
			WPrReleaseTrigger(ioWeapon, UUcFalse);
		}

		// Track reload time
		if (ioWeapon->reloadTime)
			ioWeapon->reloadTime--;

		// Track AI alert delay timer
		if (ioWeapon->ai_alert_timer)
			ioWeapon->ai_alert_timer--;

		// Track weapon chamber time
		if (ioWeapon->chamber_time) {
			ioWeapon->chamber_time--;

			if (ioWeapon->chamber_time == 0) {
				if (ioWeapon->flags & WPcWeaponFlag_IsFiring) {
					if (ioWeapon->weaponClass->flags & WPcWeaponClassFlag_RestartEachShot) {
						// force a release of the trigger
						WPrReleaseTrigger(ioWeapon, UUcFalse);
					}
					if (ioWeapon->weaponClass->flags & WPcWeaponClassFlag_UseAmmoContinuously) {
						// find the shooter that is firing
						for (itr = 0; itr < ioWeapon->weaponClass->attachment_count; itr++) {
							if ((ioWeapon->attachments_started & (1 << itr)) == 0)
								continue;

							if (ioWeapon->weaponClass->attachment[itr].ammo_used == 0)
								continue;

							if (ioWeapon->current_ammo < ioWeapon->weaponClass->attachment[itr].ammo_used) {
								// we are out of ammo, stop firing
								WPrReleaseTrigger(ioWeapon, UUcTrue);
							} else {
								// use some ammo and keep firing
								ioWeapon->current_ammo -= ioWeapon->weaponClass->attachment[itr].ammo_used;
								if (ioWeapon->weaponClass->flags & WPcWeaponClassFlag_Autofire) {
									// autofire weapons receive another pulse event here
									WPiPulseShooter(ioWeapon, ioWeapon->weaponClass->attachment[itr].shooter_index,
													&ioWeapon->weaponClass->attachment[itr]);
								} else {
									// just increment the chamber time, don't pulse again
									if (WPgGatlingCheat && (ioWeapon->owner != NULL) && (ioWeapon->owner->charType == ONcChar_Player)) {
										ioWeapon->chamber_time = ioWeapon->weaponClass->attachment[itr].cheat_chamber_time;
									} else {
										ioWeapon->chamber_time = ioWeapon->weaponClass->attachment[itr].chamber_time;
									}
								}
							}
							break;
						}
					}
				}

				WPrStopChamberDelay(ioWeapon);
			}
		}

		// Track weapon reload-delay time
		if (ioWeapon->reload_delay_time > 0) {
			ioWeapon->reload_delay_time--;
		}

		// Track start delay time
		if (ioWeapon->flags & WPcWeaponFlag_IsFiring) {
			for (itr = 0; itr < ioWeapon->weaponClass->attachment_count; itr++) {
				if (ioWeapon->start_delay[itr] == 0)
					continue;

				ioWeapon->start_delay[itr]--;
				if (ioWeapon->start_delay[itr] == 0) {
					WPiDoEvent(ioWeapon, P3cEvent_Start, itr);
				}
			}
		}

	}
	else {
		UUmAssert((ioWeapon->flags & WPcWeaponFlag_IsFiring) == 0);

		// weapons falling or on the ground must not have particles
		WPiDeleteParticles(ioWeapon);

		if (ioWeapon->physics != NULL) {
			ioWeapon->physics->sphereTree->sphere.center = ioWeapon->physics->position;

			// CB: this is a god-awful hack but necessary to stop weapons from being flung into the stratosphere
			// by the physics system. all hail the glory of the physics system   - september 26, 2000
			ioWeapon->physics->velocity.y = UUmMin(1.5f * WPcItemLaunchSpeed, ioWeapon->physics->velocity.y);
		}

		if (!WPgDisableFade) {
			// Track free time
			if (ioWeapon->freeTime) {
				ioWeapon->freeTime--;

				if (0 == ioWeapon->freeTime) {
					ioWeapon->fadeTime = WPcFadeTime;
				}
			}

			// Track fade time
			if (ioWeapon->fadeTime)
			{
				ioWeapon->fadeTime--;

				if (0 == ioWeapon->fadeTime) {
					WPrDelete(ioWeapon);
				}
			}
		}
	}

	// unowned weapon w/ matricies get their matrix updated
	if ((ioWeapon->physics != NULL) && (NULL == ioWeapon->owner)) {
		ioWeapon->matrix = ioWeapon->physics->matrix;
	}

	return;
}

void WPrUpdate(void)
{
	/**********************
	* Updates all the weapons for a frame
	*/

	UUtUns32 i;
	WPtWeapon *weapon;

	for (i = 0, weapon = WPgWeapons; i < WPcMaxWeapons; i++, weapon++)
	{
		if ((weapon->flags & WPcWeaponFlag_InUse) == 0)
			continue;

		WPiUpdate(weapon);
	}
}

void WPrDisplay(
	void)
{
	/************************
	* Displays all the weapons
	*/

	UUtUns32 i;
	WPtWeapon *weapon;
	WPtWeaponClass *weaponClass;
	float alpha_f, glow_xscale, glow_yscale;
	M3tPoint3D glow_center;
	UUtUns32 alpha_i;
	UUtBool is_visible;
	M3tMatrix4x3 weapon_matrix;
	M3tBoundingBox_MinMax bbox;

	M3rGeom_State_Push();

	// powerup glow sprite alpha is unsorted
	M3rGeom_State_Set(M3cGeomStateIntType_SubmitMode, M3cGeomState_SubmitMode_Normal);
	M3rGeom_State_Set(M3cGeomStateIntType_SpriteMode, M3cGeomState_SpriteMode_Flat);
	M3rGeom_State_Commit();

	for (i=0; i<WPcMaxWeapons; i++)
	{
		weapon = WPgWeapons + i;
		weaponClass = weapon->weaponClass;

		if (!(weapon->flags & WPcWeaponFlag_InUse)) continue;

		UUmAssert(weaponClass->geometry);

		// Note: owned weapons are displayed as an extra body part
		if (!weapon->owner)
		{
			alpha_i = 255;
			if (weapon->fadeTime) {
				alpha_f = (float)weapon->fadeTime / (float)WPcFadeTime;
				alpha_f = UUmPin(alpha_f,0.0f,1.0f);
				alpha_i = (UUtUns32)(255.0f * alpha_f);
			}

			if (NULL == weapon->physics) {
				weapon_matrix = weapon->matrix;
			}
			else {
				weapon_matrix = weapon->physics->matrix;
			}

			MUrMatrix3x3_RotateY((M3tMatrix3x3 *) &weapon_matrix, weapon->rotation);
			weapon_matrix.m[3][1] += weaponClass->display_yoffset;

			M3rGeometry_GetBBox(weaponClass->geometry, &weapon_matrix, &bbox);

			{
				// S.S. speed up visibility determination for stationary weapons
				//is_visible = AKrEnvironment_IsBoundingBoxMinMaxVisible(&bbox);
				M3tPoint3D center;
				static const float tolerance= 0.001f;
				float dx, dy, dz;

				center.x= (bbox.minPoint.x + bbox.maxPoint.x) * 0.5f;
				center.y= (bbox.minPoint.y + bbox.maxPoint.y) * 0.5f;
				center.z= (bbox.minPoint.z + bbox.maxPoint.z) * 0.5f;
				dx= weapon->last_position.x - center.x;
				dy= weapon->last_position.y - center.y;
				dz= weapon->last_position.z - center.z;
				dx*= dx;
				dy*= dy;
				dz*= dz;

				if ((dx>tolerance) || (dy>tolerance) || (dz>tolerance) || (weapon->oct_tree_node_index[0] == 0)) {
					AKrEnvironment_NodeList_Get(&bbox, WPcWeaponNodeCount, weapon->oct_tree_node_index, 0);
					weapon->last_position= center;
				}

				is_visible= AKrEnvironment_NodeList_Visible(weapon->oct_tree_node_index);
			}

			if (is_visible) {
				M3rGeom_State_Push();

				if (weaponClass->glow_texture != NULL) {
					// calculate the size and position of the weapon's glow texture
					bbox = weaponClass->geometry->pointArray->minmax_boundingBox;
					glow_xscale = WPcGlowTextureScale * weaponClass->glow_scale[0] * (bbox.maxPoint.x - bbox.minPoint.x);
					glow_yscale = WPcGlowTextureScale * weaponClass->glow_scale[1] * (bbox.maxPoint.z - bbox.minPoint.z);

					glow_center.x = (bbox.maxPoint.x + bbox.minPoint.x) / 2;
					glow_center.y = bbox.minPoint.y + 0.1f;
					glow_center.z = (bbox.maxPoint.z + bbox.minPoint.z) / 2;
					MUrMatrix_MultiplyPoint(&glow_center, &weapon_matrix, &glow_center);

					// draw the glow texture as a sprite
					M3rDraw_State_SetInt(M3cDrawStateIntType_ZWrite, M3cDrawState_ZWrite_Off);
					M3rDraw_State_Commit();

					M3rSprite_Draw(weaponClass->glow_texture, &glow_center, glow_xscale, glow_yscale, IMcShade_White,
									(UUtUns16) (((alpha_i * M3cMaxAlpha) / 3) >> 8), 0, NULL, (M3tMatrix3x3 *) &weapon_matrix, 0, 0, 0);

					if ((weaponClass->loaded_glow_texture != NULL) && (weapon->current_ammo > 0)) {
						M3rSprite_Draw(weaponClass->loaded_glow_texture, &glow_center, glow_xscale, glow_yscale, IMcShade_White,
										(UUtUns16) (((alpha_i * M3cMaxAlpha) / 3) >> 8), 0, NULL, (M3tMatrix3x3 *) &weapon_matrix, 0, 0, 0);
					}

					M3rDraw_State_SetInt(M3cDrawStateIntType_ZWrite, M3cDrawState_ZWrite_On);
					M3rDraw_State_Commit();
				}

				if (alpha_i < 255) {
					// S.S.
					M3rDraw_State_SetInt(M3cDrawStateIntType_FrameBufferBlendWithConstantAlpha, UUcTrue);
					M3rDraw_State_SetInt(M3cDrawStateIntType_ZWrite, M3cDrawState_ZWrite_Off); // S.S.
				}

				M3rGeom_State_Set(M3cGeomStateIntType_Alpha, alpha_i);
				M3rGeom_State_Commit();

				M3rGeometry_MultiplyAndDraw(weaponClass->geometry, &weapon_matrix);

				M3rGeom_State_Pop();
			}
		}
	}

	M3rGeom_State_Pop();

	return;
}

void WPrSetAmmo(WPtWeapon *ioWeapon, UUtUns32 inAmmo)
{
	ioWeapon->current_ammo = (UUtUns16) UUmPin(inAmmo, 0, ioWeapon->weaponClass->max_ammo);

	if ((ioWeapon->owner != NULL) && (ioWeapon->flags & WPcWeaponFlag_InHand)) {
		if (ioWeapon->current_ammo > 0) {
			WPrStartBackgroundEffects(ioWeapon);
		} else {
			WPrStopBackgroundEffects(ioWeapon);
		}
	}
	return;
}

void
WPrSetAmmoPercentage(
	WPtWeapon			*ioWeapon,
	UUtUns16			inAmmoPercentage)
{
	UUtUns16 new_ammo;

	UUmAssert(ioWeapon);
	UUmAssert(ioWeapon->weaponClass);

	new_ammo = inAmmoPercentage * ioWeapon->weaponClass->max_ammo / 100;
	WPrSetAmmo(ioWeapon, new_ammo);
}

void
WPrSetFlags(
	WPtWeapon			*ioWeapon,
	UUtUns16			inFlags)
{
	UUmAssert(ioWeapon);
	ioWeapon->flags = inFlags;
}

void
WPrSetPosition(
	WPtWeapon			*ioWeapon,
	const M3tPoint3D	*inPosition,
	float				inRotation)
{
	UUmAssert(ioWeapon);

	if (NULL == ioWeapon->physics) {
		ioWeapon->matrix = MUgIdentityMatrix;
		MUrMatrix_SetTranslation(&ioWeapon->matrix, inPosition);
	}
	else {
		ioWeapon->physics->position = *inPosition;
	}

	ioWeapon->rotation = inRotation;

	return;
}

#if defined(__linux__) && UUmSDL
// Visual-only recent muzzle flashes; no gameplay randomness or asset changes.
typedef struct OniMuzzleFlash { M3tPoint3D position; UUtUns32 tick; } OniMuzzleFlash;
static OniMuzzleFlash oniMuzzleFlashes[ONI_MAX_EXPERIMENTAL_LIGHTS];
static UUtUns32 oniMuzzleCount=0, oniMuzzleNext=0, oniMuzzleLastTick=0;
static UUtInt32 oniMuzzleLevel=-1;
static UUtBool ONiExperimentalLights_Enabled(void) {
    static int enabled=-1;
    if(enabled<0){const char *v=getenv("ONI_WORLD_LIGHT");enabled=(v && !strcmp(v,"1"));}
    return (UUtBool)enabled;
}
static UUtBool ONiExperimentalLights_HitEnabled(void) {
    static int enabled=-1;
    if(enabled<0){const char *v=getenv("ONI_HIT_LIGHT");enabled=(v && !strcmp(v,"1"));}
    return (UUtBool)enabled;
}
float ONrExperimentalLights_AnimationTime(void) { return ONgGameState ? (float)ONrGameState_GetGameTime()/60.f : 0.f; }
UUtBool ONrExperimentalLights_WeaponEnabled(void) { return ONiExperimentalLights_Enabled() || ONiExperimentalLights_HitEnabled(); }
static UUtBool oniExperimentalCorrectDirection;
UUtBool ONrExperimentalLights_CorrectDirection(void) { return oniExperimentalCorrectDirection; }
void ONrExperimentalLights_SetCorrectDirection(UUtBool enabled) { oniExperimentalCorrectDirection=enabled; }
// This experiment uses surviving object lights, not guessed emissive textures.
#define ONI_MAX_WAREHOUSE_FIXTURES 2048
static OniExperimentalLight oniWarehouseFixtures[ONI_MAX_WAREHOUSE_FIXTURES];
static UUtUns32 oniWarehouseCount, oniWarehouseObjects;
static UUtBool oniWarehouseScanned;
#define ONI_MAX_INDICATOR_LIGHTS 256
static OniExperimentalLight oniIndicatorLights[ONI_MAX_INDICATOR_LIGHTS];
static UUtUns32 oniIndicatorCount, oniIndicatorTick;
static UUtBool oniIndicatorTickValid, oniIndicatorLogged;
#define ONI_MAX_EXTRA_CORONAS 512
#define ONI_CORONA_CELL_CACHE 1024
typedef struct OniCoronaCell { UUtBool valid; UUtUns32 tick,count,indices[6]; int x,y,z; } OniCoronaCell;
static OniCoronaCell oniCoronaCells[ONI_CORONA_CELL_CACHE];
static OniExperimentalLight oniExtraCoronas[ONI_MAX_EXTRA_CORONAS];
static UUtBool oniExtraVehicle[ONI_MAX_EXTRA_CORONAS];
static UUtUns32 oniExtraCoronaCount;
static UUtBool oniExtraCoronaLogged;
#define ONI_MAX_FIRE_LIGHTS 128
static OniExperimentalLight oniFireLights[ONI_MAX_FIRE_LIGHTS];
static UUtUns32 oniFireCount;
static UUtBool oniFireLogged;
#define ONI_MAX_PROJECTILE_LIGHTS 128
static OniExperimentalLight oniProjectileLights[ONI_MAX_PROJECTILE_LIGHTS];
static UUtUns32 oniProjectileCount, oniProjectileTick;
static UUtBool oniProjectileTickValid, oniProjectileLogged;
#define ONI_MAX_HIT_LIGHTS 32
static OniExperimentalLight oniHitLights[ONI_MAX_HIT_LIGHTS];
static UUtUns32 oniHitCount,oniHitTick;
static UUtBool oniHitTickValid,oniHitLogged;

#define ONI_LIGHT_CELL_CACHE 2048
typedef struct OniLightCell {
    UUtBool valid;
    int x, y, z;
    UUtUns32 count, indices[ONI_MAX_EXPERIMENTAL_LIGHTS];
} OniLightCell;
static OniLightCell oniLightCells[ONI_LIGHT_CELL_CACHE];
#define ONI_OCCLUSION_CACHE_SIZE 32768
typedef struct OniOcclusionEntry {
    UUtBool valid, blocked, vehicle;
    UUtInt32 source[3], target[3];
    UUtUns32 tick;
} OniOcclusionEntry;
static OniOcclusionEntry oniOcclusionCache[ONI_OCCLUSION_CACHE_SIZE];
UUtBool ONrExperimentalLights_SceneEnabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        const char *v=getenv("ONI_RELIGHT"), *legacy=getenv("ONI_WAREHOUSE_LIGHT");
        enabled=(v && !strcmp(v,"1")) || (legacy && !strcmp(legacy,"1"));
    }
    return enabled && ONgGameState && ONgGameState->levelNumber > 0;
}
static UUtBool ONiWarehouseFixture(OBJtObject *object, uintptr_t unused) {
    OBJtOSD_All osd;
    OBJtLSData *data = NULL;
    UUtUns32 count = 0;
    (void)unused;
    ++oniWarehouseObjects;
    OBJrObject_GetObjectSpecificData(object, &osd);
    if (object->object_type == OBJcType_Furniture) {
        data = osd.osd.furniture_osd.ls_data_array; count = osd.osd.furniture_osd.num_ls_datas;
    } else if (object->object_type == OBJcType_Console) {
        data = osd.osd.console_osd.ls_data_array; count = osd.osd.console_osd.num_ls_datas;
    }
    for (UUtUns32 i = 0; data && i < count && oniWarehouseCount < ONI_MAX_WAREHOUSE_FIXTURES; ++i) {
        if (!(data[i].light_flags & OBJcLightFlag_HasLight)) continue;
        OniExperimentalLight *light = &oniWarehouseFixtures[oniWarehouseCount++];
        light->spinning=UUcFalse;
        light->position = object->position;
        light->radius = 190.f;
        light->intensity = 0.45f; /* Original intensity units require calibration. */
        light->red = data[i].filter_color[0]; light->green = data[i].filter_color[1]; light->blue = data[i].filter_color[2];
        if (light->red <= 0 && light->green <= 0 && light->blue <= 0)
            light->red = light->green = light->blue = 1.0f;
    }
    return UUcTrue;
}
// Indicator coronas are environmental particles, not coloured wall pixels.
// Read their current tint and actual world position; scripts may change or
// destroy them at any point. Never retain particle pointers between updates.
static void ONiWarehouseCorona(EPtEnvParticle *env, uintptr_t unused) {
    (void)unused;
    if (oniIndicatorCount>=ONI_MAX_INDICATOR_LIGHTS ||
        (strcmp(env->classname,"locklight") && strcmp(env->classname,"locklight2")) ||
        !env->particle || !env->particle_class ||
        env->particle->header.self_ref!=env->particle_self_ref ||
        (env->particle->header.flags & (P3cParticleFlag_Hidden|P3cParticleFlag_Deleted))) return;
    IMtShade *tint=P3rGetTintPtr(env->particle_class,env->particle);
    if (!tint) return;
    float r=((*tint>>16)&255)/255.f,g=((*tint>>8)&255)/255.f,b=(*tint&255)/255.f;
    if (r<0.02f && g<0.02f && b<0.02f) return;
    M3tPoint3D position;
    if (P3rGetRealPosition(env->particle_class,env->particle,&position)) return;
    OniExperimentalLight *light=&oniIndicatorLights[oniIndicatorCount++];
    light->spinning=UUcFalse;
    light->position=position;light->radius=40.f;light->intensity=0.45f;
    light->red=r;light->green=g;light->blue=b;
}
// An authored fire emitter can remain present while scripts disable all its
// emitters. Only illuminate while at least one flame emitter is actually on.
// Flicker is deterministic visual animation and never consumes gameplay RNG.
static void ONiSceneFire(EPtEnvParticle *env, uintptr_t data) {
    UUtUns32 now=(UUtUns32)data;
    if(oniFireCount>=ONI_MAX_FIRE_LIGHTS || !env->particle || !env->particle_class ||
       env->particle->header.self_ref!=env->particle_self_ref ||
       (env->particle->header.flags & P3cParticleFlag_Deleted)) return;
    if ((strncmp(env->classname,"env_fire",8) || !strstr(env->classname,"_e01") || strstr(env->classname,"fireball")) &&
        strcmp(env->classname,"flame1_e") && strcmp(env->classname,"flame2_e")) return;
    P3tParticleDefinition *def=env->particle_class->definition;
    if(!def || !def->num_emitters) return;
    P3tParticleEmitterData *emitters=(P3tParticleEmitterData *)env->particle->vararray;
    UUtBool active=UUcFalse;
    for(UUtUns32 i=0;i<def->num_emitters;++i) {
        if(emitters[i].num_emitted!=P3cEmitterDisabled &&
           (strstr(def->emitter[i].classname,"_p01") || !strncmp(def->emitter[i].classname,"flame",5))) {active=UUcTrue;break;}
    }
    if(!active)return;
    M3tPoint3D position;if(P3rGetRealPosition(env->particle_class,env->particle,&position))return;
    for(UUtUns32 i=0;i<oniFireCount;++i) {
        float x=position.x-oniFireLights[i].position.x,y=position.y-oniFireLights[i].position.y,z=position.z-oniFireLights[i].position.z;
        if(x*x+y*y+z*z<256.f)return;
    }
    float phase=position.x*0.013f+position.y*0.017f+position.z*0.019f;
    float time=(float)now/60.f;
    float flicker=0.82f+0.11f*sinf(time*6.3f+phase)+0.07f*sinf(time*10.7f+phase*1.7f);
    OniExperimentalLight *light=&oniFireLights[oniFireCount++];
    light->spinning=UUcFalse;
    light->position=position;light->radius=210.f;light->intensity=2.8f*flicker;
    light->red=1.f;light->green=0.37f;light->blue=0.08f;
}
// Some vehicle flares are persistent particles spawned once by an emitter.
// After that one emission the emitter becomes disabled even though its flare
// remains visible. Check the emitted particles themselves before deciding
// that a vehicle light is off. Match both class and position, since other
// vehicles can have instances of the same flare class elsewhere in the level.
static UUtBool ONiSceneHasLiveFlare(const P3tParticleDefinition *def, const M3tPoint3D *origin) {
    for(UUtUns32 i=0;i<def->num_emitters;++i) {
        P3tParticleClass *cls=def->emitter[i].emittedclass;
        if(!cls)continue;
        for(P3tParticle *p=cls->headptr;p;p=p->header.nextptr) {
            if(p->header.flags & (P3cParticleFlag_Hidden|P3cParticleFlag_Deleted))continue;
            M3tPoint3D pos;if(P3rGetRealPosition(cls,p,&pos))continue;
            float x=pos.x-origin->x,y=pos.y-origin->y,z=pos.z-origin->z;
            if(x*x+y*y+z*z<4.f)return UUcTrue;
        }
    }
    return UUcFalse;
}
// Track actual live headlight and emergency-light coronas, plus coronas
// attached to moving objects. Never infer an active light from a car model.
static void ONiSceneExtraCorona(EPtEnvParticle *env) {
    UUtBool headlight=!strncmp(env->classname,"headlight",9);
    UUtBool indicator=!strcmp(env->classname,"emlight") || !strcmp(env->classname,"emlight2");
    UUtBool beacon=!strncmp(env->classname,"env_emlite_e",12);
    UUtBool emergencyCorona=!strncmp(env->classname,"env_emlite_p",12);
    UUtBool moving=!strncmp(env->classname,"env_lensflare",13) &&
        (env->dynamic_matrix || env->owner_type==EPcParticleOwner_Object || env->owner_type==EPcParticleOwner_Furniture);
    if((!headlight && !indicator && !beacon && !emergencyCorona && !moving) || oniExtraCoronaCount>=ONI_MAX_EXTRA_CORONAS ||
       !env->particle || !env->particle_class || !env->particle_class->definition ||
       env->particle->header.self_ref!=env->particle_self_ref ||
       (env->particle->header.flags & P3cParticleFlag_Deleted) ||
       ((env->particle->header.flags & P3cParticleFlag_Hidden) && !headlight))return;
    P3tParticleClass *cls=env->particle_class;P3tParticle *particle=env->particle;
    P3tParticleDefinition *def=cls->definition;IMtShade tint=0;
    IMtShade *dynamic=P3rGetTintPtr(cls,particle);
    if(dynamic)tint=*dynamic;
    else if(def->appearance.tint.type==P3cValueType_Shade)tint=def->appearance.tint.u.shade_const.val;
    M3tPoint3D position;if(P3rGetRealPosition(cls,particle,&position))return;
    // Some headlights use an invisible parent to spawn the visible flare.
    if(headlight && (particle->header.flags & P3cParticleFlag_Hidden) &&
       !ONiSceneHasLiveFlare(def,&position))return;
    if(beacon && def->num_emitters) {
        P3tParticleEmitterData *data=(P3tParticleEmitterData *)particle->vararray;
        UUtBool active=UUcFalse;
        for(UUtUns32 i=0;i<def->num_emitters;++i) {
            if(data[i].num_emitted==P3cEmitterDisabled)continue;
            active=UUcTrue;
            P3tParticleClass *emitted=def->emitter[i].emittedclass;
            if(emitted && emitted->definition && emitted->definition->appearance.tint.type==P3cValueType_Shade)
                tint=emitted->definition->appearance.tint.u.shade_const.val;
            break;
        }
        if(!active && !ONiSceneHasLiveFlare(def,&position))return;
    }
    float r=((tint>>16)&255)/255.f,g=((tint>>8)&255)/255.f,b=(tint&255)/255.f;
    if(r<0.02f && g<0.02f && b<0.02f)return;
    oniExtraVehicle[oniExtraCoronaCount]=headlight || beacon || emergencyCorona;
    OniExperimentalLight *light=&oniExtraCoronas[oniExtraCoronaCount++];
    light->position=position;light->radius=headlight?200.f:165.f;
    light->intensity=headlight?0.9f:(beacon?0.55f:0.5f);
    light->red=r;light->green=g;light->blue=b;
    /* e01 is the roof beacon. Its authored effect combines orange and red
       flares; use opposing beams rather than a single stationary red lamp. */
    light->spinning=beacon && !strncmp(env->classname,"env_emlite_e01",14);
    if(light->spinning) {
        light->intensity=1.6f;
        light->red=1.f;light->green=0.10f;light->blue=0.10f;
        if(oniExtraCoronaCount<ONI_MAX_EXTRA_CORONAS) {
            oniExtraVehicle[oniExtraCoronaCount]=UUcTrue;
            OniExperimentalLight *orange=&oniExtraCoronas[oniExtraCoronaCount++];
            *orange=*light;orange->red=1.f;orange->green=0.42f;orange->blue=0.04f;
        }
    }
}
static void ONiSceneDynamicParticle(EPtEnvParticle *env, uintptr_t now) {
    ONiWarehouseCorona(env,0);
    ONiSceneFire(env,now);
    ONiSceneExtraCorona(env);
}
static void ONiWarehouseRefreshCoronas(UUtUns32 now) {
    if (!ONrExperimentalLights_SceneEnabled() || (oniIndicatorTickValid && oniIndicatorTick==now)) return;
    oniIndicatorTick=now;oniIndicatorTickValid=UUcTrue;oniIndicatorCount=0;oniFireCount=0;oniExtraCoronaCount=0;
    EPrEnumerateAllParticles(ONiSceneDynamicParticle,(uintptr_t)now);
    if(oniExtraCoronaCount && !oniExtraCoronaLogged) {
        UUtUns32 spinning=0;for(UUtUns32 i=0;i<oniExtraCoronaCount;++i)if(oniExtraCoronas[i].spinning)++spinning;
        UUrStartupMessage("[scene-light] active extra coronas: %u (%u sweeping beacons)",(unsigned)oniExtraCoronaCount,(unsigned)spinning);
        oniExtraCoronaLogged=UUcTrue;
    }
    if(oniFireCount && !oniFireLogged) {
        UUrStartupMessage("[scene-light] active fire emitters: %u",(unsigned)oniFireCount);
        oniFireLogged=UUcTrue;
    }
    if (oniIndicatorCount && !oniIndicatorLogged) {
        UUtUns32 red=0,green=0;
        for(UUtUns32 i=0;i<oniIndicatorCount;++i) {
            if(oniIndicatorLights[i].red>oniIndicatorLights[i].green)++red;
            else if(oniIndicatorLights[i].green>oniIndicatorLights[i].red)++green;
        }
        UUrStartupMessage("[scene-light] active locklight coronas: %u (%u red, %u green)",
            (unsigned)oniIndicatorCount,(unsigned)red,(unsigned)green);
        oniIndicatorLogged=UUcTrue;
    }
}
// Known fixture materials are proxies for authored lights; the original
// retail levels do not necessarily retain their light records.
static UUtUns32 ONiWarehouseSurfaceLights(void) {
    AKtEnvironment *env = ONgGameState->level ? ONgGameState->level->environment : NULL;
    UUtUns32 added = 0;
    if (!env || !env->gqGeneralArray || !env->gqRenderArray || !env->textureMapArray || !env->pointArray) return 0;
    for (UUtUns32 i=0; i<env->gqGeneralArray->numGQs && i<env->gqRenderArray->numGQs; ++i) {
        UUtUns32 tex=env->gqRenderArray->gqRender[i].textureMapIndex;
        if (tex>=env->textureMapArray->numMaps) continue;
        M3tTextureMap *map=env->textureMapArray->maps[tex];
        if (!map) continue;
        static const char *fixtureNames[]={"WH_LIGHT_FACE","WH_LIGHTFACERED","WH_INTBLUELITE",
            "WH_INTLIGHTGRID","WH_TRLIGHTGRID","TR_LAMP","TC_ELIGHT","MC_MOUNT_LITE1",
            "MC_CEILING_LIGHT","LIGHT_CIEL01","SB_STREETLIGHT","AIR_BAYLIGHT2",
            "MPC_LIGHT_FACE","MPC_INTERIOR_WALL_LIGHT"};
        UUtBool known=UUcFalse;
        for(UUtUns32 t=0;t<sizeof(fixtureNames)/sizeof(fixtureNames[0]);++t)
            if(!strcmp(map->debugName,fixtureNames[t])) {known=UUcTrue;break;}
        if(!known)continue;
        AKtGQ_General *gq=&env->gqGeneralArray->gqGeneral[i];
        UUtUns32 n=(gq->flags & AKcGQ_Flag_Triangle)?3:4;
        M3tPoint3D center={0,0,0}; UUtBool valid=UUcTrue;
        for (UUtUns32 j=0;j<n;++j) {
            UUtUns32 idx=gq->m3Quad.vertexIndices.indices[j];
            if (idx>=env->pointArray->numPoints) {valid=UUcFalse;break;}
            M3tPoint3D *v=&env->pointArray->points[idx];
            center.x+=v->x/n;center.y+=v->y/n;center.z+=v->z/n;
        }
        if (!valid) continue;
        UUtBool duplicate=UUcFalse;
        for (UUtUns32 j=0;j<oniWarehouseCount;++j) {
            float x=center.x-oniWarehouseFixtures[j].position.x, y=center.y-oniWarehouseFixtures[j].position.y, z=center.z-oniWarehouseFixtures[j].position.z;
            if (x*x+y*y+z*z<225.0f) {duplicate=UUcTrue;break;}
        }
        if (duplicate || oniWarehouseCount>=ONI_MAX_WAREHOUSE_FIXTURES) continue;
        OniExperimentalLight *light=&oniWarehouseFixtures[oniWarehouseCount++];
        light->spinning=UUcFalse;
        light->position=center;light->intensity=0.65f;light->radius=190.f;
        if (strstr(map->debugName,"RED")) {light->red=1.f;light->green=0.35f;light->blue=0.30f;}
        else if (strstr(map->debugName,"BLUE")) {light->red=0.45f;light->green=0.65f;light->blue=1.f;}
        else {light->red=1.f;light->green=0.9f;light->blue=0.75f;}
        ++added;
    }
    return added;
}
// Environment coronas provide additional authored fixture positions on maps
// whose light-source records were discarded. This is a static approximation;
// scripted door indicators are handled separately and updated every tick.
static void ONiSceneCoronaFixture(EPtEnvParticle *env, uintptr_t unused) {
    (void)unused;
    if(strncmp(env->classname,"env_lensflare",13) || env->dynamic_matrix ||
       env->owner_type==EPcParticleOwner_Object || env->owner_type==EPcParticleOwner_Furniture ||
       oniWarehouseCount>=ONI_MAX_WAREHOUSE_FIXTURES ||
       !env->particle || !env->particle_class || env->particle->header.self_ref!=env->particle_self_ref ||
       (env->particle->header.flags & (P3cParticleFlag_Hidden|P3cParticleFlag_Deleted))) return;
    M3tPoint3D position;if(P3rGetRealPosition(env->particle_class,env->particle,&position))return;
    for(UUtUns32 i=0;i<oniWarehouseCount;++i){
        float x=position.x-oniWarehouseFixtures[i].position.x,y=position.y-oniWarehouseFixtures[i].position.y,z=position.z-oniWarehouseFixtures[i].position.z;
        if(x*x+y*y+z*z<225.f)return;
    }
    OniExperimentalLight *light=&oniWarehouseFixtures[oniWarehouseCount++];
    light->spinning=UUcFalse;
    light->position=position;light->radius=160.f;light->intensity=0.35f;
    light->red=1.f;light->green=0.9f;light->blue=0.75f;
    IMtShade *p=P3rGetTintPtr(env->particle_class,env->particle);IMtShade tint=0;
    if(p)tint=*p;
    else if(env->particle_class->definition->appearance.tint.type==P3cValueType_Shade)
        tint=env->particle_class->definition->appearance.tint.u.shade_const.val;
    float r=((tint>>16)&255)/255.f,g=((tint>>8)&255)/255.f,b=(tint&255)/255.f;
    float max=UUmMax(r,UUmMax(g,b)),min=UUmMin(r,UUmMin(g,b));
    if(max>.15f && max-min>.15f){light->red=r;light->green=g;light->blue=b;}
}
static void ONiWarehouseScan(void) {
    if (oniWarehouseScanned || !ONrExperimentalLights_SceneEnabled()) return;
    oniWarehouseObjects = oniWarehouseCount = 0;
    OBJrObjectType_EnumerateObjects(OBJcType_Furniture, ONiWarehouseFixture, 0);
    OBJrObjectType_EnumerateObjects(OBJcType_Console, ONiWarehouseFixture, 0);
    if (!ONgGameState->level || !ONgGameState->level->environment) return;
    UUtUns32 marked=oniWarehouseCount;
    UUtUns32 proxies=ONiWarehouseSurfaceLights();
    UUtUns32 beforeCoronas=oniWarehouseCount;
    EPrEnumerateAllParticles(ONiSceneCoronaFixture,0);
    UUtUns32 coronas=oniWarehouseCount-beforeCoronas;
    oniWarehouseScanned = UUcTrue;
    UUrStartupMessage("[scene-light] level %d: %u objects, %u marked lights, %u material proxies, %u corona proxies (max %u)",
        (int)ONgGameState->levelNumber,(unsigned)oniWarehouseObjects,(unsigned)marked,(unsigned)proxies,(unsigned)coronas,ONI_MAX_WAREHOUSE_FIXTURES);
}
// Luminous moving/colliding projectile classes from Oni's weapon data.
// Ordinary pistol and SMG bullets are intentionally excluded; their muzzle
// flashes are handled separately. Exclude smoke, trails and impacts too.
typedef struct OniProjectileStyle { const char *name; float r,g,b; } OniProjectileStyle;
static const OniProjectileStyle oniProjectileStyles[] = {
    {"w3_phr_p01",.65f,.45f,1.f}, {"w4_psm_p01",1.f,1.f,.60f},
    {"w4_psm_p02",.10f,.80f,.70f}, {"w5_sbg_p01",1.f,.48f,.15f},
    {"w6_vdg_p01",1.f,.30f,.20f}, {"w7_scc_p01",.65f,.85f,1.f},
    {"w7a_scc_p01",.65f,.85f,1.f}, {"w8_mbo_p01",.55f,.72f,1.f},
    {"w9_scr_p01",1.f,.48f,.15f}, {"w11_ba1_p01",1.f,.48f,.15f},
    {"w12_ba2_p01",1.f,.38f,.12f}, {"w13_muro_p01",1.f,.35f,.35f},
    {"w14_muro_p01",1.f,.35f,.35f}
};
static void ONiExperimentalLights_RefreshProjectiles(UUtUns32 now) {
    if (!ONiExperimentalLights_Enabled() || (oniProjectileTickValid && oniProjectileTick==now)) return;
    oniProjectileTick=now;oniProjectileTickValid=UUcTrue;oniProjectileCount=0;
    P3tParticleClassIterateState state=P3cParticleClass_None;P3tParticleClass *cls;
    while(P3rIterateClasses(&state,&cls) && oniProjectileCount<ONI_MAX_PROJECTILE_LIGHTS) {
        if(!cls->definition || !cls->headptr)continue;
        const OniProjectileStyle *style=NULL;
        for(UUtUns32 i=0;i<sizeof(oniProjectileStyles)/sizeof(oniProjectileStyles[0]);++i)
            if(!strcmp(cls->classname,oniProjectileStyles[i].name)){style=&oniProjectileStyles[i];break;}
        if(!style || !(cls->definition->flags & (P3cParticleClassFlag_Physics_CollideEnv|P3cParticleClassFlag_Physics_CollideChar)))continue;
        for(P3tParticle *particle=cls->headptr;particle && oniProjectileCount<ONI_MAX_PROJECTILE_LIGHTS;particle=particle->header.nextptr) {
            if(particle->header.flags & (P3cParticleFlag_Deleted|P3cParticleFlag_Hidden))continue;
            M3tPoint3D position;if(P3rGetRealPosition(cls,particle,&position))continue;
            OniExperimentalLight *light=&oniProjectileLights[oniProjectileCount++];
            light->spinning=UUcFalse;
            light->position=position;light->radius=65.f;light->intensity=1.2f;
            light->red=style->r;light->green=style->g;light->blue=style->b;
            IMtShade *dynamic=P3rGetTintPtr(cls,particle);IMtShade tint=0;
            if(dynamic)tint=*dynamic;
            else if(cls->definition->appearance.tint.type==P3cValueType_Shade)tint=cls->definition->appearance.tint.u.shade_const.val;
            float r=((tint>>16)&255)/255.f,g=((tint>>8)&255)/255.f,b=(tint&255)/255.f;
            float max=UUmMax(r,UUmMax(g,b)),min=UUmMin(r,UUmMin(g,b));
            if(max>.15f && max-min>.15f){light->red=r;light->green=g;light->blue=b;}
        }
    }
    if(oniProjectileCount && !oniProjectileLogged){
        UUrStartupMessage("[world-light] detected active weapon projectiles");oniProjectileLogged=UUcTrue;
    }
}
// A hit effect contains several long-lived sparks; its p02 particle is the
// short, visible central flash. Read only live flashes once per simulation
// tick, so the light follows the particle's own color, position and lifetime.
static void ONiExperimentalLights_RefreshHits(UUtUns32 now) {
    if(!ONiExperimentalLights_HitEnabled() || (oniHitTickValid && oniHitTick==now))return;
    oniHitTick=now;oniHitTickValid=UUcTrue;oniHitCount=0;
    P3tParticleClassIterateState state=P3cParticleClass_None;P3tParticleClass *cls;
    const char suffix[]="flash_p02";
    while(P3rIterateClasses(&state,&cls) && oniHitCount<ONI_MAX_HIT_LIGHTS) {
        size_t len=strlen(cls->classname),suffixLen=sizeof(suffix)-1;
        if(!cls->definition || !cls->headptr || len<4+suffixLen || strncmp(cls->classname,"h2h_",4) ||
           strcmp(cls->classname+len-suffixLen,suffix))continue;
        for(P3tParticle *p=cls->headptr;p && oniHitCount<ONI_MAX_HIT_LIGHTS;p=p->header.nextptr) {
            if(p->header.flags & (P3cParticleFlag_Deleted|P3cParticleFlag_Hidden))continue;
            if(!isfinite(p->header.lifetime) || p->header.lifetime<=0.f)continue;
            M3tPoint3D position;if(P3rGetRealPosition(cls,p,&position))continue;
            if(!isfinite(position.x) || !isfinite(position.y) || !isfinite(position.z))continue;
            float initial=.125f;
            if(cls->definition->lifetime.type==P3cValueType_Float && cls->definition->lifetime.u.float_const.val>0.f)
                initial=cls->definition->lifetime.u.float_const.val;
            float fade=UUmPin(p->header.lifetime/initial,0.f,1.f);
            IMtShade tint=0xffffffff;IMtShade *dynamic=P3rGetTintPtr(cls,p);
            if(dynamic)tint=*dynamic;
            else if(cls->definition->appearance.tint.type==P3cValueType_Shade)tint=cls->definition->appearance.tint.u.shade_const.val;
            float r=((tint>>16)&255)/255.f,g=((tint>>8)&255)/255.f,b=(tint&255)/255.f;
            if(r<.02f && g<.02f && b<.02f)continue;
            UUtBool merged=UUcFalse;
            for(UUtUns32 i=0;i<oniHitCount;++i) {
                float x=position.x-oniHitLights[i].position.x,y=position.y-oniHitLights[i].position.y,z=position.z-oniHitLights[i].position.z;
                if(x*x+y*y+z*z<4.f){if(fade>oniHitLights[i].intensity){oniHitLights[i].intensity=fade;oniHitLights[i].red=r;oniHitLights[i].green=g;oniHitLights[i].blue=b;}merged=UUcTrue;break;}
            }
            if(merged)continue;
            OniExperimentalLight *light=&oniHitLights[oniHitCount++];light->position=position;light->radius=85.f;
            light->intensity=fade;light->red=r;light->green=g;light->blue=b;light->spinning=UUcFalse;
        }
    }
    if(oniHitCount && !oniHitLogged){UUrStartupMessage("[world-light] detected active melee hit flashes");oniHitLogged=UUcTrue;}
}
static void ONiExperimentalLights_Sync(void) {
    if(!ONgGameState)return;
    UUtUns32 now=ONrGameState_GetGameTime();
    if(oniMuzzleLevel!=ONgGameState->levelNumber || now<oniMuzzleLastTick){
        oniMuzzleCount=oniMuzzleNext=0;oniMuzzleLevel=ONgGameState->levelNumber;
        oniProjectileCount=0;oniProjectileTickValid=oniProjectileLogged=UUcFalse;
        oniHitCount=0;oniHitTickValid=oniHitLogged=UUcFalse;
        oniWarehouseScanned=UUcFalse; oniWarehouseCount=0; oniIndicatorCount=0; oniFireCount=0; oniExtraCoronaCount=0; oniExtraCoronaLogged=UUcFalse; memset(oniCoronaCells,0,sizeof(oniCoronaCells)); oniFireLogged=UUcFalse; oniIndicatorTickValid=oniIndicatorLogged=UUcFalse; memset(oniLightCells,0,sizeof(oniLightCells)); memset(oniOcclusionCache,0,sizeof(oniOcclusionCache));
    }
    oniMuzzleLastTick=now;
}
static void ONiExperimentalLights_Fire(WPtWeapon *weapon, const WPtParticleAttachment *shooter) {
    if(!ONiExperimentalLights_Enabled() || !ONgGameState)return;
    ONiExperimentalLights_Sync();
    M3tPoint3D local,world;
    MUrMatrix_GetCol(&shooter->matrix,3,&local);
    MUrMatrix_MultiplyPoint(&local,&weapon->matrix,&world);
    {
        static UUtBool logged = UUcFalse;
        if (!logged) {
            UUrStartupMessage("[world-light] first weapon shot at %.1f, %.1f, %.1f", world.x, world.y, world.z);
            logged = UUcTrue;
        }
    }
    oniMuzzleFlashes[oniMuzzleNext].position=world;
    oniMuzzleFlashes[oniMuzzleNext].tick=ONrGameState_GetGameTime();
    oniMuzzleNext=(oniMuzzleNext+1)%ONI_MAX_EXPERIMENTAL_LIGHTS;
    if(oniMuzzleCount<ONI_MAX_EXPERIMENTAL_LIGHTS)++oniMuzzleCount;
}
// A short-lived line-of-sight cache avoids per-frame collision work for every
// surface. This is a per-surface approximation, not a shadow map: large quads
// use their centre and moving doors may not be represented by static geometry.
static UUtBool ONiExperimentalLightOccluded(const M3tPoint3D *source,const M3tPoint3D *target,UUtUns32 now,UUtBool vehicle) {
    if(!ONgGameState || !ONgGameState->level || !ONgGameState->level->environment) return UUcFalse;
    float dx=target->x-source->x,dy=target->y-source->y,dz=target->z-source->z;
    float distance=MUrSqrt(dx*dx+dy*dy+dz*dz);
    if(!isfinite(distance) || distance<3.f)return UUcFalse;
    UUtInt32 a[3]={(UUtInt32)floorf(source->x*2.f),(UUtInt32)floorf(source->y*2.f),(UUtInt32)floorf(source->z*2.f)};
    UUtInt32 b[3]={(UUtInt32)floorf(target->x*2.f),(UUtInt32)floorf(target->y*2.f),(UUtInt32)floorf(target->z*2.f)};
    UUtUns32 hash=((UUtUns32)a[0]*73856093u ^ (UUtUns32)a[1]*19349663u ^ (UUtUns32)a[2]*83492791u ^
        (UUtUns32)b[0]*2654435761u ^ (UUtUns32)b[1]*2246822519u ^ (UUtUns32)b[2]*3266489917u) % ONI_OCCLUSION_CACHE_SIZE;
    OniOcclusionEntry *entry=&oniOcclusionCache[hash];
    if(entry->valid && entry->vehicle==vehicle && now>=entry->tick && now-entry->tick<15 &&
       !memcmp(entry->source,a,sizeof(a)) && !memcmp(entry->target,b,sizeof(b)))return entry->blocked;
    float trim=0.75f/distance;
    M3tPoint3D start={source->x+dx*trim,source->y+dy*trim,source->z+dz*trim};
    M3tVector3D ray={dx*(1.f-2.f*trim),dy*(1.f-2.f*trim),dz*(1.f-2.f*trim)};
    // Akira collision queries use global result storage. Preserve any result a
    // caller already holds while the renderer performs this visual-only query.
    static AKtCollision saved[AKcMaxNumCollisions];
    UUtUns16 savedCount=AKgNumCollisions;
    if(savedCount>AKcMaxNumCollisions)savedCount=AKcMaxNumCollisions;
    if(savedCount)memcpy(saved,AKgCollisionList,savedCount*sizeof(saved[0]));
    AKtEnvironment *env=ONgGameState->level->environment;
    UUtUns16 maxHits=vehicle?32:1;
    UUtBool blocked=AKrCollision_Point(env,&start,&ray,
        AKcGQ_Flag_LOS_CanSee_Skip_AI | AKcGQ_Flag_Jello,maxHits);
    if(blocked && vehicle && AKgNumCollisions<maxHits && env->gqRenderArray && env->textureMapArray) {
        // Some authored lamp particles are inside the static car mesh. Ignore
        // nearby car body / siren hits, but retain all walls, floors and
        // distant cars as occluders. Never change level geometry or flags.
        blocked=UUcFalse;
        for(UUtUns16 i=0;i<AKgNumCollisions;++i) {
            AKtCollision *hit=&AKgCollisionList[i];UUtBool housing=UUcFalse;
            float x=hit->collisionPoint.x-source->x,y=hit->collisionPoint.y-source->y,z=hit->collisionPoint.z-source->z;
            if(x*x+y*y+z*z<24.f*24.f && hit->gqIndex<env->gqRenderArray->numGQs) {
                UUtUns32 ti=env->gqRenderArray->gqRender[hit->gqIndex].textureMapIndex;
                M3tTextureMap *map=ti<env->textureMapArray->numMaps?env->textureMapArray->maps[ti]:NULL;
                if(map) {const char *name=map->debugName;
                    housing=!strncmp(name,"TCTFCAR",7) || !strncmp(name,"TCTFARMCAR",10) ||
                        !strncmp(name,"TC_CIVCAR",9) || !strncmp(name,"CAR_A",5) ||
                        !strncmp(name,"CAR_B",5) || !strcmp(name,"SIREN");
                }
            }
            if(!housing){blocked=UUcTrue;break;}
        }
    }
    if(savedCount)memcpy(AKgCollisionList,saved,savedCount*sizeof(saved[0]));
    AKgNumCollisions=savedCount;
    entry->valid=UUcTrue;entry->blocked=blocked;entry->vehicle=vehicle;entry->tick=now;
    memcpy(entry->source,a,sizeof(a));memcpy(entry->target,b,sizeof(b));
    return blocked;
}
UUtUns32 ONrExperimentalLights_GetAt(const M3tPoint3D *position, OniExperimentalLight *out, UUtUns32 capacity) {
    if ((!ONrExperimentalLights_WeaponEnabled() && !ONrExperimentalLights_SceneEnabled()) || !ONgGameState || !out) return 0;
    ONiExperimentalLights_Sync();
    if(capacity>ONI_MAX_EXPERIMENTAL_LIGHTS)capacity=ONI_MAX_EXPERIMENTAL_LIGHTS;
    UUtBool vehicleLight[ONI_MAX_EXPERIMENTAL_LIGHTS]={0};
    UUtUns32 count=0,now=ONrGameState_GetGameTime();
    ONiExperimentalLights_RefreshProjectiles(now);
    ONiExperimentalLights_RefreshHits(now);
    UUtBool selectedHits[ONI_MAX_HIT_LIGHTS]={0};
    for(UUtUns32 slot=0;slot<4 && count<capacity;++slot) {
        float best=160.f*160.f;UUtInt32 selected=-1;
        for(UUtUns32 i=0;i<oniHitCount;++i) {
            float d=0.f;if(position){float x=position->x-oniHitLights[i].position.x,y=position->y-oniHitLights[i].position.y,z=position->z-oniHitLights[i].position.z;d=x*x+y*y+z*z;}
            if(!selectedHits[i] && d<best){best=d;selected=(UUtInt32)i;}
        }
        if(selected<0)break;selectedHits[selected]=UUcTrue;out[count++]=oniHitLights[selected];
    }
    UUtBool selectedProjectiles[ONI_MAX_PROJECTILE_LIGHTS]={0};
    for(UUtUns32 slot=0;slot<6 && count<capacity;++slot) {
        float best=160.f*160.f;UUtInt32 selected=-1;
        for(UUtUns32 i=0;i<oniProjectileCount;++i) {
            float d=0.f;
            if(position){float x=position->x-oniProjectileLights[i].position.x,y=position->y-oniProjectileLights[i].position.y,z=position->z-oniProjectileLights[i].position.z;d=x*x+y*y+z*z;}
            if(!selectedProjectiles[i] && d<best){best=d;selected=(UUtInt32)i;}
        }
        if(selected<0)break;
        selectedProjectiles[selected]=UUcTrue;out[count++]=oniProjectileLights[selected];
    }
    UUtUns32 muzzleAdded=0;
    for(UUtUns32 i=0;i<oniMuzzleCount && count<capacity && muzzleAdded<2;++i){
        UUtUns32 age=now-oniMuzzleFlashes[i].tick;
        if(age>=8)continue;
        if (position) {
            float x=position->x-oniMuzzleFlashes[i].position.x, y=position->y-oniMuzzleFlashes[i].position.y, z=position->z-oniMuzzleFlashes[i].position.z;
            if (x*x+y*y+z*z>160.0f*160.0f) continue;
        }
        out[count].position=oniMuzzleFlashes[i].position;out[count].spinning=UUcFalse;
        out[count].intensity=1.0f-(float)age/8.0f; out[count].radius=100.f;
        out[count].red=1.0f; out[count].green=0.80f; out[count].blue=0.55f;
        ++count; ++muzzleAdded;
    }
    ONiWarehouseScan();
    ONiWarehouseRefreshCoronas(now);
    if (position && ONrExperimentalLights_SceneEnabled() && count<capacity) {
        // Reserve room for the nearest visible indicator coronas before filling
        // the remaining slots with architectural lights.
        UUtBool used[ONI_MAX_INDICATOR_LIGHTS]={0};
        for(UUtUns32 slot=0;slot<6 && count<capacity;++slot) {
            float best=75.f*75.f; UUtInt32 selected=-1;
            for(UUtUns32 i=0;i<oniIndicatorCount;++i) {
                float x=position->x-oniIndicatorLights[i].position.x,y=position->y-oniIndicatorLights[i].position.y,z=position->z-oniIndicatorLights[i].position.z;
                float d=x*x+y*y+z*z;if(!used[i] && d<best){best=d;selected=(UUtInt32)i;}
            }
            if(selected<0)break;
            used[selected]=UUcTrue;out[count++]=oniIndicatorLights[selected];
        }
    }
    if (position && ONrExperimentalLights_SceneEnabled() && count<capacity && oniExtraCoronaCount) {
        int cx=(int)floorf(position->x/48.f),cy=(int)floorf(position->y/48.f),cz=(int)floorf(position->z/48.f);
        UUtUns32 hash=((UUtUns32)cx*73856093u ^ (UUtUns32)cy*19349663u ^ (UUtUns32)cz*83492791u)%ONI_CORONA_CELL_CACHE;
        OniCoronaCell *cell=&oniCoronaCells[hash];
        if(!cell->valid || cell->tick!=now || cell->x!=cx || cell->y!=cy || cell->z!=cz) {
            float distances[6];cell->valid=UUcTrue;cell->tick=now;cell->x=cx;cell->y=cy;cell->z=cz;cell->count=0;
            float px=(cx+.5f)*48.f,py=(cy+.5f)*48.f,pz=(cz+.5f)*48.f;
            for(UUtUns32 i=0;i<oniExtraCoronaCount;++i) {
                float x=oniExtraCoronas[i].position.x-px,y=oniExtraCoronas[i].position.y-py,z=oniExtraCoronas[i].position.z-pz;
                float d=x*x+y*y+z*z,limit=oniExtraCoronas[i].radius+42.f;if(d>limit*limit)continue;
                UUtUns32 at=0;while(at<cell->count && distances[at]<=d)++at;if(at>=6)continue;
                UUtUns32 end=cell->count<6?cell->count++:5;
                while(end>at){distances[end]=distances[end-1];cell->indices[end]=cell->indices[end-1];--end;}
                distances[at]=d;cell->indices[at]=i;
            }
        }
        for(UUtUns32 i=0;i<cell->count && count<capacity;++i) {
            UUtUns32 index=cell->indices[i];
            vehicleLight[count]=oniExtraVehicle[index];out[count++]=oniExtraCoronas[index];
        }
    }
    if (position && ONrExperimentalLights_SceneEnabled() && count<capacity) {
        UUtBool usedFires[ONI_MAX_FIRE_LIGHTS]={0};
        for(UUtUns32 slot=0;slot<6 && count<capacity;++slot) {
            float best=250.f*250.f;UUtInt32 selected=-1;
            for(UUtUns32 i=0;i<oniFireCount;++i) {
                float x=position->x-oniFireLights[i].position.x,y=position->y-oniFireLights[i].position.y,z=position->z-oniFireLights[i].position.z;
                float d=x*x+y*y+z*z;if(!usedFires[i] && d<best){best=d;selected=(UUtInt32)i;}
            }
            if(selected<0)break;
            usedFires[selected]=UUcTrue;out[count++]=oniFireLights[selected];
        }
    }
    if (position && ONrExperimentalLights_SceneEnabled() && count<capacity) {
        // Cache nearest fixtures by world-space cell, not around the player. The
        // static selection therefore does not change just because the camera moves.
        int cx=(int)floorf(position->x/48.f), cy=(int)floorf(position->y/48.f), cz=(int)floorf(position->z/48.f);
        UUtUns32 hash=((UUtUns32)cx*73856093u ^ (UUtUns32)cy*19349663u ^ (UUtUns32)cz*83492791u) % ONI_LIGHT_CELL_CACHE;
        OniLightCell *cell=&oniLightCells[hash];
        if (!cell->valid || cell->x!=cx || cell->y!=cy || cell->z!=cz) {
            float distances[ONI_MAX_EXPERIMENTAL_LIGHTS];
            cell->valid=UUcTrue;cell->x=cx;cell->y=cy;cell->z=cz;cell->count=0;
            float px=(cx+.5f)*48.f,py=(cy+.5f)*48.f,pz=(cz+.5f)*48.f;
            for (UUtUns32 i=0;i<oniWarehouseCount;++i) {
                float x=oniWarehouseFixtures[i].position.x-px,y=oniWarehouseFixtures[i].position.y-py,z=oniWarehouseFixtures[i].position.z-pz;
                float d=x*x+y*y+z*z;
                if (d>235.f*235.f) continue;
                UUtUns32 at=0;while(at<cell->count && distances[at]<=d)++at;
                if(at>=ONI_MAX_EXPERIMENTAL_LIGHTS)continue;
                UUtUns32 end=cell->count<ONI_MAX_EXPERIMENTAL_LIGHTS?cell->count++:ONI_MAX_EXPERIMENTAL_LIGHTS-1;
                while(end>at){distances[end]=distances[end-1];cell->indices[end]=cell->indices[end-1];--end;}
                distances[at]=d;cell->indices[at]=i;
            }
        }
        for(UUtUns32 i=0;i<cell->count && count<capacity;++i) out[count++]=oniWarehouseFixtures[cell->indices[i]];
    }
    if(position) {
        UUtUns32 visible=0;
        for(UUtUns32 i=0;i<count;++i)
            if(!ONiExperimentalLightOccluded(&out[i].position,position,now,vehicleLight[i])) out[visible++]=out[i];
        count=visible;
    }
    return count;
}
// Find the nearest point on a triangle, including its edges. Large road
// polygons need this rather than a single centroid for local light selection.
static M3tPoint3D ONiLightClosestTriangle(M3tPoint3D p,M3tPoint3D a,M3tPoint3D b,M3tPoint3D c) {
    M3tPoint3D v[3]={a,b,c},best=a;
    float abx=b.x-a.x,aby=b.y-a.y,abz=b.z-a.z;
    float acx=c.x-a.x,acy=c.y-a.y,acz=c.z-a.z;
    float apx=p.x-a.x,apy=p.y-a.y,apz=p.z-a.z;
    float d00=abx*abx+aby*aby+abz*abz,d01=abx*acx+aby*acy+abz*acz;
    float d11=acx*acx+acy*acy+acz*acz,d20=apx*abx+apy*aby+apz*abz,d21=apx*acx+apy*acy+apz*acz;
    float denom=d00*d11-d01*d01;
    if(denom>1e-8f) {
        float u=(d11*d20-d01*d21)/denom,w=(d00*d21-d01*d20)/denom;
        if(u>=0.f && w>=0.f && u+w<=1.f) {
            M3tPoint3D q={a.x+u*abx+w*acx,a.y+u*aby+w*acy,a.z+u*abz+w*acz};return q;
        }
    }
    float bestD=INFINITY;
    for(int i=0;i<3;++i) {
        M3tPoint3D x=v[i],y=v[(i+1)%3];float dx=y.x-x.x,dy=y.y-x.y,dz=y.z-x.z;
        float len=dx*dx+dy*dy+dz*dz,t=len>1e-8f?((p.x-x.x)*dx+(p.y-x.y)*dy+(p.z-x.z)*dz)/len:0.f;
        t=UUmPin(t,0.f,1.f);M3tPoint3D q={x.x+t*dx,x.y+t*dy,x.z+t*dz};
        dx=p.x-q.x;dy=p.y-q.y;dz=p.z-q.z;float d=dx*dx+dy*dy+dz*dz;
        if(d<bestD){bestD=d;best=q;}
    }
    return best;
}
static float ONiLightDistanceSq(M3tPoint3D a,M3tPoint3D b) {
    float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return x*x+y*y+z*z;
}
static UUtBool ONiLightSame(const OniExperimentalLight *a,const OniExperimentalLight *b) {
    return a->position.x==b->position.x && a->position.y==b->position.y && a->position.z==b->position.z &&
        a->red==b->red && a->green==b->green && a->blue==b->blue && a->spinning==b->spinning;
}
UUtUns32 ONrExperimentalLights_GetForSurface(const M3tPoint3D *v,UUtUns32 n,OniExperimentalLight *out,UUtUns32 capacity) {
    if(!v || n<3 || !out || !capacity)return 0;
    if(n>4)n=4;
    M3tPoint3D center={(v[0].x+v[1].x+v[2].x)/3.f,(v[0].y+v[1].y+v[2].y)/3.f,(v[0].z+v[1].z+v[2].z)/3.f};
    OniExperimentalLight base[ONI_MAX_EXPERIMENTAL_LIGHTS];
    UUtUns32 limit=UUmMin(capacity,(UUtUns32)ONI_MAX_EXPERIMENTAL_LIGHTS);
    UUtUns32 baseCount=ONrExperimentalLights_GetAt(&center,base,limit),count=0;
    if(ONrExperimentalLights_SceneEnabled() && oniExtraCoronaCount) {
        // At most six nearby live coronas get priority; retain existing
        // collision checks, but trace to the nearest point on the surface.
        float distances[6];UUtUns32 indices[6],selected=0;UUtUns32 now=ONrGameState_GetGameTime();
        M3tPoint3D lo=v[0],hi=v[0];
        for(UUtUns32 i=1;i<n;++i){lo.x=UUmMin(lo.x,v[i].x);lo.y=UUmMin(lo.y,v[i].y);lo.z=UUmMin(lo.z,v[i].z);
            hi.x=UUmMax(hi.x,v[i].x);hi.y=UUmMax(hi.y,v[i].y);hi.z=UUmMax(hi.z,v[i].z);}
        for(UUtUns32 i=0;i<oniExtraCoronaCount;++i) {
            const OniExperimentalLight *light=&oniExtraCoronas[i];M3tPoint3D p=light->position;float r=light->radius;
            if(p.x<lo.x-r || p.x>hi.x+r || p.y<lo.y-r || p.y>hi.y+r || p.z<lo.z-r || p.z>hi.z+r)continue;
            M3tPoint3D q=ONiLightClosestTriangle(p,v[0],v[1],v[2]);float d=ONiLightDistanceSq(p,q);
            if(n==4) {M3tPoint3D q2=ONiLightClosestTriangle(p,v[0],v[2],v[3]);float d2=ONiLightDistanceSq(p,q2);if(d2<d){d=d2;q=q2;}}
            if(d>=light->radius*light->radius)continue;
            UUtUns32 at=0;while(at<selected && distances[at]<=d)++at;if(at>=6)continue;
            if(ONiExperimentalLightOccluded(&p,&q,now,oniExtraVehicle[i]))continue;
            UUtUns32 end=selected<6?selected++:5;
            while(end>at){distances[end]=distances[end-1];indices[end]=indices[end-1];--end;}
            distances[at]=d;indices[at]=i;
        }
        for(UUtUns32 i=0;i<selected && count<limit;++i)out[count++]=oniExtraCoronas[indices[i]];
    }
    for(UUtUns32 i=0;i<baseCount && count<limit;++i) {
        UUtBool duplicate=UUcFalse;for(UUtUns32 j=0;j<count;++j)if(ONiLightSame(&base[i],&out[j])){duplicate=UUcTrue;break;}
        if(!duplicate)out[count++]=base[i];
    }
    return count;
}
UUtUns32 ONrExperimentalLights_Get(OniExperimentalLight *out, UUtUns32 capacity) {
    return ONrExperimentalLights_GetAt(NULL,out,capacity);
}
#endif

static void WPiPulseShooter(WPtWeapon *inWeapon, UUtUns16 inIndex, WPtParticleAttachment *inShooter)
{
	UUtUns32 index;
	ONtCharacter *owner;
	ONtActiveCharacter *active_character;
	float recoil_amount;
	AI2tShootingSkill *shooting_skill;

	owner = inWeapon->owner;
	if (owner == NULL) {
		return;
	}

#if defined(__linux__) && UUmSDL
	ONiExperimentalLights_Fire(inWeapon,inShooter);
#endif
	// pulse all particles attached to this shooter
	WPrSendEvent(inWeapon, P3cEvent_Pulse, inShooter->shooter_index);

	// this is the shooter and it's being started - i.e. the weapon is actually firing.
	if ((owner->flags & ONcCharacterFlag_InfiniteAmmo) == 0) {
		// use ammo
		inWeapon->current_ammo -= inShooter->ammo_used;
	}

	if (inWeapon->current_ammo == 0) {
		// the weapon is empty now
		WPrStopBackgroundEffects(inWeapon);

	} else if ((inWeapon->flags & WPcWeaponFlag_ChamberDelay) == 0) {
		// the weapon might have to recharge now
		WPrSendEvent(inWeapon, P3cEvent_DelayStart, (UUtUns16) -1);
		inWeapon->flags |= WPcWeaponFlag_ChamberDelay;
	}

	// weapon now cannot fire for a period
	if (WPgGatlingCheat && (inWeapon->owner != NULL) && (inWeapon->owner->charType == ONcChar_Player)) {
		inWeapon->chamber_time			= inShooter->cheat_chamber_time;
		inWeapon->chamber_time_total	= inShooter->cheat_chamber_time;
	} else {
		inWeapon->chamber_time			= inShooter->chamber_time;
		inWeapon->chamber_time_total	= inShooter->chamber_time;
	}

	active_character = ONrForceActiveCharacter(owner);

	if (active_character != NULL) {
		// apply recoil to our owner
		TRtAnimType recoil_anim_type = inWeapon->weaponClass->recoilAnimType;
		const WPtRecoil *recoil_info = WPrClass_GetRecoilInfo(inWeapon->weaponClass);
		const TRtAnimation *recoil;

		recoil =
			TRrCollection_Lookup(
			owner->characterClass->animations,
			recoil_anim_type,
			ONcAnimState_Anything,
			active_character->animVarient);

		if (NULL != recoil) {
			ONrOverlay_Set(active_character->overlay + ONcOverlayIndex_Recoil, recoil, 0);
		}

		recoil_amount = recoil_info->base;
		if (WPgGatlingCheat && (owner->charType == ONcChar_Player)) {
			recoil_amount = (recoil_amount * inShooter->cheat_chamber_time) / inShooter->chamber_time;
		}

		// AI2 characters can partially compensate for recoil
		if (owner->charType == ONcChar_AI2) {
			index = (inShooter->shooter_index == 0) ? inWeapon->weaponClass->ai_parameters.shootskill_index
													 : inWeapon->weaponClass->ai_parameters_alt.shootskill_index;
			UUmAssert((index >= 0) && (index < AI2cCombat_MaxWeapons));

			shooting_skill = &owner->characterClass->ai2_behavior.combat_parameters.shooting_skill[index];

			recoil_amount *= (1.0f - shooting_skill->recoil_compensation);
		}

		owner->recoilSpeed += recoil_amount;
	}

	// alert the AI, if it's time to do so (we send an event at most every
	// WPcAI_Alert_Delay frames)
	if (inWeapon->ai_alert_timer == 0) {
		WPiAlertAI(inWeapon, (inShooter->shooter_index > 0));
		inWeapon->ai_alert_timer = WPcAI_Alert_Delay;
	}

	return;
}

static void WPiDoEvent(WPtWeapon *inWeapon, UUtUns16 inEvent, UUtUns16 inIndex)
{
	P3tParticle *particle;
	P3tOwner *p_owner;
	M3tVector3D *p_velocity;
	WPtParticleAttachment *attachment;
	ONtCharacter *owner;
	UUtUns16 attachment_mask;

	UUmAssert((inIndex >= 0) && (inIndex < inWeapon->weaponClass->attachment_count));

	attachment = &inWeapon->weaponClass->attachment[inIndex];
	particle = inWeapon->particle[inIndex];
	owner = inWeapon->owner;

	UUmAssert((inIndex >= 0) && (inIndex < 16));
	attachment_mask = (UUtUns16) (1 << inIndex);

	if (inEvent == P3cEvent_Start) {
		inWeapon->attachments_started |= attachment_mask;

	} else if (inEvent == P3cEvent_Stop) {
		if ((inWeapon->attachments_started & attachment_mask) == 0) {
			// this attachment was never started
			return;
		}
		inWeapon->attachments_started &= ~attachment_mask;
	}

	if ((particle == NULL) || (particle->header.self_ref != inWeapon->particle_ref[inIndex])) {
		// this particle has died!
		inWeapon->particle[inIndex] = NULL;
		inWeapon->particle_ref[inIndex] = P3cParticleReference_Null;
		return;
	}

	// we must set up this particle's velocity as equal to the owner's
	// so that emitters which are tagged as "emit parent's velocity"
	// have something to use
	p_velocity = P3rGetVelocityPtr(attachment->classptr, particle);
	if (p_velocity != NULL) {
		if (owner == NULL) {
			MUmVector_Set(*p_velocity, 0, 0, 0);
		} else {
			MUmVector_Copy(*p_velocity, owner->weaponMovementThisFrame);
			MUmVector_Scale(*p_velocity, UUcFramesPerSecond);
		}
	}

	// set up this particle's owner as the current character who's holding the weapon
	p_owner = P3rGetOwnerPtr(attachment->classptr, particle);
	if (p_owner != NULL) {
		*p_owner = (owner == NULL) ? 0 : WPrOwner_MakeWeaponDamageFromCharacter(owner);
	}

	if (WPgDebugShowEvents) {
		COrConsole_Printf_Color(UUcTrue, 0xffffffff, 0xff202060, "%05d %s: %s %s", ONrGameState_GetGameTime(),
			TMrInstance_GetInstanceName(inWeapon->weaponClass), P3gEventName[inEvent], attachment->classname);
	}
	P3rSendEvent(attachment->classptr, particle, inEvent, ((float) ONrGameState_GetGameTime()) / UUcFramesPerSecond);

	// if we just started a shooter, pulse all particles with its index
	if ((inEvent == P3cEvent_Start) && (attachment->ammo_used > 0)) {
		WPiPulseShooter(inWeapon, attachment->shooter_index, attachment);
	}
}

// send an event to all particles attached to a weapon
void WPrSendEvent(WPtWeapon *inWeapon, UUtUns16 inEvent, UUtUns16 inShooter)
{
	UUtUns16 itr;
	WPtParticleAttachment *attachment;

	for (itr = 0, attachment = inWeapon->weaponClass->attachment;
	     itr < inWeapon->weaponClass->attachment_count; itr++, attachment++) {

		// only send the event to attachments with the correct shooter index - this
		// check is bypassed if either index is -1, which matches everything
		if ((inShooter != (UUtUns16) -1) &&
			(attachment->shooter_index != (UUtUns16) -1) &&
			(attachment->shooter_index != inShooter))
			continue;

		if (inEvent == P3cEvent_Start) {
			inWeapon->start_delay[itr] = attachment->start_delay;
			if (inWeapon->start_delay[itr] > 0) {
				continue;
			}
		}

		WPiDoEvent(inWeapon, inEvent, itr);
	}

	return;
}

static UUtBool WPiFireShooter(WPtWeapon *ioWeapon, UUtUns16 shooter_index, UUtBool inForced, UUtBool *outNoAmmo)
{
	WPtParticleAttachment *shooter;
	UUtUns16 attachment_index;
	AI2tDodgeFiringSpread *spread;

	UUmAssert((shooter_index >= 0) && (shooter_index < ioWeapon->weaponClass->shooter_count));

	// find the shooter's attachment
	for (attachment_index = 0, shooter = ioWeapon->weaponClass->attachment;
		 attachment_index < ioWeapon->weaponClass->attachment_count; attachment_index++, shooter++) {
		if ((shooter->ammo_used > 0) && (shooter->shooter_index == shooter_index))
			break;
	}

	// couldn't find the attachment that corresponds to this shooter
	if (attachment_index >= ioWeapon->weaponClass->attachment_count) {
		return UUcFalse;
	}

	// can't fire without ammo
	if (ioWeapon->current_ammo < shooter->ammo_used) {
		if (outNoAmmo != NULL) {
			*outNoAmmo = UUcTrue;
		}
		return UUcFalse;
	}

	if ((ioWeapon->flags & WPcWeaponFlag_IsFiring) == 0) {
		// start firing
		ioWeapon->flags |= WPcWeaponFlag_IsFiring;

		if (!inForced) {
			// set up minimum-fire time
			ioWeapon->must_fire_time		= UUmMax(ioWeapon->must_fire_time, shooter->minfire_time);
		}

		UUmAssert(ioWeapon->ai_firingspread_index == (UUtUns32) -1);
		if (ioWeapon->weaponClass->ai_parameters.dodge_pattern_length > 0) {
			spread = AI2rKnowledge_NewDodgeFiringSpread(&ioWeapon->ai_firingspread_index);
			if (spread != NULL) {
				AI2rKnowledge_SetupDodgeFiringSpread(spread, ioWeapon);
			}
		}

		WPrSendEvent(ioWeapon, P3cEvent_Start, shooter_index);

	}
	else if (ioWeapon->weaponClass->flags & WPcWeaponClassFlag_Autofire) {
		// keep firing
		WPiPulseShooter(ioWeapon, shooter_index, shooter);
	}

	if (!inForced) {
		ioWeapon->flags &= ~WPcWeaponFlag_DelayRelease;
	}

	// we can't reload for a period after firing
	ioWeapon->reload_delay_time		= ioWeapon->weaponClass->reload_delay_time;

	return UUcTrue;
}

// trigger has been depressed.
//
// NB: if this is an autofire weapon then we may get multiple DepressTrigger
// calls without corresponding ReleaseTrigger calls
UUtBool WPrDepressTrigger(WPtWeapon *ioWeapon, UUtBool inForced, UUtBool inAlternate, UUtBool *outNoAmmo)
{
	UUtBool fired = UUcFalse;

	UUmAssertReadPtr(ioWeapon, sizeof(*(ioWeapon)));
	UUmAssert(ioWeapon->owner);

	fired = UUcFalse;
	if (outNoAmmo != NULL) {
		*outNoAmmo = UUcFalse;
	}

	// can't fire if the weapon is reloading or between shots
	if ((ioWeapon->chamber_time) || (ioWeapon->reloadTime))
		return UUcFalse;

	if (ioWeapon->weaponClass->shooter_count == 0) {
		return UUcFalse;
	}

	if (inAlternate) {
		if (ioWeapon->weaponClass->flags & WPcWeaponClassFlag_HasAlternate) {
			fired = WPiFireShooter(ioWeapon, 1, inForced, outNoAmmo);
		}
	} else {
		fired = WPiFireShooter(ioWeapon, 0, inForced, outNoAmmo);
	}

	return fired;
}

// used by AI to determine that it can't fire a single-shot weapon
UUtBool WPrMustWaitToFire(WPtWeapon *inWeapon)
{
	if ((inWeapon->chamber_time > 0) || (inWeapon->reloadTime > 0))
		return UUcTrue;

	return UUcFalse;
}

// used by AI to determine that a weapon is firing
UUtBool WPrIsFiring(WPtWeapon *inWeapon)
{
	return ((inWeapon->flags & WPcWeaponFlag_IsFiring) > 0);
}

// send a gunshot event to the new AI
static void WPiAlertAI(WPtWeapon *ioWeapon, UUtBool inAlternate)
{
	M3tPoint3D location;
	float volume;

	volume = inAlternate ? ioWeapon->weaponClass->ai_parameters_alt.detection_volume : ioWeapon->weaponClass->ai_parameters.detection_volume;

	if (volume > 0) {
		MUmVector_Copy(location, ioWeapon->owner->location);
		location.y += ioWeapon->owner->heightThisFrame;

		AI2rKnowledge_Sound(AI2cContactType_Sound_Gunshot, &location, volume, ioWeapon->owner, AI2rGetCombatTarget(ioWeapon->owner));
	}
}

// release the trigger on a weapon
void WPrReleaseTrigger(WPtWeapon *ioWeapon, UUtBool inForceImmediate)
{
	if (!inForceImmediate) {
		if (ioWeapon->must_fire_time > 0) {
			// delay the release of the trigger until the minimum burst time on the weapon
			ioWeapon->flags |= WPcWeaponFlag_DelayRelease;
			return;
		}

		if ((ioWeapon->weaponClass->flags & WPcWeaponClassFlag_DontStopWhileFiring) &&
			(ioWeapon->chamber_time > ioWeapon->weaponClass->stopChamberThreshold)) {
			// delay the release of the trigger until the chamber time is finished
			ioWeapon->flags |= WPcWeaponFlag_DelayRelease;
			return;
		}
	}

	// stop firing
	ioWeapon->flags &= ~WPcWeaponFlag_DelayRelease;
	ioWeapon->must_fire_time = 0;

	if (ioWeapon->flags & WPcWeaponFlag_IsFiring) {
		ioWeapon->flags &= ~WPcWeaponFlag_IsFiring;

		if (ioWeapon->ai_firingspread_index != (UUtUns32) -1) {
			AI2rKnowledge_DeleteDodgeFiringSpread(ioWeapon->ai_firingspread_index);
			ioWeapon->ai_firingspread_index = (UUtUns32) -1;
		}

		WPrSendEvent(ioWeapon, P3cEvent_Stop, -1);
	}
	UUmAssert(ioWeapon->attachments_started == 0);
}

void WPrStartBackgroundEffects(WPtWeapon *ioWeapon)
{
	if ((ioWeapon->flags & WPcWeaponFlag_BackgroundFX) == 0) {
		ioWeapon->flags |= WPcWeaponFlag_BackgroundFX;

		WPrSendEvent(ioWeapon, P3cEvent_BackgroundStart, -1);
	}
}

void WPrStopBackgroundEffects(WPtWeapon *ioWeapon)
{
	if (ioWeapon->flags & WPcWeaponFlag_BackgroundFX) {
		ioWeapon->flags &= ~WPcWeaponFlag_BackgroundFX;

		WPrSendEvent(ioWeapon, P3cEvent_BackgroundStop, -1);
	}
}

void WPrStopChamberDelay(WPtWeapon *ioWeapon)
{
	if (ioWeapon->flags & WPcWeaponFlag_ChamberDelay) {
		WPrSendEvent(ioWeapon, P3cEvent_DelayStop, (UUtUns16) -1);
		ioWeapon->flags &= ~WPcWeaponFlag_ChamberDelay;
	}
}

void WPrAssign(
	WPtWeapon *inWeapon,
	ONtCharacter *inOwner)
{
	/**************
	* Assigns a weapon to a character
	*/

	if ((inWeapon->flags & WPcWeaponFlag_InUse) == 0) { return; }

	// Assign owner
	inWeapon->owner = inOwner;
	inWeapon->reloadTime = 0;
	inWeapon->flags &=~WPcWeaponFlag_HitGround;
	inWeapon->flags &=~WPcWeaponFlag_Untouchable;
	inWeapon->flags &=~WPcWeaponFlag_Racked;

	// weapons in hand don't need physics
	WPiWeapon_ReleasePhysics(inWeapon);

	// Setup timers
	inWeapon->freeTime = 0;
	inWeapon->fadeTime = 0;

	inWeapon->flags |= WPcWeaponFlag_InHand;

	// don't go inactive, since we have been picked up
	inWeapon->flags &= ~WPcWeaponFlag_DeferUpdate_GoInactive;

	if (inWeapon->current_ammo > 0) {
		WPrStartBackgroundEffects(inWeapon);
	}
}

void WPrRelease(
	WPtWeapon *inWeapon,
	UUtBool inKnockAway)
{
	UUtUns32 itr;

	/**************
	* Release ownership of a weapon
	*/

	PHtPhysicsContext *physics;
	M3tMatrix4x3 *weapon_matrix;

	if ((inWeapon->flags & WPcWeaponFlag_InUse) == 0) { return; }

	// tell the AI that we've stopped firing
	if (inWeapon->ai_firingspread_index != (UUtUns32) -1) {
		AI2rKnowledge_DeleteDodgeFiringSpread(inWeapon->ai_firingspread_index);
		inWeapon->ai_firingspread_index = (UUtUns32) -1;
	}

	// Configure physics
	WPiWeapon_CreatePhysics(inWeapon);

	physics = inWeapon->physics;
	weapon_matrix = NULL;
	if (inWeapon->owner != NULL) {
		WPrNotThroughWall(inWeapon->owner, &physics->matrix);
		physics->position = MUrMatrix_GetTranslation(&physics->matrix);

		weapon_matrix = ONrCharacter_GetMatrix(inWeapon->owner, ONcWeapon_Index);
		ONrCharacter_NotifyReleaseWeapon(inWeapon->owner);
	}

	if (weapon_matrix != NULL) {
		// rotation of the weapon is determined by where its X axis is pointing
		inWeapon->rotation = MUrATan2(weapon_matrix->m[0][0], weapon_matrix->m[0][2]) - M3cHalfPi;
		UUmTrig_ClipLow(inWeapon->rotation);
	} else {
		inWeapon->rotation = (M3c2Pi * UUrRandom()) / UUcMaxUns16;
	}

	physics->velocity.x = physics->velocity.y = physics->velocity.z = 0.0f;

	if (inKnockAway)
	{
		physics->velocity.y += WPcItemLaunchSpeed;	// Give a nice ballistic arc

		if (inWeapon->owner != NULL) {
			ONtActiveCharacter *active_character = ONrGetActiveCharacter(inWeapon->owner);

			if (active_character != NULL) {
				float kickback;

				if (inWeapon->weaponClass->flags & (WPcWeaponClassFlag_Unstorable | WPcWeaponClassFlag_Big)) {
					kickback = WPcCannonKickbackScale;
				} else if (inWeapon->weaponClass->flags & WPcWeaponClassFlag_TwoHanded) {
					kickback = WPcRifleKickbackScale;
				} else {
					kickback = WPcPistolKickbackScale;
				}

				// add velocity in direction of last impact
				MUmVector_ScaleIncrement(physics->velocity, kickback, active_character->lastImpact);
			}
		}
	}

	// Release owner xyzzy
	inWeapon->owner = NULL;
	inWeapon->must_fire_time = 0;
	inWeapon->reloadTime = 0;
	inWeapon->ai_alert_timer = 0;
	inWeapon->chamber_time = 0;
	inWeapon->reload_delay_time = 0;
	inWeapon->flags &= ~WPcWeaponFlag_InHand;
	for (itr = 0; itr < inWeapon->weaponClass->attachment_count; itr++) {
		inWeapon->start_delay[itr] = 0;
	}

	inWeapon->fadeTime = 0;
	if (inWeapon->weaponClass->flags & WPcWeaponClassFlag_DontFadeOut) {
		inWeapon->freeTime = 0;
	}
	else {
		inWeapon->freeTime = WPcFreeTime;
	}

	// we are being dropped, defer these state changes because we might be called from within
	// the particle system which depends on us not calling particle events
	inWeapon->flags |= (WPcWeaponFlag_DeferUpdate_GoInactive | WPcWeaponFlag_DeferUpdate_StopFiring);

	return;
}


void WPrGetMarker(
	WPtWeapon *inWeapon,
	WPtMarker *inMarker,
	M3tPoint3D *outPosition,
	M3tVector3D *outVelocity)
{
	/***************
	* Returns the current world position and
	* vector of a marker node
	*/

	M3tPoint3D worldMuzzle[2];

	MUmVector_Add(worldMuzzle[1], inMarker->position, inMarker->vector);
	worldMuzzle[0] = inMarker->position;

	MUrMatrix_MultiplyPoints(2, (M3tMatrix4x3 *)&inWeapon->matrix, worldMuzzle, worldMuzzle);

	// velocity and position
	if (outVelocity)
	{
		MUmVector_Subtract(*outVelocity, worldMuzzle[1], worldMuzzle[0]);
		MUmVector_Normalize(*outVelocity);
	}
	if (outPosition) *outPosition = worldMuzzle[0];

	return;
}

UUtBool WPrIsAutoFire(WPtWeapon *inWeapon)
{
	UUmAssert(inWeapon);
	UUmAssert(inWeapon->weaponClass);

	if (inWeapon->weaponClass->flags & WPcWeaponClassFlag_Autofire)
		return UUcTrue;
	else
		return UUcFalse;
}

UUtBool WPrHasAmmo(
	WPtWeapon *inWeapon)
{
	/***********
	* Returns whether a gun is able to fire or not
	*/

	if (WPrGetAmmo(inWeapon) == 0)
		return UUcFalse;
	else
		return UUcTrue;
}

static void WPiConsole_ShowKeys(
	UUtUns32			inArgC,
	char**				inArgV,
	void*				inRefcon)
{
	/**********
	* Console command do display keys carried
	*/

	char string[256];

	WPrInventory_GetKeyString(&ONgGameState->characters[ONrGameState_GetPlayerNum()].inventory,string);
	COrConsole_Printf("Keys: %s",string);
}


// we have a staticly allocated internal sphere tree now
void WPiWeaponPhysics_PreDispose_Callback(PHtPhysicsContext *ioContext)
{
	ioContext->sphereTree = NULL;

	return;
}


// we have a staticly allocated internal sphere tree now
void WPiPowerupPhysics_PreDispose_Callback(PHtPhysicsContext *ioContext)
{
	ioContext->sphereTree = NULL;

	return;
}


static UUtBool
WPiCreateWeapon_Callback(
	OBJtObject				*inObject,
	uintptr_t				inUserData)
{
	UUtError				error;
	OBJtOSD_Weapon			*weapon_osd;
	WPtWeaponClass			*weapon_class;

	weapon_osd = (OBJtOSD_Weapon*)inObject->object_data;

	error =
		TMrInstance_GetDataPtr(
			WPcTemplate_WeaponClass,
			weapon_osd->weapon_class_name,
			&weapon_class);
	if (error == UUcError_None)
	{
		WPtWeapon				*weapon;
		M3tPoint3D				position;

		weapon = WPrNew(weapon_class, WPcPlacedWeapon);
		position = inObject->position;
		position.y += 2.0f;				// don't embed weapons in floor

		WPrSetPosition(weapon, &position, inObject->rotation.x * M3cDegToRad);
		weapon->flags |= WPcWeaponFlag_Object;
	}

	return UUcTrue;
}

static void
WPiCreateWeaponsFromObjects(
	void)
{
	OBJrObjectType_EnumerateObjects(
		OBJcType_Weapon,
		WPiCreateWeapon_Callback,
		0);
}

static UUtError
WPiWeapons_Reset(
	SLtErrorContext*		inErrorContext,
	UUtUns32				inParameterListLength,
	SLtParameter_Actual*	inParameterList,
	UUtUns32				*outTicksTillCompletion,	// This returns the number of ticks the caller should be slept
	UUtBool					*outStall,					// This means the caller should be slept and this call made again
	SLtParameter_Actual		*ioReturnValue)
{
	WPtWeapon				*weapon;
	UUtUns32				itr;

	// delete the unheld weapons currently in use
	for (itr = 0, weapon = WPgWeapons; itr < WPcMaxWeapons; itr++, weapon++)
	{
		if ((weapon->flags & WPcWeaponFlag_InUse) == 0) { continue; }
		if (weapon->owner != NULL) { continue; }

		// delete the weapon
		WPrDelete(weapon);
	}

	WPiCreateWeaponsFromObjects();

	return UUcError_None;
}

static UUtBool
WPiCreatePowerup_Callback(
	OBJtObject				*inObject,
	uintptr_t				inUserData)
{
	OBJtOSD_PowerUp			*powerup_osd;

	powerup_osd = (OBJtOSD_PowerUp*)inObject->object_data;
	powerup_osd->powerup = WPrPowerup_New(powerup_osd->powerup_type, WPrPowerup_DefaultAmount(powerup_osd->powerup_type),
											&inObject->position, inObject->rotation.y * M3cDegToRad);

	if (powerup_osd->powerup != NULL) {
		powerup_osd->powerup->flags |= WPcPowerupFlag_PlacedAsObject;

		if (powerup_osd->powerup->geometry != NULL) {
			// don't create powerups embedded in floor
			powerup_osd->powerup->position.y -= powerup_osd->powerup->geometry->pointArray->minmax_boundingBox.minPoint.y;
		}
	}

	return UUcTrue;
}

static void
WPiCreatePowerupsFromObjects(
	void)
{
	OBJrObjectType_EnumerateObjects(
		OBJcType_PowerUp,
		WPiCreatePowerup_Callback,
		0);
}

static UUtError
WPiPowerups_Reset(
	SLtErrorContext*		inErrorContext,
	UUtUns32				inParameterListLength,
	SLtParameter_Actual*	inParameterList,
	UUtUns32				*outTicksTillCompletion,	// This returns the number of ticks the caller should be slept
	UUtBool					*outStall,					// This means the caller should be slept and this call made again
	SLtParameter_Actual		*ioReturnValue)
{
	WPtPowerup				*powerup;
	UUtUns32				itr;

	// delete all powerups... note that we must do this from the end of the array
	// because the delete operation moves the last powerup up to fill the hole made by
	// deleting one
	for (itr = WPgNumPowerups, powerup = &WPgPowerups[WPgNumPowerups - 1]; itr > 0; itr--, powerup--) {
		// delete the powerup
		WPrPowerup_Delete(powerup);
	}

	WPiCreatePowerupsFromObjects();

	return UUcError_None;
}


UUtError WPrInitialize(
	void)
{
	/**********
	* One time game init
	*/

	UUtError error;
	UUtUns16 i;
	WPtWeapon *weapon;

#if CONSOLE_DEBUGGING_COMMANDS
	error = SLrGlobalVariable_Register_Bool("debug_weapons", "prints debugging info about weapon particle events", &WPgDebugShowEvents);
	error = SLrGlobalVariable_Register_Bool("wp_kickable", "lets the player kick weapons", &WPgPlayerKicksWeapons);
	error = SLrGlobalVariable_Register_Bool("recoil_edit", "enables editable recoil", &WPgRecoilEdit);
	error = SLrGlobalVariable_Register_Float("recoil_base", "base", &WPgRecoil_Edited.base);
	error = SLrGlobalVariable_Register_Float("recoil_max", "max", &WPgRecoil_Edited.max);
	error = SLrGlobalVariable_Register_Float("recoil_factor", "factor", &WPgRecoil_Edited.factor);
	error = SLrGlobalVariable_Register_Float("recoil_return_speed", "return speed", &WPgRecoil_Edited.returnSpeed);

	error = SLrGlobalVariable_Register_Int32("wp_hypostrength", "Sets strength of hypo spray", &WPgHypoStrength);
	UUmError_ReturnOnError(error);

	error = SLrGlobalVariable_Register_Int32("wp_fadetime", "Sets free time for powerups", &WPgFadeTime);
	UUmError_ReturnOnError(error);
#endif

	WPgHypoStrength = 15;
	WPgFadeTime = 360;

	WPgDisableFade = UUcFalse;
	error = SLrGlobalVariable_Register_Bool("wp_disable_fade", "Disables weapon fading", &WPgDisableFade);
	UUmError_ReturnOnError(error);

	error =
	SLrScript_Command_Register_Void(
		"give_powerup",
		"Gives a powerup to a character",
		"powerup_name:string [amount:int | ] [character:int | ]",
		WPiCommand_GivePowerup);
	UUmError_ReturnOnError(error);

#if CONSOLE_DEBUGGING_COMMANDS
	error =
		SLrScript_Command_Register_Void(
			"weapon_reset",
			"resets all unheld weapons to their starting state",
			"",
			WPiWeapons_Reset);
	UUmError_ReturnOnError(error);

	error =
		SLrScript_Command_Register_Void(
			"powerup_reset",
			"resets all placed powerups to their starting points",
			"",
			WPiPowerups_Reset);
	UUmError_ReturnOnError(error);
#endif

	// clear the weapon array
	UUrMemory_Clear(WPgWeapons, sizeof(WPtWeapon) * WPcMaxWeapons);

	for (i = 0, weapon = WPgWeapons; i < WPcMaxWeapons; i++, weapon++)
	{
		weapon->index = i;
	}

	return UUcError_None;
}

void
WPrTerminate(
	void)
{
}

UUtError WPrLevel_Begin(void)
{
	UUrMemory_Clear(WPgWeapons,sizeof(WPtWeapon) * WPcMaxWeapons);

	WPgWeaponImpactType = MArImpactType_GetByName("Weapon");
	WPgPowerupImpactType = MArImpactType_GetByName("Powerup");

	WPgNumPowerups = 0;

	WPiCreateWeaponsFromObjects();
	WPiCreatePowerupsFromObjects();

	return UUcError_None;
}

void WPrLevel_End(void)
{
	UUtUns16 i;

	// Delete weapons
	for (i=0; i<WPcMaxWeapons; i++)
	{
		WPtWeapon *curWeapon = WPgWeapons + i;
		if (!(curWeapon->flags & WPcWeaponFlag_InUse)) continue;

		WPrDelete(curWeapon);
	}

	WPgNumPowerups = 0;

	return;
}

WPtWeapon *WPrFind_Weapon_XZ_Squared(
	const M3tPoint3D *inLocation,
	float inMinY,
	float inMaxY,
	WPtWeapon *inWeapon,			// weapon to find or NULL to find any weapon
	float inRadiusSquared)
{
	UUtUns32 i;
	float best_distance_squared = inRadiusSquared;
	WPtWeapon *best_weapon =NULL;
	M3tPoint3D pickup_position;

	for (i=0; i<WPcMaxWeapons; i++)
	{
		WPtWeapon *weapon = WPgWeapons + i;
		float distance_squared;

		if ((weapon->flags & WPcWeaponFlag_InUse) == 0)
			continue;

		if (!WPrCanBePickedUp(weapon))
			continue;

		if ((NULL != inWeapon) && (inWeapon != weapon)) continue;

		WPrGetPickupPosition(weapon, &pickup_position);

		if ((pickup_position.y < inMinY) || (pickup_position.y > inMaxY)) {
			continue;
		}

		distance_squared = UUmSQR(inLocation->x - pickup_position.x) + UUmSQR(inLocation->z - pickup_position.z);

		if (distance_squared < best_distance_squared) {
			best_distance_squared = distance_squared;
			best_weapon = weapon;
		}
	}

	return best_weapon;
}

UUtBool WPrCanBePickedUp(WPtWeapon *inWeapon)
{
	if ((inWeapon->flags & WPcWeaponFlag_InUse) == 0) {
		return UUcFalse;
	}

	if ((inWeapon->owner) != NULL) {
		return UUcFalse;
	}

	if (inWeapon->flags & WPcWeaponFlag_Untouchable) {
		return UUcFalse;
	}

	return UUcTrue;
}

WPtWeapon *WPrFindBarabbasWeapon(void)
{
	WPtWeapon *weapon;
	UUtUns32 i;

	for (i = 0, weapon = WPgWeapons; i < WPcMaxWeapons; i++, weapon++) {
		if ((weapon->flags & WPcWeaponFlag_InUse) == 0)
			continue;

		if ((weapon->weaponClass->flags & WPcWeaponClassFlag_Unreloadable) == 0)
			continue;

		return weapon;
	}
	return NULL;
}

UUtUns32 WPrLocateNearbyWeapons(const M3tPoint3D *inLocation, float inMaxRadius)
{
	UUtUns32 prev_itr, itr, closest_weapon = (UUtUns32) -1;
	UUtUns32 i;
	WPtWeapon *weapon;
	float distance_squared = UUmSQR(inMaxRadius);
	M3tPoint3D pickup_position;

	for (i = 0, weapon = WPgWeapons; i < WPcMaxWeapons; i++, weapon++) {
		if ((weapon->flags & WPcWeaponFlag_InUse) == 0)
			continue;

		weapon->flags &= ~WPcWeaponFlag_Nearby;
		weapon->nearby_link = (UUtUns32) -1;

		if (!WPrCanBePickedUp(weapon))
			continue;

		WPrGetPickupPosition(weapon, &pickup_position);

		weapon->nearby_distance_squared = MUrPoint_Distance_Squared(inLocation, &pickup_position);
		if (weapon->nearby_distance_squared > distance_squared)
			continue;

		weapon->flags |= WPcWeaponFlag_Nearby;

		// find whereabouts this weapon fits into the closest-weapon linked list
		prev_itr = (UUtUns32) -1;
		itr = closest_weapon;
		while (itr != (UUtUns32) -1) {
			UUmAssert((itr >= 0) && (itr < WPcMaxWeapons));
			UUmAssert(WPgWeapons[itr].flags & WPcWeaponFlag_InUse);

			if (WPgWeapons[itr].nearby_distance_squared > weapon->nearby_distance_squared)
				break;

			prev_itr = itr;
			itr = WPgWeapons[itr].nearby_link;
		}

		// add this weapon to the linked list
		weapon->nearby_link = itr;
		if (prev_itr == (UUtUns32) -1) {
			closest_weapon = i;
		} else {
			WPgWeapons[prev_itr].nearby_link = i;
		}
	}

	return closest_weapon;
}

// traverse along the list of weapons set up by WPrLocateNearbyWeapons
WPtWeapon *WPrClosestNearbyWeapon(UUtUns32 *inIterator)
{
	WPtWeapon *weapon;

	// end-of-list sentinel
	if (*inIterator == (UUtUns32) -1)
		return NULL;

	UUmAssert((*inIterator >= 0) && (*inIterator < WPcMaxWeapons));
	weapon = WPgWeapons + *inIterator;
	UUmAssert(weapon->flags & WPcWeaponFlag_InUse);

	if ((weapon->flags & WPcWeaponFlag_Nearby) == 0) {
		// the chain has been broken somehow
		return NULL;
	}

	*inIterator = weapon->nearby_link;
	return weapon;
}

float WPrGetNearbyDistance(WPtWeapon *inWeapon)
{
	UUmAssert(inWeapon->flags & WPcWeaponFlag_InUse);
	return MUrSqrt(inWeapon->nearby_distance_squared);
}


WPtPowerup *WPrFind_Powerup_XZ_Squared(
	ONtCharacter *inCharacter,
	const M3tPoint3D *inLocation,
	float inMinY,
	float inMaxY,
	WPtPowerup *inPowerup,			// powerup to find or NULL to find any powerup
	float inRadiusSquared,
	UUtBool *outInventoryFull)
{
	UUtUns32 i;
	float best_distance_squared = inRadiusSquared;
	WPtPowerup *best_powerup = NULL;
	UUtBool inventory_full = UUcFalse;

	for (i=0; i<WPgNumPowerups; i++)
	{
		WPtPowerup *powerup = WPgPowerups + i;
		float distance_squared;

		if ((NULL != inPowerup) && (inPowerup != powerup)) continue;

		if ((powerup->position.y < inMinY) || (powerup->position.y > inMaxY)) {
			continue;
		}

		distance_squared = UUmSQR(inLocation->x - powerup->position.x) + UUmSQR(inLocation->z - powerup->position.z);

		if (distance_squared >= inRadiusSquared) {
			continue;
		}

		if (WPrPowerup_CouldReceive(inCharacter, powerup->type, powerup->amount) == 0) {
			inventory_full = UUcTrue;
			continue;
		}

		if (distance_squared < best_distance_squared)
		{
			best_powerup = powerup;
			best_distance_squared = distance_squared;
		}
	}

	if (NULL != best_powerup) {
		inventory_full = UUcFalse;
	}

	if (outInventoryFull != NULL) {
		*outInventoryFull = inventory_full;
	}

	return best_powerup;
}

void WPrSlot_Drop(
	WPtInventory *inInventory,
	UUtUns16 inSlotNum,
	UUtBool inWantKnockAway,
	UUtBool inStoreOnly)
{
	/***************
	* Drops the weapon in this slot
	*/

	WPtWeapon *weapon = inInventory->weapons[inSlotNum];

	if (NULL == weapon) return;
	if (!(weapon->flags & WPcWeaponFlag_InUse)) return;

	if (inSlotNum == WPcPrimarySlot) {
		// CB: we must delay the update of this weapon because it's
		// possible for our weapon to be forced to be dropped during
		// the particlesystem update. this can cause problems if we
		// want to delete our particles.
		weapon->flags |= (WPcWeaponFlag_DeferUpdate_GoInactive | WPcWeaponFlag_DeferUpdate_StopFiring);
		weapon->flags &= ~WPcWeaponFlag_InHand;
	}
	else {
		// we are dropping a weapon directly from inventory... update its position before
		// we drop it. also note that weapon_index won't be being updated because we don't have
		// a weapon out
		WPrMagicDrop(weapon->owner, weapon, inWantKnockAway, UUcFalse);
	}

	if (!inStoreOnly) {
		WPrRelease(weapon,inWantKnockAway);
	}

	inInventory->weapons[inSlotNum] = NULL;

	return;
}

// delete all weapons in all slots of an inventory
void WPrSlots_DeleteAll(WPtInventory *inInventory)
{
	UUtUns32 i;

	for (i=0; i<WPcMaxSlots; i++)
	{
		WPtWeapon *weapon = inInventory->weapons[i];

		if (NULL == weapon) continue;

		WPrDelete(weapon);
	}

	return;
}

void WPrSlots_DropAll(
	WPtInventory *inInventory,
	UUtBool inWantKnockAway,
	UUtBool inStoreOnly)
{
	/************
	* Drop all weapons in all slots
	*/

	UUtUns32 i;

	for (i=0; i<WPcMaxSlots; i++)
	{
		WPrSlot_Drop(inInventory, (UUtUns16) i, inWantKnockAway, inStoreOnly);
	}

	return;
}

#define WPcMagicDrop_InFrontDistance		10.0f

static void WPrNotThroughWall(ONtCharacter *inCharacter, M3tMatrix4x3 *ioMatrix)
{
	M3tPoint3D from_location = inCharacter->actual_position;
	M3tPoint3D to_location = MUrMatrix_GetTranslation(ioMatrix);
	M3tVector3D delta;
	float length_of_delta;
	float distance_off_wall = 6.f;

	from_location.y += ONcCharacterHeight / 2.f;

	MUmVector_Subtract(delta, to_location, from_location);
	length_of_delta = MUmVector_GetLength(delta);
	MUrVector_SetLength(&delta, length_of_delta + distance_off_wall);

	if (AKrCollision_Point(ONgGameState->level->environment, &from_location, &delta, AKcGQ_Flag_Col_Skip, 1)) {
		M3tPoint3D new_location;
		float length_to_decrement = UUmMin(distance_off_wall, length_of_delta);

		MUrVector_SetLength(&delta, distance_off_wall);
		MUmVector_Subtract(new_location, AKgCollisionList[0].collisionPoint, delta);

		MUrMatrix_SetTranslation(ioMatrix, &new_location);
	}

	return;
}


// make a character magically drop a weapon, even though the weapon
// might not have been theirs
void WPrMagicDrop(ONtCharacter *inCharacter, WPtWeapon *inWeapon, UUtBool inWantKnockAway, UUtBool inPlaceInFrontOf)
{
	if ((inWeapon == NULL) || ((inWeapon->flags & WPcWeaponFlag_InUse) == 0)) {
		return;
	}

	{
		// update the weapon's position to match the character's location
		M3tMatrix4x3 *matrix_ptr = ONrCharacter_GetMatrix(inCharacter, ONcRHand_Index);
		if (matrix_ptr == NULL) {
			// character is inactive, choose approximate location
			MUrMatrix_SetTranslation(&inWeapon->matrix, &inCharacter->actual_position);
			inWeapon->matrix.m[3][1] += inCharacter->heightThisFrame;
		}
		else {
			inWeapon->matrix = *matrix_ptr;
		}

		inWeapon->rotation = (M3c2Pi * UUrRandom()) / UUcMaxUns16;
	}

	if (inPlaceInFrontOf) {
		inWeapon->matrix.m[3][0] += WPcMagicDrop_InFrontDistance * MUrSin(inCharacter->facing);
		inWeapon->matrix.m[3][2] += WPcMagicDrop_InFrontDistance * MUrCos(inCharacter->facing);
	}

	// make it so we can't drop the weapon through a wall
	WPrNotThroughWall(inCharacter, &inWeapon->matrix);

	WPiWeapon_CreatePhysics(inWeapon);
	inWeapon->physics->matrix = inWeapon->matrix;
	inWeapon->physics->position = MUrMatrix_GetTranslation(&inWeapon->matrix);
	inWeapon->physics->sphereTree->sphere.center = inWeapon->physics->position;

	WPrRelease(inWeapon, inWantKnockAway);
}

void WPrInventory_Reset(
	ONtCharacterClass *inClass,
	WPtInventory *inInventory)
{
	/***********
	* Reset inventory according to a character class
	*/

	// Empty out slots
	WPrSlots_DropAll(inInventory, UUcFalse, UUcFalse);
	UUrMemory_Clear(inInventory->weapons, sizeof(inInventory->weapons));

	// CB: set up a "magic slot" for cinematic usage. note that this comes at the start
	// because WPrFindSlot looks from the end of the array first. we don't want to have to use
	// the magic slot if we can help it.
	inInventory->ammo = inInventory->hypo = inInventory->cell = 0;

	inInventory->shieldRemaining = 0;
	inInventory->invisibilityRemaining = 0;
	inInventory->numInvisibleFrames = 0;

	return;
}

static UUtUns16 WPrInventory_GetHypoUpTime(ONtCharacter *inCharacter)
{
	UUtUns16 time = inCharacter->characterClass->inventoryConstants.hypoTime;

	return time;
}

static UUtUns16 WPrInventory_GetHypoDownTime(ONtCharacter *inCharacter)
{
	UUtUns16 time = 10;

	return time;
}

void WPrInventory_Update(
	ONtCharacter *inCharacter)
{
	/********************
	* Tracks a character's inventory
	*/

	WPtInventory *inventory = &inCharacter->inventory;
	UUtUns32 boosted_max;
	UUtBool is_player = (inCharacter == ONrGameState_GetPlayerCharacter());

	// Track hypos
	if (inventory->hypoTimer) {
		inventory->hypoTimer--;
	}
	else {
		if (inventory->hypoRemaining) {
			boosted_max = MUrUnsignedSmallFloat_To_Uns_Round(inCharacter->maxHitPoints * ONgGameSettings->boosted_health);

			if (inCharacter->hitPoints >= boosted_max) {
				// Fully healed, so stop
				inCharacter->hitPoints = boosted_max;
				inventory->hypoRemaining = 0;
				inventory->hypoTimer = WPrInventory_GetHypoDownTime(inCharacter);
			}
			else {
				inventory->hypoRemaining--;
				ONrCharacter_Heal(inCharacter, 1, UUcTrue);

				if (inCharacter->hitPoints < inCharacter->maxHitPoints) {
					inventory->hypoTimer = WPrInventory_GetHypoUpTime(inCharacter);
				}
			}

		}
		else if (WPgRegenerationCheat && (inCharacter->charType == ONcChar_Player)) {
			// CB: this cheat slowly regenerates health
			if ((inCharacter->hitPoints < inCharacter->maxHitPoints) && ((ONrGameState_GetGameTime() % 60) == 0)) {
				inCharacter->hitPoints++;
			}
		}
		else {
			if (inCharacter->hitPoints > inCharacter->maxHitPoints) {
				inCharacter->hitPoints--;
				inventory->hypoTimer = WPrInventory_GetHypoDownTime(inCharacter);
			}
		}
	}

	// update shield display
	if (inventory->shieldDisplayAmount < inventory->shieldRemaining) {
		inventory->shieldDisplayAmount += WPcShieldDisplaySpeed;
	}
	inventory->shieldDisplayAmount = UUmMin(inventory->shieldDisplayAmount, inventory->shieldRemaining);

	// track how long the character has been invisible
	if (inventory->invisibilityRemaining > 0) {
		inventory->numInvisibleFrames += 1;
	} else {
		inventory->numInvisibleFrames = 0;
	}
}

UUtBool WPrSlot_FindEmpty(
	WPtInventory *inInventory,
	UUtUns16 *outSlotNum,	// optional
	UUtBool inAllowMagic)
{
	/***************
	* Finds highest empty slot, returns success or failure
	*/

	UUtInt32 i;
	UUtUns16 result_slot;
	UUtBool did_find_slot = UUcFalse;

	if ((inAllowMagic) && (NULL == inInventory->weapons[WPcMagicSlot])) {
		did_find_slot = UUcTrue;
		result_slot = WPcMagicSlot;

		goto exit;
	}


	for (i=WPcMaxSlots-1; i>=0; i--)
	{
		// CB: magic slots can only be found by a magic slot-find query... used for
		// cinematics
		if (i == WPcMagicSlot) {
			continue;
		}

		if (NULL == inInventory->weapons[i])
		{
			UUmAssert(i>=0 && i<WPcMaxSlots);

			did_find_slot = UUcTrue;
			result_slot = (UUtUns16) i;

			goto exit;
		}
	}

exit:
	if ((did_find_slot) && (NULL != outSlotNum)) {
		*outSlotNum = result_slot;
	}

	return did_find_slot;
}

WPtWeapon *WPrSlot_FindLastStowed(
	WPtInventory *inInventory,
	UUtUns16 inExcludeSlot,
	UUtUns16 *outSlotNum)	// optional
{
	/************
	* Finds highest slot number with a stowed weapon and returns
	* the weapon and the slot number, or NULL if nothing stowed (and returns an available slot)
	*/

	UUtInt32 i;

	// first: check the magic slot
	if ((inExcludeSlot != WPcMagicSlot) && (inInventory->weapons[WPcMagicSlot] != NULL)) {
		if (outSlotNum != NULL) {
			*outSlotNum = WPcMagicSlot;
		}

		return inInventory->weapons[WPcMagicSlot];
	}

	for (i=WPcMaxSlots-1; i>=0; i--)
	{
		if (i == inExcludeSlot) {
			continue;
		}

		if (inInventory->weapons[i]) {
			if (NULL != outSlotNum) {
				*outSlotNum = (UUtUns16)i;
			}

			UUmAssert(i>=0 && i<WPcMaxSlots);

			return inInventory->weapons[i];
		}
	}

	UUmAssert(WPcMaxSlots>1);
	WPrSlot_FindEmpty(inInventory, outSlotNum, UUcFalse);

	return NULL;
}

void WPrSlot_Swap(
	WPtInventory *inInventory,
	UUtUns16 inSlotA,
	UUtUns16 inSlotB)
{
	/**************
	* Swaps contents of slots A and B. Assumes swap is legal
	*/

	WPtWeapon *t;

	UUmAssert(inSlotA >= 0 && inSlotA < WPcMaxSlots);
	UUmAssert(inSlotB >= 0 && inSlotB < WPcMaxSlots);
	UUmAssert(inSlotA != inSlotB);

	if (inInventory->weapons[inSlotA] != NULL) {
		if (inSlotA == WPcPrimarySlot) {
			// weapon is no longer being held in hand
			inInventory->weapons[inSlotA]->flags |= (WPcWeaponFlag_DeferUpdate_GoInactive | WPcWeaponFlag_DeferUpdate_StopFiring);
			inInventory->weapons[inSlotA]->flags &= ~WPcWeaponFlag_InHand;
		}
		if (inSlotB == WPcPrimarySlot) {
			// weapon is being held in hand
			inInventory->weapons[inSlotA]->flags |= WPcWeaponFlag_InHand;
		}
	}
	if (inInventory->weapons[inSlotB] != NULL) {
		if (inSlotB == WPcPrimarySlot) {
			// weapon is no longer being held in hand
			inInventory->weapons[inSlotB]->flags |= (WPcWeaponFlag_DeferUpdate_GoInactive | WPcWeaponFlag_DeferUpdate_StopFiring);
			inInventory->weapons[inSlotB]->flags &= ~WPcWeaponFlag_InHand;
		}
		if (inSlotA == WPcPrimarySlot) {
			// weapon is being held in hand
			inInventory->weapons[inSlotB]->flags |= WPcWeaponFlag_InHand;
		}
	}

	// swap the weapons
	t = inInventory->weapons[inSlotA];
	inInventory->weapons[inSlotA] = inInventory->weapons[inSlotB];
	inInventory->weapons[inSlotB] = t;
}

static WPtPowerup *WPiPowerup_FindOldest(
	void)
{
	/******************
	* Returns the oldest powerup, or NULL if none
	*/

	UUtUns32 i,oldDate = UUcMaxUns32;
	WPtPowerup *powerup,*oldest = NULL;

	for (i=0; i<WPgNumPowerups; i++)
	{
		powerup = WPgPowerups + i;

		if (powerup->flags & WPcPowerupFlag_PlacedAsObject) {
			continue;
		}

		if (powerup->birthDate < oldDate)
		{
			oldDate = powerup->birthDate;
			oldest = powerup;
		}
	}

	return oldest;
}

static void WPrPowerup_AddPhysics(
	WPtPowerup *inPowerup)
{
	PHtPhysicsContext *physics;

	// create physics context
	physics = inPowerup->physics = PHrPhysicsContext_Add();
	PHrPhysics_Init(physics);
	OBrAnim_Reset(&physics->animContext);
	physics->level = PHcPhysics_Linear;

	physics->callbackData = inPowerup;
	physics->flags |= PHcFlags_Physics_NoMidairFriction | PHcFlags_Physics_CharacterCollisionSkip;
	physics->callback = &gPowerupCallbacks;
	physics->staticFriction = WPcPowerupStaticFriction;
	physics->dynamicFriction = WPcPowerupDynamicFriction;

	// set up bounding volumes and sphere tree
	physics->sphereTree = PHrSphereTree_New(&inPowerup->sphereTree, NULL);
	physics->sphereTree->sibling = NULL;
	physics->sphereTree->child = NULL;
	MUmVector_Set(physics->sphereTree->sphere.center, 0, 0, 0);
	if (inPowerup->geometry == NULL) {
		M3tBoundingBox_MinMax dummybox;

		physics->sphereTree->sphere.radius = 2.0f;
		MUmVector_Set(dummybox.minPoint, -2.0f, -2.0f, -2.0f);
		MUmVector_Set(dummybox.maxPoint, +2.0f, +2.0f, +2.0f);
		M3rMinMaxBBox_To_BBox(&dummybox, &physics->axisBox);
	} else {
		// note: movement code assumes that the sphere is centered on 0, 0, 0 in the powerup's space. however
		// long thin powerups are causing problems with sphere collision if we simply use the points' bounding
		// sphere. instead we use a sphere that touches the bottom of the powerup, since powerup physics is mainly
		// just used for falling anyway.
		physics->sphereTree->sphere.radius = (float)fabs(inPowerup->geometry->pointArray->minmax_boundingBox.minPoint.y);
		physics->sphereTree->sphere.radius = UUmMax(physics->sphereTree->sphere.radius, WPcPowerup_MinSphereRadius);
		M3rMinMaxBBox_To_BBox(&inPowerup->geometry->pointArray->minmax_boundingBox, &physics->axisBox);
	}

	// set up position and velocity
	physics->sphereTree->sphere.center = physics->position = inPowerup->position;
	MUmVector_Set(physics->velocity, 0, 0, 0);
	PHrPhysics_MaintainMatrix(physics);
}

static void WPrPowerup_RemovePhysics(WPtPowerup *inPowerup)
{
	if (inPowerup->physics)
	{
		PHrPhysicsContext_Remove(inPowerup->physics);
		inPowerup->physics = NULL;
	}

	return;
}

WPtPowerup *WPrPowerup_Drop(
	WPtPowerupType inType,
	UUtUns16 inAmount,
	ONtCharacter *inDroppingCharacter,
	UUtBool inPlaceInFrontOf)
{
	WPtPowerup *powerup;
	M3tPoint3D position;
	float rotation;
	ONtActiveCharacter *active_character;

	// create powerup near to character's location
	position = inDroppingCharacter->actual_position;
	position.y += inDroppingCharacter->heightThisFrame;
	AUrDeviateXZ(&position, 1.0f);

	if (inPlaceInFrontOf) {
		position.x += WPcMagicDrop_InFrontDistance * MUrSin(inDroppingCharacter->facing);
		position.z += WPcMagicDrop_InFrontDistance * MUrCos(inDroppingCharacter->facing);
	}

	rotation = (M3c2Pi * UUrRandom()) / UUcMaxUns16;
	powerup = WPrPowerup_New(inType, inAmount, &position, rotation);
	if (powerup == NULL) {
		return powerup;
	}

	WPrPowerup_AddPhysics(powerup);
	UUmAssertReadPtr(powerup->physics, sizeof(PHtPhysicsContext));
	powerup->flags |= WPcPowerupFlag_Moving;

	// add a small random velocity offset
	AUrDeviateXZ(&powerup->physics->velocity, 1.5f / UUcFramesPerSecond);

	if (!inPlaceInFrontOf) {
		// the character is dropping their items because they're being killed.
		active_character = ONrGetActiveCharacter(inDroppingCharacter);
		if (active_character != NULL) {
			// add velocity in direction of last impact
			MUmVector_ScaleIncrement(powerup->physics->velocity, WPcPowerupKickbackScale, active_character->lastImpact);
		}

		// make the powerups do a nice arc
		powerup->physics->velocity.y += WPcItemLaunchSpeed;
	}

	return powerup;
}

WPtPowerup *WPrPowerup_New(
	WPtPowerupType inType,
	UUtUns16 inAmount,
	const M3tPoint3D *inPosition,
	float inRotation)
{
	/****************
	* Creates a new powerup and places it in the level
	*/

	WPtPowerup *powerup;
	UUtError error = UUcError_None;
	M3tPoint3D pos;
	UUtBool reusing;

	if (inAmount <= 0) {
		return NULL;
	}

	UUmAssert(inPosition);
	if (WPgNumPowerups >= WPcMaxPowerups)
	{
		powerup = WPiPowerup_FindOldest();
		UUmAssert(powerup);
		reusing = UUcTrue;

		if (powerup->physics) {
			WPrPowerup_RemovePhysics(powerup);
		}
	}
	else
	{
		powerup = WPgPowerups + WPgNumPowerups;
		reusing = UUcFalse;
	}

	UUrMemory_Clear(powerup,sizeof(WPtPowerup));
	error = WPrPowerup_SetType(powerup, inType);
	if (error != UUcError_None) return NULL;

	pos = *inPosition;
	powerup->flags = 0;
	powerup->amount = inAmount;
	powerup->position = pos;
	powerup->rotation = inRotation;
	powerup->birthDate = ONgGameState->gameTime;
	powerup->physicsRemoveTime = 0;

	if (!reusing) WPgNumPowerups++;
	return powerup;
}

WPtPowerup *WPrPowerup_SpawnAtFlag(
	WPtPowerupType inType,
	UUtUns16 inAmount,
	ONtFlag *inFlag)
{
	WPtPowerup *powerup;

	powerup = WPrPowerup_New(inType, inAmount, &inFlag->location, inFlag->rotation);
	if (powerup == NULL) {
		return NULL;
	}

	powerup->flags |= WPcPowerupFlag_PlacedAsObject;
	if (powerup->geometry != NULL) {
		// don't create powerups embedded in floor
		powerup->position.y -= powerup->geometry->pointArray->minmax_boundingBox.minPoint.y;
	}

	return powerup;
}

void WPrPowerup_Display(
	void)
{
	/*************
	* Displays all the powerups
	*/

	UUtUns32 i;
	WPtPowerup *powerup;
	M3tPoint3D sprite_pos;
//	UUtUns32 alpha_i;
//	float alpha_f;
//	UUtUns32 level;
//	float float_level;

	// powerup glow sprite alpha is unsorted
	M3rGeom_State_Set(M3cGeomStateIntType_SubmitMode, M3cGeomState_SubmitMode_Normal);
	M3rGeom_State_Commit();

	// calculate a level to make the stuff pulse
/*	level = (ONgGameState->gameTime % 64) >> 1;
	if (level < 16) {
		level += 15;
	}
	else {
		level = 32 - level;
		level += 15;
	}*/

	// level is 0..31
//	float_level = level * (1.0f / 31.0f);
//	ONrGameState_ConstantColorLighting(float_level, float_level, float_level);

	M3rGeom_State_Set(M3cGeomStateIntType_SpriteMode, M3cGeomState_SpriteMode_Flat);
	M3rGeom_State_Commit();

	for (i=0; i<WPgNumPowerups; i++)
	{
		M3tBoundingBox_MinMax bbox;
		M3tMatrix4x3 powerup_matrix;
		UUtBool is_visible;
		powerup = WPgPowerups + i;

		if (OBgObjectsShowDebug && (powerup->physics != NULL))
		{
			// draw debugging physics collision
			UUtUns32 index = powerup->physics - PHgPhysicsContextArray->contexts;

			M3rGeom_State_Push();

			// Draw bounding spheres and boxes
			M3rGeom_State_Set(M3cGeomStateIntType_Fill, M3cGeomState_Fill_Line);
			M3rDraw_State_SetInt(M3cDrawStateIntType_ConstantColor,IMcShade_White);
			M3rGeom_State_Set(M3cGeomStateIntType_Appearance, M3cGeomState_Appearance_Gouraud);

			if (powerup->physics->sphereTree) PHrSphereTree_Draw(powerup->physics->sphereTree);

			M3rGeom_State_Pop();
		}

/*		alpha_i = 255;
		if (powerup->fadeTime)
		{
			alpha_f = (float)powerup->fadeTime / (float)WPcFadeTime;
			alpha_f = UUmPin(alpha_f,0.0f,1.0f);
			alpha_i = (UUtUns32)(255.0f * alpha_f);
			M3rGeom_State_Set(M3cGeomStateIntType_Alpha, alpha_i);
		}

		M3rGeom_State_Set(M3cGeomStateIntType_Alpha, alpha_i);
		M3rGeom_State_Commit();*/

		MUrMatrix_BuildTranslate(powerup->position.x, powerup->position.y, powerup->position.z, &powerup_matrix);
		MUrMatrixStack_RotateY(&powerup_matrix, powerup->rotation);
//		MUrMatrixStack_RotateZ(&powerup_matrix, M3cHalfPi);

		if (powerup->geometry == NULL)
		{
			is_visible = UUcTrue;
		}
		else
		{
			static const float tolerance= 0.001f;
			float dx, dy, dz;

			dx= powerup->last_position.x - powerup->position.x;
			dy= powerup->last_position.y - powerup->position.y;
			dz= powerup->last_position.z - powerup->position.z;
			dx*= dx;
			dy*= dy;
			dz*= dz;

			M3rGeometry_GetBBox(powerup->geometry, &powerup_matrix, &bbox);

			if ((dx>tolerance) || (dy>tolerance) || (dz>tolerance) || (powerup->oct_tree_node_index[0] == 0)) {
				AKrEnvironment_NodeList_Get(&bbox, WPcPowerupNodeCount, powerup->oct_tree_node_index, 0);
				powerup->last_position= powerup->position;
			}

			is_visible= AKrEnvironment_NodeList_Visible(powerup->oct_tree_node_index);
		}

		if (is_visible) {
			if (powerup->glow_texture != NULL) {
				sprite_pos = powerup->position;
				if (powerup->geometry != NULL) {
					sprite_pos.y += powerup->geometry->pointArray->minmax_boundingBox.minPoint.y + 0.1f;
				}

				M3rDraw_State_SetInt(M3cDrawStateIntType_ZWrite, M3cDrawState_ZWrite_Off);
				M3rDraw_State_Commit();
				M3rSprite_Draw(powerup->glow_texture, &sprite_pos, powerup->glow_size[0], powerup->glow_size[1], IMcShade_White,
								M3cMaxAlpha / 3, 0, NULL, (M3tMatrix3x3 *) &powerup_matrix, 0, 0, 0);

				M3rDraw_State_SetInt(M3cDrawStateIntType_ZWrite, M3cDrawState_ZWrite_On);
				M3rDraw_State_Commit();
			}
			if (powerup->geometry != NULL) {
				M3rGeometry_MultiplyAndDraw(powerup->geometry, &powerup_matrix);
			}
		}
	}

	// reset the level to 1.0f
//	ONrGameState_ConstantColorLighting(1.0f, 1.0f, 1.0f);

	M3rGeom_State_Set(M3cGeomStateIntType_SpriteMode, M3cGeomState_SpriteMode_Normal);
	M3rGeom_State_Set(M3cGeomStateIntType_SubmitMode, M3cGeomState_SubmitMode_SortAlphaTris);
	M3rGeom_State_Commit();
	return;
}

void WPrPowerup_Delete(
	WPtPowerup *inPowerup)
{
	/***************
	* Remove a powerup from play
	*/

	UUtUns32 i;

	UUmAssert(inPowerup);
	UUmAssert(WPgNumPowerups>0);

	WPrPowerup_RemovePhysics(inPowerup);

	for (i=0; i<WPgNumPowerups; i++)
	{
		if (WPgPowerups + i == inPowerup)
		{
			// Swap in last element
			WPgNumPowerups--;
			*inPowerup = WPgPowerups[WPgNumPowerups];
			return;
		}
	}

	return;
}

UUtBool WPrTryReload(ONtCharacter *inCharacter, WPtWeapon *inWeapon, UUtBool *outWeaponFull, UUtBool *outNoAmmo)
{
	UUtBool reloaded;
	ONtActiveCharacter *active_character = ONrForceActiveCharacter(inCharacter);

	UUmAssert(inCharacter != NULL);
	UUmAssert(inWeapon != NULL);

	if (outWeaponFull != NULL) {
		*outWeaponFull = UUcFalse;
	}

	if (outNoAmmo != NULL) {
		*outNoAmmo = UUcFalse;
	}

	if (NULL == active_character) {
		goto exit;
	}

	if (NULL == inWeapon) {
		goto exit;
	}

	if (NULL == inCharacter->inventory.weapons[0]) {
		goto exit;
	}

	if (WPrGetAmmo(inWeapon) == WPrMaxAmmo(inWeapon)) {
		if (outWeaponFull != NULL) {
			*outWeaponFull = UUcTrue;
		}

		goto exit;
	}

	if (!ONrCharacter_HasAmmoForReload(inCharacter, inWeapon)) {
		// we have no ammo so cannot reload
		if (outNoAmmo != NULL) {
			*outNoAmmo = UUcTrue;
		}

		goto exit;
	}

	// can't reload in an attack or victim animation
	if ((TRrAnimation_IsAttack(active_character->animation)) || (ONrAnimType_IsVictimType(active_character->curAnimType))) {
		goto exit;
	}

	// can't reload if we're already reloading
	if (ONrOverlay_IsActive(active_character->overlay + ONcOverlayIndex_InventoryManagement)) {
		goto exit;
	}

	// must have ammo to reload
	if (!ONrCharacter_HasAmmoForReload(inCharacter, inCharacter->inventory.weapons[0])) {
		goto exit;
	}

	if ((inWeapon->reload_delay_time > 0) || (inWeapon->chamber_time > 0)) {
		goto exit;
	}

	// ok, if we can find a reload animation then actually run the reload
	// we use the powerup here and start the reload animation
	{
		const TRtAnimation *reload_animation;

		reload_animation = TRrCollection_Lookup(
			inCharacter->characterClass->animations,
			WPrGetClass(inCharacter->inventory.weapons[0])->reloadAnimType,
			ONcAnimState_Standing,
			active_character->animVarient);

		if (NULL == reload_animation) {
			COrConsole_Printf("failed to find reload animation");
			goto exit;
		}

		if (reload_animation != NULL) {
			// use the ammo and start the reload
			WPrPowerup_Use(inCharacter, WPrAmmoType(inCharacter->inventory.weapons[0]));
			ONrCharacter_NotIdle(inCharacter);
			ONrOverlay_Set(active_character->overlay + ONcOverlayIndex_InventoryManagement, reload_animation, 0);
		}
	}

	reloaded = UUcTrue;

exit:
	return reloaded;
}

// returns number of specified powerup that a character has
UUtUns32 WPrPowerup_Count(ONtCharacter *inCharacter, WPtPowerupType inType)
{
	switch (inType)
	{
		case WPcPowerup_AmmoBallistic:
			return inCharacter->inventory.ammo;

		case WPcPowerup_AmmoEnergy:
			return inCharacter->inventory.cell;

		case WPcPowerup_Hypo:
			return inCharacter->inventory.hypo;

		case WPcPowerup_None:
			return 0;

		default:
			UUmAssert(!"Illegal powerup type");
			return UUcFalse;
	}
}

// uses a powerup. does *NOT* check to see if it would be useful to do so.
// this must be done separately (e.g. WPrTryReload)
UUtBool WPrPowerup_Use(ONtCharacter *inCharacter,
						WPtPowerupType inType)
{
	switch (inType)
	{
		case WPcPowerup_AmmoBallistic:
			if (!inCharacter->inventory.ammo)
				return UUcFalse;

			inCharacter->inventory.ammo--;
			return UUcTrue;

		case WPcPowerup_AmmoEnergy:
			if (!inCharacter->inventory.cell)
				return UUcFalse;

			inCharacter->inventory.cell--;
			return UUcTrue;

		case WPcPowerup_Hypo:
		{
			UUtUns32 hypoAmount;
			UUtUns32 targetHitPoints;
			UUtUns32 boostedMaxHitPoints;

			targetHitPoints = inCharacter->hitPoints + inCharacter->inventory.hypoRemaining;

			if ((inCharacter->inventory.hypo == 0) || (targetHitPoints > inCharacter->maxHitPoints))
			{
				// we can't hypo if we have none, or if we're already in the super-hypo range
				return UUcFalse;
			}

			if (targetHitPoints >= inCharacter->maxHitPoints)
			{
				// more hypo in boost mode
				hypoAmount = MUrUnsignedSmallFloat_To_Uns_Round(inCharacter->maxHitPoints * ONgGameSettings->hypo_boost_amount);
				targetHitPoints += hypoAmount;
			}
			else
			{
				// calculate the amount of health that this hypo gives us
				hypoAmount = MUrUnsignedSmallFloat_To_Uns_Round(inCharacter->maxHitPoints * ONgGameSettings->hypo_amount);

				if (targetHitPoints + hypoAmount > inCharacter->maxHitPoints) {
					float			amount_remaining;

					// this hypo takes us past the character's max hit points, calculate how much of it remains
					// once we reach max
					amount_remaining = 1.0f - (inCharacter->maxHitPoints - targetHitPoints) / ((float) hypoAmount);
					hypoAmount = MUrUnsignedSmallFloat_To_Uns_Round(amount_remaining * inCharacter->maxHitPoints
																				* ONgGameSettings->hypo_boost_amount);
					targetHitPoints = inCharacter->maxHitPoints + hypoAmount;
				} else {
					// this hypo does not take us past the character's max hit points
					targetHitPoints += hypoAmount;
				}
			}

			// we can never hypo beyond our max boosted hit points
			boostedMaxHitPoints = (UUtUns16) (inCharacter->maxHitPoints * ONgGameSettings->boosted_health);
			targetHitPoints = UUmMin(targetHitPoints, boostedMaxHitPoints);

			inCharacter->inventory.hypoTimer = 0;
			inCharacter->inventory.hypoRemaining = (UUtUns16) (targetHitPoints - inCharacter->hitPoints);

			inCharacter->inventory.hypo--;
			return UUcTrue;
		}

		case WPcPowerup_None:
			return UUcFalse;

		default:
			UUmAssert(!"Illegal powerup type");
			return UUcFalse;
	}
}

static UUtError
WPiCommand_GivePowerup(
	SLtErrorContext*	inErrorContext,
	UUtUns32			inParameterListLength,
	SLtParameter_Actual*		inParameterList,
	UUtUns32			*outTicksTillCompletion,
	UUtBool				*outStall,
	SLtParameter_Actual		*ioReturnValue)
{
	WPtPowerupType p_type;
	UUtInt32 character_number;
	UUtUns16 amount, itr, index;
	UUtBool give_to_all = UUcFalse;

	if (inParameterListLength < 1)
		return UUcError_Generic;

	for (itr = 0; itr < WPcPowerup_NumTypes; itr++) {
		if (UUmString_IsEqual(inParameterList[0].val.str, WPgPowerupName[itr])) {
			p_type = itr;
			break;
		}
	}

	if (itr >= WPcPowerup_NumTypes) {
		COrConsole_Printf("### unknown powerup type! valid powerup types are:");
		for (itr = 0; itr < WPcPowerup_NumTypes; itr++) {
			COrConsole_Printf("  %s", WPgPowerupName[itr]);
		}
		return UUcError_None;
	}

	if (inParameterListLength < 2) {
		amount = WPrPowerup_DefaultAmount(p_type);
	}
	else {
		amount = (UUtUns16) inParameterList[1].val.i;
	}

	if (inParameterListLength  < 3) {
		character_number = ONrGameState_GetPlayerNum();
	}
	else if (inParameterListLength >= 3) {
		character_number = inParameterList[2].val.i ;
	}

	{
		UUtUns32 itr;
		UUtUns32 count = ONrGameState_LivingCharacterList_Count();
		ONtCharacter **list = ONrGameState_LivingCharacterList_Get();

		for(itr = 0; itr < count; itr++) {
			index = ONrCharacter_GetIndex(list[itr]);

			if ((character_number < 0) || (character_number == index)) {
				WPrPowerup_Give(list[itr], p_type, amount, UUcTrue, UUcFalse);
			}
		}
	}

	return UUcError_None;
}

// check to make sure maximum values for inventory are not exceeded
static UUtUns16 WPrPowerup_BoundsCheck(UUtUns16 inCurrentAmount, UUtUns16 inMaxAmount, UUtUns16 inGiveAmount, UUtBool inAdditive)
{
	if (inAdditive) {
		if (inCurrentAmount + inGiveAmount < inMaxAmount) {
			return inGiveAmount;

		} else if (inCurrentAmount >= inMaxAmount) {
			return 0;

		} else {
			return (inMaxAmount - inCurrentAmount);
		}
	} else {
		if (inCurrentAmount >= inGiveAmount) {
			return 0;
		} else {
			return (inGiveAmount - inCurrentAmount);
		}
	}
}

// returns how much of a powerup a character could receive
UUtUns16 WPrPowerup_CouldReceive(ONtCharacter *inCharacter, WPtPowerupType inType, UUtUns16 inAmount)
{
	UUtBool is_player_character = ONgGameState->local.playerCharacter == inCharacter;

	switch (inType)
	{
		case WPcPowerup_AmmoBallistic:
			return WPrPowerup_BoundsCheck(inCharacter->inventory.ammo, WPcMaxAmmoBallistic, inAmount, UUcTrue);

		case WPcPowerup_AmmoEnergy:
			return WPrPowerup_BoundsCheck(inCharacter->inventory.cell, WPcMaxAmmoEnergy, inAmount, UUcTrue);

		case WPcPowerup_Hypo:
			return WPrPowerup_BoundsCheck(inCharacter->inventory.hypo, WPcMaxHypos, inAmount, UUcTrue);

		case WPcPowerup_ShieldBelt:
			return WPrPowerup_BoundsCheck(inCharacter->inventory.shieldRemaining, WPcMaxShields, inAmount, UUcFalse);

		case WPcPowerup_Invisibility:
			return WPrPowerup_BoundsCheck(inCharacter->inventory.invisibilityRemaining, WPcMaxInvisibility, inAmount, UUcFalse);

		case WPcPowerup_LSI:
			return is_player_character;

		case WPcPowerup_None:
			return 0;

		default:
			UUmAssert(!"Illegal powerup type");
			return 0;
	}
}

// gives a powerup.
UUtUns16 WPrPowerup_Give(ONtCharacter *inCharacter, WPtPowerupType inType, UUtUns16 inAmount, UUtBool inIgnoreMaximums, UUtBool inPlaySound)
{
	UUtUns16 give_amount;
	UUtBool play_sound, is_player, is_new_item = UUcFalse;

	is_player = (inCharacter == ONrGameState_GetPlayerCharacter());
	play_sound = inPlaySound && is_player;

	if (inIgnoreMaximums) {
		give_amount = inAmount;
	} else {
		give_amount = WPrPowerup_CouldReceive(inCharacter, inType, inAmount);
	}

	if (give_amount > 0) {
		switch (inType)
		{
			case WPcPowerup_AmmoBallistic:
				inCharacter->inventory.ammo += give_amount;
				if (play_sound) {
					ONrGameState_EventSound_Play(ONcEventSound_ReceiveAmmo, NULL);
				}
			break;

			case WPcPowerup_AmmoEnergy:
				inCharacter->inventory.cell += give_amount;
				if (play_sound) {
					ONrGameState_EventSound_Play(ONcEventSound_ReceiveCell, NULL);
				}
			break;

			case WPcPowerup_Hypo:
				inCharacter->inventory.hypo += give_amount;
				if (play_sound) {
					ONrGameState_EventSound_Play(ONcEventSound_ReceiveHypo, NULL);
				}
			break;

			case WPcPowerup_ShieldBelt:
				inCharacter->inventory.shieldRemaining += give_amount;
			break;

			case WPcPowerup_Invisibility:
				inCharacter->inventory.invisibilityRemaining += give_amount;
			break;

			case WPcPowerup_LSI:
				inCharacter->inventory.has_lsi = UUcTrue;
				if (play_sound) {
					ONrGameState_EventSound_Play(ONcEventSound_ReceiveLSI, NULL);
				}
			break;

			case WPcPowerup_None:
			break;

			default:
				UUmAssert(!"Illegal powerup type");
		}

		if (is_player) {
			if (inType == WPcPowerup_LSI) {
				is_new_item = UUcTrue;

			} else if (!ONrPersist_ItemUnlocked(inType)) {
				// we can now read the persistent info about this kind of item
				ONrPersist_UnlockItem(inType);
				is_new_item = UUcTrue;
			}

			if (is_new_item) {
				ONrInGameUI_NewItem(inType);
			}
		}
	}

	return give_amount;
}

UUtError
WPrPowerup_SetType(
	WPtPowerup			*ioPowerup,
	WPtPowerupType		inPowerupType)
{
	UUmAssert(ioPowerup);

	ioPowerup->type = inPowerupType;
	ioPowerup->geometry = NULL;
	ioPowerup->glow_texture = NULL;
	ioPowerup->glow_size[0] = WPcPowerupDefaultGlowSize;
	ioPowerup->glow_size[1] = WPcPowerupDefaultGlowSize;

	if ((inPowerupType < 0) || (inPowerupType >= WPcPowerup_NumTypes)) {
		UUmError_ReturnOnErrorMsg(UUcError_Generic, "WPrPowerup_SetType: unknown powerup type");
	}

	ioPowerup->glow_size[0] = ONgGameSettings->powerup_glowsize[inPowerupType][0];
	ioPowerup->glow_size[1] = ONgGameSettings->powerup_glowsize[inPowerupType][1];
	TMrInstance_GetDataPtr(M3cTemplate_Geometry,	ONgGameSettings->powerup_geom[inPowerupType], &ioPowerup->geometry);
	TMrInstance_GetDataPtr(M3cTemplate_TextureMap,	ONgGameSettings->powerup_glow[inPowerupType], &ioPowerup->glow_texture);
	return UUcError_None;
}

WPtPowerupType WPrAmmoType(
	WPtWeapon *inWeapon)
{
	/**************
	* Returns the ammo type used by this weapon
	*/

	if (inWeapon->weaponClass->flags & WPcWeaponClassFlag_Unreloadable)
		return WPcPowerup_None;

	if (inWeapon->weaponClass->flags & WPcWeaponClassFlag_Energy)
		return WPcPowerup_AmmoEnergy;

	return WPcPowerup_AmmoBallistic;
}

UUtBool WPrInventory_HasKey(
	WPtInventory *inInventory,
	UUtUns16 inKeyNum)
{
	/****************
	* Returns whether this inventory contains a key
	*/

	UUmAssert(inKeyNum>0 && inKeyNum<=WPcMaxKey);

	if (UUrBitVector_TestBit(&inInventory->keys,inKeyNum)) return UUcTrue;

	return UUcFalse;
}

void WPrInventory_AddKey(
	WPtInventory *inInventory,
	UUtUns16 inKeyNum)
{
	/****************
	* Adds a key to this inventory
	*/

	UUmAssert(inKeyNum>0 && inKeyNum<=WPcMaxKey);

	UUrBitVector_SetBit(&inInventory->keys,inKeyNum);
	WPiConsole_ShowKeys(0,NULL,NULL);
}

void WPrInventory_RemoveKey(
	WPtInventory *inInventory,
	UUtUns16 inKeyNum)
{
	/****************
	* Removes a key from this inventory
	*/

	UUmAssert(inKeyNum>0 && inKeyNum<=WPcMaxKey);

	UUrBitVector_ClearBit(&inInventory->keys,inKeyNum);
	WPiConsole_ShowKeys(0,NULL,NULL);
}

void WPrInventory_GetKeyString(
	WPtInventory *inInventory,
	char *outString)
{
	/**************
	* Outputs a string listing our keys for debugging
	*/

	UUtUns16 i;
	char *s = outString;

	UUmAssert(outString);

	*s = '\0';
	for (i=1; i<=WPcMaxKey; i++)
	{
		if (WPrInventory_HasKey(inInventory,i))
		{
			sprintf(s,"%d ",i);
			while (*s) s++;
		}
	}
}

const WPtRecoil *WPrClass_GetRecoilInfo(
	const WPtWeaponClass *inClass)
{
	const WPtRecoil *result;

	if (WPgRecoilEdit) {
		WPrRecoil_UserTo_Internal(&WPgRecoil_Edited, &WPgRecoil_Commited);
		result = &WPgRecoil_Commited;
	}
	else {
		result = &inClass->private_recoil_info;
	}

	return result;
}

void WPrRecoil_UserTo_Internal(
	const WPtRecoil *inUserRecoil,
	WPtRecoil *outInternalRecoil)
{
	outInternalRecoil->base = inUserRecoil->base * (M3c2Pi / 360.f);
	outInternalRecoil->max = inUserRecoil->max * (M3c2Pi / 360.f);
	outInternalRecoil->returnSpeed = inUserRecoil->returnSpeed * (M3c2Pi / 360.f);
	outInternalRecoil->factor = inUserRecoil->factor;

	outInternalRecoil->base /= (1 + 1 / (outInternalRecoil->factor - 1));

}

/*
 * damage ownership routines
 */

ONtCharacter *WPrOwner_GetCharacter(UUtUns32 inOwner)
{
	switch(inOwner & WPcDamageOwner_TypeMask) {
		case WPcDamageOwner_CharacterWeapon:
		case WPcDamageOwner_CharacterMelee:
		{
			ONtCharacter *char_list, *character = ONrGameState_GetCharacterList();
			UUtUns32 char_index, raw_index;

			char_index = inOwner & WPcDamageOwner_IndexMask;
			raw_index = char_index & ONcCharacterIndexMask_RawIndex;
			if (raw_index < ONrGameState_GetNumCharacters()) {
				char_list = ONrGameState_GetCharacterList();
				character = char_list + raw_index;

				if ((character->flags & ONcCharacterFlag_InUse) && (character->index == char_index)) {
					// the character index matches what was stored in the damage owner...
					// this must be the same character (and not just another one stored
					// in the same place), because the salt matches.
					return character;
				}
			}
			break;
		}
	}

	// could not find the owning character
	return NULL;
}

UUtBool WPrOwner_IsSelfDamage(UUtUns32 inOwner, ONtCharacter *inCharacter)
{
	switch(inOwner & WPcDamageOwner_TypeMask) {
		case WPcDamageOwner_CharacterWeapon:
		case WPcDamageOwner_CharacterMelee:
			if ((inOwner & WPcDamageOwner_IndexMask) == inCharacter->index) {
				return UUcTrue;
			}
		break;
	}

	return UUcFalse;
}

OBJtOSD_Turret *WPrOwner_GetTurret(UUtUns32 inOwner)
{
	OBJtOSD_Turret		*turret_osd;
	OBJtObject			*object;
	UUtUns32			turret_index = inOwner & WPcDamageOwner_IndexMask;

	UUmAssert((inOwner & WPcDamageOwner_TypeMask) == WPcDamageOwner_Turret);

	// FIXME: need to return turret number turret_index from turret array
	object = ONrMechanics_GetObjectWithID( OBJcType_Turret, (UUtUns16) turret_index );

 	UUmAssert( object->object_type == OBJcType_Turret );

	turret_osd = (OBJtOSD_Turret*) object->object_data;

	return turret_osd;
}

UUtUns32 WPrOwner_MakeWeaponDamageFromCharacter(ONtCharacter *inCharacter)
{
	return WPcDamageOwner_CharacterWeapon | ((UUtUns32) inCharacter->index);
}

UUtUns32 WPrOwner_MakeMeleeDamageFromCharacter(ONtCharacter *inCharacter)
{
	return WPcDamageOwner_CharacterMelee | ((UUtUns32) inCharacter->index);
}

UUtUns32 WPrOwner_MakeFromTurret(OBJtOSD_Turret *inTurret)
{
	return WPcDamageOwner_Turret | inTurret->id;
}

UUtBool WPrParticleBelongsToSelf(UUtUns32 inParticleOwner, PHtPhysicsContext *inHitContext)
{
	ONtCharacter *particle_owner;

	if (inHitContext->callback->type != PHcCallback_Character) {
		return UUcFalse;
	}

	particle_owner = WPrOwner_GetCharacter(inParticleOwner);
	return (inHitContext->callbackData == (void *) particle_owner);
}

float WPrGetChamberTimeScale( WPtWeapon *inWeapon )
{
	float		scale;
	if( inWeapon->chamber_time )
	{
		scale = (float) inWeapon->chamber_time / (float) inWeapon->chamber_time_total;
	}
	else
	{
		scale = 0;
	}
	return scale;
}

float WPrParticleInaccuracy(UUtUns32 inParticleOwner)
{
	UUtUns32 owner_type = inParticleOwner & WPcDamageOwner_TypeMask;
	ONtCharacter	*owner_char, *player_char;
	OBJtOSD_Turret	*owner_turret;
	AI2tShootingSkill *skill;
	float inaccuracy = 0;
	UUtBool modify_by_difficulty = UUcFalse;

	// find the owner's shooting skill, and apply inaccuracy which is determined by the AI's current state
	switch (owner_type) {
		case WPcDamageOwner_CharacterWeapon:
			owner_char = WPrOwner_GetCharacter(inParticleOwner);
			if (owner_char == NULL)
				return 0;

			if (((owner_char->flags & ONcCharacterFlag_InUse) == 0) || (owner_char->flags & ONcCharacterFlag_Dead))
				return 0;

			if (owner_char->charType != ONcChar_AI2)
				return 0;

			if (owner_char->ai2State.currentGoal == AI2cGoal_Combat) {
				skill = owner_char->ai2State.currentState->state.combat.targeting.shooting_skill;
				inaccuracy += skill->gun_inaccuracy;

			} else if (owner_char->ai2State.currentGoal == AI2cGoal_Patrol) {
				skill = owner_char->ai2State.currentState->state.patrol.targeting.shooting_skill;
				inaccuracy += skill->gun_inaccuracy;

				inaccuracy += owner_char->ai2State.currentState->state.patrol.targeting_inaccuracy;
			}

			player_char = ONrGameState_GetPlayerCharacter();
			if ((player_char != NULL) && (AI2rTeam_FriendOrFoe(owner_char->teamNumber, player_char->teamNumber) == AI2cTeam_Hostile)) {
				// only enemy characters get more accurate on higher difficulty levels
				modify_by_difficulty = UUcTrue;
			}

			break;

		case WPcDamageOwner_Turret:
			owner_turret = WPrOwner_GetTurret(inParticleOwner);
			inaccuracy += owner_turret->turret_class->shooting_skill.gun_inaccuracy;

			// turrets also get more accurate on higher difficulty levels
			modify_by_difficulty = UUcTrue;
			break;

		default:
			return 0;
	}

	if (modify_by_difficulty) {
		// apply difficulty level inaccuracy changes
		inaccuracy *= ONgGameSettings->inaccuracy_multipliers[ONrPersist_GetDifficulty()];
	}

	return inaccuracy;
}

UUtUns16 WPrPowerup_DefaultAmount(WPtPowerupType inType)
{
	if (inType == WPcPowerup_ShieldBelt) {
		return WPcMaxShields;

	} else if (inType == WPcPowerup_Invisibility) {
		return WPcMaxInvisibility;

	} else {
		return 1;
	}
}

WPtWeaponClass *WPrWeaponClass_GetFromName(const char *inTemplateName)
{
	WPtWeaponClass *weapon_class = TMrInstance_GetFromName(WPcTemplate_WeaponClass, inTemplateName);

	return weapon_class;
}

void WPrUpdateSoundPointers(void)
{
	UUtUns32 itr, num_classes;
	WPtWeaponClass *weapon_class;
	UUtError error;

	num_classes = TMrInstance_GetTagCount(WPcTemplate_WeaponClass);

	for (itr = 0; itr < num_classes; itr++) {
		error = TMrInstance_GetDataPtr_ByNumber(WPcTemplate_WeaponClass, itr, (void **) &weapon_class);
		if (error == UUcError_None) {
			weapon_class->empty_sound = OSrImpulse_GetByName(weapon_class->empty_soundname);
		}
	}
}

UUtBool WPrOutOfAmmoSound(WPtWeapon *ioWeapon)
{
	UUtUns32 itr;
	WPtParticleAttachment *attachment;

	if ((ioWeapon->chamber_time > 0) || (ioWeapon->reloadTime > 0)) {
		// wait for chamber time to expire
		return UUcFalse;
	}

	if (ioWeapon->weaponClass->empty_sound != NULL) {
		M3tPoint3D play_point = MUrMatrix_GetTranslation(&ioWeapon->matrix);

		OSrImpulse_Play(ioWeapon->weaponClass->empty_sound, &play_point, NULL, NULL, NULL);

		if (ioWeapon->weaponClass->flags & WPcWeaponClassFlag_Autofire) {
			// add chamber time of our primary shooter on here
			for (itr = 0, attachment = ioWeapon->weaponClass->attachment; itr < ioWeapon->weaponClass->attachment_count; itr++, attachment++) {
				if (attachment->ammo_used == 0)
					continue;

				if (WPgGatlingCheat && (ioWeapon->owner != NULL) && (ioWeapon->owner->charType == ONcChar_Player)) {
					ioWeapon->chamber_time       = attachment->cheat_chamber_time;
					ioWeapon->chamber_time_total = attachment->cheat_chamber_time;
				} else {
					ioWeapon->chamber_time       = attachment->chamber_time;
					ioWeapon->chamber_time_total = attachment->chamber_time;
				}
				break;
			}
		}

		return UUcTrue;
	}

	return UUcFalse;
}

void WPrInventory_Weapon_Purge_Magic(
	WPtInventory *inInventory)
{
	if (inInventory->weapons[WPcMagicSlot] != NULL) {
		WPtWeapon *normal_weapon = inInventory->weapons[WPcNormalSlot];
		WPtWeapon *magic_weapon = inInventory->weapons[WPcMagicSlot];

		if (NULL != normal_weapon) {
			COrConsole_Printf("If you did not F7 a weapon, the inventory was damage");
		}

		inInventory->weapons[WPcNormalSlot] = magic_weapon;
		inInventory->weapons[WPcMagicSlot] = normal_weapon;
	}

	return;
}


static void WPrSetupCallbacks(void)
{
	UUrMemory_Clear(&gWeaponCallbacks, sizeof(gWeaponCallbacks));

	gWeaponCallbacks.type = PHcCallback_Weapon;
	gWeaponCallbacks.findEnvCollisions = PHrPhysics_Callback_FindEnvCollisions;
	gWeaponCallbacks.findPhyCollisions = PHrPhysics_Callback_FindPhyCollisions;
	gWeaponCallbacks.skipPhyCollisions = WPiWeaponPhysics_SkipPhyCollisions_Callback;
	gWeaponCallbacks.receiveForce = WPiWeaponPhysics_Callback_ReceiveForce;
	gWeaponCallbacks.friction = PHrPhysics_Callback_Friction;
	gWeaponCallbacks.postCollision = WPiWeaponPhysics_PostCollision_Callback;
	gWeaponCallbacks.update = NULL;
	gWeaponCallbacks.preDispose = WPiWeaponPhysics_PreDispose_Callback;
	gWeaponCallbacks.applyForce = NULL;

	UUrMemory_Clear(&gPowerupCallbacks, sizeof(gPowerupCallbacks));

	gPowerupCallbacks.type = PHcCallback_Powerup;
	gPowerupCallbacks.findEnvCollisions = PHrPhysics_Callback_FindEnvCollisions;
	gPowerupCallbacks.findPhyCollisions = PHrPhysics_Callback_FindPhyCollisions;
	gPowerupCallbacks.skipPhyCollisions = WPiPowerupPhysics_SkipPhyCollisions_Callback;
	gPowerupCallbacks.receiveForce = WPiPowerupPhysics_Callback_ReceiveForce;
	gPowerupCallbacks.friction = PHrPhysics_Callback_Friction;
	gPowerupCallbacks.postCollision = WPiPowerupPhysics_PostCollision_Callback;
	gPowerupCallbacks.update = NULL;
	gPowerupCallbacks.preDispose = WPiPowerupPhysics_PreDispose_Callback;
	gPowerupCallbacks.applyForce = NULL;

	return;
}

void WPrSetTimers(WPtWeapon *ioWeapon, UUtUns16 inFreeTime, UUtUns16 inFadeTime)
{
	ioWeapon->freeTime = inFreeTime;
	ioWeapon->fadeTime = inFadeTime;
}
