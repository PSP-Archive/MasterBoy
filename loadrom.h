#ifndef _LOADROM_H_
#define _LOADROM_H_

/* Function prototypes */
int load_rom(char *filename);
unsigned char *loadzip(char *archive, char *filename, int *filesize);

#endif /* _LOADROM_H_ */

