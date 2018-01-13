#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#if (defined(__x86_64__) || defined(__amd64__) || defined(_M_AMD64)) && \
     defined(_POSIX_MAPPED_FILES) && ((_POSIX_MAPPED_FILES -0) > 0)
#include <sys/mman.h>
#define ENABLE_JIT
#else
#warning JIT has been disabled.
#endif

#define MAX_BRACE_DEPTH 256
#define MAX_LOOP_MOVES 2048

char validChars[] = "+-<>[].,"; 
char compressChars[] = "+-<>";

unsigned char *mem; // must be global so asm can see it...

int getFileLen(FILE *fp) {
	fseek(fp, 0L, SEEK_END);
	int len = ftell(fp);
	fseek(fp, 0L, SEEK_SET);
	return len;
}

int isInStr(char c, char *str) {
	for (int i = 0; i < strlen(str); i++) {
		if (c == str[i]) return 1;
	}
	return 0;
}

#ifdef ENABLE_JIT
void jit(char *prog, int *meta, int *metaB, int progLen) {
	static char addMem[] = {0x41, 0x8a, 0x07, 0x04, 'X', 0x41, 0x88, 0x07};
	/*
	41 8a 07             	mov    (%r15),%al
	04 X                 	add    X,%al
	41 88 07             	mov    %al,(%r15)
	*/
	static char setMem[] = {0xb0, 'X', 0x41, 0x88, 0x07};
	/*
	b0 X                 	mov    $X,%al  // two stages may be unnecessary
	41 88 07             	mov    %al,(%r15)
	*/
	static char mulMemOffset[] = {0x41, 0x8a, 0x07, 0xb3, 'X', 0xf6, 0xeb, 0x4d, 0x89, 0xfe, 0x49, 0x83, 0xc6, 'Y', 0x41, 0x8a, 0x1e, 0x00, 0xc3, 0x41, 0x88, 0x1e}; // TODO: simplify this
	/*
	41 8a 07             	mov    (%r15),%al
	b3 X                 	mov    X,%bl
	f6 eb                	imul   %bl
	4d 89 fe             	mov    %r15,%r14
	49 83 c6 Y           	add    Y,%r14
	41 8a 1e             	mov    (%r14),%bl
	00 c3                	add    %al,%bl
	41 88 1e             	mov    %bl,(%r14)
	*/
	static char addPtr[] = {0x49, 0x81, 0xc7, 'X', 'X', 'X', 'X'}; // TODO this can be optimised when XXXX fits in a single byte (most of the time)
	/*
	49 81 c7 X X X X     	add    XXXX,%r15
	*/
	static char printR15[] = {0x48, 0xc7, 0xc0, 0x01, 0x00, 0x00, 0x00, 0x48, 0x89, 0xc7, 0x48, 0x89, 0xc2, 0x4c, 0x89, 0xfe, 0x0f, 0x05};
	/*
	48 c7 c0 01 00 00 00 	mov    $0x1,%rax // TODO: maybe this could be optimised
	48 89 c7             	mov    %rax,%rdi
	48 89 c2             	mov    %rax,%rdx
	4c 89 fe             	mov    %r15,%rsi
	0f 05                	syscall 
	*/
	static char openBrace[] = {0x41, 0x8a, 0x07, 0x84, 0xc0, 0x0f, 0x84, 'X', 'X', 'X', 'X'}; // TODO this can be optimised when XXXX is small
	/*
	41 8a 07             	mov    (%r15),%al
	84 c0                	test   %al,%al
	0f 84 X X X X        	je     XXXX
	*/
	static char closeBrace[] = {0x41, 0x8a, 0x07, 0x84, 0xc0, 0x0f, 0x85, 'X', 'X', 'X', 'X'}; // TODO this can be optimised when XXXX is small
	/*
	41 8a 07             	mov    (%r15),%al
	84 c0                	test   %al,%al
	0f 85 X X X X        	jne    XXXX
	*/
	static char readR15[] = {
		0x48, 0xc7, 0xc0, 0x01, 0x00, 0x00, 0x00,
		0x48, 0x89, 0xc2,
		0x48, 0xc7, 0xc0, 0x00, 0x00, 0x00, 0x00,
		0x48, 0x89, 0xc7,
		0x4c, 0x89, 0xfe,
		0x0f, 0x05};
	/*
	48 c7 c0 01 00 00 00 	mov    $0x1,%rax // TODO: maybe this could be optimised
	48 89 c2             	mov    %rax,%rdx
	48 c7 c0 00 00 00 00 	mov    $0x0,%rax // TODO: maybe this could be optimised
	48 89 c7             	mov    %rax,%rdi
	4c 89 fe             	mov    %r15,%rsi
	0f 05                	syscall 
	*/
	static char ret[] = {0xc3};

	void *binary = mmap(NULL, 1000000, PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0); // TODO calculate actual length
	int binPtr = 0;
	char cval; // used for casting int to char
	int ival;
	
	for (int i = 0; i < progLen; i++) {
		switch(prog[i]) {
			case '+':
				memcpy(binary + binPtr, &addMem, sizeof(addMem));
				cval = meta[i];
				memcpy(binary + binPtr + 4, &cval, sizeof(cval));
				binPtr += sizeof(addMem);
				break;
			case 'A':
				memcpy(binary + binPtr, &setMem, sizeof(setMem));
				cval = meta[i];
				memcpy(binary + binPtr + 1, &cval, sizeof(cval));
				binPtr += sizeof(setMem);
				break;
			case 'M': // TODO repeated M instructions can reuse r14
				memcpy(binary + binPtr, &mulMemOffset, sizeof(mulMemOffset));
				cval = metaB[i];
				memcpy(binary + binPtr + 4, &cval, sizeof(cval));
				cval = meta[i];
				memcpy(binary + binPtr + 13, &cval, sizeof(cval));
				binPtr += sizeof(mulMemOffset);
				break;
			case '>':
				memcpy(binary + binPtr, &addPtr, sizeof(addPtr));
				memcpy(binary + binPtr + 3, &meta[i], sizeof(int));
				binPtr += sizeof(addPtr);
				break;
			case '.':
				memcpy(binary + binPtr, &printR15, sizeof(printR15));
				binPtr += sizeof(printR15);
				break;
			case ',':
				memcpy(binary + binPtr, &readR15, sizeof(readR15));
				binPtr += sizeof(readR15);
				break;
			case '[':
				memcpy(binary + binPtr, &openBrace, sizeof(openBrace));
				binPtr += sizeof(openBrace);
				metaB[meta[i]] = binPtr; // so ] knows where to jump to
				break;
			case ']':
				memcpy(binary + binPtr, &closeBrace, sizeof(closeBrace));
				ival = binPtr - metaB[i];// how far to jump forwards from corresponding [
				memcpy(binary + metaB[i] - 4, &ival, sizeof(int));
				ival = -ival - sizeof(closeBrace); // how far to jump back 
				memcpy(binary + binPtr + 7, &ival, sizeof(int));
				binPtr += sizeof(closeBrace);
				break;
		}
	}
	memcpy(binary + binPtr, &ret, sizeof(ret));
	
	int (*myFunction)() = binary;
	mem = calloc(30000, 1);
	asm("mov mem, %r15"); // r15 stores the memory pointer
	myFunction();
	munmap(binary, 1000000);
	free(mem);
}
#endif

void interpret(char *prog, int *meta, int *metaB, int progLen) {
	
	printf("Interpreting...\n");
	
	int iPtr = 0;
	int memPtr = 0;
	mem = calloc(30000, 1);
	while (iPtr < progLen) {
		switch(prog[iPtr]) {
		case '+':
			mem[memPtr] += meta[iPtr];
			break;
		case '>':
			memPtr += meta[iPtr];
			break;
		case '[':
			if (mem[memPtr] == 0) {
				iPtr = meta[iPtr];
			}
			break;
		case ']':
			if (mem[memPtr] != 0) {
				iPtr = meta[iPtr];
			}
			break;
		case 'A':
			mem[memPtr] = meta[iPtr];
			break;
		case 'M': // meta stores absolute destination, metaB is multitplied by mem[memPtr], and added to dest
			mem[memPtr + meta[iPtr]] += mem[memPtr] * metaB[iPtr]; // mem[ptr+A] += mem[ptr]* b
			break;
		case '.':
			putc(mem[memPtr], stdout);
			break;
		case ',':
			mem[memPtr] = getchar();
			break;
		}
		iPtr++;
	}
	free(mem);
}

int main(int argc, char *argv[]) {

	if (argc != 2 && argc != 3) {
		printf("Usage: %s input.b [-interpret]\n", argv[0]);
		return 0;
	}
	
	/* Load file into prog */
	
	FILE *inFile = fopen(argv[1], "r");
	int fLen = getFileLen(inFile);
	char *prog = malloc(fLen);
	fread(prog, 1, fLen, inFile);
	
	/* strip invalid chars */
	
	int ptr = 0;
	for (int i = 0; i < fLen; i++) {
		if (isInStr(prog[i], validChars)) prog[ptr++] = prog[i];
	}
	int progLen = ptr;
	
	int *meta = calloc(progLen, sizeof(int)); // multipurpose optimisation metadata
	int *metaB = calloc(progLen, sizeof(int)); 
	char currentChar = prog[0];
	unsigned int count = 1;
	ptr = 1; // ptr points to where we are writing to
	
	/* Compress repeated instructions */
	
	for (int i = 1; i < progLen; i++) {
		if (prog[i] == currentChar) {
			count++;
			if (!isInStr(currentChar, compressChars)) prog[ptr++] = currentChar;
		} else {
			if (isInStr(currentChar, compressChars)) {
				meta[ptr-1] = count;
			}
			count = 1;
			currentChar = prog[i];
			prog[ptr++] = prog[i];
		}
	}
	progLen = ptr;
	
	/* Remove - and < */
	
	for (int i = 0; i < progLen; i++) {
		if (prog[i] == '-') {
			prog[i] = '+';
			meta[i] = -meta[i];
		} else if (prog[i] == '<') {
			prog[i] = '>';
			meta[i] = -meta[i];
		}
	}
	
	/* Compress assignment */
	
	ptr = 0;
	for (int i = 0; i < progLen; i++) {
		if(memcmp(&prog[i], "[+]", 3) == 0 && abs(meta[i+1]) == 1) {
			prog[ptr] = 'A';
			if (prog[i+3] == '+') {
				meta[ptr] = meta[i+3];
				i++;
			} else {
				meta[ptr] = 0;
			}
			i += 2;
			ptr++;
		} else {
			meta[ptr] = meta[i];
			prog[ptr++] = prog[i];
		}
	}
	progLen = ptr;
	
	/* Compress multiplication/move loops */
	
	char *tmpProg = malloc(progLen);
	int *tmpMeta = malloc(progLen * sizeof(int));
	memcpy(tmpProg, prog, progLen);
	memcpy(tmpMeta, meta, progLen * sizeof(int));
	int openPtr = 0;
	ptr = 0;
	for (int i = 0; i < progLen; i++) {
		if (tmpProg[i] == '[') {
			openPtr = i;
		}
		if (tmpProg[i] == ']' && openPtr != 0) {
			char *counts = calloc(MAX_LOOP_MOVES, sizeof(char));
			int countPtr = 0;
			int failed = 0;
			for (int j = openPtr+1; j < i; j++) {
				switch(tmpProg[j]) {
					case '+':
						if (countPtr < -128 || countPtr > 127) /* && is Jit */
							failed = 1;
						else
							counts[countPtr+(MAX_LOOP_MOVES/2)] += tmpMeta[j];
						break;
					case '>':
						countPtr += tmpMeta[j];
						break;
					default: // if there are any other instructions, we can't optimise
						failed = 1;
				}
			}
			if (countPtr == 0 && !failed && (counts[MAX_LOOP_MOVES/2]) == -1) {
				ptr = openPtr - (i - ptr);
				for (int j = -(MAX_LOOP_MOVES/2); j < (MAX_LOOP_MOVES/2); j++) {
					if(counts[j+(MAX_LOOP_MOVES/2)] != 0 && j != 0) {
						prog[ptr] = 'M';
						meta[ptr] = j;
						metaB[ptr] = counts[j+(MAX_LOOP_MOVES/2)];
						ptr++;
					}
				}
				prog[ptr] = 'A';
				meta[ptr] = 0; // TODO this could be combined with the next instruction.
				ptr++;
				openPtr = 0;
			} else {
				prog[ptr] = tmpProg[i];
				meta[ptr] = tmpMeta[i];
				ptr++;
			}
			free(counts);
			openPtr = 0;
		} else {
			prog[ptr] = tmpProg[i];
			meta[ptr] = tmpMeta[i];
			ptr++;
		}
	}
	free(tmpProg);
	free(tmpMeta);
	progLen = ptr;
	
	/* Index braces */
	
	ptr = 0;
	unsigned int braceStack[MAX_BRACE_DEPTH];
	int braceStackPtr = 0; // points to next empty location on stack
	
	for (int i = 0; i < progLen; i++) {
		if (prog[i] == '[') {
			braceStack[braceStackPtr++] = i;
			if (braceStackPtr >= MAX_BRACE_DEPTH) {
				printf("Error: To many nested braces!\n");
				return 1;
			}
		} else if (prog[i] == ']') {
			braceStackPtr--;
			if (braceStackPtr < 0) {
				printf("Error: Too many ']'. Or maybe not enough '['?\n");
				return 1;
			}
			meta[braceStack[braceStackPtr]] = i;
			meta[i] = braceStack[braceStackPtr];
		}
	}
	
	if (braceStackPtr > 0) {
		printf("Error: Not enough ']'. Or maybe too many '['?\n");
		return 1;
	}
	
	//prog[progLen] = 0;
	//printf("%s\n", prog);
	
	setbuf(stdout, NULL); // disable output buffering
	
	prog = realloc(prog, progLen); // shrink program arrays to fit optimised program
	meta = realloc(meta, progLen * sizeof(int));
	metaB = realloc(metaB, progLen * sizeof(int));
	
#ifdef ENABLE_JIT
	if (argc == 2) {
		jit(prog, meta, metaB, progLen);
	} else
#endif
		interpret(prog, meta, metaB, progLen);
	
	free(prog);
	free(meta);
	free(metaB);
	return 0;
}
