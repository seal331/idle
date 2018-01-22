/*
 * Castaway
 *  (C) 1994 - 2002 Martin Doering, Joachim Hoenig
 *
 * $File$ - debugging fns - CAUTION: this file is a mess!
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * revision history
 *  23.05.2002  JH  FAST1.0.1 code import: KR -> ANSI, restructuring
 *  09.07.2002  JH  Added a disassembler. Traced instructions now
 *                  disassembled.
 *  22.08.2002  JH  Bugfix: Shift instructions now disassembled correctly
 *  29.08.2002  JH  Bugfix: MOVEC disassembled correctly.
 *  03.09.2002  JH  MOVEM register list now disassembled.
 *  08.09.2002  JH  Fixed several disassembler bugs for opcodes 0x0...
 *  16.09.2002  JH  Disassemble LINE A/F, minor fixes.
 *  17.09.2002  JH  Trace internal execution, minor fixes.
 *  08.10.2002  JH  Fixed MOVEP disassembly
 */
typedef signed char     int8;
typedef signed short    int16;
typedef signed long     int32;
typedef unsigned char   uint8;
typedef unsigned short  uint16;
typedef unsigned long   uint32;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
 * 680x0 Disassembler functions (experimental)
 */
char   *disass_cc(uint8 cc)
{
    switch(cc) {
    case 0:
        return "T ";
    case 1:
        return "F ";
    case 2:
        return "HI";
    case 3:
        return "LS";
    case 4:
        return "CC";
    case 5:
        return "CS";
    case 6:
        return "NE";
    case 7:
        return "EQ";
    case 8:
        return "VC";
    case 9:
        return "VS";
    case 0xa:
        return "PL";
    case 0xb:
        return "MI";
    case 0xc:
        return "GE";
    case 0xd:
        return "LT";
    case 0xe:
        return "GT";
    case 0xf:
        return "LE";
    default:
        return "??";
    }
}

char   *disass_areg(uint8 spec)
{
    switch (spec) {
    case 0: return "A0";
    case 1: return "A1";
    case 2: return "A2";
    case 3: return "A3";
    case 4: return "A4";
    case 5: return "A5";
    case 6: return "A6";
    case 7: return "A7";
    default: return "??";
    }
}

char   *disass_dreg(uint8 spec)
{
    switch (spec) {
    case 0: return "D0";
    case 1: return "D1";
    case 2: return "D2";
    case 3: return "D3";
    case 4: return "D4";
    case 5: return "D5";
    case 6: return "D6";
    case 7: return "D7";
    default: return "??";
    }
}

char   *disass_reg(uint8 spec)
{
    if (spec >= 8) return disass_areg(spec & 0x7);
    else return disass_dreg(spec);
}

void    disass_reglist(char *buf, uint16 reglist, int predecrement)
{
    int             index = 0, len;
    *buf = 0;
    if (!predecrement) {
        while (reglist) {
            if (reglist & 0x1) {
                strcat(buf, disass_reg(index));
                strcat(buf, "/");
            }
            index++;
            reglist = reglist >> 1;
        }
    } else {
        while (reglist) {
            if (reglist & 0x8000) {
                strcat(buf, disass_reg(index));
                strcat(buf, "/");
            }
            index++;
            reglist = reglist << 1;
        }
    }
    /* remove trailing / */
    len = strlen(buf);
    if (len > 0) *(buf + len - 1) = 0;
}

int     disass_count(uint8 spec)
{
    if (spec == 0) return 8;
    else return spec;
}


static int
SignExt16(int a) {
	return (((a&0x8000)==0x8000)?a|0xFFFF0000:a);
}

static int
SignExt8(int a) {
	return (((a&0x80)==0x80)?a|0xFFFFFF00:a);
}

/*
 * return # of extension words used for addressing
 */
int     disass_displacement(char *buf, uint16 *inst_stream, int8 displacement)
{
    if (displacement == 0) {
        sprintf(buf, "%d", SignExt16((int)inst_stream[1]));
        return 1;
    } else if (displacement == -1) {
        sprintf(buf, "%d", ((int)(inst_stream[1]<<16)|inst_stream[2]));
        return 2;
    } else {
        sprintf(buf, "%d", SignExt8((int)displacement));
        return 0;
    }
}

/*
 * return # of extension words used for addressing
 */
int     disass_ea(char *buf, uint16  *inst_stream, uint8 mode, uint8 spec, uint8 size)
{
    switch(mode) {
    case 0:
        strcpy(buf, disass_dreg(spec));
        return 0;
    case 1:
        strcpy(buf, disass_areg(spec));
        return 0;
    case 2:
        sprintf(buf, "(%s)", disass_areg(spec));
        return 0;
    case 3:
        sprintf(buf, "(%s)+", disass_areg(spec));
        return 0;
    case 4:
        sprintf(buf, "-(%s)", disass_areg(spec));
        return 0;
    case 5:
        sprintf(buf, "%d(%s)", SignExt16(inst_stream[1]), disass_areg(spec));
        return 1;
    case 6: // TODO: 680x0 full extension format
        sprintf(buf, "(%0d(%s,%s.%c*%x))",
            SignExt8(inst_stream[1]), disass_areg(spec),
            disass_reg(inst_stream[1] >> 12),
            (inst_stream[1] & 0x0800)?'L':'W',
            1 << ((inst_stream[1] & 0x0600) >> 9));
        return 1;
    case 7:
        switch(spec) {
        case 0:
            sprintf(buf, "$%04x", inst_stream[1]);
            return 1;
        case 1:
            sprintf(buf, "$%04x%04x", inst_stream[1], inst_stream[2]);
            return 2;
        case 2:
            sprintf(buf, "%d(PC)",SignExt16((int)inst_stream[1]));
            return 1;
        case 3: // TODO: 680x0 full extension format
            sprintf(buf, "(%d(PC,%s.%c*%x)",
                SignExt16((int)inst_stream[1]),
                disass_reg(inst_stream[1] >> 12),
                (inst_stream[1] & 0x0800)?'L':'W',
                1 << ((inst_stream[1] & 0x0600) >> 9));
            return 1;
        case 4:
            switch(size) {
            case 0:
            case 1:
                sprintf(buf, "#$%04x", inst_stream[1]);
                return 1;
            case 2:
                sprintf(buf, "#$%04x%04x", inst_stream[1], inst_stream[2]);
                return 2;
            case 3:
                sprintf(buf, "???");
                return 0;
            }
        }
    }
    sprintf (buf, "???");
    return 0;
}

// lisa : disass now will return instruction size (in words)
int    disass(char *buf, uint16 *inst_stream)
{
    char    src_ea_buf[80];
    char    tgt_ea_buf[80];
    uint8   src_mode = (inst_stream[0] & 0x0038) >> 3;
    uint8   src_spec = (inst_stream[0] & 0x0007);
    uint8   src_size = (inst_stream[0] & 0x00c0) >> 6;
    uint8   tgt_mode = (inst_stream[0] & 0x01c0) >> 6;
    uint8   tgt_spec = (inst_stream[0] & 0x0e00) >> 9;
    uint8   condition = (inst_stream[0] & 0x0f00) >> 8;
    int wc;
    switch (inst_stream[0] & 0xf000) {
    case 0x0000:
        // TODO: CAS2, CAS
        switch (inst_stream[0]) {
        case 0x003c:
            sprintf(buf, "ORI.B    #$%02x,CCR", (int8) inst_stream[1]);
            return 2;
        case 0x007c:
            sprintf(buf, "ORI.W    #$%04x,SR", inst_stream[1]);
            return 2; 
        case 0x023c:
            sprintf(buf, "ANDI.B   #$%02x,CCR", (int8) inst_stream[1]);
            return 2;
        case 0x027c:
            sprintf(buf, "ANDI.W   #$%04x,SR", inst_stream[1]);
            return 2;
        case 0x0a3c:
            sprintf(buf, "EORI.B   #$%02x,CCR", (int8) inst_stream[1]);
            return 2;
        case 0x0a7c:
            sprintf(buf, "EORI.W   #$%04x,SR", inst_stream[1]);
            return 2;
        }
        switch (inst_stream[0] & 0x0fc0) {
        case 0x0000:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            sprintf(buf, "ORI.B    #$%02x,%s", (int8) inst_stream[1], src_ea_buf);
            return 2+wc;
        case 0x0040:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            sprintf(buf, "ORI.W    #$%04x,%s", inst_stream[1], src_ea_buf);
            return 2+wc;
        case 0x0080:
            wc=disass_ea(src_ea_buf, inst_stream + 2, src_mode, src_spec, src_size);
            sprintf(buf, "ORI.L    #$%04x%04x,%s", inst_stream[1], inst_stream[2], src_ea_buf);
            return 3+wc;
        case 0x0200:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            sprintf(buf, "ANDI.B   #$%02x,%s", (int8) inst_stream[1], src_ea_buf);
            return 2+wc;
        case 0x0240:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            sprintf(buf, "ANDI.W   #$%04x,%s", inst_stream[1], src_ea_buf);
            return 2+wc;
        case 0x0280:
            wc=disass_ea(src_ea_buf, inst_stream + 2, src_mode, src_spec, src_size);
            sprintf(buf, "ANDI.L   #$%04x%04x,%s", inst_stream[1], inst_stream[2], src_ea_buf);
            return 3+wc;
        case 0x0400:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            sprintf(buf, "SUBI.B   #$%02x,%s", (int8) inst_stream[1], src_ea_buf);
            return 2+wc;
        case 0x0440:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            sprintf(buf, "SUBI.W   #$%04x,%s", inst_stream[1], src_ea_buf);
            return 2+wc;
        case 0x0480:
            wc=disass_ea(src_ea_buf, inst_stream + 2, src_mode, src_spec, src_size);
            sprintf(buf, "SUBI.L   #$%04x%04x,%s", inst_stream[1], inst_stream[2], src_ea_buf);
            return 3+wc;
        case 0x0600:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            sprintf(buf, "ADDI.B   #$%02x,%s", (int8) inst_stream[1], src_ea_buf);
            return 2+wc;
        case 0x0640:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            sprintf(buf, "ADDI.W   #$%04x,%s", inst_stream[1], src_ea_buf);
            return 2+wc;
        case 0x0680:
            wc=disass_ea(src_ea_buf, inst_stream + 2, src_mode, src_spec, src_size);
            sprintf(buf, "ADDI.L   #$%04x%04x,%s", inst_stream[1], inst_stream[2], src_ea_buf);
            return 3+wc;
        case 0x0800:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            sprintf(buf, "BTST     #$%02x,%s", (int8) inst_stream[1], src_ea_buf);
            return 2+wc;
        case 0x0840:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            sprintf(buf, "BCHG     #$%02x,%s", (int8) inst_stream[1], src_ea_buf);
            return 2+wc;
        case 0x0880:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            sprintf(buf, "BCLR     #$%02x,%s", (int8) inst_stream[1], src_ea_buf);
            return 2+wc;
        case 0x08c0:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            sprintf(buf, "BSET     #$%02x,%s", (int8) inst_stream[1], src_ea_buf);
            return 2+wc;
        case 0x0a00:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            sprintf(buf, "EORI.B   #$%02x,%s", (int8) inst_stream[1], src_ea_buf);
            return 2+wc;
        case 0x0a40:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            sprintf(buf, "EORI.W   #$%04x,%s", inst_stream[1], src_ea_buf);
            return 2+wc;
        case 0x0a80:
            wc=disass_ea(src_ea_buf, inst_stream + 2, src_mode, src_spec, src_size);
            sprintf(buf, "EORI.L   #$%04x%04x,%s", inst_stream[1], inst_stream[2], src_ea_buf);
            return 3+wc;
        case 0x0c00:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            sprintf(buf, "CMPI.B   #$%02x,%s", (int8) inst_stream[1], src_ea_buf);
            return 2+wc;
        case 0x0c40:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            sprintf(buf, "CMPI.W   #$%04x,%s", inst_stream[1], src_ea_buf);
            return 2+wc;
        case 0x0c80:
            wc=disass_ea(src_ea_buf, inst_stream + 2, src_mode, src_spec, src_size);
            sprintf(buf, "CMPI.L   #$%04x%04x,%s", inst_stream[1], inst_stream[2], src_ea_buf);
            return 3+wc;
        case 0x0e00:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            switch (inst_stream[1] & 0x0800) {
            case 0x0000: sprintf(buf, "MOVES.B  %s,%s", src_ea_buf, disass_reg((inst_stream[1] >> 12) & 0xf));
                return 2+wc;
            case 0x0800: sprintf(buf, "MOVES.B  %s,%s", disass_reg((inst_stream[1] >> 12) & 0xf), src_ea_buf);
                return 2;
            }
        case 0x0e40:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            switch (inst_stream[1] & 0x0800) {
            case 0x0000: sprintf(buf, "MOVES.W  %s,%s", src_ea_buf, disass_reg((inst_stream[1] >> 12) & 0xf));
                return 2+wc;
            case 0x0800: sprintf(buf, "MOVES.W  %s,%s", disass_reg((inst_stream[1] >> 12) & 0xf), src_ea_buf);
                return 2;
            }
        case 0x0e80:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            switch (inst_stream[1] & 0x0800) {
            case 0x0000: sprintf(buf, "MOVES.L  %s,%s", src_ea_buf, disass_reg((inst_stream[1] >> 12) & 0xf));
                return 2+wc;
            case 0x0800: sprintf(buf, "MOVES.L  %s,%s", disass_reg((inst_stream[1] >> 12) & 0xf), src_ea_buf);
                return 2;
            }
        case 0x06c0:
            wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
            switch (inst_stream[0] & 0x0030) {
            case 0x0000: sprintf(buf, "RTM      %s", disass_reg(inst_stream[0] & 0xf));
                return 1;
            case 0x0030: sprintf(buf, "CALLM    %s", src_ea_buf);
                return 1+wc;
            }
        case 0x00c0:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 0);
            switch (inst_stream[1] & 0x0800) {
            case 0x0000: sprintf(buf, "CMP2.B   %s,%s", src_ea_buf, disass_reg((inst_stream[1] >> 12) & 0xf));
                return 2+wc;
            case 0x0800: sprintf(buf, "CHK2.B   %s,%s", src_ea_buf, disass_reg((inst_stream[1] >> 12) & 0xf));
                return 2+wc;
            }
        case 0x02c0:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 1);
            switch (inst_stream[1] & 0x0800) {
            case 0x0000: sprintf(buf, "CMP2.W   %s,%s", src_ea_buf, disass_reg((inst_stream[1] >> 12) & 0xf));
                return 2+wc;
            case 0x0800: sprintf(buf, "CHK2.W   %s,%s", src_ea_buf, disass_reg((inst_stream[1] >> 12) & 0xf));
                return 2+wc;
            }
        case 0x04c0:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 2);
            switch (inst_stream[1] & 0x0800) {
            case 0x0000: sprintf(buf, "CMP2.L   %s,%s", src_ea_buf, disass_reg((inst_stream[1] >> 12) & 0xf));
                return 2+wc;
            case 0x0800: sprintf(buf, "CHK2.L   %s,%s", src_ea_buf, disass_reg((inst_stream[1] >> 12) & 0xf));
                return 2+wc;
            }
        }
        if (inst_stream[0] & 0x0100) {
            if (src_mode == 1) {
                switch (inst_stream[0] & 0x00c0) {
                case 0x0000: sprintf(buf, "MOVEP.W  #$%04x(%s),%s", inst_stream[1], disass_areg(src_spec), disass_dreg(tgt_spec));
                    return 2;
                case 0x0040: sprintf(buf, "MOVEP.L  #$%04x(%s),%s", inst_stream[1], disass_areg(src_spec), disass_dreg(tgt_spec));
                    return 2;
                case 0x0080: sprintf(buf, "MOVEP.W  %s,#$%04x(%s)", disass_dreg(tgt_spec), inst_stream[1], disass_areg(src_spec));
                    return 2;
                case 0x00c0: sprintf(buf, "MOVEP.L  %s,#$%04x(%s)", disass_dreg(tgt_spec), inst_stream[1], disass_areg(src_spec));
                    return 2;
                }
            }
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, src_size);
            switch (inst_stream[0] & 0x00c0) {
            case 0x0000: sprintf(buf, "BTST     %s,%s", disass_dreg(tgt_spec), src_ea_buf);
                return 1+wc;
            case 0x0040: sprintf(buf, "BCHG     %s,%s", disass_dreg(tgt_spec), src_ea_buf);
                return 1+wc;
            case 0x0080: sprintf(buf, "BCLR     %s,%s", disass_dreg(tgt_spec), src_ea_buf);
                return 1+wc;
            case 0x00c0: sprintf(buf, "BSET     %s,%s", disass_dreg(tgt_spec), src_ea_buf);
                return 1+wc;
            }
        }
        sprintf(buf, "???");
        return 1;
    case 0x1000: /* Move Byte */
        wc = disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 0);
        wc+=disass_ea(tgt_ea_buf, inst_stream + wc, tgt_mode, tgt_spec, 0);
        sprintf(buf, "MOVE.B   %s,%s", src_ea_buf, tgt_ea_buf);
        return 1+wc;
    case 0x2000: /* Move Long */
        wc = disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 2);
        wc+=disass_ea(tgt_ea_buf, inst_stream + wc, tgt_mode, tgt_spec, 2);
        sprintf(buf, "MOVE.L   %s,%s", src_ea_buf, tgt_ea_buf);
        return 1+wc;
    case 0x3000: /* Move Word */
        wc = disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 1);
        wc+=disass_ea(tgt_ea_buf, inst_stream + wc, tgt_mode, tgt_spec, 1);
        sprintf(buf, "MOVE.W   %s,%s", src_ea_buf, tgt_ea_buf);
        return 1+wc;
    case 0x4000: /* Miscellaneous */
        // TODO: MULU.L, MULS.L, DIVU.L, DIVS.L, EXTB.L
        wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, src_size);
        switch (inst_stream[0] & 0xffc0) {
        case 0x4000:
            sprintf(buf, "NEGX.B   %s", src_ea_buf);
            return 1+wc;
        case 0x4040:
            sprintf(buf, "NEGX.W   %s", src_ea_buf);
            return 1+wc;
        case 0x4080:
            sprintf(buf, "NEGX.L   %s", src_ea_buf);
            return 1+wc;
        case 0x40c0:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 1);
            sprintf(buf, "MOVE.W   SR,%s", src_ea_buf);
            return 1+wc;
        case 0x4200:
            sprintf(buf, "CLR.B    %s", src_ea_buf);
            return 1+wc;
        case 0x4240:
            sprintf(buf, "CLR.W    %s", src_ea_buf);
            return 1+wc;
        case 0x4280:
            sprintf(buf, "CLR.L    %s", src_ea_buf);
            return 1+wc;
        case 0x42c0:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 1);
            sprintf(buf, "MOVE.W   CCR,%s", src_ea_buf);
            return 1+wc;
        case 0x4400:
            sprintf(buf, "NEG.B    %s", src_ea_buf);
            return 1+wc;
        case 0x4440:
            sprintf(buf, "NEG.W    %s", src_ea_buf);
            return 1+wc;
        case 0x4480:
            sprintf(buf, "NEG.L    %s", src_ea_buf);
            return 1+wc;
        case 0x44c0:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 1);
            sprintf(buf, "MOVE.W   %s,CCR", src_ea_buf);
            return 1+wc;
        case 0x4600:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 1);
            sprintf(buf, "NOT.B    %s", src_ea_buf);
            return 1+wc;
        case 0x4640:
            sprintf(buf, "NOT.W    %s", src_ea_buf);
            return 1+wc;
        case 0x4680:
            sprintf(buf, "NOT.L    %s", src_ea_buf);
            return 1+wc;
        case 0x46c0:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 1);
            sprintf(buf, "MOVE.W   %s,SR", src_ea_buf);
            return 1+wc;
        case 0x4800:
            if (src_mode == 1) {
                sprintf(buf, "LINK.L   #$%04x%04x,%s",
                    inst_stream[1], inst_stream[2], disass_areg(src_spec));
                    return 3;
            } else {
                sprintf(buf, "NBCD     %s", src_ea_buf);
            }
            return 1+wc;
        case 0x4840:
            if (src_mode == 0) {
                sprintf(buf, "SWAP.W   %s", disass_dreg(src_spec));
            } else if (src_mode == 1) {
                sprintf(buf, "BKPT     #%x", src_spec);
            } else {
                sprintf(buf, "PEA      %s", src_ea_buf);
                 return 1+wc;
            }
            return 1;
        case 0x4880:
            if (src_mode == 0) {
                sprintf(buf, "EXT.W    %s", disass_dreg(src_spec));
                return 1;
            } else {
                disass_reglist(tgt_ea_buf, inst_stream[1], src_mode == 4);
                wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
                sprintf(buf, "MOVEM.W  %s,%s", tgt_ea_buf, src_ea_buf);
            }
            return 2+wc;
        case 0x48c0:
            if (src_mode == 0) {
                sprintf(buf, "EXT.L    %s", disass_dreg(src_spec));
                return 1;
            } else {
                disass_reglist(tgt_ea_buf, inst_stream[1], src_mode == 4);
                wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
                sprintf(buf, "MOVEM.L  %s,%s", tgt_ea_buf, src_ea_buf);
            }
            return 2+wc;
        case 0x4a00:
            sprintf(buf, "TST.B    %s", src_ea_buf);
            return 1+wc;
        case 0x4a40:
            sprintf(buf, "TST.W    %s", src_ea_buf);
            return 1+wc;
        case 0x4a80:
            sprintf(buf, "TST.L    %s", src_ea_buf);
            return 1+wc;
        case 0x4ac0:
            switch (inst_stream[0] & 0x003f) {
            case 0x003a:
                sprintf(buf, "BGND   ");
                return 1;
            case 0x003c:
                sprintf(buf, "ILLEGAL");
                return 1;
            default:
                wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 0);
                sprintf(buf, "TAS    %s", src_ea_buf);
                return 1+wc;
            }
        case 0x4c80:
            if (src_mode == 0) {
             // fixme !!
            } else {
                disass_reglist(tgt_ea_buf, inst_stream[1], src_mode == 4);
                wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, src_size);
                sprintf(buf, "MOVEM.W  %s,%s", src_ea_buf, tgt_ea_buf);
            }
            return 1+wc;
        case 0x4cc0:
            if (src_mode == 0) 
            {
             // fixme !!
            } else {
                disass_reglist(tgt_ea_buf, inst_stream[1], src_mode == 4);
                wc=disass_ea(src_ea_buf, inst_stream + 1, src_mode, src_spec, 2);
                sprintf(buf, "MOVEM.L  %s,%s", src_ea_buf, tgt_ea_buf);
            }
            return 1+wc;
        case 0x4e40:
            switch(src_mode) {
            case 0:
            case 1:
                sprintf(buf, "TRAP     #$%x", inst_stream[0] & 0xf);
                return 1;
            case 2:
                sprintf(buf, "LINK     #$%4x,%s", inst_stream[1], disass_areg(src_spec));
                return 2;
            case 3:
                sprintf(buf, "UNLK     %s", disass_areg(src_spec));
                return 1;
            case 4:
                sprintf(buf, "MOVE.L   %s,USP", disass_areg(src_spec));
                return 1;
            case 5:
                sprintf(buf, "MOVE.L   USP,%s", disass_areg(src_spec));
                return 1;
            case 6:
                switch(inst_stream[0]) {
                case 0x4e70:
                    sprintf(buf, "RESET    ");
                    return 1;
                case 0x4e71:
                    sprintf(buf, "NOP      ");
                    return 1;
                case 0x4e72:
                    sprintf(buf, "STOP     ");
                    return 1;
                case 0x4e73:
                    sprintf(buf, "RTE      ");
                    return 1;
                case 0x4e74:
                    sprintf(buf, "RTD      ");
                    return 1;
                case 0x4e75:
                    sprintf(buf, "RTS      ");
                    return 1;
                case 0x4e76:
                    sprintf(buf, "TRAPV    ");
                    return 1;
                case 0x4e77:
                    sprintf(buf, "RTR      ");
                    return 1;
                }
            case 7:
                switch(inst_stream[0]) {
                case 0x4e7a:
                    sprintf(buf, "MOVEC    #$%04x,%s", inst_stream[1] & 0xfff, disass_reg(inst_stream[1] >> 12));
                    return 2;
                case 0x4e7b:
                    sprintf(buf, "MOVEC    #%s,$%04x", disass_reg(inst_stream[1] >> 12), inst_stream[1] & 0xfff);
                    return 2;
                }
            }
            sprintf(buf, "???      ");
            return 1;
        case 0x4e80:
            sprintf(buf, "JSR      %s", src_ea_buf);
            return 1+wc;
        case 0x4ec0:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 2);
            sprintf(buf, "JMP      %s", src_ea_buf);
            return 1+wc;
        }
        switch (tgt_mode) {
        case 4:
            sprintf(buf, "CHK.L    %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 6:
            sprintf(buf, "CHK.W    %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 7:
            sprintf(buf, "LEA      %s,%s", src_ea_buf, disass_areg(tgt_spec));
            return 1+wc;
        }
        sprintf(buf, "???");
        return 1;
    case 0x5000: /* ADDQ/SUBQ/Scc/DBcc/TRAPcc */
        // TODO: TRAPcc
        wc = disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, src_size);
        switch (inst_stream[0] & 0x00c0) {
        case 0x0000:
            if (inst_stream[0] & 0x0100) {
                sprintf(buf, "SUBQ.B   #%x,%s", disass_count(tgt_spec), src_ea_buf);
            } else {
                sprintf(buf, "ADDQ.B   #%x,%s", disass_count(tgt_spec), src_ea_buf);
            }
            return 1+wc;
        case 0x0040:
            if (inst_stream[0] & 0x0100) {
                sprintf(buf, "SUBQ.W   #%x,%s", disass_count(tgt_spec), src_ea_buf);
            } else {
                sprintf(buf, "ADDQ.W   #%x,%s", disass_count(tgt_spec), src_ea_buf);
            }
            return 1+wc;
        case 0x0080:
            if (inst_stream[0] & 0x0100) {
                sprintf(buf, "SUBQ.L   #%x,%s", disass_count(tgt_spec), src_ea_buf);
            } else {
                sprintf(buf, "ADDQ.L   #%x,%s", disass_count(tgt_spec), src_ea_buf);
            }
            return 1+wc;
        case 0x00c0:
            if (src_mode == 1) {
                wc=disass_displacement(tgt_ea_buf, inst_stream + wc, 0);
                sprintf(buf, "DB%s     %s,%s", disass_cc(condition), tgt_ea_buf, disass_dreg(src_spec));
            } else {
                sprintf(buf, "S%s      %s", disass_cc(condition), src_ea_buf);
            }
            return 1+wc;
        }
    case 0x6000: /* Bcc/BSR/BRA */
        wc=disass_displacement(tgt_ea_buf, inst_stream, inst_stream[0]);
        if ((inst_stream[0] & 0xff00) == 0x6000) {
            sprintf(buf, "BRA      %s", tgt_ea_buf);
        } else if ((inst_stream[0] & 0xff00) == 0x6100) {
            sprintf(buf, "BSR      %s", tgt_ea_buf);
        } else {
            sprintf(buf, "B%s      %s",disass_cc(condition), tgt_ea_buf);
        }
        return 1+wc;
    case 0x7000: /* MOVEQ */
        sprintf(buf, "MOVEQ.L  #$%02x,%s", (int8) inst_stream[0], disass_dreg(tgt_spec));
        return 1;
    case 0x8000: /* OR/DIV/SBCD */
        // TODO: PACK, UNPK
        wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, src_size);
        switch (inst_stream[0] & 0x01c0) {
        case 0x0000:
            sprintf(buf, "OR.B     %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 0x0040:
            sprintf(buf, "OR.W     %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 0x0080:
            sprintf(buf, "OR.L     %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 0x00c0:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 1);
            sprintf(buf, "DIVU.W   %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 0x0100:
            if (src_mode == 0) {
                sprintf(buf, "SBCD     %s,%s", disass_dreg(src_spec), disass_dreg(tgt_spec));
            } else if (src_mode == 1) {
                sprintf(buf, "SBCD     -(%s),-(%s)", disass_areg(src_spec), disass_areg(tgt_spec));
            } else {
                sprintf(buf, "OR.B     %s,%s", disass_dreg(tgt_spec), src_ea_buf);
                return 1+wc;
            }
            return 1;
        case 0x0140:
            sprintf(buf, "OR.W     %s,%s", disass_reg(tgt_spec), src_ea_buf);
            return 1+wc;
        case 0x0180:
            sprintf(buf, "OR.L     %s,%s", disass_reg(tgt_spec), src_ea_buf);
            return 1+wc;
        case 0x01c0:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 1);
            sprintf(buf, "DIVS.W   %s,%s", src_ea_buf, disass_reg(tgt_spec));
            return 1+wc;
        }
    case 0x9000:
        wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, src_size);
        switch (inst_stream[0] & 0x01c0) {
        case 0x0000:
            sprintf(buf, "SUB.B    %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 0x0040:
            sprintf(buf, "SUB.W    %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 0x0080:
            sprintf(buf, "SUB.L    %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 0x00c0:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 1);
            sprintf(buf, "SUBA.W   %s,%s", src_ea_buf, disass_areg(tgt_spec));
            return 1+wc;
        case 0x0100:
            if (src_mode == 0) {
                sprintf(buf, "SUBX.B   %s,%s", disass_dreg(src_spec), disass_dreg(tgt_spec));
            } else if (src_mode == 1) {
                sprintf(buf, "SUBX.B   -(%s),-(%s)", disass_areg(src_spec), disass_areg(tgt_spec));
            } else {
                sprintf(buf, "SUB.B    %s,%s", disass_dreg(tgt_spec), src_ea_buf);
                return 1+wc;
            }
            return 1;
        case 0x0140:
            if (src_mode == 0) {
                sprintf(buf, "SUBX.W   %s,%s", disass_dreg(src_spec), disass_dreg(tgt_spec));
            } else if (src_mode == 1) {
                sprintf(buf, "SUBX.W   -(%s),-(%s)", disass_areg(src_spec), disass_areg(tgt_spec));
            } else {
                sprintf(buf, "SUB.W    %s,%s", disass_dreg(tgt_spec), src_ea_buf);
                return 1+wc;
            }
            return 1;
        case 0x0180:
            if (src_mode == 0) {
                sprintf(buf, "SUBX.L   %s,%s", disass_dreg(src_spec), disass_dreg(tgt_spec));
            } else if (src_mode == 1) {
                sprintf(buf, "SUBX.L   -(%s),-(%s)", disass_areg(src_spec), disass_areg(tgt_spec));
            } else {
                sprintf(buf, "SUB.L    %s,%s", disass_dreg(tgt_spec), src_ea_buf);
                return 1+wc;
            }
            return 1;
        case 0x01c0:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 2);
            sprintf(buf, "SUBA.L   %s,%s", src_ea_buf, disass_areg(tgt_spec));
            return 1+wc;
        }
    case 0xa000:
        sprintf(buf, "DC.W     $%04x", inst_stream[0]);
        return 1;
    case 0xb000:
        wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, src_size);
        switch (inst_stream[0] & 0x01c0) {
        case 0x0000:
            sprintf(buf, "CMP.B    %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 0x0040:
            sprintf(buf, "CMP.W    %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 0x0080:
            sprintf(buf, "CMP.L    %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 0x00c0:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 1);
            sprintf(buf, "CMPA.W   %s,%s", src_ea_buf, disass_areg(tgt_spec));
            return 1+wc;
        case 0x0100:
            if (src_mode == 1) {
                sprintf(buf, "CMPM.B   (%s)+,(%s)+", disass_areg(src_spec), disass_areg(tgt_spec));
            } else {
                sprintf(buf, "EOR.B    %s,%s", disass_dreg(tgt_spec), src_ea_buf);
                return 1+wc;
            }
            return 1;
        case 0x0140:
            if (src_mode == 1) {
                sprintf(buf, "CMPM.W   (%s)+,(%s)+", disass_areg(src_spec), disass_areg(tgt_spec));
            } else {
                sprintf(buf, "EOR.W    %s,%s", disass_dreg(tgt_spec), src_ea_buf);
                return 1+wc;
            }
            return 1;
        case 0x0180:
            if (src_mode == 1) {
                sprintf(buf, "CMPM.L   (%s)+,(%s)+", disass_areg(src_spec), disass_areg(tgt_spec));
            } else {
                sprintf(buf, "EOR.L    %s,%s", disass_dreg(tgt_spec), src_ea_buf);
                return 1+wc;
            }
            return 1;
        case 0x01c0:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 2);
            sprintf(buf, "CMPA.L   %s,%s", src_ea_buf, disass_areg(tgt_spec));
            return 1+wc;
        }
    case 0xc000:
        wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, src_size);
        switch (inst_stream[0] & 0x01c0) {
        case 0x0000:
            sprintf(buf, "AND.B    %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 0x0040:
            sprintf(buf, "AND.W    %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 0x0080:
            sprintf(buf, "AND.L    %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 0x00c0:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 1);
            sprintf(buf, "MULU.W   %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 0x0100:
            if (src_mode == 0) {
                sprintf(buf, "ABCD     %s,%s", disass_dreg(src_spec), disass_dreg(tgt_spec));
            } else if (src_mode == 1) {
                sprintf(buf, "ABCD     -(%s),-(%s)", disass_areg(src_spec), disass_reg(tgt_spec));
            } else {
                sprintf(buf, "AND.B    %s,%s", disass_dreg(tgt_spec), src_ea_buf);
                return 1+wc;
            }
            return 1;
        case 0x0140:
            if (src_mode == 0) {
                sprintf(buf, "EXG      %s,%s", disass_dreg(src_spec), disass_dreg(tgt_spec));
            } else if (src_mode == 1) {
                sprintf(buf, "EXG      %s,%s", disass_areg(src_spec), disass_areg(tgt_spec));
            } else {
                sprintf(buf, "AND.W    %s,%s", disass_reg(tgt_spec), src_ea_buf);
                return 1+wc;
            }
            return 1;
        case 0x0180:
            if (src_mode == 1) {
                sprintf(buf, "EXG      %s,%s", disass_dreg(src_spec), disass_areg(tgt_spec));
            } else {
                sprintf(buf, "AND.L    %s,%s", disass_dreg(tgt_spec), src_ea_buf);
                return 1+wc;
            }
            return 1;
        case 0x01c0:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 1);
            sprintf(buf, "MULS.W   %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        }
    case 0xd000:
        wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, src_size);
        switch (inst_stream[0] & 0x01c0) {
        case 0x0000:
            sprintf(buf, "ADD.B    %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 0x0040:
            sprintf(buf, "ADD.W    %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 0x0080:
            sprintf(buf, "ADD.L    %s,%s", src_ea_buf, disass_dreg(tgt_spec));
            return 1+wc;
        case 0x00c0:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 1);
            sprintf(buf, "ADDA.W   %s,%s", src_ea_buf, disass_areg(tgt_spec));
            return 1+wc;
        case 0x0100:
            if (src_mode == 0) {
                sprintf(buf, "ADDX.B   %s,%s", disass_dreg(src_spec), disass_dreg(tgt_spec));
            } else if (src_mode == 0x0008) {
                sprintf(buf, "ADDX.B   -(%s),-(%s)", disass_areg(src_spec), disass_areg(tgt_spec));
            } else {
                sprintf(buf, "ADD.B    %s,%s", disass_dreg(tgt_spec), src_ea_buf);
                return 1+wc;
            }
            return 1;
        case 0x0140:
            if (src_mode == 0) {
                sprintf(buf, "ADDX.W   %s,%s", disass_dreg(src_spec), disass_dreg(tgt_spec));
            } else if (src_mode == 1) {
                sprintf(buf, "ADDX.W   -(%s),-(%s)", disass_areg(src_spec), disass_areg(tgt_spec));
            } else {
                sprintf(buf, "ADD.W    %s,%s", disass_dreg(tgt_spec), src_ea_buf);
                return 1+wc;
            }
            return 1;
        case 0x0180:
            if (src_mode == 0) {
                sprintf(buf, "ADDX.L   %s,%s", disass_dreg(src_spec), disass_dreg(tgt_spec));
            } else if (src_mode == 1) {
                sprintf(buf, "ADDX.L   -(%s),-(%s)", disass_areg(src_spec), disass_areg(tgt_spec));
            } else {
                sprintf(buf, "ADD.L    %s,%s", disass_dreg(tgt_spec), src_ea_buf);
                return 1+wc;
            }
            return 1;
        case 0x01c0:
            wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, 2);
            sprintf(buf, "ADDA.L   %s,%s", src_ea_buf, disass_areg(tgt_spec));
            return 1+wc;
        }
    case 0xe000:
        // TODO: BFTST, BFEXTU, BFCHG, BFEXTS, BFCLR, BFFFO, BFSET, BFINS
        wc=disass_ea(src_ea_buf, inst_stream, src_mode, src_spec, src_size);
        switch (inst_stream[0] & 0x07c0) {
        case 0x00c0:
            sprintf(buf, "ASR.W    #1,%s", src_ea_buf);
            return 1+wc;
        case 0x01c0:
            sprintf(buf, "ASL.W    #1,%s", src_ea_buf);
            return 1+wc;
        case 0x02c0:
            sprintf(buf, "LSR.W    #1,%s", src_ea_buf);
            return 1+wc;
        case 0x03c0:
            sprintf(buf, "LSL.W    #1,%s", src_ea_buf);
            return 1+wc;
        case 0x04c0:
            sprintf(buf, "ROXR.W   #1,%s", src_ea_buf);
            return 1+wc;
        case 0x05c0:
            sprintf(buf, "ROXL.W   #1,%s", src_ea_buf);
            return 1+wc;
        case 0x06c0:
            sprintf(buf, "ROR.W    #1,%s", src_ea_buf);
            return 1+wc;
        case 0x07c0:
            sprintf(buf, "ROL.W    #1,%s", src_ea_buf);
            return 1+wc;
        }
        switch (inst_stream[0] & 0x01f8) {
        case 0x0000:
            sprintf(buf, "ASR.B    #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0008:
            sprintf(buf, "LSR.B    #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0010:
            sprintf(buf, "ROXR.B   #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0018:
            sprintf(buf, "ROR.B    #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0020:
            sprintf(buf, "ASR.B    %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0028:
            sprintf(buf, "LSR.B    %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0030:
            sprintf(buf, "ROXR.B   %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0038:
            sprintf(buf, "ROR.B    %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0040:
            sprintf(buf, "ASR.W    #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0048:
            sprintf(buf, "LSR.W    #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0050:
            sprintf(buf, "ROXR.W   #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0058:
            sprintf(buf, "ROR.W    #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0060:
            sprintf(buf, "ASR.W    %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0068:
            sprintf(buf, "LSR.W    %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0070:
            sprintf(buf, "ROXR.W   %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0078:
            sprintf(buf, "ROR.W    %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0080:
            sprintf(buf, "ASR.L    #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0088:
            sprintf(buf, "LSR.L    #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0090:
            sprintf(buf, "ROXR.L   #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0098:
            sprintf(buf, "ROR.L    #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x00a0:
            sprintf(buf, "ASR.L    %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x00a8:
            sprintf(buf, "LSR.L    %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x00b0:
            sprintf(buf, "ROXR.L   %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x00b8:
            sprintf(buf, "ROR.L    %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0100:
            sprintf(buf, "ASL.B    #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0108:
            sprintf(buf, "LSL.B    #%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0110:
            sprintf(buf, "ROXL.B   #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0118:
            sprintf(buf, "ROL.B    #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0120:
            sprintf(buf, "ASL.B    %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0128:
            sprintf(buf, "LSL.B    %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0130:
            sprintf(buf, "ROXL.B   %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0138:
            sprintf(buf, "ROL.B    %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0140:
            sprintf(buf, "ASL.W    #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0148:
            sprintf(buf, "LSL.W    #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0150:
            sprintf(buf, "ROXL.W   #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0158:
            sprintf(buf, "ROL.W    #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0160:
            sprintf(buf, "ASL.W    %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0168:
            sprintf(buf, "LSL.W    %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0170:
            sprintf(buf, "ROXL.W   %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0178:
            sprintf(buf, "ROL.W    %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0180:
            sprintf(buf, "ASL.L    #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0188:
            sprintf(buf, "LSL.L    #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0190:
            sprintf(buf, "ROXL.L   #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x0198:
            sprintf(buf, "ROL.L    #$%x,%s", disass_count(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x01a0:
            sprintf(buf, "ASL.L    %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x01a8:
            sprintf(buf, "LSL.L    %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x01b0:
            sprintf(buf, "ROXL.L   %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        case 0x01b8:
            sprintf(buf, "ROL.L    %s,%s", disass_dreg(tgt_spec), disass_dreg(src_spec));
            return 1;
        }
    case 0xf000:
        sprintf(buf, "DC.W    $%04x", inst_stream[0]);
        return 1;
}}



void unassemble_block(unsigned char *p,int start,int end) {
	uint16 *l_inststream;
	char l_inst[256];
	int inst_size;
	int i;
	
	// align... (memory for debugger is short int, buffer is char).
	// allow for maximum opcode size to avoid problem
	l_inststream=malloc(((end-start)/2+16)*sizeof(uint16));
	
	for (i=start;i<end;i+=2) {
		l_inststream[i/2]=(p[i]<<8)|p[i+1];
	}
	
	i=start;
	while (i<end) {
		inst_size=disass(l_inst,&l_inststream[i/2]);
		fprintf(stdout,"\t%-50s; $%08x %010d\n",l_inst,i,i);
		i+=inst_size*2;
	}
	free(l_inststream);
    }
