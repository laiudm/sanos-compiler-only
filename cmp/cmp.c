#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define BLKSIZE 4096

void usage() {
	fprintf(stderr, "Usage: cmp file1 file2 <optional-offset> \n");
	exit(1);
}

void error(char *text) {
	fprintf(stderr, "Error: %s\n", text);
	exit(1);
}

int main(int argc, char *argv[]) {
	char buffer1[BLKSIZE];
	char buffer2[BLKSIZE];
	struct stat st1;
	struct stat st2;
	long offset = 0;
	
	//printf("cmp - argc = %i\n Exiting with exit()\n", argc);
	
	if ( !(argc == 3 || argc == 4) ) 
		usage();
	
	if (argc == 4)
		offset = strtol(argv[3], NULL, 0);	// paras: start-ptr, end-ptr, base=0 means taken from string
	
	// open 1st file
	int file1 = open(argv[1], O_RDONLY | O_BINARY);
	if (file1 < 0  || fstat(file1, &st1) < 0)
		error("Can't open file1\n");
	
	// open 2nd file
	int file2 = open(argv[2], O_RDONLY | O_BINARY);
	if (file2 < 0  || fstat(file2, &st2) < 0)
		error("Can't open file2\n");
	
	// compare file lengths
	if (st1.st_size != st2.st_size) 
		error("File lengths don't match\n");
	
	// move each file to the specified offset
	if ( (lseek(file1, offset, SEEK_SET) < 0) || (lseek(file2, offset, SEEK_SET) < 0) )
		error("Invalid offset\n");
		

	// read each file block by block
	int blockCount = 0;
	int n1, n2;
	while ((n1 = read(file1, buffer1, BLKSIZE)) != 0) {
		if (n1 < 0)
			error("Read error on file1\n");
		n2 = read(file2, buffer2, BLKSIZE);
		if (n2 != n1)
			error("Read error on file2\n");
		
		int i;
		for (i=0; i < n1; i++) {
			if (buffer1[i] != buffer2[i] ) {
				printf("File mismatch on block 0x%x, offset 0x%x. Left = %x, Right = %x, n1 = %i\n", blockCount, i, buffer1[i], buffer2[i], n1);
				exit(1);
				//error("Files don't match\n");
			}
		}
		blockCount++;
	}
	return 0;
}
