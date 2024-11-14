#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <Windows.h>
#include <conio.h>

typedef uint16_t u16;
typedef uint8_t  u8;
typedef int16_t  s16;
typedef int8_t   s8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t  s32;
typedef int64_t  s64;


#define MEMORY_MAX (1 << 16)
u16 memory[MEMORY_MAX]; /* 65536 locations */

enum
{
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC, /* program counter */
    R_COND,
    R_COUNT
};

u16 reg[R_COUNT];
enum
{
    FL_POS = 1 << 0, /* P */
    FL_ZRO = 1 << 1, /* Z */
    FL_NEG = 1 << 2, /* N */
};

enum
{
    OP_BR = 0, /* branch */
    OP_ADD,    /* add  */
    OP_LD,     /* load */
    OP_ST,     /* store */
    OP_JSR,    /* jump register */
    OP_AND,    /* bitwise and */
    OP_LDR,    /* load register */
    OP_STR,    /* store register */
    OP_RTI,    /* unused */
    OP_NOT,    /* bitwise not */
    OP_LDI,    /* load indirect */
    OP_STI,    /* store indirect */
    OP_JMP,    /* jump */
    OP_RES,    /* reserved (unused) */
    OP_LEA,    /* load effective address */
    OP_TRAP    /* execute trap */
};
HANDLE hStdin = INVALID_HANDLE_VALUE;
DWORD fdwMode, fdwOldMode;

void disable_input_buffering(){
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &fdwOldMode); /* save old mode */
    fdwMode = fdwOldMode
            ^ ENABLE_ECHO_INPUT  /* no input echo */
            ^ ENABLE_LINE_INPUT; /* return when one or
                                    more characters are available */
    SetConsoleMode(hStdin, fdwMode); /* set new mode */
    FlushConsoleInputBuffer(hStdin); /* clear buffer */
}

void restore_input_buffering(){
	SetConsoleMode(hStdin, fdwOldMode);
}


u16 check_key(){
	return WaitForSingleObject(hStdin, 1000) == WAIT_OBJECT_0 && _kbhit();
}


u16 sign_extend(u16 x, int bit_count) {
	if ((x >> (bit_count - 1)) & 1) {
		x |= (0xFFFF << bit_count);
	}
	return x;
}



void update_flags(u16 r) {
	if (reg[r] == 0){
		reg[R_COND] = FL_ZRO;
	}else if(reg[r] >> 15) {
		reg[R_COND] = FL_NEG;
	}else{
		reg[R_COND] = FL_POS;
	}
}


enum {
	TRAP_GETC = 0x20, /* Get chraracter from keyboard, not echoed onto the terminal*/
	TRAP_OUT  = 0x21, /* output a character */
	TRAP_PUTS = 0x22, /* output a word string*/
	TRAP_IN   = 0x23, /* get chracter from keybaord, echoed onto the terminal*/
	TRAP_PUTSP = 0x24, /* output a byte string */
	TRAP_HALT  = 0x25, /* halt the program */
};

//LC-3 programs are big-endian
u16 swap16(u16 x){
	return (x << 8) | (x >> 8);
}

void read_image_file(FILE* file){
	u16 origin;
	fread(&origin, sizeof(origin), 1, file);
	origin = swap16(origin);

	u16 max_read = MEMORY_MAX - origin;
	u16* p = memory + origin;
	size_t read = fread(p, sizeof(u16), max_read, file);

	while(read-- > 0){
		*p = swap16(*p);
		++p;
	}
}


enum {
	MR_KBSR = 0xFE00, /* Keyboard status */
	MR_KBDR = 0xFE02, /* Keyboard data   */
};
void mem_write(u16 address, u16 val){
	memory[address] = val;
}

u16 mem_read(u16 address){
	if(address == MR_KBSR){
		if(check_key()){
			memory[MR_KBSR] = (1 << 15);
			memory[MR_KBDR] = getchar();
		}else{
			memory[MR_KBSR] = 0;
		}
	}
	return memory[address];
}

int read_image(const char* image_path){
	FILE* file = fopen(image_path, "rb");
	if (!file) {return 0;}
	read_image_file(file);
	fclose(file);
	return 1;
}

void handle_interrupt(int signal){
	restore_input_buffering();
	printf("\n");
	exit(-2);
}



int main(int argc, char const *argv[])
{

	if(argc < 2){
		/* Show usage string */
		printf("lc3 [image-file] ...\n");
		exit(2);
	}

	for (int j = 1; j < argc; ++j) {
		if(!read_image(argv[j])) {
			printf("Failed to load image :%s \n", argv[j]);
			exit(1);
		}
	}

	signal(SIGINT, handle_interrupt);
	disable_input_buffering();



	reg[R_COND] = FL_ZRO;

	/* set the PC to starting position */
	/* 0x300 is the default */

	enum {PC_START = 0x3000};
	reg[R_PC] = PC_START;
	int running = 1;

	while(running){
		/* FETCH */

		u16 instr = mem_read(reg[R_PC]++);
		u16 op    = instr >> 12;

		switch(op) {

		case OP_ADD:{
			u16 r0       = (instr >> 9) & 0x7;
			u16 r1       = (instr >> 6) & 0x7;
			u16 imm_flag = (instr >> 5) & 0x1;

			if(imm_flag){
				u16 imm5 = sign_extend(instr & 0x1F, 5);
				reg[r0]  = reg[r1] + imm5;
			}else{
				u16 r2   = instr & 0x7;
				reg[r0]  = reg[r1] + reg[r2];
			}
			update_flags(r0);
		} break;  

		case OP_AND:{

			u16 r0      = (instr >> 9) & 0x7;
			u16 r1      = (instr >> 6) & 0x7;
			u16 mm_flag = (instr >> 5) & 0x1;

			if (mm_flag){
				u16 imm5 = sign_extend(instr & 0x1F, 5);
				reg[r0]  = reg[r1] & imm5;
			}else{
				u16 r2  = instr & 0x7;
				reg[r0] = reg[r1] & reg[r2]; 
			}
			update_flags(r0);
		}break;

		case OP_NOT: {
			u16 r0      = (instr >> 9) & 0x7;
			u16 r1      = (instr >> 6) & 0x7;

			reg[r0] = ~reg[r1];
			update_flags(r0);
		}break;

		case OP_BR:{
			u16 pc_offset = sign_extend(instr & 0x1FF, 9);
			u16 cond_flag = (instr >> 9) & 0x7;
			if (cond_flag & reg[R_COND]){
				reg[R_PC] += pc_offset;
			}
		}break;

		case OP_JMP:{
			u16 r1    = (instr >> 6) & 0x7;
			reg[R_PC] = reg[r1];
		}break; 

		case OP_LD:{
			u16 r0        = (instr >> 9) & 0x7; //which register to load to
			u16 pc_offset = sign_extend(instr & 0x1FF, 9);
			reg[r0]       = mem_read(reg[R_PC] + pc_offset); 
			update_flags(r0);
		}break;

		case OP_ST:{
			u16 r0        = (instr >> 9) & 0x7;
			u16 pc_offset = sign_extend(instr & 0x1FF, 9);
			mem_write(reg[R_PC] + pc_offset, reg[r0]);
			update_flags(r0);
		}break;

		case OP_JSR:{ 
			u16 long_flag = (instr >> 11) & 1;
			reg[R_R7] = reg[R_PC];
			if(long_flag){
				u16 long_pc_offset  = sign_extend(instr & 0x7FF, 11);
				reg[R_PC]          += long_pc_offset;
			}else{
				u16 r1              = (instr >> 6) & 0x7;
				reg[R_PC]           = reg[r1];
			}
		}break;


		case OP_LDR:{
			u16 r0     = (instr >> 9) & 0x7;
			u16 r1     = (instr >> 6) & 0x7;
			u16 offset = sign_extend(instr & 0x3F, 6);
			reg[r0]    = mem_read(reg[r1] + offset);
			update_flags(r0);
		}break;

		case OP_STR:{
			u16 r0 = (instr >> 9) & 0x7;
			u16 r1 = (instr >> 6) & 0x7;
			u16 offset = sign_extend(instr & 0x3F, 6);
			mem_write(reg[r1] + offset, reg[r0]);
		}break;


		case OP_LDI:{
			/* Destination register (DR) */
			u16 r0        = (instr >> 9) & 0x7;

			/* PCoffset 9 */
			u16 pc_offset = sign_extend(instr & 0x1FF, 9);

			/* add pc_offset to the current PC, look at that memory location to get the final address */

			reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
			update_flags(r0);
		} break;    

		case OP_STI:{
			u16 r0 = (instr >> 9) & 0x7;
			u16 pc_offset = sign_extend(instr & 0x1FF, 9);
			mem_write(mem_read(reg[R_PC] + pc_offset), reg[r0]);
		}break;

		case OP_LEA:{//Loads the actuall address as value in the register
			u16 r0        = (instr >> 9) & 0x7;
			u16 pc_offset = sign_extend(instr & 0x1FF, 9);
			reg[r0]       = reg[R_PC] + pc_offset;
			update_flags(r0);
		}break;    

		case OP_TRAP:{

			reg[R_R7] = reg[R_PC];
			switch (instr & 0xFF) {
				case TRAP_GETC:
				{
					reg[R_R0] = (u16)getchar();
					update_flags(R_R0);
				}
				break;
				case TRAP_OUT:
				{
					putc((char)reg[R_R0], stdout);
					fflush(stdout);
				}
				break;
				case TRAP_PUTS:
				{
					u16 *c = memory + reg[R_R0];
					while (*c)
					{
						putc((char)*c, stdout);
						++c;
					}
					fflush(stdout);
				}
				break;
				case TRAP_IN:
				{
					printf("Enter a character: ");
					char c = getchar();
					putc(c, stdout);
					fflush(stdout);
					reg[R_R0] = (u16)c;
					update_flags(R_R0);
				}
				break;

				case TRAP_PUTSP:
				{
					u16 *c = memory + reg[R_R0];
					while (*c)
					{
						char char1 = (*c) & 0xFF;
						putc(char1, stdout);
						char char2 = (*c) >> 8;
						if (char2)
							putc(char2, stdout);
						++c;
					}
					fflush(stdout);
				}
				break;
				case TRAP_HALT:
				{
					puts("HALT");
					fflush(stdout);
					running = 0;
				} break;
			} 
		}break;
		case OP_RES:
		case OP_RTI:
		default:
			abort();
			break;
		}

	}
	restore_input_buffering();
	return 0;
}



















