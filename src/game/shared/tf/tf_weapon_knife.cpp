//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======
//
// Purpose: Weapon Knife.
//
//=============================================================================
#include "cbase.h"
#include "tf_gamerules.h"
#include "tf_weapon_knife.h"
#include "decals.h"

// Client specific.
#ifdef CLIENT_DLL
#include "c_tf_player.h"
// Server specific.
#else
#include "ilagcompensationmanager.h"
#include "tf_player.h"
#include "tf_gamestats.h"
#endif

//=============================================================================
//
// Weapon Knife tables.
//
IMPLEMENT_NETWORKCLASS_ALIASED( TFKnife, DT_TFWeaponKnife );

BEGIN_NETWORK_TABLE( CTFKnife, DT_TFWeaponKnife )
#ifdef CLIENT_DLL
	RecvPropBool( RECVINFO( m_bReadyToBackstab ) ),
#else
	SendPropBool( SENDINFO( m_bReadyToBackstab ) ),
#endif
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA( CTFKnife )
#ifdef CLIENT_DLL
	DEFINE_PRED_FIELD( m_bReadyToBackstab, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
#endif
END_PREDICTION_DATA()

LINK_ENTITY_TO_CLASS( tf_weapon_knife, CTFKnife );
PRECACHE_WEAPON_REGISTER( tf_weapon_knife );

ConVar tf_use_new_backstabs( "tf_use_new_backstabs", "1", FCVAR_NOTIFY | FCVAR_REPLICATED, "Changes knife backstab behavior.", true, 0, true, 1 );

//=============================================================================
//
// Weapon Knife functions.
//

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CTFKnife::CTFKnife()
{
}

//-----------------------------------------------------------------------------
// Purpose: Change idle anim to raised if we're ready to backstab.
//-----------------------------------------------------------------------------
bool CTFKnife::Deploy( void )
{
	if ( BaseClass::Deploy() )
	{
		m_bReadyToBackstab = false;
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CTFKnife::ItemPostFrame( void )
{
	if (tf_use_new_backstabs.GetBool())
		BackstabVMThink();
	BaseClass::ItemPostFrame();
}

//-----------------------------------------------------------------------------
// Purpose: Set stealth attack bool
//-----------------------------------------------------------------------------
void CTFKnife::PrimaryAttack( void )
{
	CTFPlayer *pPlayer = ToTFPlayer( GetPlayerOwner() );

	if ( !CanAttack() )
		return;

	// Set the weapon usage mode - primary, secondary.
	m_iWeaponMode = TF_WEAPON_PRIMARY_MODE;

#if !defined (CLIENT_DLL)
	// Move other players back to history positions based on local player's lag
	lagcompensation->StartLagCompensation( pPlayer, pPlayer->GetCurrentCommand() );
#endif

	trace_t trace;
	if ( DoSwingTrace( trace ) == true )
	{
		// we will hit something with the attack
		if( trace.m_pEnt && trace.m_pEnt->IsPlayer() )
		{
			CTFPlayer *pTarget = ToTFPlayer( trace.m_pEnt );

			if ( pTarget && pTarget->GetTeamNumber() != pPlayer->GetTeamNumber() )
			{
				// Deal extra damage to players when stabbing them from behind
				if ( !tf_use_new_backstabs.GetBool() ? IsBehindTarget( trace.m_pEnt ) : IsBehindAndFacingTarget( trace.m_pEnt ) )
				{
					// this will be a backstab, do the strong anim
					m_iWeaponMode = TF_WEAPON_SECONDARY_MODE;

					// store the victim to compare when we do the damage
					m_hBackstabVictim = trace.m_pEnt;
				}
			}
		}
	}

#ifndef CLIENT_DLL
	pPlayer->RemoveInvisibility();
	pPlayer->RemoveDisguise();
#endif

#if !defined (CLIENT_DLL)
	lagcompensation->FinishLagCompensation( pPlayer );
#endif

	// Reset "backstab ready" state after each attack.
	m_bReadyToBackstab = false;

	// Swing the weapon.
	Swing( pPlayer );
	
	// And hit instantly.
	if ( tf_use_new_backstabs.GetBool() )
	{
		Smack();
		m_flSmackTime = 0.0f;
	}

#if !defined( CLIENT_DLL ) 
	pPlayer->SpeakWeaponFire();
	CTF_GameStats.Event_PlayerFiredWeapon( pPlayer, IsCurrentAttackACritical() );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Do backstab damage
//-----------------------------------------------------------------------------
float CTFKnife::GetMeleeDamage( CBaseEntity *pTarget, int &iCustomDamage )
{
	float flBaseDamage = BaseClass::GetMeleeDamage( pTarget, iCustomDamage );

	CTFPlayer *pOwner = GetTFPlayerOwner();

	if ( pOwner && pTarget->IsPlayer() )
	{
		if ( ( ( !tf_use_new_backstabs.GetBool() ) ? IsBehindTarget( pTarget ) : IsBehindAndFacingTarget( pTarget ) ) ||
			(m_iWeaponMode == TF_WEAPON_SECONDARY_MODE && m_hBackstabVictim.Get() == pTarget))
		{
			// this will be a backstab, do the strong anim.
			// Do twice the target's health so that random modification will still kill him.
			flBaseDamage = pTarget->GetHealth() * 2;

			// Declare a backstab.
			iCustomDamage = TF_DMG_CUSTOM_BACKSTAB;
		}
		else if ( pOwner->m_Shared.IsCritBoosted() )
		{
			m_bCurrentAttackIsCrit = true;
		}
		else
		{
			m_bCurrentAttackIsCrit = false;	// don't do a crit if we failed the above checks.
		}
	}

	return flBaseDamage;
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CTFKnife::IsBehindAndFacingTarget( CBaseEntity *pTarget )
{
	Assert( pTarget );

	// Get the forward view vector of the target, ignore Z
	Vector vecVictimForward;
	AngleVectors( pTarget->EyeAngles(), &vecVictimForward );
	vecVictimForward.z = 0.0f;
	vecVictimForward.NormalizeInPlace();

	// Get a vector from my origin to my targets origin
	Vector vecToTarget;
	vecToTarget = pTarget->WorldSpaceCenter() - GetOwner()->WorldSpaceCenter();
	vecToTarget.z = 0.0f;
	vecToTarget.NormalizeInPlace();

	// Get a forward vector of the attacker.
	Vector vecOwnerForward;
	AngleVectors( GetOwner()->EyeAngles(), &vecOwnerForward );
	vecOwnerForward.z = 0.0f;
	vecOwnerForward.NormalizeInPlace();

	float flDotOwner = DotProduct( vecToTarget, vecOwnerForward );
	float flDotVictim = DotProduct( vecToTarget, vecVictimForward );
	float flDotViewAngles = DotProduct ( vecVictimForward, vecOwnerForward );

	// Make sure they're actually facing the target.
	// This needs to be done because lag compensation can place target slightly behind the attacker.

	return ( flDotVictim > 0.f && flDotOwner > 0.5f && flDotViewAngles > -0.3f );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool CTFKnife::IsBehindTarget( CBaseEntity *pTarget )
{
	Assert( pTarget );

	// Get the forward view vector of the target, ignore Z
	Vector vecVictimForward;
	AngleVectors( pTarget->EyeAngles(), &vecVictimForward, NULL, NULL );
	vecVictimForward.z = 0.0f;
	vecVictimForward.NormalizeInPlace();

	// Get a vector from my origin to my targets origin
	Vector vecToTarget;
	vecToTarget = pTarget->WorldSpaceCenter() - GetOwner()->WorldSpaceCenter();
	vecToTarget.z = 0.0f;
	vecToTarget.NormalizeInPlace();

	float flDot = DotProduct( vecVictimForward, vecToTarget );

	return ( flDot > -0.1 );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CTFKnife::CalcIsAttackCriticalHelper( void )
{
	// Always crit from behind, never from front
	return ( m_iWeaponMode == TF_WEAPON_SECONDARY_MODE );
}

//-----------------------------------------------------------------------------
// Purpose: Allow melee weapons to send different anim events
// Input  :  - 
//-----------------------------------------------------------------------------
void CTFKnife::SendPlayerAnimEvent( CTFPlayer *pPlayer )
{
	if ( m_iWeaponMode == TF_WEAPON_SECONDARY_MODE )
	{
		pPlayer->DoAnimationEvent( PLAYERANIMEVENT_CUSTOM_GESTURE, ACT_MP_ATTACK_STAND_SECONDARYFIRE );
	}
	else
	{
		pPlayer->DoAnimationEvent( PLAYERANIMEVENT_ATTACK_PRIMARY );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CTFKnife::DoViewModelAnimation( void )
{
	// Overriding so it doesn't do backstab animation on crit.
	Activity act;
	if (!tf_use_new_backstabs.GetBool())
		act = (m_iWeaponMode == TF_WEAPON_PRIMARY_MODE) ? ACT_VM_HITCENTER : ACT_VM_SWINGHARD_SPECIAL;
	else
		act = (m_iWeaponMode == TF_WEAPON_PRIMARY_MODE) ? ACT_VM_HITCENTER : ACT_VM_SWINGHARD;

	SendWeaponAnim( act );
}

//-----------------------------------------------------------------------------
// Purpose: Change idle anim to raised if we're ready to backstab.
//-----------------------------------------------------------------------------
bool CTFKnife::SendWeaponAnim( int iActivity )
{
	if ( m_bReadyToBackstab && iActivity == ACT_VM_IDLE )
	{
		return BaseClass::SendWeaponAnim( ACT_BACKSTAB_VM_IDLE );
	}

	return BaseClass::SendWeaponAnim( iActivity );
}

//-----------------------------------------------------------------------------
// Purpose: Check for knife raise conditions.
//-----------------------------------------------------------------------------
void CTFKnife::BackstabVMThink( void )
{
	CTFPlayer *pOwner = GetTFPlayerOwner();
	if ( !pOwner )
		return;

	if ( GetActivity() == ACT_VM_IDLE || GetActivity() == ACT_BACKSTAB_VM_IDLE )
	{
		trace_t tr;
		if ( CanAttack() && DoSwingTrace( tr ) &&
			tr.m_pEnt->IsPlayer() && tr.m_pEnt->GetTeamNumber() != pOwner->GetTeamNumber() &&
			IsBehindAndFacingTarget( tr.m_pEnt ) )
		{
			if ( !m_bReadyToBackstab )
			{
				m_bReadyToBackstab = true;
				SendWeaponAnim( ACT_BACKSTAB_VM_UP );
			}
		}
		else
		{
			if ( m_bReadyToBackstab )
			{
				m_bReadyToBackstab = false;
				SendWeaponAnim( ACT_BACKSTAB_VM_DOWN );
			}
		}
	}
}
