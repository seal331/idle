/*
	An experimental macintosh MPW object to ASM converter
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"



typedef struct 
{
	char *val;
	int id;
} symbolEntry_t;


symbolEntry_t *l_symbolTab=NULL;
int l_symbolTabSize=0;

char *
getSymbol(int id) {
	int i;
	for (i=0;i<l_symbolTabSize;i++) {
		if (l_symbolTab[i].id==id) {
			return l_symbolTab[i].val;
		}
	}
	return NULL;
}


void
addSymbol(char *val,int id) {
	if (getSymbol(id)==NULL) {
		l_symbolTabSize++;
		if (l_symbolTabSize==1) {
			l_symbolTab=(symbolEntry_t*)malloc(l_symbolTabSize*sizeof(symbolEntry_t));
		} else {
			l_symbolTab=realloc(l_symbolTab,l_symbolTabSize*sizeof(symbolEntry_t));
		}
		l_symbolTab[l_symbolTabSize-1].id=id;
		l_symbolTab[l_symbolTabSize-1].val=malloc(strlen(val)+1);
		strcpy(l_symbolTab[l_symbolTabSize-1].val,val);
	}
}



int 
readByte(FILE *f) {
	return ((int) getc(f));
}

int
readWord(FILE *f) {
	return (((int)fgetc(f))<<8)|((int)fgetc(f));
}

void
decodeObjFile(FILE *fin) {
	int type;
	int curId=-1;
	while (!feof(fin)) {
		type=readByte(fin);
		if (feof(fin)) break;
		
		switch (type) {
			// pad record
			case 0 : break;
			// first record
			case 1 : {
				int flags;
				int version;
				flags=readByte(fin);
				version=readWord(fin);
				fprintf(stdout,"* First record flags=%x version=%d\n",flags,version);
				break;
			}
			// last record
			case 2 : {
				int dummy;
				dummy=readByte(fin);
				fprintf(stdout,"* Last record\n");
				break;
			}
			// comment record
			case 3 : {
				int size;
				unsigned char *s;
				size=readWord(fin)-4;
				s=malloc(size);
				fread(s,size,sizeof(char),fin);
				// truncate to 80 max
				if (size>78) {
					s[77]='\0';
				}
				fprintf(stdout,"* comment\n");
				fprintf(stdout,"* %s\n",s);				
				free(s);
				break;
			}
			// dictionnary record
			case 4 : {
				int flags;
				int size;
				int firstId;
				int where;
				int idNum;
				unsigned char *s;
				flags=readByte(fin);
				size=readWord(fin)-5;				
				firstId=readWord(fin);		
				idNum=firstId;
				fprintf(stdout,"* Dictionnary flags=x%x firstID=x%x\n",flags,firstId);
				where=0;
				while (where<size) {
					int symbolSize;
					symbolSize=readByte(fin);
					// padding here also...
					if (symbolSize==0) {
						break;
					}
					s=malloc(symbolSize+1);
					fread(s,symbolSize,sizeof(char),fin);
					s[symbolSize]='\0';
					fprintf(stdout,"* Symbol=%05d %s\n",idNum,s);
					// create entry in symbol table
					addSymbol(s,idNum);
					free(s);
					where+=1+symbolSize;
					idNum++;
					//fprintf(stdout,"%d %d \n",where,size);
				}
				break;
			}
			// code or data module desc
			case 5 : {
				int flags;
				int moduleId;
				int segmentId;
				int size;
				flags=readByte(fin);
				moduleId=readWord(fin);
				curId=moduleId;
				fprintf(stdout,"* code or data module\n");
				
				if ((flags&0x01)==0) {
					segmentId=readWord(fin);
					fprintf(stdout,"* flags=x%x moduleId=%d segmentId=%d\n",flags,moduleId,segmentId);
				} else {
					size=readWord(fin);
					fprintf(stdout,"* flags=x%x moduleId=%d size=%d\n",flags,moduleId,size);
				}
				break;
			}
			// code or data content
			case 8 : {
				int flags;
				int moduleId;
				int segmentId;
				int size;
				unsigned char *s;
				flags=readByte(fin);
				fprintf(stdout,"* content\n");
				size=readWord(fin)-4;
				s=malloc(size);
				fread(s,size,sizeof(char),fin);
				fprintf(stdout,"* flags=x%x size=%d\n",flags,size);
				if ((flags &0x01)==0) {
					char *symb=getSymbol(curId);
					if (symb!=NULL) {
						fprintf(stdout,"%s:\n",symb);
					}
					// code here
					unassemble_block(s,4,size);
				}
				free(s);
				break;
			}
			// reference
			case 9 : {
				int flags;
				int moduleId;
				int segmentId;
				int size;
				unsigned char *s;
				flags=readByte(fin);
				fprintf(stdout,"* reference\n");
				size=readWord(fin)-4;
				s=malloc(size);
				fread(s,size,sizeof(char),fin);
				fprintf(stdout,"* reference size=%d\n",size);
				free(s);
				break;
			}			
			default : {
				fprintf(stdout,"* Unknown block %x\n",type);
			}
		}
	}
}


void die(char * msg) {
	fprintf(stderr,"ABORT : %s\n",msg);
	fprintf(stderr,"Usage : blah blah\n");
	exit(-1);
}

void success(char * msg) {
	fprintf(stderr,"%s\n",msg);
	exit(0);
}

int
main(int argc, char *argv[]) {
	FILE *f;

	if (argc<2) die("not enough parameters");
	f=fopen(argv[1],"rb");
	if (f==NULL) {
		die("cannot open file");
	}
	
	decodeObjFile(f);
	
	success("");
}
