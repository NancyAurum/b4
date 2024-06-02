/******************************************************************************

  b4.c by Nancy Sadkov
  License: Public Domain (CC0)

  Compile: cc b4.c -o b4

  4-bit opcode size virtual machine.
  Think Brainfuck but fast.
  A loop of 1,000,000,000 iterations completes in 4 sec, taking 8 bytes.

  Enters 3-states:
  * EXE state for normal execution
  * BCD state for loading a variable length binary coded decimal.
  * JMP state, for resolving the if/loop jump targets

  BCD encoding is special, since it uses the codes 0xA and 0xB,
  which act as both stream terminators and values.
  C, D, E and F can't be used, due to coinciding with [<]> codes in 4bit.

  TODO:
  * `intern` directive for the macro processor
    it will pre-intern the symbols, without leaving ids in the bytecode
  * Loop which go towards zero if counter is negative
  * Optimized implementation which can do 2,3,4 opcodes at a time
    for CPUs with huge code caches.
  * Macros:
    '{...}': when preceeded by 'Name(Arg0 Arg1 ... ArgN}' defines a macro
         If Name begins with a digit, collect digits till the matching '}',
         then otput the code to generate that number.
         For example,
           PRINT3 {3[PRINT/1-]} ; loop 3 times printing the loop counter
         if it is an alphabet character, get the value for it.
         For example,
           {Count=123} 

Examples:
  'Hello, World!'.say  ; print hello world (29 bytes)
  ?-                   ; negate (1 bytes)
  [??<]1>              ; not (4 bytes)
  not:[?@]1:           ; define `not` as a function (6 bytes)
  ?4=1[top.1+]         ; loop from 0 to 4, printing the count (8 bytes)
  1000000000=?[]       ; loop a billion times (8 bytes)
  %[.top=]             ; print 0 terminated string's bytes (5 bytes)
  1[1=]                ; endless loop (4 bytes)
  bool:[1@]?:          ; returns 1 if integer is not 0, or 0 (3 bytes)
  and:[[1@]?@]!?:      ; logical and (8 bytes)
  or:[!1@][1@]?:       ; logical or (9 bytes)

Mnemonics:
0:N/A: enters BCD input state
1:'+': add two number on the stack
2:'-': subtract two numbers on the stack
3:'*': multiply two numbers on the stack
4:'$': pop a value I from the stack
       if I >= 0: push(stack[sp-1-I])
       else
         pop a value V from the stack;
         stack[sp-1+I] = V
5:'=': A = pop
6:'?': push A
7:'!': pop and discard
8:',': swap two top values
9:':': Pops function id from the stack. The code (terminated by `;`).
         dblsum:+2*: ; function[dblsum] = (a+b)*2
A:'.': calls a function with id on the stack.
         123. ; calls function with id=123
         .top ; when prefixes a symbol name, calls it
B:'@': return from a function
C:'[': pop value, if it is 0, seek to the code after the next matching ']'.
D:']': if the value in the register A is not 0; seek to the matching '['
E:'<': pop value, if value <= 0, seek to the code after the next matching '>'.
F:'>': if the value in the register B is not 0; seek to the matching '<'
_:'%': duplicate the top element (shorthand for 0$)

Predefined functions:
top: print the value at the top of the stack without popping it.
say: print 0-terminated string on stack.
hlt: termiante execution


*******************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#define S static

typedef int32_t T;  //type of the operands
typedef int32_t P;  //type of the code pointer
typedef uint8_t C;  //type of the code value

#define MAXSP 1024
#define MAXFR 1024
#define MAXFN 1024
#define MAXNP 1024
#define MAXNM 256

#define BADIP (-1)

enum { //opcodes
  C_BCD, //0: read BCD
  C_ADD, //1: pop X, pop Y, push X+Y
  C_SUB, //2: pop X, pop Y, push X-Y
  C_MUL, //3: pop X, pop Y, push X*Y
  C_RWS, //4: pop I, if I<0 {pop V; st[-I] = V} else {push st[I]}
  C_RDA, //5: A = pop
  C_STA, //6: push A
  C_POP, //7: pop and discard
  C_SWP, //8: swap two top values
  C_DFN, //9: define function
  C_RUN, //A: run function
  C_RET, //B: return from a function
  C_JAO, //C: jump on A opening
  C_JAC, //D: jump on A closing
  C_JBO, //E: jump on B opening
  C_JBC, //F: jump on B closing
};


S T st[MAXSP];
S int sp, fp, np;
S struct { P start, end, ip; T ra; } fr[MAXFN]; //frames
S struct { P start, end; } fn[MAXFR]; //functions
S char *nm[MAXNP]; //names
S C *code;
S P *jtbl; //we can use a few values cache if memory is a concern
S P ip, start, end;
S T ra; //register A
S char name[MAXNM];

#define push(v) (st[sp++] = (v))
#define pop (st[--sp])
#define top (st[sp-1])

#define rd ((ip&1) ? code[ip++/2]>>4 : code[ip++/2]&0xF)
#define pk ((ip&1) ? code[ip/2]>>4 : code[ip/2]&0xF)
#define pr ((ip&1) ? code[(ip-1)/2]>>4 : code[(ip-1)/2]&0xF)

S void jmp(C open, C close, P inc, P end);

S P dp(P p) {printf("dp:%d\n", p); return p;}
S C dc(C c) {printf("dc:%d\n", c); return c;}

enum {BCD_N=10, BCD_P=11}; //normal or prefixed

S void bcd() {
  T v = 0, b = 1;
  C c;
  for (;;) switch((c = rd)&0xF) {
  case 0: case 1: case 2: case 3: case 4:
  case 5: case 6: case 7: case 8: case 9:
    v = v*10 + c;
    b *= 10;
    break;
  case BCD_N: case BCD_P:
    v += b*(c==BCD_P);
    push(v);
    return;
  //below can be put at the beginning of bytecode to indicate special parameters
  //like syscalls and architecture extensions.
  case 12: case 13: case 14: case 15:
    printf("Bad BCD `%d`\n", c);
    exit(-1);
  }
}

//FIXME: put predefined functions into a table.
enum { SI_TOP, SI_SAY, SI_HLT};

S void swi(T id) {
  switch (id) {
  case SI_TOP: printf("top: %d\n", top); break;
  case SI_SAY: {
    int e = sp;
    int s = sp;
    while (s && st[s]) s--;
    sp = s;
    while (s < e) putchar(st[s++]);
    putchar('\n');
    break;
    }
  case SI_HLT: exit(-1); break;
  default:
    printf("Bad function `%d`\n", id);
    exit(-1);
  }
}

S P dfn_close() {
  for (; ip<end; ip++) {
    if (pk == C_DFN) return ++ip;
    if (pk == C_BCD) for (; ip<end && pk != BCD_N && pk != BCD_P; ip++);
  }
  printf("Couldn't match `:`\n");
  exit(-1);
}

S void dfn(T id) {
  fn[id].start = ip;
  fn[id].end = dfn_close()-1;
}

S void run(T id) {
  if (!fn[id].end) {
    swi(id);
    return;
  }
  fr[fp].ra = ra;
  fr[fp].ip = ip;
  fr[fp].start = start;
  fr[fp++].end = end;
  start = fn[id].start;
  end = fn[id].end;
  ip = start;
  ra = 0;
}

S void rw(T index) {
  if (index >= 0) push(st[sp-1-index]);
  else {
    T v = pop;
    st[sp+index] = v;
  }
}

S void jmp(C open, C close, P inc, P end) {
  P target = jtbl[ip];
  if (target!=BADIP) {
    ip = target;
    return;
  }
  int sip = ip;
  int depth = 0;
  for (; ip!=end; ip+=inc) {
    if (pk == open) depth++;
    else if (pk == close) {
      if(!depth) {
        jtbl[sip] = ++ip;
        return;
      } depth--;
    }
  }
  printf("Couldn't match `%X`\n", open);
  exit(-1);
}

#define LJ(open,close,r)  do { \
  if (r) { \
    --r; \
    ip-=2; \
    jmp(open, close, -1, start); \
  } \
} while(0)

S void exe() {
  while (ip < end) switch(rd&0xF) {
  case C_BCD: bcd(); break;
  case C_ADD: push(pop+pop); break;
  case C_SUB: push(pop-pop); break;
  case C_MUL: push(pop*pop); break;
  case C_RWS: rw(pop); break;
  case C_RDA: ra = pop; break;
  case C_STA: push(ra); break;
  case C_POP: pop; break;
  case C_SWP: {T a = pop; T b = pop; push(a); push(b);} break;
  case C_DFN: dfn(pop); break;
  case C_RUN: run(pop); break;
  case C_RET: return; break;
  case C_JAO: if (!pop) jmp(C_JAO, C_JAC, 1, end); break;
  case C_JAC: LJ(C_JAC,C_JAO,ra); break;
  case C_JBO: if (pop<=0) jmp(C_JBO, C_JBC, 1, end); break;
  case C_JBC: LJ(C_JBC,C_JBO,ra); break;
  }
}


#define emit(c) do {if (ip&1) q[ip++/2]|=(c)<<4; else q[ip++/2]=(c);} while(0)

S T sym(char *name) {
  for (int i = 0; i < np; i++) if (!strcmp(nm[i],name)) return i;
  if (np == MAXNP) {
    printf("Name table overflow.\n");
    exit(-1);
  }
  nm[np] = strdup(name);
  return np++;
}

S P emitBCD(uint8_t *q ,P ip, int v) {
  char *n = name;
  do { *n++ = v%10; v /= 10; } while (v);
  int b = *--n;
  emit(C_BCD);
  if (b>1) emit(b);
  while (n > name) emit(*--n);
  emit(b==1 ? 11 : 10);
  return ip;
}

S char *b4asmS(uint8_t *q, P ip, char *p, char *end, P *oip) {
  int run = 0;
  while (p < end) {
    int c = *p++;
    if (isalpha(c)||c=='_') {
      char *n = name;
      char *e = name+MAXNM;
      *n++ = c;
      while (isalnum(*p)||*p=='_') {
        *n++ = *p++;
        if (n == e) {
          n[-1] = 0;
          printf("Name is too long: %s...\n", name);
          exit(-1);
        }
      }
      *n = 0;
      ip = emitBCD(q, ip, sym(name));
      if (run) { emit(C_RUN); run = 0; }
      continue;
    }
    switch(c) {
    case '\'': {
      ip = emitBCD(q, ip, 0);
      while (p<end && *p != '\'') {
        if (*p == '\\') p++;
        ip = emitBCD(q, ip, *p++);
      }
      if (p++ == end) {
        printf("Unterminated quote\n");
        exit(-1);
      }
      break;
    }
    case ' ': case '\n': break; //nop
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      int b = c-'0';
      emit(C_BCD);
      if (b>1) emit(b);
      while (isdigit(*p)) emit(*p++-'0');
      emit(b==1 ? 11 : 10);
      break;
    case '+': emit(C_ADD); break;
    case '-': emit(C_SUB); break; 
    case '*': emit(C_MUL); break;
    case '[': emit(C_JAO); break;
    case ']': emit(C_JAC); break;
    case '<': emit(C_JBO); break;
    case '>': emit(C_JBC); break;
    case ':': emit(C_DFN); break;
    case '.': if (isalpha(*p)||*p=='_') run = 1; else emit(C_RUN); break;
    case '@': emit(C_RET); break;
    case '$': emit(C_RWS); break;
    case '!': emit(C_POP); break;
    case ',': emit(C_SWP); break;
    case '=': emit(C_RDA); break;
    case '?': emit(C_STA); break;
    case '%': emit(C_BCD); emit(10); emit(C_RWS); break;
    default:
      printf("Bad opcode `%c`\n", c);
      exit(-1);
    }
  }
  *oip = ip;
  return p;
}

uint8_t *b4asm(P *osize, char *statement) {
  char *p = statement;
  P insz = strlen(p);
  char *end = p + strlen(p);
  uint8_t *q = malloc(insz*2+100); //mul by 2 since 7 becomes #7n
  P ip = 0;
  b4asmS(q, ip, p, end, &ip);
  *osize = ip;
  P sz = (ip+1)/2;
  uint8_t *r = malloc(sz);
  memcpy(r, q, sz);
  free(q);
  return r;
}

S int ready;


S int init() {
  sym("top");
  sym("say");
  sym("hlt");
  sym("_entry");
  ready = 1;
}

S void frloop() {
  for (;;) {
    exe();
    if (!--fp) return;
    ip = fr[fp].ip;
    start = fr[fp].start;
    end = fr[fp].end;
    ra = fr[fp].ra;
  }
}

void b4cmd(char *command) {
  P csz;
  if (!ready) init();

  code = b4asm(&csz, command);

  printf("Code size: %d bytes\n", (csz+1)/2);

  jtbl = malloc(csz*sizeof(P));
  
  for (int i = 0; i < csz; i++) jtbl[i] = BADIP;

  int entry = sym("_entry");
  fn[entry].start = 0;
  fn[entry].end = csz;
  run(entry);
  frloop();

  free(code);
  free(jtbl);
  code = 0;
  jtbl = 0;
}

void b4dump() {
  printf("A = %d\n", ra);
  int i = sp;
  while (i-- > 0) printf("st[%d] = %d\n", i, st[i]);
}

int main(int argc, char **argv) {
  if (argc < 2) {
     printf("Usage: %s <expression>\n", argv[0]);
     return 0;
  }
  b4cmd(argv[1]);
  b4dump();
  return 0;
}
