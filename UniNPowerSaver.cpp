
/*
 * Copyright (c) 1999 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights Reserved.
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */


#include "UniNEnet.h"
#include "UniNEnetMII.h"
#include <libkern/OSByteOrder.h>

#define super IOEthernetController



	/****** From iokit/IOKit/pwr_mgt/IOPMpowerState.h 
		struct IOPMPowerState
		{
		unsigned long	version;				// version number of this struct

		IOPMPowerFlags	capabilityFlags;		// bits that describe the capability 
		IOPMPowerFlags	outputPowerCharacter;	// description (to power domain children) 
		IOPMPowerFlags	inputPowerRequirement;	// description (to power domain parent)

		unsigned long	staticPower;			// average consumption in milliwatts
		unsigned long	unbudgetedPower;		// additional consumption from separate power supply (mw)
		unsigned long	powerToAttain;			// additional power to attain this state from next lower state (in mw)

		unsigned long	timeToAttain;			// (microseconds)
		unsigned long	settleUpTime;			// (microseconds)
		unsigned long	timeToLower;			// (microseconds)
		unsigned long	settleDownTime;			// (microseconds)

		unsigned long	powerDomainBudget;		// power in mw a domain in this state can deliver to its children
		};
	*******/

	static IOPMPowerState	ourPowerStates[ kNumOfPowerStates ] =
	{
		{ 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 1, IOPMDeviceUsable | IOPMMaxPerformance, IOPMPowerOn, IOPMPowerOn,
				50, 0, 0, kUniNsettle_time, kUniNsettle_time, kUniNsettle_time,
				kUniNsettle_time, 0 }
	};


	// Method: registerWithPolicyMaker - Called by superclass - not by
	//		Power Management
	//
	// Purpose:
	//	 Initialize the driver for power management and register
	//	 ourselves with policy-maker.

IOReturn UniNEnet::registerWithPolicyMaker( IOService *policyMaker )
{
	IOReturn	rc;


	ELG( IOThreadSelf(), 0, 'RwPM', "registerWithPolicyMaker" );

	if ( fBuiltin )
		 rc = policyMaker->registerPowerDriver( this, ourPowerStates, kNumOfPowerStates );
	else rc = super::registerWithPolicyMaker( policyMaker );	// return unsupported

	return rc;
}/* end registerWithPolicyMaker */


	// Method: maxCapabilityForDomainState
	//
	// Purpose:
	//		  returns the maximum state of card power, which would be
	//		  power on without any attempt to power manager.

unsigned long UniNEnet::maxCapabilityForDomainState( IOPMPowerFlags domainState )
{
	ELG( IOThreadSelf(), domainState, 'mx4d', "maxCapabilityForDomainState" );

	if ( domainState & IOPMPowerOn )
		return kNumOfPowerStates - 1;

	return 0;
}/* end maxCapabilityForDomainState */


	// Method: initialPowerStateForDomainState
	//
	// Purpose:
	// The power domain may be changing state.	If power is on in the new
	// state, that will not affect our state at all.  If domain power is off,
	// we can attain only our lowest state, which is off.

unsigned long UniNEnet::initialPowerStateForDomainState( IOPMPowerFlags domainState )
{
	ELG( IOThreadSelf(), domainState, 'ip4d', "initialPowerStateForDomainState" );

	if ( domainState & IOPMPowerOn )
	   return kNumOfPowerStates - 1;

	return 0;
}/* end initialPowerStateForDomainState */


	// Method: powerStateForDomainState
	//
	// Purpose:
	//		   The power domain may be changing state.	If power is on in the new
	// state, that will not affect our state at all.  If domain power is off,
	// we can attain only our lowest state, which is off.

unsigned long UniNEnet::powerStateForDomainState(IOPMPowerFlags domainState )
{
	ELG( IOThreadSelf(), domainState, 'ps4d', "UniNEnet::powerStateForDomainState" );

	if ( domainState & IOPMPowerOn )
		return 1;						// This should answer What If?

	return 0;
}/* end powerStateForDomainState */


	// Method: setPowerState

IOReturn UniNEnet::setPowerState(	unsigned long	powerStateOrdinal,
									IOService		*whatDevice )
{
	ELG( IOThreadSelf(), (currentPowerState << 16) | powerStateOrdinal, 'Pwr!', "UniNEnet::setPowerState" );

	if ( powerStateOrdinal >= kNumOfPowerStates )
		return IOPMNoSuchState;						// Do nothing if state invalid

	if ( powerStateOrdinal == currentPowerState )
		return IOPMAckImplied;						// no change required

	return IOPMAckImplied;
}/* end setPowerState */


	// This method sets up the PHY registers for low power.
	// Copied from stopEthernetController() in OS9.

void UniNEnet::stopPHY()
{
	UInt32	  val32;
	UInt16	  i, val16;

	ELG( fWOL, fPHYType, '-Phy', "UniNEnet::stopPHY" );

	if ( !fBuiltin || (fPHYType == 0) )
		return;

	if ( fWOL == false )
	{		// disabling MIF interrupts on the 5201 is explicit
		if ( fPHYType == 0x5201 )
			miiWriteWord( 0x0000, MII_BCM5201_INTERRUPT );
	}

		/* Turn off PHY status-change polling to prevent immediate wakeup:	*/
	val32 = READ_REGISTER( MIFConfiguration );
	val32 &= ~kMIFConfiguration_Poll_Enable;
	WRITE_REGISTER( MIFConfiguration, val32 );

		// 5th ADDR in Broadcom PHY docs
	miiReadWord( &val16, MII_LINKPARTNER );	

		// don't know why OS9 writes it back unchanged
	miiWriteWord( val16, MII_LINKPARTNER );	

	if ( fWOL )
	{
			// For multicast filtering these bits must be enabled
		WRITE_REGISTER( RxMACConfiguration,		kRxMACConfiguration_Hash_Filter_Enable
											  | kRxMACConfiguration_Strip_FCS
											  | kRxMACConfiguration_Rx_Mac_Enable );

		UInt16	*p16;
		p16 = (UInt16*)myAddress.bytes;

		WRITE_REGISTER( WOLMagicMatch[ 2 ], p16[ 0 ] );		// enet address
		WRITE_REGISTER( WOLMagicMatch[ 1 ], p16[ 1 ] );
		WRITE_REGISTER( WOLMagicMatch[ 0 ], p16[ 2 ] );

		WRITE_REGISTER( WOLPatternMatchCount, kWOLPatternMatchCount_M | kWOLPatternMatchCount_N );

		val32 = kWOLWakeupCSR_Magic_Wakeup_Enable;		// Assume GMII
		if ( !(fXIFConfiguration & kXIFConfiguration_GMIIMODE) )
			 val32 |= kWOLWakeupCSR_Mode_MII;			// NG - indicate non GMII
		WRITE_REGISTER( WOLWakeupCSR, val32 );
	}
	else
	{
		WRITE_REGISTER( RxMACConfiguration, 0 );
		IOSleep( 4 ); 		// it takes time for enable bit to clear
	}

	WRITE_REGISTER( TxMACConfiguration, 0 );
	WRITE_REGISTER( XIFConfiguration,	0 );

	WRITE_REGISTER( TxConfiguration, 0 );
	WRITE_REGISTER( RxConfiguration, 0 );

	if ( !fWOL )
	{
			// this doesn't power down stuff, but if we don't hit it then we can't
			// superisolate the transceiver
		WRITE_REGISTER( SoftwareReset, kSoftwareReset_TX | kSoftwareReset_RX );

		i = 0;
		do
		{
			IODelay( 10 );
			if ( i++ >= 100 )
			{
				ALRT( 0, val32, 'Sft-', "UniNEnet::stopPHY - timeout on SoftwareReset" );
				break;
			}
			val32 = READ_REGISTER( SoftwareReset );
		} while ( (val32 & (kSoftwareReset_TX | kSoftwareReset_RX)) != 0 );

		WRITE_REGISTER( TxMACSoftwareResetCommand, kTxMACSoftwareResetCommand_Reset );
		WRITE_REGISTER( RxMACSoftwareResetCommand, kRxMACSoftwareResetCommand_Reset );

			// This is what actually turns off the LINK LED

		switch ( fPHYType )
		{
		case 0x5400:
		case 0x5401:
#if 0
				// The 5400 has read/write privilege on this bit,
				// but 5201 is read-only.
			miiWriteWord( MII_CONTROL_POWERDOWN, MII_CONTROL );
#endif
			break;

		case 0x5221:
				// 1: enable shadow mode registers in 5221 (0x1A-0x1E)
			miiReadWord( &val16, MII_BCM5221_TestRegister );
			miiWriteWord( val16 | MII_BCM5221_ShadowRegEnableBit, MII_BCM5221_TestRegister );	

				// 2: Force IDDQ mode for max power savings
				// remember..after setting IDDQ mode we have to "hard" reset
				// the PHY in order to access it.
			miiReadWord( &val16, MII_BCM5221_AuxiliaryMode4 );
			miiWriteWord( val16 | MII_BCM5221_SetIDDQMode, MII_BCM5221_AuxiliaryMode4 );
			break;

		case 0x5201:
#if 0
			miiReadWord( &val16, MII_BCM5201_AUXMODE2 );
			miiWriteWord( val16 & ~MII_BCM5201_AUXMODE2_LOWPOWER,  MII_BCM5201_AUXMODE2 );
#endif

			miiWriteWord( MII_BCM5201_MULTIPHY_SUPERISOLATE, MII_BCM5201_MULTIPHY );
			break;


		case 0x5411:
		case 0x5421:
		default:
			miiWriteWord( MII_CONTROL_POWERDOWN, MII_CONTROL );
			break;
		}/* end SWITCH on PHY type */

			/* Put the MDIO pins into a benign state.							*/
			/* Note that the management regs in the PHY will be inaccessible.	*/
			/* This is to guarantee max power savings on Powerbooks and			*/
			/* to eliminate damage to Broadcom PHYs.							*/
	
		WRITE_REGISTER( MIFConfiguration, kMIFConfiguration_BB_Mode );	// bit bang mode
	
		WRITE_REGISTER( MIFBitBangClock,		0x0000 );
		WRITE_REGISTER( MIFBitBangData,			0x0000 );
		WRITE_REGISTER( MIFBitBangOutputEnable, 0x0000 );
		WRITE_REGISTER( XIFConfiguration,		kXIFConfiguration_GMIIMODE
											 |  kXIFConfiguration_MII_Int_Loopback );
		val32 = READ_REGISTER( XIFConfiguration );	/// ??? make sure it takes.
	}// end of non-WOL case

	return;
}/* end stopPHY */


	// start the PHY

void UniNEnet::startPHY()
{
	UInt32	  val32;
	UInt16	  val16;


	ELG( this, fPHYType, 'Phy+', "startPHY" );

	fTxConfiguration |= kTxConfiguration_Tx_DMA_Enable;
	WRITE_REGISTER( TxConfiguration, fTxConfiguration );

	val32 = READ_REGISTER( RxConfiguration );
	WRITE_REGISTER( RxConfiguration, val32 | kRxConfiguration_Rx_DMA_Enable );

	fTxMACConfiguration |= kTxMACConfiguration_TxMac_Enable;
	WRITE_REGISTER( TxMACConfiguration, fTxMACConfiguration );

	val32  = READ_REGISTER( RxMACConfiguration );	/// ??? use fRxMACConfiguration?
	val32 |= kRxMACConfiguration_Rx_Mac_Enable | kRxMACConfiguration_Hash_Filter_Enable;
	if ( fIsPromiscuous )
		 val32 &= ~kRxMACConfiguration_Strip_FCS;
	else val32 |=  kRxMACConfiguration_Strip_FCS;

	WRITE_REGISTER( RxMACConfiguration, val32 );

		// Set flag to RxMACEnabled somewhere??

		/* These registers are only for the Broadcom 5201.
		   We write the auto low power mode bit here because if we do it earlier
		   and there is no link then the xcvr registers become unclocked and
		   unable to be written
		 */
	if ( fPHYType == 0x5201 )
	{
			// Ask Enrique why the following 2 lines are not necessary in OS 9.
			// These 2 lines should take the PHY out of superisolate mode.
		 	// All MII inputs are ignored until the PHY is out of isolate mode.

		miiReadWord( &val16, MII_BCM5201_MULTIPHY );
		miiWriteWord( val16 & ~MII_BCM5201_MULTIPHY_SUPERISOLATE, MII_BCM5201_MULTIPHY );

#if 0
			// Automatically go into low power mode if no link
		miiReadWord( &val16, MII_BCM5201_AUXMODE2 );
		miiWriteWord( val16 | MII_BCM5201_AUXMODE2_LOWPOWER, MII_BCM5201_AUXMODE2 );
#endif
	}

		// WARNING... this code is untested on gigabit Broadcom 5400 PHY,
		// there should be a case to handle it for MII_CONTROL_POWERDOWN bit
		// here, unless it is unnecessary after a hardware reset

	WRITE_REGISTER( RxKick, fRxRingElements - 4 );
//	}
	return;
}/* end startPHY */


	/*-------------------------------------------------------------------------
	 * Assert the reset pin on the PHY momentarily to initialize it, and also
	 * to bring the PHY out of low-power mode.
	 *
	 *-------------------------------------------------------------------------*/

bool UniNEnet::hardwareResetPHY()
{
	IOReturn	result;


	result = keyLargo->callPlatformFunction( keyLargo_resetUniNEthernetPhy, false, 0, 0, 0, 0 );
	ELG( keyLargo, result, 'RPhy', "hardwareResetPHY" );
///	if ( result != kIOReturnSuccess )
///		return false;

	phyId = fK2 ? 1 : 0;
//	if ( phyId != 0xFF )
	{		// If PHY location is known, clear Powerdown and reset:
		miiWriteWord( MII_CONTROL_RESET, MII_CONTROL );
		IOSleep( 10 );
	}
	return true;	/// return value not used.
}/* end hardwareResetPHY */



#ifdef NOT_YET
IOReturn UniNEnet::powerStateWillChangeTo(	IOPMPowerFlags	flags,
											UInt32			stateNumber,
											IOService*		policyMaker )
{
	IOReturn	rc = IOPMAckImplied;

	ELG( IOThreadSelf(), (stateNumber << 16) | (flags & 0xFFFF), 'Wil1', "powerStateWillChangeTo - before calling superclass." );
	rc = super::powerStateWillChangeTo( flags, stateNumber, policyMaker );
	ELG( stateNumber, rc, 'Wil2', "powerStateWillChangeTo - after calling superclass." );
	return rc;
}/* end powerStateWillChangeTo */


IOReturn UniNEnet::powerStateDidChangeTo(	IOPMPowerFlags	flags,
											UInt32			stateNumber,
											IOService*		policyMaker )
{
	IOReturn	rc = IOPMAckImplied;


	ELG( IOThreadSelf(), (stateNumber << 16) | (flags & 0xFFFF), 'Did1', "powerStateDidChangeTo - before calling superclass." );
	rc = super::powerStateDidChangeTo( flags, stateNumber, policyMaker );
	ELG( stateNumber, rc, 'Did2', "powerStateDidChangeTo - after calling superclass." );
	return rc;
}/* end powerStateDidChangeTo */
#endif // NOT_YET
