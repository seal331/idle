
int dc42_insert(char *filename,int drive);
int dart_insert(char *filename,int drive);
int read_sector(int drive,int track,int sector,int side,unsigned char **data,unsigned char **tags,int *datasize,int *tagsize);
int write_sector(int drive,int track,int sector,int side,unsigned char *data,unsigned char *tags);
void unclamp(int drive);
int check_inserted(int drive);
