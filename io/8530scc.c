/*********************************************************************

	8530scc.c

	Zilog 8530 SCC (Serial Control Chip) code
	
*********************************************************************/


#include "../config.h"
#include "8530scc.h"
#include "../mmu/mmu.h"

/*
	SCCEMDEV.c

	Copyright (C) 2004 Philip Cummins, Weston Pawlowski, Paul C. Pratt

	You can redistribute this file and/or modify it under the terms
	of version 2 of the GNU General Public License as published by
	the Free Software Foundation.  You should have received a copy
	of the license along with this file; see the file COPYING.

	This file is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	license for more details.
*/

/*
	Serial Communications Controller EMulated DEVice

	Emulates the Z8530 SCC found in the Mac Plus.
		But only the minimum amount needed to emulate
		normal operation in a Mac Plus with nothing
		connected to the serial ports.
		(and not even that much is complete yet)

	This code adapted from "SCC.c" in vMac by Philip Cummins.
	With additional code by Weston Pawlowski from the Windows
	port of vMac.
²
	Further information was found in the
	"Zilog SCC/ESCC User's Manual".
*/

/* If char has more then 8 bits, good night. */
typedef unsigned char ui3b;
typedef signed char si3b;

/* ui4b should be two byte unsigned integer */
typedef unsigned short ui4b;

/* si4b should be two byte signed integer */
typedef short si4b;

/* ui5b should be four byte unsigned integer */
typedef unsigned long ui5b;

/* si5b should be four byte signed integer */
typedef long si5b;

typedef ui5b CPTR;

#define blnr int
#define trueblnr 1
#define falseblnr 0

#define nullpr ((void *) 0)

typedef ui3b *ui3p;
#define anyp ui3p

/* pascal string, single byte characters */
#define ps3p ui3p

#define LOCALVAR static
#define GLOBALVAR
#define EXPORTVAR(t, v) extern t v;

#define LOCALFUNC static
#define FORWARDFUNC LOCALFUNC
#define GLOBALFUNC
#define EXPORTFUNC extern
#define IMPORTFUNC EXPORTFUNC
#define TYPEDEFFUNC typedef

#define LOCALPROC LOCALFUNC void
#define GLOBALPROC GLOBALFUNC void
#define EXPORTPROC EXPORTFUNC void
#define IMPORTPROC IMPORTFUNC void
#define FORWARDPROC FORWARDFUNC void
#define TYPEDEFPROC TYPEDEFFUNC void

#define ReportAbnormal(S) IDLE_TRACE(S)

/* Just to make things a little easier */
#define Bit0 1
#define Bit1 2
#define Bit2 4
#define Bit3 8
#define Bit4 16
#define Bit5 32
#define Bit6 64
#define Bit7 128

/* SCC Interrupts */
#define SCC_A_Rx       8 /* Rx Char Available */
#define SCC_A_Rx_Spec  7 /* Rx Special Condition */
#define SCC_A_Tx_Empty 6 /* Tx Buffer Empty */
#define SCC_A_Ext      5 /* External/Status Change */
#define SCC_B_Rx       4 /* Rx Char Available */
#define SCC_B_Rx_Spec  3 /* Rx Special Condition */
#define SCC_B_Tx_Empty 2 /* Tx Buffer Empty */
#define SCC_B_Ext      1 /* External/Status Change */

ui3b SCCInterruptRequest;

typedef struct {
	blnr TxEnable;
	blnr RxEnable;
	blnr TxIE; /* Transmit Interrupt Enable */
	blnr TxUnderrun;
	blnr SyncHunt;
	blnr TxIP; /* Transmit Interrupt Pending */
	/* though should behave as went false
	for an instant when write to transmit buffer */
	blnr TxBufferEmpty;
	blnr AllSent;
	blnr CTS; /* input pin, unattached, so false? */
	blnr DCD; /* Data Carrier Detect */
		/* input pin for mouse interrupts. but since
		not emulating mouse this way, leave false. */
	blnr RxChrAvail;
	blnr RxOverrun;
	blnr CRCFramingErr;
	blnr EndOfFrame;
	blnr ParityErr;
	blnr ZeroCount;
	blnr BreakAbort;
	blnr SyncHuntIE;
	blnr CTS_IE;
	blnr DCD_IE;
	blnr BreakAbortIE;
	ui3b BaudLo;
	ui3b BaudHi;
} Channel_Ty;

typedef struct {
	Channel_Ty a[2]; /* 0 = channel A, 1 = channel B */
	int SCC_Interrupt_Type;
	int PointerBits;
	ui3b InterruptVector;
	blnr MIE; /* master interrupt enable */
	blnr StatusHiLo;
} SCC_Ty;

LOCALVAR SCC_Ty SCC;

LOCALVAR int ReadPrint;
LOCALVAR int ReadModem;

LOCALVAR int ModemPort=0;
LOCALVAR int PrintPort=0;


EXPORTFUNC blnr SCC_InterruptsEnabled(void)
{
	return SCC.MIE;
}

/* ---- */

LOCALPROC CheckSCCInterruptFlag(void)
{
	ui3b NewSCCInterruptRequest;
	blnr ReceiveAInterrupt = falseblnr
		/*
			also dependeds on WR1, bits 3 and 4, but
			this doesn't change that it's all false
		*/
		| SCC.a[0].RxChrAvail
		| SCC.a[0].RxOverrun
		| SCC.a[0].CRCFramingErr
		| SCC.a[0].EndOfFrame
		| SCC.a[0].ParityErr
		;
	blnr TransmitAInterrupt = SCC.a[0].TxBufferEmpty;
	/*
		but probably looking for transitions not
		current value
	*/
	blnr ExtStatusAInterrupt = 0
		| SCC.a[0].ZeroCount
		/* probably want transition for these, not value */
		| SCC.a[0].DCD /* DCD IE always true */
		| SCC.a[0].CTS /* would depend on CTS_IE */
		| SCC.a[0].SyncHunt /* SyncHuntIE usually false */
		| SCC.a[0].TxUnderrun /* Tx underrun/EOM IE always false */
		| SCC.a[0].BreakAbort
		;

	if (! SCC.MIE) {
		SCC.SCC_Interrupt_Type = 0;
	} else if (SCC.a[0].TxIP && SCC.a[0].TxIE) {
		SCC.SCC_Interrupt_Type = SCC_A_Tx_Empty;
	} else if (SCC.a[1].TxIP && SCC.a[1].TxIE) {
		SCC.SCC_Interrupt_Type = SCC_B_Tx_Empty;
	} else {
		SCC.SCC_Interrupt_Type = 0;
	}
	NewSCCInterruptRequest = (SCC.SCC_Interrupt_Type != 0) ? 1 : 0;

	if (NewSCCInterruptRequest != SCCInterruptRequest) {
		SCCInterruptRequest = NewSCCInterruptRequest;
#ifdef SCCinterruptChngNtfy
 		SCCinterruptChngNtfy();
#endif
	}
}

LOCALPROC SCC_InitChannel(int chan)
{
	/* anything not done by ResetChannel */

	SCC.a[chan].SyncHunt = trueblnr;
	SCC.a[chan].DCD = falseblnr; /* input pin, reset doesn't change */
	SCC.a[chan].CTS = falseblnr; /* input pin, reset doesn't change */
	SCC.a[chan].AllSent = trueblnr;
	SCC.a[chan].BaudLo = 0;
	SCC.a[chan].BaudHi = 0;
	SCC.a[chan].BreakAbort = falseblnr;
}

LOCALPROC SCC_ResetChannel(int chan)
{
/* RR 0 */
	SCC.a[chan].RxChrAvail = falseblnr;
	SCC.a[chan].ZeroCount = falseblnr;
	SCC.a[chan].TxBufferEmpty = trueblnr;
	SCC.a[chan].TxUnderrun = trueblnr;
/* RR 1 */
	SCC.a[chan].ParityErr = falseblnr;
	SCC.a[chan].RxOverrun = falseblnr;
	SCC.a[chan].CRCFramingErr = falseblnr;
	SCC.a[chan].EndOfFrame = falseblnr;
/* RR 3 */
	SCC.a[chan].TxIP = falseblnr;

	SCC.a[chan].TxEnable = falseblnr;
	SCC.a[chan].RxEnable = falseblnr;
	SCC.a[chan].TxIE = falseblnr;
	SCC.a[chan].DCD_IE = trueblnr;
	SCC.a[chan].CTS_IE = trueblnr;
	SCC.a[chan].SyncHuntIE = trueblnr;
	SCC.a[chan].BreakAbortIE = trueblnr;

	SCC.PointerBits = 0;

	if (chan != 0) {
		ReadPrint = 0;
	} else {
		ReadModem = 0;
	}
}

GLOBALPROC SCC_Reset(void)
{
//	SCCwaitrq = 1;

	SCC.SCC_Interrupt_Type = 0;

	SCCInterruptRequest = 0;
	SCC.PointerBits = 0;
	SCC.MIE = falseblnr;
	SCC.InterruptVector = 0;
	SCC.StatusHiLo = falseblnr;

	SCC_InitChannel(1);
	SCC_InitChannel(0);

	SCC_ResetChannel(1);
	SCC_ResetChannel(0);
}

LOCALPROC SCC_Interrupt(int Type)
{
	if (SCC.MIE) { /* Master Interrupt Enable */

		if (Type > SCC.SCC_Interrupt_Type) {
			SCC.SCC_Interrupt_Type = Type;
		}

		CheckSCCInterruptFlag();
	}
}

LOCALPROC SCC_Int(void)
{
	/* This should be called at regular intervals */

	/* Turn off Sync/Hunt Mode */
	if (SCC.a[0].SyncHunt) {
		SCC.a[0].SyncHunt = falseblnr;

#ifdef _SCC_Debug2
		vMac_Message("SCC_Int: Disable Sync/Hunt on A");
#endif

		if (SCC.a[0].SyncHuntIE) {
			SCC_Interrupt(SCC_A_Ext);
		}
	}
	if (SCC.a[1].SyncHunt) {
		SCC.a[1].SyncHunt = falseblnr;

#ifdef _SCC_Debug2
		vMac_Message("SCC_Int: Disable Sync/Hunt on B");
#endif

		if (SCC.a[1].SyncHuntIE) {
			SCC_Interrupt(SCC_B_Ext);
		}
	}

#if 0
	/* Check for incoming data */
	if (ModemPort)
	{
		if (! SCC.a[0].RxEnable) { /* Rx Disabled */
			ReadModem = 0;
		}

		if ((ModemBytes > 0) && (ModemCount > ModemBytes - 1))
		{
			SCC.a[0].RxChrAvail = falseblnr;
			ReadModem = ModemBytes = ModemCount = 0;
		}

		if (ReadModem)
		{
			ReadModem = 2;

			SCC.a[0].RxChrAvail = trueblnr;

			if (SCC.a[0].WR[0] & Bit5 && ! (SCC.a[0].WR[0] & (Bit4 | Bit3))) /* Int on next Rx char */
				SCC_Interrupt(SCC_A_Rx);
			else if (SCC.a[0].WR[1] & Bit3 && ! (SCC.a[0].WR[1] & Bit4)) /* Int on first Rx char */
				SCC_Interrupt(SCC_A_Rx);
			else if (SCC.a[0].WR[1] & Bit4 && ! (SCC.a[0].WR[1] & Bit3)) /* Int on all Rx chars */
				SCC_Interrupt(SCC_A_Rx);
		}
	}
	if (PrintPort)
	{
		if (! SCC.a[1].RxEnable) { /* Rx Disabled */
			ReadPrint = 0;
		}

		if ((PrintBytes > 0) && (PrintCount > PrintBytes - 1))
		{
			SCC.a[1].RxChrAvail = falseblnr;
			ReadPrint = PrintBytes = PrintCount = 0;
		}

		if (ReadPrint)
		{
			ReadPrint = 2;

			SCC.a[1].RxChrAvail = trueblnr;

			if (SCC.a[1].WR[0] & Bit5 && ! (SCC.a[1].WR[0] & (Bit4 | Bit3))) /* Int on next Rx char */
				SCC_Interrupt(SCC_B_Rx);
			else if (SCC.a[1].WR[1] & Bit3 && ! (SCC.a[1].WR[1] & Bit4)) /* Int on first Rx char */
				SCC_Interrupt(SCC_B_Rx);
			else if (SCC.a[1].WR[1] & Bit4 && ! (SCC.a[1].WR[1] & Bit3)) /* Int on all Rx chars */
				SCC_Interrupt(SCC_B_Rx);
		}
	}
#endif
}

LOCALFUNC ui3b SCC_GetReg(int chan, ui3b SCC_Reg)
{
    IDLE_INIT_FUNC("SCC_GetReg()");
    IDLE_DEBUG("called chan=%d SCC_Reg=x%02x",chan,SCC_Reg);
	switch (SCC_Reg) {
		case 4: /* same as RR0 */
			ReportAbnormal("RR 4");
			/* fall through */
		case 0:
			/* happens on boot always */
			return 0
				| (SCC.a[chan].BreakAbort ? (1 << 7) : 0)
				| (SCC.a[chan].TxUnderrun ? (1 << 6) : 0)
				| (SCC.a[chan].CTS ? (1 << 5) : 0)
				| (SCC.a[chan].SyncHunt ? (1 << 4) : 0)
				| (SCC.a[chan].DCD ? (1 << 3) : 0)
				| (SCC.a[chan].TxBufferEmpty ? (1 << 2) : 0)
				| (SCC.a[chan].ZeroCount ? (1 << 1) : 0)
				| (SCC.a[chan].RxChrAvail ? (1 << 0) : 0)
				;
			break;

		case 5: /* same as RR1 */
			ReportAbnormal("RR 5");
			/* fall through */
		case 1:
			/* happens in MacCheck */
			return Bit2 | Bit1
				| (SCC.a[chan].AllSent ? (1 << 0) : 0)
				| (SCC.a[chan].ParityErr ? (1 << 4) : 0)
				| (SCC.a[chan].RxOverrun ? (1 << 5) : 0)
				| (SCC.a[chan].CRCFramingErr ? (1 << 6) : 0)
				| (SCC.a[chan].EndOfFrame ? (1 << 7) : 0)
				;
			break;

		case 6: /* same as RR2 */
			ReportAbnormal("RR 6");
			/* fall through */
		case 2:
			/* happens in MacCheck */
			/* happens in Print to ImageWriter */
			{
				ui3b TempData = SCC.InterruptVector;
				if (chan != 0) { /* B Channel */
					if (SCC.StatusHiLo) {
						/* Status High */
						TempData = TempData & (Bit0 | Bit1 | Bit2 | Bit3 | Bit7);

						switch (SCC.SCC_Interrupt_Type) {
							case SCC_A_Rx:
								TempData |= Bit4 | Bit5;
								break;

							case SCC_A_Rx_Spec:
								TempData |= Bit4 | Bit5 | Bit6;
								break;

							case SCC_A_Tx_Empty:
								TempData |= Bit4;
								break;

							case SCC_A_Ext:
								TempData |= Bit4 | Bit6;
								break;

							case SCC_B_Rx:
								TempData |= Bit5;
								break;

							case SCC_B_Rx_Spec:
								TempData |= Bit5 | Bit6;
								break;

							case SCC_B_Tx_Empty:
								TempData |= 0;
								break;

							case SCC_B_Ext:
								TempData |= Bit6;
								break;

							default:
								TempData |= Bit5 | Bit6;
								break;
						}
					} else
					{
						/* Status Low */
						TempData = TempData & (Bit0 | Bit4 | Bit5 | Bit6 | Bit7);

						switch (SCC.SCC_Interrupt_Type) {
							case SCC_A_Rx:
								TempData |= Bit3 | Bit2;
								break;

							case SCC_A_Rx_Spec:
								TempData |= Bit3 | Bit2 | Bit1;
								break;

							case SCC_A_Tx_Empty:
								TempData |= Bit3;
								break;

							case SCC_A_Ext:
								TempData |= Bit3 | Bit1;
								break;

							case SCC_B_Rx:
								TempData |= Bit2;
								break;

							case SCC_B_Rx_Spec:
								TempData |= Bit2 | Bit1;
								break;

							case SCC_B_Tx_Empty:
								TempData |= 0;
								break;

							case SCC_B_Ext:
								TempData |= Bit1;
								break;

							default:
								TempData |= Bit2 | Bit1;
								break;
						}
					}

					SCC.SCC_Interrupt_Type = 0; 

				}
				return TempData;
			}
			break;

		case 7: /* same as RR3 */
			ReportAbnormal("RR 7");
			/* fall through */
		case 3:
			ReportAbnormal("RR 3");
			if (chan == 0) {
				return 0
					| (SCC.a[1].TxIP ? (1 << 1) : 0)
					| (SCC.a[0].TxIP ? (1 << 4) : 0)
					;
			} else {
				return 0;
			}
			break;

		case 8: /* Receive Buffer */
			/* happens on boot with appletalk on */
			if (SCC.a[chan].RxEnable) { /* Rx Enable */
				ReportAbnormal("read rr8 when RxEnable");

				/* Input 1 byte from Modem Port/Printer into Data */
			} else {
				/* happens on boot with appletalk on */
			}
			break;

		case 10:
			/* happens on boot with appletalk on */
			break;

		case 12:
			ReportAbnormal("RR 12");
			return SCC.a[chan].BaudLo;
			break;

		case 9: /* same as RR13 */
			ReportAbnormal("RR 9");
			/* fall through */
		case 13:
			ReportAbnormal("RR 13");
			return SCC.a[chan].BaudHi;
			break;

		case 14:
			ReportAbnormal("RR 14");
			break;

		case 11: /* same as RR15 */
			ReportAbnormal("RR 11");
			/* fall through */
		case 15:
			ReportAbnormal("RR 15");
			return 0
				| (SCC.a[chan].DCD_IE ? Bit3 : 0)
				| (SCC.a[chan].SyncHuntIE ? Bit4 : 0)
				| (SCC.a[chan].CTS_IE ? Bit5 : 0)
				| (SCC.a[chan].BreakAbortIE ? Bit7 : 0)
				;
			break;
	}
	return 0;
}

LOCALPROC SCC_PutReg(int Data, int chan, ui3b SCC_Reg)
{
    IDLE_INIT_FUNC("SCC_PutReg()");
    IDLE_TRACE("called chan=%d SCC_Reg=x%02x",chan,SCC_Reg);
	switch (SCC_Reg) {
		case 0:
			switch ((Data >> 6) & 3) {
				case 1: /* Reset Rx CRC Checker */
					ReportAbnormal("Reset Rx CRC Checker");
					break;
				case 2: /* Reset Tx CRC Generator */
					ReportAbnormal("Reset Tx CRC Generator");
					/* happens on boot with appletalk on */
					break;
				case 3: /* Reset Tx Underrun/EOM Latch */
					ReportAbnormal("Reset Tx Underrun/EOM Latch");
					/* happens on boot with appletalk on */
					if (SCC.a[chan].TxEnable) /* Tx Enabled */
					{
						SCC.a[chan].TxUnderrun = falseblnr;

#if 0
						if (SCC.a[chan].WR[10] & Bit2) /* Abort/Flag on Underrun */
						{
							/* Send Abort */
							SCC.a[chan].TxUnderrun = trueblnr;
							SCC.a[chan].TxBufferEmpty = trueblnr;

							/* Send Flag */
						}
#endif
					}
					break;
				case 0:
				default:
					/* Null Code */
					break;
			}
			SCC.PointerBits = Data & 0x07;
			switch ((Data >> 3) & 7) {
				case 1: /* Point High */
					SCC.PointerBits |= 8;
					break;
				case 2: /* Reset Ext/Status Ints */
					/* happens on boot always */
					SCC.a[chan].SyncHunt = falseblnr;
					SCC.a[chan].TxUnderrun = falseblnr;
					SCC.a[chan].ZeroCount = falseblnr;
					SCC.a[chan].BreakAbort = falseblnr;
					break;
				case 3: /* Send Abort (SDLC) */
					ReportAbnormal("Send Abort (SDLC)");
					SCC.a[chan].TxUnderrun = trueblnr;
					SCC.a[chan].TxBufferEmpty = trueblnr;
					break;
				case 4: /* Enable Int on next Rx char */
					ReportAbnormal("Enable Int on next Rx char");
					/* happens in MacCheck */
					break;
				case 5: /* Reset Tx Int Pending */
					ReportAbnormal("Reset Tx Int Pending");
					/* happens in MacCheck */
					/* happens in Print to ImageWriter */
					SCC.a[chan].TxIP = falseblnr;
					CheckSCCInterruptFlag();
					break;
				case 6: /* Error Reset */
					ReportAbnormal("Error Reset");		
					/* happens on boot with appletalk on */
					SCC.a[chan].ParityErr = falseblnr;
					SCC.a[chan].RxOverrun = falseblnr;
					SCC.a[chan].CRCFramingErr = falseblnr;
					break;
				case 7: /* Reset Highest IUS */
					ReportAbnormal("Reset Highest IUS");
					break;
				case 0:
				default:
					/* Null Code */
					break;
			}
			break;
		case 1:
			if ((Data & Bit0) == 0) { /* EXT INT Enable */
				/* happens in MacCheck */
				/* happens in Print to ImageWriter */
			}

			{
				blnr NewTxIE = (Data & Bit1) != 0; /* Tx Int Enable */
				if (SCC.a[chan].TxIE != NewTxIE) {
					/* happens in MacCheck */
					/* happens in Print to ImageWriter */
					SCC.a[chan].TxIE = NewTxIE;
					CheckSCCInterruptFlag();
				}
			}

			if ((Data & Bit2) != 0) { /* Parity is special condition */
				/* happens in MacCheck */
				/* happens in Print to ImageWriter */
			}
			switch ((Data >> 3) & 3) {
				case 0: /* Rx INT Disable */
					/* happens on boot always */
					break;
				case 1: /* Rx INT on 1st char or special condition */
					/* happens on boot with appletalk on */
					break;
				case 2: /* INT on all Rx char or special condition */
					/* happens in MacCheck */
					/* happens in Print to ImageWriter */
					break;
				case 3: /* Rx INT on special condition only */
					ReportAbnormal("Rx INT on special condition only");
					break;
			}
			if ((Data & Bit5) != 0) { /* Wait/DMA request on receive/transmit */
				/* happens in MacCheck */
			}
			if ((Data & Bit6) != 0) { /* Wait/DMA request function */
				/* happens in MacCheck */
			}
			if ((Data & Bit7) != 0) { /* Wait/DMA request enable */
				/* happens in MacCheck */
			}
			break;
		case 2: /* Interrupt Vector (same on RR & WR) */
			/*
				Only 1 interrupt vector for the SCC, which
				is stored in channels A and B. B is modified
				when read.
			*/

			/* happens on boot always */
			SCC.InterruptVector = Data;
			if ((Data & Bit0) != 0) { /* interrupt vector 0 */
				ReportAbnormal("interrupt vector 0");
			}
			if ((Data & Bit1) != 0) { /* interrupt vector 1 */
				ReportAbnormal("interrupt vector 1");
			}
			if ((Data & Bit2) != 0) { /* interrupt vector 2 */
				ReportAbnormal("interrupt vector 2");
			}
			if ((Data & Bit3) != 0) { /* interrupt vector 3 */
				ReportAbnormal("interrupt vector 3");
			}
			if ((Data & Bit4) != 0) { /* interrupt vector 4 */
				ReportAbnormal("interrupt vector 4");
			}
			if ((Data & Bit5) != 0) { /* interrupt vector 5 */
				ReportAbnormal("interrupt vector 5");
			}
			if ((Data & Bit6) != 0) { /* interrupt vector 6 */
				ReportAbnormal("interrupt vector 6");
			}
			if ((Data & Bit7) != 0) { /* interrupt vector 7 */
				ReportAbnormal("interrupt vector 7");
			}
			break;
		case 3:
#if 0
			SCC_SetBitsPerChar(chan, (Data >> 6) & 3);
#endif
				/* 0: Rx 5 Bits/Character */
				/* 1: Rx 7 Bits/Character */
				/* 2: Rx 6 Bits/Character */
				/* 3: Rx 8 Bits/Character */
			if ((Data & Bit5) != 0) { /* Auto Enables */
				/* use DCD input as receiver enable,
				and set RTS output when transmit buffer empty */
				ReportAbnormal("Auto Enables");
			}
			if ((Data & Bit4) != 0) { /* Enter Hunt Mode */
				/* happens on boot with appletalk on */
				if (! (SCC.a[chan].SyncHunt)) {
					SCC.a[chan].SyncHunt = trueblnr;

					if (SCC.a[chan].SyncHuntIE) {
						SCC_Interrupt((chan == 0) ? SCC_A_Ext : SCC_B_Ext);
					}
				}
			}
			if ((Data & Bit3) != 0) { /* Rx CRC Enable */
				/* happens on boot with appletalk on */
			}
			if ((Data & Bit2) != 0) { /* Addr Search Mode (SDLC) */
				/* happens on boot with appletalk on */
			}
			if ((Data & Bit1) != 0) { /* Sync Char Load Inhibit */
				/* happens on boot with appletalk on */
			}

			SCC.a[chan].RxEnable = (Data & Bit0) != 0; /* Rx Enable */
			if ((Data & Bit0) != 0) { /* Rx Enable */
				/* true on boot with appletalk on */
				/* true on Print to ImageWriter */
			}

			break;
		case 4:
			if (((Data >> 2) & 3) == 0) {
				/* happens on boot with appletalk on */
			}
#if 0
			SCC_SetStopBits(chan, (Data >> 2) & 3);
				/* 0: Sync Modes Enable */
				/* 1: 1 Stop Bit */
				/* 2: 1 1/2 Stop Bits */
				/* 3: 2 Stop Bits */
			SCC_SetParity(chan, Data & 3);
				/* Bit0 = 1, Bit1 = 1: even parity */
				/* Bit0 = 1, Bit1 = 0: odd parity */
				/* Bit0 = 0: no parity */
#endif
			switch ((Data >> 4) & 3) {
				case 0: /* 8 bit sync char */
					/* happens on boot always */
					break;
				case 1: /* 16 bit sync char */
					ReportAbnormal("16 bit sync char");
					break;
				case 2: /* SDLC MODE */
					/* happens on boot with appletalk on */
					break;
				case 3: /* External sync mode */
					ReportAbnormal("External sync mode");
					break;
			}
			switch ((Data >> 6) & 3) {
				case 0: /* X1 clock mode */
					/* happens on boot with appletalk on */
					ReportAbnormal("X1 clock mode");
					break;
				case 1: /* X16 clock mode */
					/* happens on boot always */
					ReportAbnormal("X16 clock mode");
					break;
				case 2: /* X32 clock mode */
					ReportAbnormal("X32 clock mode");
					break;
				case 3: /* X64 clock mode */
					ReportAbnormal("X64 clock mode");
					break;
			}
			break;
		case 5:
			/* happens on boot with appletalk on */
			/* happens in Print to ImageWriter */
			if ((Data & Bit0) != 0) { /* Tx CRC enable */
					ReportAbnormal("Tx CRC enable");
				/* both values on boot with appletalk on */
			}
			if ((Data & Bit1) == 0) { /* RTS */
					ReportAbnormal("RTS");
				/* both values on boot with appletalk on */
				/* value of Request To Send output pin, when
				Auto Enable is off */
			}
			if ((Data & Bit2) != 0) { /* SDLC/CRC-16 */
				ReportAbnormal("SDLC/CRC-16");
			}

			SCC.a[chan].TxEnable = (Data & Bit3) != 0; /* Tx Enable */
			if ((Data & Bit3) != 0) { /* Tx Enable */
				/* happens on boot with appletalk on */
				/* happens in Print to ImageWriter */
			}

			if ((Data & Bit4) != 0) { /* send break */
				/* happens in Print to LaserWriter 300 */
			}
			switch ((Data >> 5) & 3) {
				case 0: /* Tx 5 bits/char */
					ReportAbnormal("Tx 5 bits/char");
					break;
				case 1: /* Tx 7 bits/char */
					ReportAbnormal("Tx 7 bits/char");
					break;
				case 2: /* Tx 6 bits/char */
					ReportAbnormal("Tx 6 bits/char");
					break;
				case 3: /* Tx 8 bits/char */
					ReportAbnormal("Tx 8 bits/char");
					/* happens on boot with appletalk on */
					/* happens in Print to ImageWriter */
					break;
			}
			if ((Data & Bit7) == 0) { /* DTR */
					ReportAbnormal("DTR");
				/* happens in MacCheck */
				/* value of Data Terminal Ready output pin,
				when WR14 D2 = 0 (DTR/request function)  */
			}
			break;
		case 6:
			/* happens on boot with appletalk on */
			break;
		case 7:
			/* happens on boot with appletalk on */
			break;
		case 8: /* Transmit Buffer */
			/* happens on boot with appletalk on */
			/* happens in Print to ImageWriter */
			if (SCC.a[chan].TxEnable) { /* Tx Enable */
				/* Output (Data) to Modem(B) or Printer(A) Port */

				/* happens on boot with appletalk on */
				SCC.a[chan].TxBufferEmpty = trueblnr;
				SCC.a[chan].TxUnderrun = trueblnr; /* underrun ? */

				SCC.a[chan].TxIP = trueblnr;
				CheckSCCInterruptFlag();
			} else {
				ReportAbnormal("write when Transmit Buffer not Enabled");
				SCC.a[chan].TxBufferEmpty = falseblnr;
			}
			break;
		case 9: /* Only 1 WR9 in the SCC */

			if ((Data & Bit0) != 0) { /* VIS */
				ReportAbnormal("VIS");
			}
			if ((Data & Bit1) == 0) { /* NV */
				/* has both values on boot always */
			}
			if ((Data & Bit2) != 0) { /* DLC */
				ReportAbnormal("DLC");
			}

			{
				blnr NewMIE = (Data & Bit3) != 0;
					/* has both values on boot always */
				if (SCC.MIE != NewMIE) {
					SCC.MIE = NewMIE;
					CheckSCCInterruptFlag();
				}
			}

			SCC.StatusHiLo = (Data & Bit4) != 0;
			if ((Data & Bit5) != 0) { /* WR9 b5 should be 0 */
				ReportAbnormal("WR9 b5 should be 0");
			}

			switch ((Data >> 6) & 3) {
				case 1: /* Channel Reset B */
					/* happens on boot always */
					SCC_ResetChannel(1);
					CheckSCCInterruptFlag();
					break;
				case 2: /* Channel Reset A */
					/* happens on boot always */
					SCC_ResetChannel(0);
					CheckSCCInterruptFlag();
					break;
				case 3: /* Force Hardware Reset */
					ReportAbnormal("SCC_Reset");
					SCC_Reset();
					CheckSCCInterruptFlag();
					break;
				case 0: /* No Reset */
				default:
					break;
			}

			break;
		case 10:
			/* happens on boot with appletalk on */
			/* happens in Print to ImageWriter */
			if ((Data & Bit0) != 0) { /* 6 bit/8 bit sync */
				ReportAbnormal("6 bit/8 bit sync");
			}
			if ((Data & Bit1) != 0) { /* loop mode */
				ReportAbnormal("loop mode");
			}
			if ((Data & Bit2) != 0) { /* abort/flag on underrun */
				ReportAbnormal("abort/flag on underrun");
			}
			if ((Data & Bit3) != 0) { /* mark/flag idle */
				ReportAbnormal("mark/flag idle");
			}
			if ((Data & Bit4) != 0) { /* go active on poll */
				ReportAbnormal("go active on poll");
			}
			switch ((Data >> 5) & 3) {
				case 0: /* NRZ */
					/* happens in MacCheck */
					/* happens in Print to ImageWriter */
					break;
				case 1: /* NRZI */
					ReportAbnormal("NRZI");
					break;
				case 2: /* FM1 */
					ReportAbnormal("FM1");
					break;
				case 3: /* FM0 */
					/* happens on boot with appletalk on */
					break;
			}
			if ((Data & Bit7) != 0) { /* CRC preset I/O */
				/* false happens in MacCheck */
				/* true happens in Print to ImageWriter */
			}
			break;
		case 11:
			/* happens on boot with appletalk on */
			/* happens in Print to ImageWriter */
			/* happens in MacCheck */
			switch (Data & 3) {
				case 0: /* TRxC OUT = XTAL output */
					/* happens on boot with appletalk on */
					/* happens in Print to ImageWriter */
					/* happens in MacCheck */
					break;
				case 1: /* TRxC OUT = transmit clock */
					ReportAbnormal("TRxC OUT = transmit clock");
					break;
				case 2: /* TRxC OUT = BR generator output */
					ReportAbnormal("TRxC OUT = BR generator output");
					break;
				case 3: /* TRxC OUT = dpll output */
					ReportAbnormal("TRxC OUT = dpll output");
					break;
			}
			if ((Data & Bit2) != 0) { /* TRxC O/I */
				ReportAbnormal("TRxC O/I");
			}
			switch ((Data >> 3) & 3) {
				case 0: /* transmit clock = RTxC pin */
					ReportAbnormal("transmit clock = RTxC pin");
					break;
				case 1: /* transmit clock = TRxC pin */
					/* happens in Print to LaserWriter 300 */
					break;
				case 2: /* transmit clock = BR generator output */
					/* happens on boot with appletalk on */
					/* happens in Print to ImageWriter */
					/* happens in MacCheck */
					break;
				case 3: /* transmit clock = dpll output */
					ReportAbnormal("transmit clock = dpll output");
					break;
			}
			switch ((Data >> 5) & 3) {
				case 0: /* receive clock = RTxC pin */
					ReportAbnormal("receive clock = RTxC pin");
					break;
				case 1: /* receive clock = TRxC pin */
					/* happens in Print to LaserWriter 300 */
					break;
				case 2: /* receive clock = BR generator output */
					/* happens in MacCheck */
					/* happens in Print to ImageWriter */
					break;
				case 3: /* receive clock = dpll output */
					/* happens on boot with appletalk on */
					break;
			}
			if ((Data & Bit7) != 0) { /* RTxC XTAL/NO XTAL */
				ReportAbnormal("RTxC XTAL/NO XTAL");
			}
			break;
		case 12:
			/* happens on boot with appletalk on */
			/* happens in Print to ImageWriter */
			SCC.a[chan].BaudLo = Data;
#if 0
			SCC_SetBaud(chan, SCC.a[chan].BaudLo + (SCC.a[chan].BaudHi << 8));
				/* 380: BaudRate = 300   */
				/*  94: BaudRate = 1200  */
				/*  46: BaudRate = 2400  */
				/*  22: BaudRate = 4800  */
				/*  10: BaudRate = 9600  */
				/*   4: BaudRate = 19200 */
				/*   1: BaudRate = 38400 */
				/*   0: BaudRate = 57600 */
#endif
			break;
		case 13:
			/* happens on boot with appletalk on */
			/* happens in Print to ImageWriter */
			SCC.a[chan].BaudHi = Data;
#if 0
			SCC_SetBaud(chan, SCC.a[chan].BaudLo + (SCC.a[chan].BaudHi << 8));
#endif
			break;
		case 14:
			/* happens on boot with appletalk on */
			if ((Data & Bit0) != 0) { /* BR generator enable */
				/* both values on boot with appletalk on */
				/* true happens in Print to ImageWriter */
			}
			if ((Data & Bit1) != 0) { /* BR generator source */
				ReportAbnormal("BR generator source");
			}
			if ((Data & Bit2) != 0) { /* DTR/request function */
				ReportAbnormal("DTR/request function");
			}
			if ((Data & Bit3) != 0) { /* auto echo */
				ReportAbnormal("auto echo");
			}
			if ((Data & Bit4) != 0) { /* local loopback */
				ReportAbnormal("local loopback");
			}
			switch ((Data >> 5) & 7) {
				case 1: /* enter search mode */
					/* happens on boot with appletalk on */
					break;
				case 2: /* reset missing clock */
					/* happens on boot with appletalk on */
					/*
						should clear Bit 6 and Bit 7 of RR[10], but
						since these are never set, don't need
						to do anything
					*/
					break;
				case 3: /* disable dpll */
					ReportAbnormal("disable dpll");
					/*
						should clear Bit 6 and Bit 7 of RR[10], but
						since these are never set, don't need
						to do anything
					*/
					break;
				case 4: /* set source = br generator */
					ReportAbnormal("set source = br generator");
					break;
				case 5: /* set source = RTxC */
					ReportAbnormal("set source = RTxC");
					break;
				case 6: /* set FM mode */
					/* happens on boot with appletalk on */
					break;
				case 7: /* set NRZI mode */
					ReportAbnormal("set NRZI mode");
					break;
				case 0: /* No Reset */
				default:
					break;
			}
			break;
		case 15:
			/* happens on boot always */
			if ((Data & Bit0) != 0) { /* WR15 b0 should be 0 */
				ReportAbnormal("WR15 b0 should be 0");
			}
			if ((Data & Bit1) != 0) { /* zero count IE */
				ReportAbnormal("zero count IE");
			}
			if ((Data & Bit2) != 0) { /* WR15 b2 should be 0 */
				ReportAbnormal("WR15 b2 should be 0");
			}

			SCC.a[chan].DCD_IE = (Data & Bit3) != 0;

			SCC.a[chan].SyncHuntIE = (Data & Bit4) != 0;

			SCC.a[chan].CTS_IE = (Data & Bit5) != 0;
			if ((Data & Bit5) != 0) { /* CTS_IE */
				/* happens in MacCheck */
				/* happens in Print to ImageWriter */
			}

			if ((Data & Bit6) != 0) { /* Tx underrun/EOM IE */
				ReportAbnormal("Tx underrun/EOM IE");
			}

			SCC.a[chan].BreakAbortIE = (Data & Bit7) != 0;
			if ((Data & Bit7) != 0) { /* BreakAbortIE */
				/* happens in MacCheck */
				/* happens in Print to ImageWriter */
			}

			break;
	}
}

GLOBALFUNC ui5b SCC_Access(ui5b Data, blnr WriteMem, CPTR addr)
{
	ui3b SCC_Reg;
	int chan = (~ addr) & 1;
	if (((addr >> 1) & 1) == 0) {
		/* Channel Control */
		SCC_Reg = SCC.PointerBits;
		SCC.PointerBits = 0;
	} else {
		/* Channel Data */
		SCC_Reg = 8;
	}
	if (WriteMem) {
		SCC_PutReg(Data, chan, SCC_Reg);
	} else {
		Data = SCC_GetReg(chan, SCC_Reg);
	}
	return Data;
}


void scc_access(uint32 address,int* val,int mode,int size) {
     ui3b in,res;
     IDLE_INIT_FUNC("scc_access()");
     IDLE_DEBUG("called address=%06x",address);
     in=*val;
     res=SCC_Access(in,(mode==IO_WRITE),address>>1);
     if (mode!=IO_WRITE) *val=res;
}

void scc_access16(uint32 address,int* val,int mode,int size) {
     ui3b in,res;
     IDLE_INIT_FUNC("scc_access16()");
     IDLE_DEBUG("called address=%06x",address);
     if (size!=IO_WORD) {
                         IDLE_TRACE("void scc access");
                         *val=0xFF;
                         return;
                        }
     in=*val;
     res=SCC_Access(in,(mode==IO_WRITE),address>>1);
     if (mode!=IO_WRITE) *val=res;
}


void scc_init(void)
{
     int i,j,k;
     IDLE_INIT_FUNC("scc_init()");
     for (i=0xD000;i<0xD400;i+=2) 
     {
             registerIoFuncAdr(i+1     ,&scc_access);
             registerIoFuncAdr(i     ,&scc_access16);
     }
     SCC_Reset();
}
