/*
  Name: disk_image.c
  Copyright: 
  Author: 
  Date: 24/10/06 18:51
  Description: 
           
    
  Disk Copy 4.2 image handling derived from LisaFSh Tool by Ray Arachelian (www.sunder.net)
  corrected CRC checksum ()
  corrected some signed char issues
  
*/
#include "../config.h"


#include "../cpu/m68k.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include <errno.h>


typedef struct
{
    FILE *fhandle;
    char *filename;
    uint16 sectorsize;
    uint32 tagsize;
    uint32 numblocks;
    unsigned char *sectors;
    unsigned char *tags;
    int init;
    int inserted;
} FloppyType;

/*
 * 2 disk slots
 */

int init=0;
static int inserted=0;

FloppyType diska;
FloppyType diskb;

static int dc42_image_open(FloppyType *F);
static int dc42_image_close(FloppyType *F);
static int dart_image_open(FloppyType *F);

int get_sect_by_track(int track)
{
    if (track<=15) return 12;
    if (track<=31) return 11;
    if (track<=47) return 10;
    if (track<=63) return 9;
    return 8;
}

int get_sect_by_track_twiggy(int track)
{
    if (track<=3) return 22;
    if (track<=10) return 21;
    if (track<=16) return 20;
    if (track<=22) return 19;
    if (track<=28) return 18;
    if (track<=34) return 17;
    if (track<=41) return 16;
    return 15;
}

uint8 dummy[1024];

void unclamp(int drive) {
    FloppyType *disk;

    if (drive==0) disk=&diska;
    else
        disk=&diskb;

     if (disk->init!=0) 
          dc42_image_close(disk);
     disk->init=0;
     disk->inserted=0;
}

int check_inserted(int drive) {
    FloppyType *disk;

    if (drive==0) disk=&diska;
    else
        disk=&diskb;

    if (disk->inserted) {
          disk->inserted=0;
          return -1;         
    }
    return 0;   
}

int format_floppy(int drive) {
    FloppyType *disk;
    if (drive==0) disk=&diska;
    else
        disk=&diskb;
        
    if (disk->init==0) 
    {
       return 7;
    }
    
    memset(disk->sectors,0,disk->sectorsize*disk->numblocks);
    memset(disk->tags,0,disk->tagsize*disk->numblocks);
    return 0;
}

int read_sector(int drive,int track,int sector,int side,unsigned char **data,unsigned char **tags,int *datasize,int *tagsize)
{
    int bl,i;
    FloppyType *disk;
    IDLE_INIT_FUNC("read_sector()");
    
    if (drive==0) disk=&diska;
    else
        disk=&diskb;
        
    if (disk->init==0) 
    {
       *data=dummy;
       *tags=dummy;
       return 7;
    }
    
    bl=0;
    if (disk->numblocks==800) {
        for (i=0;i<track;i++)
        {
            bl=bl+get_sect_by_track(i);
        }    
        bl+=sector;
        IDLE_DEBUG("asked tr=%d sect=%d got %d",track,sector,bl);
    } else {
        if (side==1) {
            bl+=disk->numblocks/2;
        }
        for (i=0;i<track;i++)
        {
            bl=bl+get_sect_by_track_twiggy(i);
        }    
        bl+=sector;
        IDLE_TRACE("asked drive=%d tr=%d sect=%d side=%d",drive,track,sector,side);
        IDLE_TRACE("got %d",bl);
    }
    *data=&(disk->sectors[bl*disk->sectorsize]);
    *tags=&(disk->tags[bl*disk->tagsize]);
    *datasize=disk->sectorsize;
    *tagsize=disk->tagsize;
    return 0;
}

int write_sector(int drive,int track,int sector,int side,unsigned char *data,unsigned char *tags)
{
    int bl,i;
    FloppyType *disk;
    IDLE_INIT_FUNC("write_sector()");
    
    if (drive==0) disk=&diska;
    else
        disk=&diskb;
        
    if (disk->init==0) 
    {
       return 7;
    }
    
    bl=0;
    if (disk->numblocks==800) {
        for (i=0;i<track;i++)
        {
            bl=bl+get_sect_by_track(i);
        }    
        bl+=sector;
        IDLE_TRACE("written tr=%d sect=%d got %d",track,sector,bl);
    } else {
        if (side==1) {
            bl+=disk->numblocks/2;
        }
        for (i=0;i<track;i++)
        {
            bl=bl+get_sect_by_track_twiggy(i);
        }    
        bl+=sector;
        IDLE_TRACE("asked drive=%d tr=%d sect=%d side=%d",drive,track,sector,side);
        IDLE_TRACE("got %d",bl);
    }
    
    memcpy(&(disk->sectors[bl*disk->sectorsize]),data,disk->sectorsize);
    memcpy(&(disk->tags[bl*disk->tagsize]),tags,disk->tagsize);
    return 0;
}

int dc42_insert(char *filename,int drive)
{
    int ret;
    FILE *f;
    IDLE_INIT_FUNC("dc42_insert");
    f=fopen(filename,"rb+");
    if (f==NULL)
    {
                IDLE_TRACE("error opening %s",filename);
                return -1;
                }
    
    IDLE_TRACE("Insert Drive %d",drive);
    switch (drive) {
           case 0 : 
                diska.init=1;
                diska.inserted=1;
                diska.fhandle=f;
                diska.filename=(char*)malloc(strlen(filename)+1);
                strcpy(diska.filename,filename);
                ret=dc42_image_open(&diska);
                if (ret!=0) {diska.init=0;diska.inserted=0;}
                return ret;
           case 1: 
                diskb.init=1;
                diskb.inserted=1;
                diskb.fhandle=f;
                diskb.filename=(char*)malloc(strlen(filename)+1);
                strcpy(diskb.filename,filename);
                ret=dc42_image_open(&diskb);
                if (ret!=0) {diskb.init=0;diskb.inserted=0;}
                return ret;
           default:
                   return -1; 
                
           }
}

int dart_insert(char *filename,int drive)
{
    FILE *f;
    IDLE_INIT_FUNC("dart_insert");
    f=fopen(filename,"rb+");
    if (f==NULL)
    {
                IDLE_TRACE("error opening %s",filename);
                return -1;
                }
    
    init=1;
    
    switch (drive) {
           case 0 : 
                diska.fhandle=f;
                diska.filename=(char*)malloc(strlen(filename)+1);
                strcpy(diska.filename,filename);
                return dart_image_open(&diska);
           case 1: 
                diskb.fhandle=f;
                diskb.filename=(char*)malloc(strlen(filename)+1);
                strcpy(diskb.filename,filename);
                return dart_image_open(&diskb);
           default:
                   return -1; 
                
           }
}

/*
 * the disk copy 4.2 image loader
 */
int dc42_image_open(FloppyType *F)
{

uint32 i,j;
char comment[65];
unsigned char dc42head[84];
uint32 datasize, tagsize, datachks, tagchks, mydatachks, mytagchks;
uint16 diskformat, formatbyte, privflag;
int ret;
IDLE_INIT_FUNC("dc42_image_open()");
	errno=0;
	fseek(F->fhandle, 0,SEEK_SET);
	ret=fread(dc42head,84,1,F->fhandle);
	if ((ret!=1) || (errno)) {
                  IDLE_ERROR("Error reading %s",F->filename);
                  return -1;
               }

	memcpy(comment,&dc42head[1],63);
	comment[63]=0;
	if (dc42head[0]<63) 
         comment[dc42head[0]]=0;

	datasize=(dc42head[64+0]<<24)|(dc42head[64+1]<<16)|(dc42head[64+2]<<8)|dc42head[64+3];
	tagsize =(dc42head[68+0]<<24)|(dc42head[68+1]<<16)|(dc42head[68+2]<<8)|dc42head[68+3];
	datachks=(dc42head[72+0]<<24)|(dc42head[72+1]<<16)|(dc42head[72+2]<<8)|dc42head[72+3];
	tagchks =(dc42head[76+0]<<24)|(dc42head[76+1]<<16)|(dc42head[76+2]<<8)|dc42head[76+3];

	IDLE_TRACE("Header comment :\"%s\"",comment);
	IDLE_DEBUG("Data Size      :%ld (0x%08x)",datasize,datasize);
	IDLE_DEBUG("Tag total      :%ld (0x%08x)",tagsize,tagsize);
	IDLE_DEBUG("Disk format    :%d  ",diskformat);

	F->tagsize=12;
    F->numblocks=tagsize/F->tagsize;
	F->sectorsize=datasize/F->numblocks;


	IDLE_DEBUG("numblocks    :%d  ",F->numblocks);
	IDLE_DEBUG("sectorsize    :%d  ",F->sectorsize);

    if ((F->numblocks!=800) && (F->numblocks!=1702)){
       IDLE_TRACE("ERROR : bad image");
       return -1;                       
    }
	F->sectors=(unsigned char *)malloc(F->numblocks * F->sectorsize) ; 
    if ( !F->sectors) { return -1;}
    F->tags=(unsigned char *)malloc(F->numblocks * F->tagsize);     
    if ( !F->tags ) { return -1;}


    memset(F->sectors,  0,( F->numblocks * F->sectorsize) );
    memset(F->tags,     0,( F->numblocks * F->tagsize   ) );

	fseek(F->fhandle,84,SEEK_SET);
	ret=0;
	for (j=0;j<F->numblocks;j++)
	{
	    ret+=fread(&F->sectors[j*F->sectorsize],F->sectorsize,sizeof(char),F->fhandle);
     	if (errno) {
                      fclose(F->fhandle);
                      return -1;
                      }
     }
    IDLE_DEBUG("read %d",ret);
    
	mydatachks=0;
	for (i=0; i<F->numblocks; i++)
     for (j=0; j<F->sectorsize; j+=2) { mydatachks+=((F->sectors[i*(F->sectorsize)+j]<<8)|
                                                  (F->sectors[i*(F->sectorsize)+j+1]));
                                                  mydatachks=(mydatachks<<31)|(mydatachks>>1);
                                                  }
	ret=fread((char *) F->tags,F->tagsize,F->numblocks,F->fhandle);
	if (errno) {
                      fclose(F->fhandle);
                      return -1;
                      }
    IDLE_DEBUG("read %d",ret);

	mytagchks=0;
	for ( i=0; i<F->numblocks; i++)
            for (j=0; j<F->tagsize; j+=2) {
                      mytagchks+=(F->tags[i*(F->tagsize)+j]<<8)|
                                 F->tags[i*(F->tagsize)+j+1]; 
                      mytagchks=(mytagchks<<31)|(mytagchks>>1);
            }

	IDLE_TRACE("Header/Calc data chksum   :(0x%08x) ? (0x%08x):\n",datachks,mydatachks);
	IDLE_TRACE("Header/Calc tag chksum    :(0x%08x) ? (0x%08x):\n",tagchks,mytagchks);

    return 0;
}

int dc42_image_close(FloppyType *F)
{

uint32 i,j;
char comment[65];
unsigned char dc42head[84];
uint32 datasize, tagsize, datachks, tagchks, mydatachks, mytagchks;
int ret;
IDLE_INIT_FUNC("dc42_image_close()");
	errno=0;
IDLE_TRACE("called");

	fseek(F->fhandle,84,SEEK_SET);
    ret=0;
	for (j=0;j<F->numblocks;j++)
	{
	    ret+=fwrite(&F->sectors[j*F->sectorsize],F->sectorsize,sizeof(char),F->fhandle);
     	if (errno) {
                      fclose(F->fhandle);
                      return -1;
                      }
     }
    IDLE_TRACE("write %d",ret);


    
	mydatachks=0;
	for (i=0; i<F->numblocks; i++)
     for (j=0; j<F->sectorsize; j+=2) { mydatachks+=((F->sectors[i*(F->sectorsize)+j]<<8)|
                                                  (F->sectors[i*(F->sectorsize)+j+1]));
                                                  mydatachks=(mydatachks<<31)|(mydatachks>>1);
                                                  }
	ret=fwrite((char *) F->tags,F->tagsize,F->numblocks,F->fhandle);
	if (errno) {
                      fclose(F->fhandle);
                      return -1;
                      }
    IDLE_TRACE("write %d",ret);

	mytagchks=0;
	for ( i=0; i<F->numblocks; i++)
            for (j=0; j<F->tagsize; j+=2) {
                      mytagchks+=(F->tags[i*(F->tagsize)+j]<<8)|
                                 F->tags[i*(F->tagsize)+j+1]; 
                      mytagchks=(mytagchks<<31)|(mytagchks>>1);
            }


    IDLE_TRACE("chks %x/%x",mydatachks,mytagchks);
    
	fseek(F->fhandle,72,SEEK_SET);
    
    dc42head[72]=(mydatachks&0xFF000000)>>24;
    dc42head[73]=(mydatachks&0xFF0000)>>16;
    dc42head[74]=(mydatachks&0xFF00)>>8;
    dc42head[75]=(mydatachks&0xFF);

    dc42head[76]=(mytagchks&0xFF000000)>>24;
    dc42head[77]=(mytagchks&0xFF0000)>>16;
    dc42head[78]=(mytagchks&0xFF00)>>8;
    dc42head[79]=(mytagchks&0xFF);

	ret=fwrite((char *) &dc42head[72],4,2,F->fhandle);

    fclose(F->fhandle);

    return 0;
}


/*----------------------------------------------------------------------
/*----------------------------------------------------------------------

Some notes on reading a DART image file:

DART is a program written by David Mutter and Ken McLeod of Apple
Computer, Inc., for internal use in archiving and duplicating 3.5" floppy
disks. Because of its utility in distributing compressed disk images on
the Macintosh, DART is used in a number of Apple support products even
though DART is not an official Apple product and is not supported as such.

The format of a DART file is provided here for READ-ONLY use.
No guarantees are expressed or implied. Use at your own risk.

                           *  *  *  *  *
File Header
-----------
The first word in the data fork of a DART file contains a compression
identifier in the high byte (what type of compression is used), and a
disk identifier in the low byte (what sort of disk this image contains).
The second word contains the size of the source disk (e.g. 800=800K).

From this info, you can decide whether to interpret the file header as
a structure of type HDSrcInfoRec (for 1440K disks) or SrcInfoRec (for
everything smaller than 1440K.) The difference is in the size of the
block lengths array, bLength.


How DART creates a disk image
------------------------------
DART reads data from a 3.5" disk in blocks of 20480 bytes, starting with
sector 0 and continuing sequentially to the end of the disk. Each block
is read into a buffer capable of holding 20960 bytes. The remaining 480
bytes at the end of the buffer are filled with tag data (or zeroed, if
tags are not supported.) This 20960-byte buffer is then compressed and
written to the end of the image file. Finally, the appropriate element of
the block lengths array is updated with the compressed block length.


Important note about compressed block lengths
---------------------------------------------
Block lengths are always expressed in bytes, with the following two
exceptions:

  1) If the compression identifier is kRLECompress, the length of a
  compressed block is expressed in 16-bit words (or 2-byte units).
  Multiply by 2 to obtain the block size in bytes.
  
  2) If the block length is -1, then the block isn't compressed and
  is assumed to be 20960 bytes.


How to read a DART image file
------------------------------
The basic procedure is to position the file mark at the end of the file
header (which will either be sizeof(SrcInfoRec) or sizeof(HDSrcInfoRec)
bytes), and read bLength[n] bytes from the file into a buffer. You then
stream this compressed block through a decompression routine (either
RLEExpandBlock() or LZHExpandBlock(), depending on the file's compression
identifier) into a buffer capable of holding 20960 bytes. Remember that
the first 20480 bytes are data, while the remaining 480 bytes are tags.
Write out the data to disk, increment n, and repeat until bLength[n]==0
or you reach the end of the file.

Checksums
---------
DART uses the same checksum algorithm as Disk Copy (another Apple disk
utility) to verify the integrity of the disk data. The 32-bit checksums
for data and tags are stored separately in the resource fork of the image
file, rather than as part of the file header. The tag checksum is stored
in resource 'CKSM' ID=1, and the data checksum is stored in 'CKSM' ID=2.


/*----------------------------------------------------------------------
/*----------------------------------------------------------------------
	disk identifiers
/*----------------------------------------------------------------------*/
#define kMacDisk		1
#define	kLisaDisk		2
#define	kAppleIIDisk	3
		
#define	kMacHiDDisk		16
#define	kMSDOSLowDDisk	17
#define	kMSDOSHiDDisk	18

/*----------------------------------------------------------------------
	file types
/*----------------------------------------------------------------------*/

#define	kDartCreator	"DART"
#define	kOldFileType	"DMdf"
#define	kDartPrefsType	"DMd0"
#define	kMac400KType	"DMd1"
#define	kLisa400KType	"DMd2"
#define	kMac800KType	"DMd3"
#define	kApple800KType	"DMd4"
#define	kMSDOS720KType	"DMd5"
#define	kMac1440KType	"DMd6"
#define	kMSDOS1440KType	"DMd7"
#define	kDiskCopyType	"dImg"

/*----------------------------------------------------------------------
	compression identifiers
/*----------------------------------------------------------------------*/

#define	kRLECompress	0		/* "fast" algorithm */
#define	kLZHCompress	1		/* "best" algorithm */
#define	kNoCompress		2		/* not compressed */

/*----------------------------------------------------------------------
	data structures
/*----------------------------------------------------------------------*/

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned long ulong;

typedef struct SrcInfoRec
{
	uchar	srcCmp;			/* compression identifier */
	uchar	srcType;		/* disk type identifier (Lisa, Mac, etc.) */
	short	srcSize;		/* size of source disk in Kb (e.g. 800=800K) */
	short	bLength[40];	/* array of block lengths */
	/* variable-length compressed disk data follows... */
}	SrcInfoRec;

typedef struct HDSrcInfoRec
{
	uchar	srcCmp;			/* compression identifier */
	uchar	srcType;		/* disk type identifier (Lisa, Mac, etc.) */
	short	srcSize;		/* size of source disk in Kb (e.g. 800=800K) */
	short	bLength[72];	/* array of block lengths */
	/* variable-length compressed disk data follows... */
}	HDSrcInfoRec;


#define DDBLOCKSIZE	20960	/* size of an uncompressed block, in bytes */

typedef uchar DiskData[DDBLOCKSIZE], *DDPtr;

static int decodeRLE( unsigned char *in,
                      int inLen,
                      unsigned char * out,
                      int outLen) {
int i,j,k;
unsigned char val,num;
IDLE_INIT_FUNC("decodeRLE()");

k=0;
for (i=0;i<inLen;i+=2) {
    val=in[i+1];
    num=in[i];
    IDLE_TRACE("i=%d val=%d num=%d",i,val,num);
    for (j=0;j<num;j++,k++) {
            if (k>=outLen) goto error;
            out[k]=val;
        }
    }
    IDLE_TRACE("%d byte produced",k);
    return 0;
error:
    IDLE_WARN("output buffer overflow !!!");
    return 1;
}

/*
 * the DART image loader
 */
int dart_image_open(FloppyType *F)
{

    unsigned char dartInfo[4];
    unsigned char tmp[2];
    int dartCompressType;
    int dartDiskIdentifier;
    int dartDiskSize;
    int srcCmp;
    int srcType;
    int srcSize;
    int bLength[40];
    int i;
    unsigned char BufferIn[DDBLOCKSIZE];
    unsigned char BufferOut[DDBLOCKSIZE];
    

    IDLE_INIT_FUNC("dart_image_open()");
	errno=0;
	fseek(F->fhandle, 0,0);
	fread(dartInfo,4,1,F->fhandle);
	dartCompressType=dartInfo[0];
	dartDiskIdentifier=dartInfo[1];
	dartDiskSize=(dartInfo[2]<<8)|dartInfo[3];

    IDLE_TRACE("Compress=%d ident=%d size=%d",
                dartCompressType,dartDiskIdentifier,dartDiskSize);
                
    switch (dartDiskIdentifier) {
           case kLisaDisk :
                IDLE_TRACE("Disk is Lisa Type");
                break;
           case kMacDisk :
                IDLE_TRACE("Disk is Mac Type");
                break;
           default:
                   IDLE_TRACE("Disk type %d not handled abort",dartDiskIdentifier);
                    goto error;
    }

    if ((dartDiskSize!=400) && (dartDiskSize!=800)) {
         IDLE_TRACE("Disk size %d should be 400 or 800 abort",dartDiskSize);
         goto error;
    }    
    
    // reads the length array
    for (i=0;i<40;i++) {
    	if (fread(tmp,2,1,F->fhandle)!=1) goto error;
    	bLength[i]=(tmp[0]<<8)|tmp[1];
    }
    
    switch (dartCompressType) {
        case kRLECompress :
             IDLE_TRACE("Compression RLE");
             for (i=0;i<2;i++) {
                 IDLE_TRACE("length=%d",bLength[i]);
                 if (bLength[i]==0xFFFF) { // block is uncompressed
                    if (fread(BufferOut,DDBLOCKSIZE,1,F->fhandle)!=1) goto error;
                 }
                 else { // block is compressed
                    if (fread(BufferIn,bLength[i]*2,1,F->fhandle)!=1) goto error;
                    // now decode RLE ...
                    decodeRLE(BufferIn,bLength[i]*2,BufferOut,DDBLOCKSIZE);
                 }
             } 
             break;
        case kLZHCompress :
             IDLE_TRACE("Compression LZH");
             break;
        case kNoCompress :
             IDLE_TRACE("No compression");
             break;
        default :
                IDLE_TRACE("Compression method not handled");
                goto error;
    }

    fclose(F->fhandle);
   
  return 0;
error:
    IDLE_TRACE("Error reading DART image");
    fclose(F->fhandle);
    return 1;
}
