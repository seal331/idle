/*
	Xprofile image tool
	Only STAR 04 and 06 ar supported
	Only 5Mb image
	
	xprofile_tool [extract04|replace04|extract06|replace06] [odd|even] [image] [idle_image]

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROFILE5MB 0x2600
#define WIDGET	0X4C00



void die(char * msg) {
	fprintf(stderr,"ABORT : %s\n",msg);
	fprintf(stderr,"Usage : xprofile_tool [extract04|replace04|extract06|replace06] [odd|even] [image] [idle_image]\n");
	exit(-1);
}

void success(char * msg) {
	fprintf(stderr,"%s\n",msg);
	exit(0);
}

void
extract04_image(int partition,char * xp_image,char * idle_image) {
   char header[512];
   char buffer[512];
   char tags[512];
   FILE* XP;
   FILE* IDLE;
   XP=fopen(xp_image,"rb");
   IDLE=fopen(idle_image,"wb");
   fseek(XP,0X17CF00,SEEK_SET);
   fread(header,1,512,XP);
   
   {
     int sector16=0;
     int sector;
     
     for (sector16=0;(sector16<PROFILE5MB/16);sector16++) {
         fseek(XP,0x18000+(sector16*17+16)*1024+partition*512,SEEK_SET);
         fread(tags,1,512,XP);
         
         for (sector=0;sector<16;sector++) {
             fseek(XP,0x18000+(sector16*17+(13*sector)%16)*1024+partition*512,SEEK_SET);
             fread(buffer,1,512,XP);
         
             fwrite(&tags[32*((13*sector)%16)],1,20,IDLE);
             fwrite(buffer,1,512,IDLE);
         }
     }
     
   }
   fclose(XP);
   fclose(IDLE);
}

void
replace04_image(int partition,char * xp_image,char * idle_image) {
   char header[512];
   char buffer[512];
   char tags[512];
   FILE* XP;
   FILE* IDLE;
   XP=fopen(xp_image,"rb+");
   IDLE=fopen(idle_image,"rb");
   fseek(XP,0X17CF00,SEEK_SET);
   fread(header,1,512,XP);
   
   {
     int sector16=0;
     int sector;
     
     for (sector16=0;(sector16<PROFILE5MB/16);sector16++) {
         memset(tags,0,512);         
         for (sector=0;sector<16;sector++) {

             fread(&tags[32*((13*sector)%16)],1,20,IDLE);
             fread(buffer,1,512,IDLE);
    
             fseek(XP,0x18000+(sector16*17+((13*sector)%16))*1024+partition*512,SEEK_SET);
             fwrite(buffer,1,512,XP);
         
         }
         fseek(XP,0x18000+(sector16*17+16)*1024+partition*512,SEEK_SET);
         fwrite(tags,1,512,XP);
     }
     
   }
   fclose(XP);
   fclose(IDLE);
}

void
extract06_image(int partition,char * xp_image,char * idle_image) {
   char header[512];
   char buffer1[512];
   char buffer2[512];
   FILE* XP;
   FILE* IDLE;
   XP=fopen(xp_image,"rb");
   IDLE=fopen(idle_image,"wb");
   fseek(XP,0X17CF00,SEEK_SET);
   fread(header,1,512,XP);
   int sector16,sector;
     
     for (sector16=0;(sector16<(PROFILE5MB/16));sector16++) {
         for (sector=0;sector<16;sector++) {         
             fseek(XP,0x18000+(sector16*16*2+2*((13*sector)%16))*1024+partition*512,SEEK_SET);
             fread(buffer1,1,512,XP);

             fseek(XP,0x18000+(sector16*16*2+2*((13*sector)%16)+1)*1024+partition*512,SEEK_SET);
             fread(buffer2,1,512,XP);
         
             fwrite(buffer1,1,512,IDLE);
             fwrite(&buffer2[512-20],1,20,IDLE);
         }
     
     
   }
   fclose(XP);
   fclose(IDLE);
}

void
replace06_image(int partition,char * xp_image,char * idle_image) {
   char header[512];
   char buffer[512];
   char buffer2[512];
   FILE* XP;
   FILE* IDLE;
   XP=fopen(xp_image,"rb+");
   IDLE=fopen(idle_image,"rb");
   fseek(XP,0X17CF00,SEEK_SET);
   fread(header,1,512,XP);
   
   {
     int sector16=0;
     int sector;
     
     for (sector16=0;(sector16<PROFILE5MB/16);sector16++) {
         for (sector=0;sector<16;sector++) {

             fread(buffer,1,512,IDLE);
             fread(buffer2,1,20,IDLE);
             
             fseek(XP,0x18000+(sector16*16*2+2*((13*sector)%16))*1024+partition*512,SEEK_SET);
             fwrite(buffer,1,512,XP);

             fseek(XP,0x18000+(sector16*16*2+2*((13*sector)%16)+1)*1024+partition*512,SEEK_SET);
             fwrite(&buffer[20],1,512-20,XP);
             fwrite(buffer2,1,20,XP);
         }
      }    
   }
   fclose(XP);
   fclose(IDLE);
}

int
main(int argc, char *argv[]) {
	if (argc<4) die("not enough parameters");
	
	if (strcmp(argv[1],"extract04")==0) {
       if (strcmp(argv[2],"even")==0) {
       extract04_image(0,argv[3],argv[4]);
       success("extracted type 04 star OK");
       } else {
       extract04_image(1,argv[3],argv[4]);
       success("extracted type 04 star OK");
       }       
    }
    
  	if (strcmp(argv[1],"replace04")==0) {
       if (strcmp(argv[2],"even")==0) {
       replace04_image(0,argv[3],argv[4]);
       success("replaced type 04 star OK");
       } else {
       replace04_image(1,argv[3],argv[4]);
       success("replaced type 04 star OK");
       }   
    }

	if (strcmp(argv[1],"extract06")==0) {
       if (strcmp(argv[2],"even")==0) {
       extract06_image(0,argv[3],argv[4]);
       success("extracted type 04 star OK");
       } else {
       extract06_image(1,argv[3],argv[4]);
       success("extracted type 04 star OK");
       }       
    }
    
  	if (strcmp(argv[1],"replace06")==0) {
       if (strcmp(argv[2],"even")==0) {
       replace06_image(0,argv[3],argv[4]);
       success("replaced type 06 star OK");
       } else {
       replace06_image(1,argv[3],argv[4]);
       success("replaced type 06 star OK");
       }   
    }
    
    die("No command executed");
}
