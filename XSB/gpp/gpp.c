/* File:      gpp.c  -- generic preprocessor
** Author:    Denis Auroux
** Contact:   auroux@math.polytechnique.fr
** Version:   2.0
** 
** Copyright (C) Denis Auroux 1996, 1999
** 
** GPP is free software; you can redistribute it and/or modify it under the
** terms of the GNU Library General Public License as published by the Free
** Software Foundation; either version 2 of the License, or (at your option)
** any later version.
** 
** GPP is distributed in the hope that it will be useful, but WITHOUT ANY
** WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
** FOR A PARTICULAR PURPOSE.  See the GNU Library General Public License for
** more details.
** 
** You should have received a copy of the GNU Library General Public License
** along with this software; if not, write to the Free Software Foundation,
** Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
**
** $Id: gpp.c,v 1.23 2001-12-15 06:19:46 kifer Exp $
** 
*/

/* To compile under MS VC++, one must define WIN_NT */

#ifdef WIN_NT              /* WIN NT settings */
#define popen   _popen
#define pclose  _pclose
#define strdup  _strdup
#define strcasecmp _stricmp
#define SLASH '\\'
#define DEFAULT_CRLF 1
#else                      /* UNIX settings */
#define SLASH '/'
#define DEFAULT_CRLF 0
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define STACKDEPTH 50
#define MAXARGS 100
#define MAXINCL 10   /* max # of include dirs */

#define MAX_GPP_NUM_SIZE 15

typedef struct MODE {
  char *mStart;		/* before macro name */
  char *mEnd;		/* end macro without arg */
  char *mArgS;		/* start 1st argument */
  char *mArgSep;	/* separate arguments */
  char *mArgE;		/* end last argument */
  char *mArgRef;	/* how to refer to arguments in a def */
  char quotechar;	/* quote next char */
  char *stackchar;      /* characters to stack */
  char *unstackchar ;   /* characters to unstack */
} MODE;

/* translation for delimiters :
   \001 = \b = ' ' = one or more spaces    \201 = \!b = non-space
   \002 = \w = zero or more spaces 
   \003 = \B = one or more spaces or \n    \203 = \!B = non-space nor \n
   \004 = \W = zero or more spaces or \n 
   \005 = \a = alphabetic (a-z, A-Z)       \205 = \!a = non-alphabetic
   \006 = \A = alphabetic or space/\n      \206 = \!A
   \007 = \# = numeric (0-9)               \207 = \!#
   \010 = \i = identifier (a-zA-Z0-9_)     \210 = \!i
   \011 = \t, \012 = \n                    \211 = \!t, \212 = \!n
   \013 = \o = operator (+-*\/^<>=`~:.?@#&!%|) \213 = \!o
   \014 = \O = operator or ()[]{}              \214 = \!O
*/
/*                   st        end   args   sep    arge ref  quot  stk  unstk*/
struct MODE CUser = {"",       "",   "(",   ",",   ")", "#", '\\', "(", ")" };
struct MODE CMeta = {"#",      "\n", "\001","\001","\n","#", '\\', "(", ")" };
struct MODE KUser = {"",       "",   "(",   ",",   ")", "#",  0,   "(", ")" };
struct MODE KMeta = {"\n#\002","\n", "\001","\001","\n","#",  0,   "",  ""  }; 
struct MODE Tex   = {"\\",     "",   "{",   "}{",  "}", "#", '@',  "{", "}" };
struct MODE Html  = {"<#",     ">",  "\003","|",   ">", "#", '\\', "<", ">" };

#define DEFAULT_OP_STRING (unsigned char *)"+-*/\\^<>=`~:.?@#&!%|"
#define PROLOG_OP_STRING  (unsigned char *)"+-*/\\^<>=`~:.?@#&"
#define DEFAULT_OP_PLUS   (unsigned char *)"()[]{}"
#define DEFAULT_ID_STRING (unsigned char *)"\005\007_" /* or equiv. "A-Za-z0-9_" */

/* here we assume that longs are at least 32 bit... if not, change this ! */
#define LOG_LONG_BITS 5
#define CHARSET_SUBSET_LEN (256>>LOG_LONG_BITS)
typedef unsigned long *CHARSET_SUBSET;

CHARSET_SUBSET DefaultOp,DefaultExtOp,PrologOp,DefaultId;

typedef struct COMMENT {
  char *start;          /* how the comment/string starts */
  char *end;            /* how it ends */
  char quote;           /* how to prevent it from ending */
  char warn;            /* a character that shouldn't be in there */
  int flags[3];         /* meta, user, text */
  struct COMMENT *next;
} COMMENT;

#define OUTPUT_TEXT     0x1   /* what's inside will be output */
#define OUTPUT_DELIM    0x2   /* the delimiters will be output */
#define PARSE_MACROS    0x4   /* macros inside will be parsed */
#define FLAG_IGNORE     0x40 

#define FLAG_STRING    (OUTPUT_TEXT|OUTPUT_DELIM)
#define FLAG_COMMENT   0

#define FLAG_META 0
#define FLAG_USER 1
#define FLAG_TEXT 2

/* Some stuff I removed because it made for some impossible situations :

 #define PARSE_COMMENTS  0x8   
   comments inside comments will not be parsed because nesting comments is 
   too complicated (syntax conflicts, esp. to find a comment's end)
   -- of course, unless the comment is ignored.
   
 #define MACRO_FRIENDLY  0x20  
   a comment-end is to be processed even if an unfinished macro call has 
   started inside the comment, otherwise it's too hard do decide in advance 
   where a comment ends. In particular foo('bar((((') is valid.

 #define PREVENT_DELIM   0x10 
   all comments will prevent macro delimitation, i.e. foo('bar) is invalid.
   -- of course, unless the comment is ignored.
   Too bad, #define foo '...    terminates only at following "'".
   Unless one adds quotechars like in #define foo \' ...
   
 ALSO NOTE : comments are not allowed before the end of the first argument
 to a meta-macro. E.g. this is legal :   #define foo <* blah *> 3
 This is not legal :                     #define <* blah *> foo 3
 If a comment occurs here, the behavior depends on the actual meta-macro :
 most will yield an error and stop gpp (#define, #undef, #ifdef/ifndef, 
 #defeval, #include, #mode) ; #exec, #if and #eval should be ok ; 
 #ifeq will always fail while #ifneq will always succeed ;
*/ 

typedef struct SPECS {
  struct MODE User,Meta;
  struct COMMENT *comments;
  struct SPECS *stack_next;
  int preservelf;
  CHARSET_SUBSET op_set,ext_op_set,id_set;
} SPECS;

struct SPECS *S;

typedef struct MACRO {
  char *username,*macrotext,**argnames;
  int macrolen,nnamedargs;
  struct SPECS *define_specs;
  int defined_in_comment;
} MACRO;

struct MACRO *macros;
int nmacros,nalloced;
char *includedir[MAXINCL];
int nincludedirs;
int execallowed;
int dosmode;
int autoswitch;
/* must be a format-like string that has % % % in it.
   The first % is replaced with line number, the second with "filename", and
   the third with 1, 2 or blank
*/
char *include_directive_marker = NULL;
short WarningLevel = 2;

/* controls if standard dirs, like /usr/include, are to be searched for
   #include and whether the current dir is to be searched first or last. */
int NoStdInc        = 0;
int NoCurIncFirst   = 0;
int CurDirIncLast   = 0;
int file_and_stdout = 0;

typedef struct OUTPUTCONTEXT {
  char *buf;
  int len,bufsize;
  FILE *f;
} OUTPUTCONTEXT;

typedef struct INPUTCONTEXT {
  char *buf;
  char *malloced_buf; /* what was actually malloc-ed (buf may have shifted) */
  int len,bufsize;
  int lineno;
  char *filename;
  FILE *in;
  int argc;
  char **argv;
  char **namedargs;
  struct OUTPUTCONTEXT *out;
  int eof;
  int in_comment;
  int ambience; /* FLAG_TEXT, FLAG_USER or FLAG_META */
} INPUTCONTEXT;

struct INPUTCONTEXT *C;
  
int commented[STACKDEPTH],iflevel;

void ProcessContext(); /* the main loop */
static void getDirname(char *fname, char *dirname);
static FILE *openInCurrentDir(char *incfile);
char *ArithmEval(int pos1,int pos2);
void replace_definition_with_blank_lines(char *start, char *end);
void replace_directive_with_blank_line(FILE *file);
void write_include_marker(FILE *f, int lineno, char *filename, char *marker);
void construct_include_directive_marker(char **include_directive_marker,
					char *includemarker_input);
void escape_backslashes(char *instr, char **outstr);

void bug(char *s)
{
  fprintf(stderr,"%s:%d: error: %s.\n",C->filename,C->lineno,s);
  exit(1);
}

void warning(char *s)
{
  fprintf(stderr,"%s:%d: warning: %s.\n",C->filename,C->lineno,s);
}

struct SPECS *CloneSpecs(struct SPECS *Q)
{
  struct SPECS *P;
  struct COMMENT *x,*y;
  
  P=(struct SPECS *)malloc(sizeof(struct SPECS));
  if (P==NULL) bug("Out of memory.");
  memcpy((char *)P,(char *)Q,sizeof(struct SPECS));
  P->stack_next=NULL;
  if (Q->comments!=NULL) 
    P->comments=(struct COMMENT *)malloc(sizeof(struct COMMENT));
  for (x=Q->comments,y=P->comments;x!=NULL;x=x->next,y=y->next) {
    memcpy((char *)y,(char *)x,sizeof(struct COMMENT));
    y->start=strdup(x->start);
    y->end=strdup(x->end);
    if (x->next!=NULL)
      y->next=(struct COMMENT *)malloc(sizeof(struct COMMENT));
  }
  return P;
}

void FreeComments(struct SPECS *Q)
{
  struct COMMENT *p;
  
  while (Q->comments!=NULL) {
    p=Q->comments;
    Q->comments=p->next;
    free(p->start);
    free(p->end);
    free(p);
  }
}

void PushSpecs(struct SPECS *X)
{
  struct SPECS *P;
  
  P=CloneSpecs(X);
  P->stack_next=S;
  S=P;
}

void PopSpecs()
{
  struct SPECS *P;
  
  P=S;
  S=P->stack_next;
  FreeComments(P);
  free(P);
  if (S==NULL) bug("#mode restore without #mode save");
}

void usage() {
  fprintf(stderr,"GPP Version 2.0 - Generic Preprocessor - (c) Denis Auroux 1996-99\n");
  fprintf(stderr,"Usage : gpp [-{o|O} outfile] [-I/include/path] [-Dname=val ...] [-z] [-x] [-m]\n");
  fprintf(stderr,"            [-n] [-C | -T | -H | -P | -U ... [-M ...]] [+c<n> str1 str2]\n");
  fprintf(stderr,"            [+s<n> str1 str2 c] [infile]\n\n");
  fprintf(stderr,"      default:    #define x y           macro(arg,...)\n");
  fprintf(stderr," -o : output goes to outfile\n");
  fprintf(stderr," -O : output goes to outfile and stdout\n");
  fprintf(stderr," -C : maximum cpp compatibility (includes -n, +c, +s, ...)\n");
  fprintf(stderr," -T : tex-like    \\define{x}{y}         \\macro{arg}{...}\n");
  fprintf(stderr," -H : html-like   <#define x|y>         <#macro arg|...>\n");
  fprintf(stderr," -P : prolog compatible cpp-like mode\n");
  fprintf(stderr," -U : user-defined syntax (specified in 9 following args, see manual)\n");
  fprintf(stderr," -M : user-defined syntax for meta-macros (specified in 7 following args)\n\n");
  fprintf(stderr," -z : line terminator is CR-LF (MS-DOS style)\n");
  fprintf(stderr," -x : enable #exec built-in macro\n");
  fprintf(stderr," -m : enable automatic mode switching upon including .h/.c files\n");
  fprintf(stderr," -n : send LF characters serving as macro terminators to output\n");
  fprintf(stderr," +c : use next 2 args as comment start and comment end sequences\n");
  fprintf(stderr," +s : use next 3 args as string start, end and quote character\n\n");
  fprintf(stderr," -nostdinc : don't search standard directories for files to include\n\n");
  fprintf(stderr," -nocurinc : don't search the current directory for files to include\n\n");
  fprintf(stderr," -curdirinclast : search the current directory last\n\n");
  fprintf(stderr," -warninglevel  : use next integer argument as the desired warning level\n\n");
  fprintf(stderr," -includemarker : use next argument as a marker that tells\n\t\t  where #include was in the source file\n\n");
  exit(1);
}

int isdelim(unsigned char c)
{
  if (c>=128) return 0;
  if ((c>='0')&&(c<='9')) return 0;
  if ((c>='A')&&(c<='Z')) return 0;
  if ((c>='a')&&(c<='z')) return 0;
  if (c=='_') return 0;
  return 1;
}

int iswhite(char c)
{
  if (c==' ') return 1;
  if (c=='\t') return 1;
  if (c=='\n') return 1;
  return 0;
}

void newmacro(char *s,int len,int hasspecs)
{
  if (nmacros==nalloced) {
    nalloced=2*nalloced+1;
    macros=(struct MACRO *)realloc((char *)macros,nalloced*sizeof(struct MACRO));
    if (macros==NULL)
      bug("Out of memory");
  }
  macros[nmacros].username=malloc(len+1);
  strncpy(macros[nmacros].username,s,len);
  macros[nmacros].username[len]=0;
  macros[nmacros].argnames=NULL;
  macros[nmacros].nnamedargs=0;
  macros[nmacros].defined_in_comment=0;
  if (hasspecs)
    macros[nmacros].define_specs=CloneSpecs(S);
}

void lookupArgRefs(int n)
{
  int i,l;
  char *p;
  
  if (macros[n].argnames!=NULL) return; /* don't mess with those */
  macros[n].nnamedargs=-1;
  l=strlen(S->User.mArgRef);
  for (i=0,p=macros[n].macrotext;i<macros[n].macrolen;i++,p++) {
    if ((*p!=0)&&(*p==S->User.quotechar)) { i++; p++; }
    else if (!strncmp(p,S->User.mArgRef,l))
      if ((p[l]>='1')&&(p[l]<='9')) 
        { macros[n].nnamedargs=0; return; }
  }
}

char *strnl0(char *s) /* replace "\\n" by "\n" in a cmd-line arg */
{
  char *t,*u;
  t=(char *)malloc(strlen(s)+1);
  u=t;
  while (*s!=0) {
    if ((*s=='\\')&&(s[1]=='n')) { *u='\n'; s++; }
    else *u=*s;
    s++; u++;
  }
  *u=0;
  return t;
}

char *strnl(char *s) /* the same but with whitespace specifier handling */
{
  char *t,*u;
  int neg;
  t=(char *)malloc(strlen(s)+1);
  u=t;
  if (!isdelim(*s)) bug("character not allowed to start a syntax specifier");
  while (*s!=0) {
    if (((*s&0x60)==0)&&(*s!='\n')&&(*s!='\t')) 
      bug("character not allowed in syntax specifier");
    if (*s=='\\') {
      neg=(s[1]=='!');
      switch(s[neg+1]) {
      case 'n':
      case 'r':
	*u='\n';
	break;
      case 't':
	*u='\t';
	break;
      case 'b':  /* one or more spaces */
	*u='\001';
	break;
      case 'w':  /* zero or more spaces */
	if (neg)
	  bug("\\w and \\W cannot be negated");
	*u='\002';
	break;
      case 'B':  /* one or more spaces or \n */
	*u='\003';
	break;
      case 'W':  /* zero or more spaces or \n */
	if (neg)
	  bug("\\w and \\W cannot be negated");
	*u='\004';
	break;
      case 'a':  /* alphabetic */
	*u='\005';
	break;
      case 'A':  /* alphabetic + space */
	*u='\006';
	break;
      case '#':  /* numeric */
	*u='\007';
	break;
      case 'i':  /* identifier */
	*u='\010';
	break;
      case 'o':  /* operator */
	*u='\013';
	break;
      case 'O':  /* operator/parenthese */
	*u='\014';
	break;
      default: *u='\\'; neg=-1;
      }
      if (neg>0) *u+=(char)128;
      s+=neg+1;
    }
    else if (*s==' ') *u='\001';
    else *u=*s;
    s++; u++;
  }
  *u=0;
  return t;
}

/* same as strnl() but for C strings & in-place */
char *strnl2(char *s,int check_delim)
{
  char *u;
  int neg;
  u=s;
  if (check_delim&&!isdelim(*s)) 
    bug("character not allowed to start a syntax specifier");
  while (*s!='"') {
    if (((*s&0x60)==0)&&(*s!='\n')&&(*s!='\t')) 
      bug("character not allowed in syntax specifier");
    if (*s=='\\') {
      neg=(s[1]=='!');
      switch(s[neg+1]) {
      case 'n':
      case 'r':
	*u='\n';
	break;
      case 't':
	*u='\t';
	break;
      case 'b':  /* one or more spaces */
	*u='\001';
	break;
      case 'w':  /* zero or more spaces */
	if (neg)
	  bug("\\w and \\W cannot be negated");
	*u='\002';
	break;
      case 'B':  /* one or more spaces or \n */
	*u='\003';
	break;
      case 'W':  /* zero or more spaces or \n */
	if (neg)
	  bug("\\w and \\W cannot be negated");
	*u='\004';
	break;
      case 'a':  /* alphabetic */
	*u='\005';
	break;
      case 'A':  /* alphabetic + space */
	*u='\006';
	break;
      case '#':  /* numeric */
	*u='\007';
	break;
      case 'i':  /* identifier */
	*u='\010';
	break;
      case 'o':  /* operator */
	*u='\013';
	break;
      case 'O':  /* operator/parenthesis */
	*u='\014';
	break;
      case '"':
      case '\\':
	if (!neg) {
	  *u=s[1];
	  break;
	}
      default:
	bug("unknown escape sequence in syntax specifier");
      }
      if (neg>0) *u+=(char)128;
      s+=neg+1;
    }
    else if (*s==' ') *u='\001';
    else *u=*s;
    if (*s==0) bug("unterminated string in #mode command");
    s++; u++;
  }
  *u=0;
  return (s+1);
}

int iswhitesep(char *s)
{
  while (iswhite(*s)||(*s=='\001')||(*s=='\002')||(*s=='\003')||(*s=='\004')) 
    s++;
  return (*s==0);
}

int nowhite_strcmp(char *s,char *t)
{
  char *p;
  
  while (iswhite(*s)) s++;
  while (iswhite(*t)) t++;
  if ((*s==0)||(*t==0)) return strcmp(s,t);
  p=s+strlen(s)-1;
  while (iswhite(*p)) *(p--)=0;
  p=t+strlen(t)-1;
  while (iswhite(*p)) *(p--)=0;
  return strcmp(s,t);
}

void parseCmdlineDefine(char *s)
{
  int l;
  
  for (l=0;s[l]&&(s[l]!='=');l++);
  newmacro(s,l,0);
  if (s[l]=='=') l++;
  macros[nmacros].macrolen=strlen(s+l);
  macros[nmacros++].macrotext=strdup(s+l);
}

int readModeDescription(char **args,struct MODE *mode,int ismeta)
{
  if (!(*(++args))) return 0;
  mode->mStart=strnl(*args);
  if (!(*(++args))) return 0;
  mode->mEnd=strnl(*args);
  if (!(*(++args))) return 0;
  mode->mArgS=strnl(*args); 
  if (!(*(++args))) return 0;
  mode->mArgSep=strnl(*args); 
  if (!(*(++args))) return 0;
  mode->mArgE=strnl(*args); 
  if (!(*(++args))) return 0;
  mode->stackchar=strnl(*args); 
  if (!(*(++args))) return 0;
  mode->unstackchar=strnl(*args); 
  if (ismeta) return 1;
  if (!(*(++args))) return 0;
  mode->mArgRef=strnl(*args); 
  if (!(*(++args))) return 0;
  mode->quotechar=**args;
  return 1;
}

int parse_comment_specif(char c)
{
  switch (c) {
  case 'I': case 'i': return FLAG_IGNORE;
  case 'c': return FLAG_COMMENT;
  case 's': return FLAG_STRING;
  case 'q': return OUTPUT_TEXT;
  case 'S': return FLAG_STRING|PARSE_MACROS;
  case 'Q': return OUTPUT_TEXT|PARSE_MACROS;
  case 'C': return FLAG_COMMENT|PARSE_MACROS;
  default: bug("Invalid comment/string modifier"); return 0;
  }
}

void add_comment(struct SPECS *S,char *specif,char *start,char *end,char quote,char warn)
{
  struct COMMENT *p;
  
  if (*start==0) bug("Comment/string start delimiter must be non-empty");
  for (p=S->comments;p!=NULL;p=p->next)
    if (!strcmp(p->start,start)) {
      if (strcmp(p->end,end)) /* already exists with a different end */
        bug("Conflicting comment/string delimiter specifications");
      free(p->start);
      free(p->end);
      break;
    }

  if (p==NULL) {
    p=(struct COMMENT *)malloc(sizeof(struct COMMENT));
    p->next=S->comments;
    S->comments=p;
  }
  p->start=start;
  p->end=end;
  p->quote=quote;
  p->warn=warn;
  if (strlen(specif)!=3) bug("Invalid comment/string modifier");
  p->flags[FLAG_META]=parse_comment_specif(specif[0]);
  p->flags[FLAG_USER]=parse_comment_specif(specif[1]);
  p->flags[FLAG_TEXT]=parse_comment_specif(specif[2]);
}

void delete_comment(struct SPECS *S,char *start)
{
  struct COMMENT *p,*q;
  
  q=NULL;
  for (p=S->comments;p!=NULL;p=p->next) {
    if (!strcmp(p->start,start)) {
      if (q==NULL) S->comments=p->next;
      else q->next=p->next;
      free(p->start);
      free(p->end);
      free(p);
      free(start);
      return;
    }
    else q=p;
  }
  free(start);
}

void outchar(char c)
{
  if (C->out->bufsize) {
    if (C->out->len+1==C->out->bufsize) { 
      C->out->bufsize=C->out->bufsize*2;
      C->out->buf=realloc(C->out->buf,C->out->bufsize);
      if (C->out->buf==NULL) bug("Out of memory");
    }
    C->out->buf[C->out->len++]=c;
  }
  else {
    if (dosmode&&(c==10)) {
      fputc(13,C->out->f);
      if (file_and_stdout)
	fputc(13,stdout);
    }
    if (c!=13) {
      fputc(c,C->out->f);
      if (file_and_stdout)
	fputc(c,stdout);
    }
  }
}

void sendout(char *s,int l,int proc) /* only process the quotechar, that's all */
{
  int i;
  
  if (!commented[iflevel])
    for (i=0;i<l;i++) {
      if (proc&&(s[i]!=0)&&(s[i]==S->User.quotechar)) 
        { i++; if (i==l) return; }
      if (s[i]!=0) outchar(s[i]);
    }
  else
    replace_definition_with_blank_lines(s+1,s+l);
}

void extendBuf(int pos)
{
  char *p;
  if (C->bufsize<=pos) {
    C->bufsize+=pos; /* approx double */
    p=(char *)malloc(C->bufsize);
    memcpy(p,C->buf,C->len);
    free(C->malloced_buf);
    C->malloced_buf=C->buf=p;
    if (C->buf==NULL) bug("Out of memory");
  }
}

char getChar(int pos)
{
  int c;

  if (C->in==NULL) {
    if (pos>=C->len) return 0;
    else return C->buf[pos];
  }
  extendBuf(pos);
  while (pos>=C->len) {
    do { c=fgetc(C->in); } while (c==13);
    if (c=='\n') C->lineno++;
    if (c==EOF) c=0;
    C->buf[C->len++]=(char)c;
  }
  return C->buf[pos];
}

int whiteout(int *pos1,int *pos2) /* remove whitespace on both sides */
{
  while ((*pos1<*pos2)&&iswhite(getChar(*pos1))) (*pos1)++;
  while ((*pos1<*pos2)&&iswhite(getChar(*pos2-1))) (*pos2)--;
  return (*pos1<*pos2);
}

int identifierEnd(int start)
{
  char c;

  c=getChar(start);
  if (c==0) return start;
  if (c==S->User.quotechar) {
    c=getChar(start+1);
    if (c==0) return (start+1);
    if (isdelim(c)) return (start+2);
    start+=2;
    c=getChar(start);
  }
  while (!isdelim(c)) c=getChar(++start);
  return start;
}

int iterIdentifierEnd(int start)
{
  int x;
  while(1) {
    x=identifierEnd(start);
    if (x==start) return x;
    start=x;
  }
}

int IsInCharset(CHARSET_SUBSET x,int c)
{
  return (x[c>>LOG_LONG_BITS] & 1L<<(c&((1<<LOG_LONG_BITS)-1)))!=0;
}

int matchSequence(char *s,int *pos)
{
  int i=*pos;
  int match;
  char c;

  while (*s!=0) {
    if (!((*s)&0x60)) { /* special sequences */
      match=1;
      switch((*s)&0x1f) {
      case '\001':
	c=getChar(i++);
	if ((c!=' ')&&(c!='\t'))
	  { match=0; break; }
      case '\002':
	i--;
	do { c=getChar(++i); } while ((c==' ')||(c=='\t'));
	break;
      case '\003':
	c=getChar(i++);
	if ((c!=' ')&&(c!='\t')&&(c!='\n'))
	  { match=0; break; }
      case '\004':
	i--;
	do { c=getChar(++i); } while ((c==' ')||(c=='\t')||(c=='\n'));
	break;
      case '\006':
	c=getChar(i++);
	match = ((c>='a')&&(c<='z')) || ((c>='A')&&(c<='Z')) 
	  ||(c==' ')||(c=='\t')||(c=='\n');
	break;
      case '\005':
	c=getChar(i++);
	match = ((c>='a')&&(c<='z')) || ((c>='A')&&(c<='Z')); break;
      case '\007':
	c=getChar(i++);
	match = ((c>='0')&&(c<='9')); break;
      case '\010':
	c=getChar(i++);
	match = IsInCharset(S->id_set,c); break;
      case '\011':
	c=getChar(i++);
	match = (c=='\t'); break;
      case '\012':
	c=getChar(i++);
	match = (c=='\n'); break;
      case '\013':
	c=getChar(i++);
	match = IsInCharset(S->op_set,c); break;
      case '\014':
	c=getChar(i++);
	match = IsInCharset(S->ext_op_set,c) || IsInCharset(S->op_set,c); 
	break;
      }
      if ((*s)&0x80) match=!match;
      if (!match) return 0;
    }
    else if (getChar(i++)!=*s) return 0;
    s++;
  }
  *pos=i;
  return 1;
}

int matchEndSequence(char *s,int *pos)
{
  if (*s==0) return 1;
  if (!matchSequence(s,pos)) return 0;
  if (S->preservelf&&iswhite(getChar(*pos-1))) (*pos)--;
  return 1;
}

int matchStartSequence(char *s,int *pos)
{
  char c;
  int match;

  if (!((*s)&0x60)) { /* special sequences from prev. context */
    c=getChar(*pos-1);
    match=1;
    if (*s==0) return 1;
    switch((*s)&0x1f) {
    case '\001':
      if ((c!=' ')&&(c!='\t')) {
	match=0;
	break;
      }
    case '\002':
      break;
    case '\003':
      if ((c!=' ')&&(c!='\t')&&(c!='\n')) {
	match=0;
	break;
      }
    case '\004':
      break;
    case '\006':
      if ((c==' ')||(c=='\t')||(c=='\n'))
	break;
    case '\005':
      match = ((c>='a')&&(c<='z')) || ((c>='A')&&(c<='Z'));
      break;
    case '\007':
      match = ((c>='0')&&(c<='9'));
      break;
    case '\010':
      match = IsInCharset(S->id_set,c);
      break;
    case '\011':
      match = (c=='\t');
      break;
    case '\012':
      match = (c=='\n');
      break;
    case '\013':
      match = IsInCharset(S->op_set,c);
      break;
    case '\014':
      match = IsInCharset(S->ext_op_set,c) || IsInCharset(S->op_set,c);
      break;
    }
    if ((*s)&0x80) match=!match;
    if (!match) return 0;
    s++;
  }    
  return matchSequence(s,pos);
}

void AddToCharset(CHARSET_SUBSET x,int c)
{
  x[c>>LOG_LONG_BITS] |= 1L<<(c&((1<<LOG_LONG_BITS)-1));
}

CHARSET_SUBSET MakeCharsetSubset(unsigned char *s)
{
  CHARSET_SUBSET x;
  int i;
  unsigned char c;

  x=(CHARSET_SUBSET) malloc(CHARSET_SUBSET_LEN*sizeof(unsigned long));
  for (i=0;i<CHARSET_SUBSET_LEN;i++) x[i]=0;
  while (*s!=0) {
    if (!((*s)&0x60)) { /* special sequences */
      if ((*s)&0x80) bug("negated special sequences not allowed in charset specifications");
      switch((*s)&0x1f) {
      case '\002':  /* \w, \W, \i, \o, \O not allowed */
      case '\004':
      case '\010':
      case '\013':
      case '\014':
	bug("special sequence not allowed in charset specification");
      case '\003':
	AddToCharset(x,'\n');
      case '\001':
	AddToCharset(x,' ');
      case '\011':
	AddToCharset(x,'\t');
	break;
      case '\006':
	AddToCharset(x,'\n');
	AddToCharset(x,' ');
	AddToCharset(x,'\t');
      case '\005':
	for (c='A';c<='Z';c++) AddToCharset(x,c);
	for (c='a';c<='z';c++) AddToCharset(x,c);
	break;
      case '\007':
	for (c='0';c<='9';c++) AddToCharset(x,c);
	break;
      case '\012':
	AddToCharset(x,'\n');
	break;
      }
    }
    else if ((s[1]=='-')&&((s[2]&0x60)!=0)&&(s[2]>=*s)) {
      for (c=*s;c<=s[2];c++) AddToCharset(x,c);
      s+=2;
    }
    else AddToCharset(x,*s);
    s++;
  }
  return x;
}


int idequal(char *b,int l,char *s)
{
  int i;
  
  if ((int)strlen(s)!=l) return 0;
  for (i=0;i<l;i++) if (b[i]!=s[i]) return 0;
  return 1;
}

int findIdent(char *b,int l)
{
  int i;
  
  for (i=0;i<nmacros;i++)
    if (idequal(b,l,macros[i].username)) return i;
  return -1;
}

int findNamedArg(char *b,int l)
{
  char *s; 
  int i;

  for (i=0;;i++) {
    s=C->namedargs[i];
    if (s==NULL) return -1;
    if (idequal(b,l,s)) return i;
  } 
}

void shiftIn(int l)
{
  int i;
  
  if (l<=1) return;
  l--;
  if (l>=C->len) C->len=0;
  else {
    if (C->len-l>100) { /* we want to shrink that buffer */
      C->buf+=l; C->bufsize-=l;
    } else
      for (i=l;i<C->len;i++) C->buf[i-l]=C->buf[i];
    C->len-=l;
    C->eof=(C->buf[0]==0);
  }
  if (C->len<=1) {
    if (C->in==NULL) C->eof=1;
    else C->eof=feof(C->in);
  }
}

void initthings(int argc,char **argv)
{
  char **arg,*s;
  int i,isinput,isoutput,ishelp,ismode,hasmeta,usrmode;

  DefaultOp=MakeCharsetSubset(DEFAULT_OP_STRING);
  PrologOp=MakeCharsetSubset(PROLOG_OP_STRING);
  DefaultExtOp=MakeCharsetSubset(DEFAULT_OP_PLUS);
  DefaultId=MakeCharsetSubset(DEFAULT_ID_STRING);

  nmacros=0;
  nalloced=31;
  macros=(struct MACRO *)malloc(nalloced*sizeof(struct MACRO));

  S=(struct SPECS *)malloc(sizeof(struct SPECS));
  S->User=CUser;
  S->Meta=CMeta;
  S->comments=NULL;
  S->stack_next=NULL;
  S->preservelf=0;
  S->op_set=DefaultOp;
  S->ext_op_set=DefaultExtOp;
  S->id_set=DefaultId;
  
  C=(struct INPUTCONTEXT *)malloc(sizeof(struct INPUTCONTEXT));
  C->in=stdin;
  C->argc=0;
  C->argv=NULL;
  C->filename=strdup("stdin");
  C->out=(struct OUTPUTCONTEXT *)malloc(sizeof(struct OUTPUTCONTEXT));
  C->out->f=stdout;
  C->out->bufsize=0;
  C->lineno=0;
  isinput=isoutput=ismode=ishelp=hasmeta=usrmode=0;
  nincludedirs=0;
  C->bufsize=80;
  C->len=0;
  C->buf=C->malloced_buf=malloc(C->bufsize);
  C->eof=0;
  C->namedargs=NULL;
  C->in_comment=0;
  C->ambience=FLAG_TEXT;
  commented[0]=0;
  iflevel=0;
  execallowed=0;
  autoswitch=0;
  dosmode=DEFAULT_CRLF;
  
  for (arg=argv+1;*arg;arg++) {
    if (strcmp(*arg, "-nostdinc") == 0) {
      NoStdInc = 1;
      continue;
    }
    if (strcmp(*arg, "-nocurinc") == 0) {
      NoCurIncFirst = 1;
      continue;
    }
    if (strcmp(*arg, "-curdirinclast") == 0) {
      CurDirIncLast = 1;
      NoCurIncFirst = 1;
      continue;
    }
    if (strcmp(*arg, "-includemarker") == 0) {
      /* assume that the include marker string is correct, i.e., has 3 ?'s */
      construct_include_directive_marker(&include_directive_marker, *(++arg));
      continue;
    }

    if (strcmp(*arg, "-warninglevel") == 0) {
      arg++;
      WarningLevel = atoi(*arg);
      continue;
    }

    if (**arg=='+') {
      switch((*arg)[1]) {
      case 'c':
	s=(*arg)+2;
	if (*s==0) s="ccc";
	if (!(*(++arg)))
	  usage();
	if (!(*(++arg)))
	  usage();
	add_comment(S,s,strnl(*(arg-1)),strnl(*arg),0,0);
	break;
      case 's':
	s=(*arg)+2;
	if (*s==0) s="sss";
	if (!(*(++arg))) usage();
	if (!(*(++arg))) usage();
	if (!(*(++arg))) usage();
	add_comment(S,s,strnl(*(arg-2)),strnl(*(arg-1)),**arg,0);
	break;
      case 'z': 
	dosmode=0;
	break;
      default: ishelp=1;
      }
    }
    else if (**arg!='-') {
      ishelp|=isinput; isinput=1;
      C->in=fopen(*arg,"r");
      free(C->filename); C->filename=strdup(*arg);
      if (C->in==NULL) bug("Cannot open input file");
    }
    else switch((*arg)[1]) {
    case 'I':
      if (nincludedirs==MAXINCL) 
	bug("too many include directories");
      if ((*arg)[2]==0) {
	if (!(*(++arg)))
	  usage();
	includedir[nincludedirs++]=strdup(*arg);
      }
      else includedir[nincludedirs++]=strdup((*arg)+2);
      break;
    case 'C':
      ishelp|=ismode|hasmeta|usrmode; ismode=1;
      S->User=KUser; S->Meta=KMeta;
      S->preservelf=0;
      add_comment(S,"ccc",strdup("/*"),strdup("*/"),0,0);
      add_comment(S,"ccc",strdup("//"),strdup("\n"),0,0);
      add_comment(S,"ccc",strdup("\\\n"),strdup(""),0,0);
      add_comment(S,"sss",strdup("\""),strdup("\""),'\\','\n');
      add_comment(S,"sss",strdup("'"),strdup("'"),'\\','\n');
      break;
    case 'P':
      ishelp|=ismode|hasmeta|usrmode; ismode=1;
      S->User=KUser; S->Meta=KMeta;
      S->preservelf=0;
      S->op_set=PrologOp;
      add_comment(S,"css",strdup("\213/*"),strdup("*/"),0,0); /* \!o */
      add_comment(S,"cii",strdup("\\\n"),strdup(""),0,0);
      add_comment(S,"css",strdup("%"),strdup("\n"),0,0);
      add_comment(S,"sss",strdup("\""),strdup("\""),0,'\n');
      add_comment(S,"sss",strdup("\207'"),strdup("'"),0,'\n'); /* \!# */
      break;
    case 'T':
      ishelp|=ismode|hasmeta|usrmode; ismode=1;
      S->User=S->Meta=Tex;
      break;
    case 'H':
      ishelp|=ismode|hasmeta|usrmode; ismode=1;
      S->User=S->Meta=Html;
      break;
    case 'U':
      ishelp|=ismode|usrmode; usrmode=1;
      if (!readModeDescription(arg,&(S->User),0))
	  usage();
      arg+=9;
      if (!hasmeta) S->Meta=S->User;
      break;
    case 'M':
      ishelp|=ismode|hasmeta; hasmeta=1;
      if (!readModeDescription(arg,&(S->Meta),1))
	  usage();
      arg+=7;
      break;
    case 'O':
      file_and_stdout = 1;
    case 'o':
      if (!(*(++arg)))
	  usage();
      ishelp|=isoutput; isoutput=1;
      C->out->f=fopen(*arg,"w");
      if (C->out->f==NULL) bug("Cannot create output file");
      break;
    case 'D':
      if ((*arg)[2]==0) {
	if (!(*(++arg)))
	  usage();
	s=strnl0(*arg);
      }
      else s=strnl0((*arg)+2);
      parseCmdlineDefine(s); free(s); break;
    case 'x':
      execallowed=1;
      break;
    case 'n':
      S->preservelf=1;
      break;
    case 'z':
      dosmode=1;
      break;
    case 'c':
    case 's':
      if (!(*(++arg)))
	  usage();
      delete_comment(S,strnl(*arg));
      break;
    case 'm':
      autoswitch=1; break;
    default:
      ishelp=1;
    }
    if (hasmeta&&!usrmode) usage();
    if (ishelp) usage();
  }

#ifndef WIN_NT
  if ((nincludedirs==0) && !NoStdInc) {
    includedir[0]=strdup("/usr/include");
    nincludedirs=1;
  }
#endif

  for (i=0;i<nmacros;i++) {
    macros[i].define_specs=CloneSpecs(S);
    lookupArgRefs(i); /* for macro aliasing */
  }
}

int findCommentEnd(char *endseq,char quote,char warn,int pos,int flags)
{
  int i;
  char c;

  while (1) {
    c=getChar(pos);
    i=pos;
    if (c==0) bug("Input ended while scanning a comment/string");
    if (c==warn) {
      warn=0;
      if (WarningLevel > 1)
	warning("possible comment/string termination problem");
    }
    if (matchSequence(endseq,&i)) return pos;
    if (c==quote) pos+=2;
    else if ((flags&PARSE_MACROS)&&(c==S->User.quotechar)) pos+=2;
    else pos++;
  }
}

void SkipPossibleComments(int *pos,int cmtmode,int silentonly)
{
  int found;
  struct COMMENT *c;
  
  if (C->in_comment) return;
  do {
    found=0;
    if (getChar(*pos)==0) return; /* EOF */
    for (c=S->comments;c!=NULL;c=c->next)
      if (!(c->flags[cmtmode]&FLAG_IGNORE))
        if (!silentonly||(c->flags[cmtmode]==FLAG_COMMENT))
          if (matchStartSequence(c->start,pos)) {
            *pos=findCommentEnd(c->end,c->quote,c->warn,*pos,c->flags[cmtmode]);
            matchEndSequence(c->end,pos);
            found=1;
            break;
          }
  } while (found);
}

/* look for a possible user macro.
   Input :  idstart = scan start
            idcheck = check id for long macro forms before splicing args ?
            cmtmode = comment mode (FLAG_META or FLAG_USER)
   Output : idstart/idend = macro name location
            sh_end/lg_end = macro form end (-1 if no match)
            argb/arge     = argument locations for long form
            argc          = argument count for long form
            id            = macro id, if idcheck was set at input 
*/

int SplicePossibleUser(int *idstart,int *idend,int *sh_end,int *lg_end,
		       int *argb,int *arge,int *argc,int idcheck,
		       int *id,int cmtmode)
{
  int match,k,pos;

  if (!matchStartSequence(S->User.mStart,idstart)) return 0;
  *idend=identifierEnd(*idstart);
  if ((*idend)&&!getChar(*idend-1)) return 0;
  
  /* look for args or no args */
  *sh_end=*idend;
  if (!matchEndSequence(S->User.mEnd,sh_end)) *sh_end=-1;
  pos=*idend;
  match=matchSequence(S->User.mArgS,&pos);
    
  if (idcheck) {
    *id=findIdent(C->buf+*idstart,*idend-*idstart);
    if (*id<0) match=0;
  }
  *lg_end=-1;
  
  if (match) {
    *argc=0;
    while (1) {
      if (*argc>=MAXARGS) bug("too many macro parameters");
      argb[*argc]=pos;
      k=0;
      while(1) { /* look for mArgE, mArgSep, or comment-start */
        pos=iterIdentifierEnd(pos);
        SkipPossibleComments(&pos,cmtmode,0);
        if (getChar(pos)==0) return (*sh_end>=0); /* EOF */
        if (strchr(S->User.stackchar,getChar(pos))) k++;
        if (k) { if (strchr(S->User.unstackchar,getChar(pos))) k--; }
        else {
          arge[*argc]=pos;
          if (matchSequence(S->User.mArgSep,&pos)) { match=0; break; }
          if (matchEndSequence(S->User.mArgE,&pos)) 
            { match=1; break; }
        }
        pos++; /* nothing matched, go forward */
      }
      (*argc)++;
      if (match) { /* no more args */
        *lg_end=pos;
        break;
      }
    }
  }
  return ((*lg_end>=0)||(*sh_end>=0));
}

int findMetaArgs(int start,int *p1b,int *p1e,int *p2b,int *p2e,int *endm,int *argc,int *argb,int *arge)
{
  int pos,k;
  int hyp_end1,hyp_end2;
  
  /* look for mEnd or mArgS */
  pos=start;
  if (!matchSequence(S->Meta.mArgS,&pos)) {
    if (!matchEndSequence(S->Meta.mEnd,&pos)) return -1;
    *endm=pos;
    return 0;
  }
  *p1b=pos;

  /* special syntax for #define : 1st arg is a macro call */
  if ((*argc)&&SplicePossibleUser(&pos,p1e,&hyp_end1,&hyp_end2,
                                  argb,arge,argc,0,NULL,FLAG_META)) {
    *p1b=pos;
    if (hyp_end2>=0) pos=hyp_end2; else { pos=hyp_end1; *argc=0; }
    if (!matchSequence(S->Meta.mArgSep,&pos)) {
      if (!matchEndSequence(S->Meta.mArgE,&pos))
	bug("#define/#defeval requires an identifier or a single macro call");
      *endm=pos;
      return 1;
    }
  } else {
    *argc=0;
    k=0;
    while(1) { /* look for mArgE, mArgSep, or comment-start */
      pos=iterIdentifierEnd(pos);
      SkipPossibleComments(&pos,FLAG_META,0);
      if (getChar(pos)==0) bug("unfinished macro argument");
      if (strchr(S->Meta.stackchar,getChar(pos))) k++;
      if (k) { if (strchr(S->Meta.unstackchar,getChar(pos))) k--; }
      else {
        *p1e=pos;
        if (matchSequence(S->Meta.mArgSep,&pos)) break;
        if (matchEndSequence(S->Meta.mArgE,&pos)) {
          *endm=pos;
          return 1;
        }
      }
      pos++; /* nothing matched, go forward */
    }
  }
  
  *p2b=pos;
  k=0;
  while(1) { /* look for mArgE or comment-start */
    pos=iterIdentifierEnd(pos);
    SkipPossibleComments(&pos,FLAG_META,0);
    if (getChar(pos)==0) bug("unfinished macro");
    if (strchr(S->Meta.stackchar,getChar(pos))) k++;
    if (k) { if (strchr(S->Meta.unstackchar,getChar(pos))) k--; }
    else {
      *p2e=pos;
      if (matchEndSequence(S->Meta.mArgE,&pos)) break;
    }
    pos++; /* nothing matched, go forward */
  }
  *endm=pos;
  return 2;
}

char *ProcessText(char *buf,int l,int ambience)
{
  char *s;
  struct INPUTCONTEXT *T;
  
  if (l==0) { s=malloc(1); s[0]=0; return s;  }
  s=malloc(l+2);
  s[0]='\n';
  memcpy(s+1,buf,l);
  s[l+1]=0;
  T=C;
  C=(struct INPUTCONTEXT *)malloc(sizeof(struct INPUTCONTEXT));
  C->out=(struct OUTPUTCONTEXT *)malloc(sizeof(struct OUTPUTCONTEXT));
  C->in=NULL;
  C->argc=T->argc;
  C->argv=T->argv;
  C->filename=T->filename;
  C->out->buf=malloc(80);
  C->out->len=0;
  C->out->bufsize=80;
  C->out->f=NULL;
  C->lineno=T->lineno;
  C->bufsize=l+2;
  C->len=l+1;
  C->buf=C->malloced_buf=s;
  C->eof=0;
  C->namedargs=T->namedargs;
  C->in_comment=T->in_comment;
  C->ambience=ambience;
  
  ProcessContext();
  outchar(0); /* note that outchar works with the half-destroyed context ! */
  s=C->out->buf;
  free(C->out);
  free(C);
  C=T;
  return s;
}

int SpliceInfix(char *buf,int pos1,int pos2,char *sep,int *spl1,int *spl2)
{
  int pos,numpar,l;
  char *p;
  
  numpar=0; l=strlen(sep);
  for (pos=pos2-1,p=buf+pos;pos>=pos1;pos--,p--) {
    if (*p==')') numpar++;
    if (*p=='(') numpar--;
    if (numpar<0) return 0;
    if ((numpar==0)&&(pos2-pos>=l)&&!strncmp(p,sep,l))
      { *spl1=pos; *spl2=pos+l; return 1; }
  }
  return 0;
}

int DoArithmEval(char *buf,int pos1,int pos2,int *result)
{
  int spl1,spl2,result1,result2;
  char c,*p;
  
  while ((pos1<pos2)&&iswhite(buf[pos1])) pos1++;
  while ((pos1<pos2)&&iswhite(buf[pos2-1])) pos2--;
  if (pos1==pos2) return 0;
  
  /* look for C operators starting with lowest precedence */
  
  if (SpliceInfix(buf,pos1,pos2,"||",&spl1,&spl2)) {
    if (!DoArithmEval(buf,pos1,spl1,&result1)||
        !DoArithmEval(buf,spl2,pos2,&result2)) return 0;
    *result=result1||result2;
    return 1;
  }

  if (SpliceInfix(buf,pos1,pos2,"&&",&spl1,&spl2)) {
    if (!DoArithmEval(buf,pos1,spl1,&result1)||
        !DoArithmEval(buf,spl2,pos2,&result2)) return 0;
    *result=result1&&result2;
    return 1;
  }

  if (SpliceInfix(buf,pos1,pos2,"|",&spl1,&spl2)) {
    if (!DoArithmEval(buf,pos1,spl1,&result1)||
        !DoArithmEval(buf,spl2,pos2,&result2)) return 0;
    *result=result1|result2;
    return 1;
  }

  if (SpliceInfix(buf,pos1,pos2,"^",&spl1,&spl2)) {
    if (!DoArithmEval(buf,pos1,spl1,&result1)||
        !DoArithmEval(buf,spl2,pos2,&result2)) return 0;
    *result=result1^result2;
    return 1;
  }

  if (SpliceInfix(buf,pos1,pos2,"&",&spl1,&spl2)) {
    if (!DoArithmEval(buf,pos1,spl1,&result1)||
        !DoArithmEval(buf,spl2,pos2,&result2)) return 0;
    *result=result1&result2;
    return 1;
  }

  if (SpliceInfix(buf,pos1,pos2,"!=",&spl1,&spl2)) {
    if (!DoArithmEval(buf,pos1,spl1,&result1)||
        !DoArithmEval(buf,spl2,pos2,&result2)) {
      /* revert to string comparison */
      while ((pos1<spl1)&&iswhite(buf[spl1-1])) spl1--;
      while ((pos2>spl2)&&iswhite(buf[spl2])) spl2++;
      if (spl1-pos1!=pos2-spl2) *result=1;
      else *result=(strncmp(buf+pos1,buf+spl2,spl1-pos1)!=0);
    }  
    else *result=(result1!=result2);
    return 1;
  }
  
  if (SpliceInfix(buf,pos1,pos2,"==",&spl1,&spl2)) {
    if (!DoArithmEval(buf,pos1,spl1,&result1)||
        !DoArithmEval(buf,spl2,pos2,&result2)) {
      /* revert to string comparison */
      while ((pos1<spl1)&&iswhite(buf[spl1-1])) spl1--;
      while ((pos2>spl2)&&iswhite(buf[spl2])) spl2++;
      if (spl1-pos1!=pos2-spl2) *result=0;
      else *result=(strncmp(buf+pos1,buf+spl2,spl1-pos1)==0);
    }  
    else *result=(result1==result2);
    return 1;
  }

  if (SpliceInfix(buf,pos1,pos2,">=",&spl1,&spl2)) {
    if (!DoArithmEval(buf,pos1,spl1,&result1)||
        !DoArithmEval(buf,spl2,pos2,&result2)) return 0;
    *result=result1>=result2;
    return 1;
  }

  if (SpliceInfix(buf,pos1,pos2,">",&spl1,&spl2)) {
    if (!DoArithmEval(buf,pos1,spl1,&result1)||
        !DoArithmEval(buf,spl2,pos2,&result2)) return 0;
    *result=result1>result2;
    return 1;
  }
  
  if (SpliceInfix(buf,pos1,pos2,"<=",&spl1,&spl2)) {
    if (!DoArithmEval(buf,pos1,spl1,&result1)||
        !DoArithmEval(buf,spl2,pos2,&result2)) return 0;
    *result=result1<=result2;
    return 1;
  }

  if (SpliceInfix(buf,pos1,pos2,"<",&spl1,&spl2)) {
    if (!DoArithmEval(buf,pos1,spl1,&result1)||
        !DoArithmEval(buf,spl2,pos2,&result2)) return 0;
    *result=result1<result2;
    return 1;
  }
  
  if (SpliceInfix(buf,pos1,pos2,"-",&spl1,&spl2))
    if (spl1!=pos1) {
      if (!DoArithmEval(buf,pos1,spl1,&result1)||
          !DoArithmEval(buf,spl2,pos2,&result2)) return 0;
      *result=result1-result2;
      return 1;
    }

  if (SpliceInfix(buf,pos1,pos2,"+",&spl1,&spl2)) {
    if (!DoArithmEval(buf,pos1,spl1,&result1)||
        !DoArithmEval(buf,spl2,pos2,&result2)) return 0;
    *result=result1+result2;
    return 1;
  }
  
  if (SpliceInfix(buf,pos1,pos2,"%",&spl1,&spl2)) {
    if (!DoArithmEval(buf,pos1,spl1,&result1)||
        !DoArithmEval(buf,spl2,pos2,&result2)) return 0;
    if (result2==0) bug("Division by zero in expression");
    *result=result1%result2;
    return 1;
  }

  if (SpliceInfix(buf,pos1,pos2,"/",&spl1,&spl2)) {
    if (!DoArithmEval(buf,pos1,spl1,&result1)||
        !DoArithmEval(buf,spl2,pos2,&result2)) return 0;
    if (result2==0) bug("Division by zero in expression");
    *result=result1/result2;
    return 1;
  }
  
  if (SpliceInfix(buf,pos1,pos2,"*",&spl1,&spl2)) {
    if (!DoArithmEval(buf,pos1,spl1,&result1)||
        !DoArithmEval(buf,spl2,pos2,&result2)) return 0;
    *result=result1*result2;
    return 1;
  }

  if (buf[pos1]=='~') {
    if (!DoArithmEval(buf,pos1+1,pos2,&result1)) return 0;
    *result=~result1;
    return 1;
  }

  if (buf[pos1]=='!') {
    if (!DoArithmEval(buf,pos1+1,pos2,&result1)) return 0;
    *result=!result1;
    return 1;
  }

  if (buf[pos1]=='-') {
    if (!DoArithmEval(buf,pos1+1,pos2,&result1)) return 0;
    *result=-result1;
    return 1;
  }

  /* add the length() builtin to measure the length of the macro expansion */
  if (strncmp(buf+pos1,"length",strlen("length"))==0) {
    char *s;
    int symlen=strlen("length");
    s = ProcessText(buf+pos1+symlen,pos2-pos1-symlen,FLAG_META);
    /* subtract 2 to account for the parentheses */
    *result=strlen(s)-2;
    return 1;
  }
  
  if (buf[pos1]=='(') {
    if (buf[pos2-1]!=')') return 0;
    return DoArithmEval(buf,pos1+1,pos2-1,result);
  }
  
  c=buf[pos2]; buf[pos2]=0;
  *result=(int)strtol(buf+pos1,&p,0);
  buf[pos2]=c;
  return (p==buf+pos2);
}

void delete_macro(int i)
{
  int j;
  nmacros--;
  free(macros[i].username);
  free(macros[i].macrotext); 
  if (macros[i].argnames!=NULL) {
    for (j=0;j<macros[i].nnamedargs;j++) free(macros[i].argnames[j]);
    free(macros[i].argnames);
    macros[i].argnames=NULL;
  }
  FreeComments(macros[i].define_specs);
  free(macros[i].define_specs);
  memcpy((char *)(macros+i),(char *)(macros+nmacros),sizeof(struct MACRO));
}

char *ArithmEval(int pos1,int pos2)
{
  char *s,*t;
  int i;
  
  /* first define the defined(...) operator */
  i=findIdent("defined",strlen("defined"));
  if (i>=0) warning("the defined(...) macro is already defined");
  else {
    newmacro("defined",strlen("defined"),1);
    macros[nmacros].macrolen=0;
    macros[nmacros].macrotext=malloc(1);
    macros[nmacros].macrotext[0]=0;
    macros[nmacros].nnamedargs=-2; /* trademark of the defined(...) macro */
    nmacros++;
  }
  /* process the text in a usual way */
  s=ProcessText(C->buf+pos1,pos2-pos1,FLAG_META);
  /* undefine the defined(...) operator */
  if (i<0) {
    i=findIdent("defined",strlen("defined"));
    if ((i<0)||(macros[i].nnamedargs!=-2))
      warning("the defined(...) macro was redefined in expression");
    else delete_macro(i);
  }

  if (!DoArithmEval(s,0,strlen(s),&i)) return s; /* couldn't compute */
  t=malloc(MAX_GPP_NUM_SIZE);
  sprintf(t,"%d",i);
  free(s);
  return t;
}

int comment_or_white(int start,int end,int cmtmode)
{
  char c;
  
  while (start<end) {
    SkipPossibleComments(&start,cmtmode,1);
    if (start<end) {
      c=getChar(start++);
      if ((c!=' ')&&(c!='\n')&&(c!='\t')) return 0;
    }
  }
  return 1;
}

char *remove_comments(int start,int end,int cmtmode)
{
  char *s,*t;
  
  t=s=malloc(end-start+1);
  while (start<end) {
    SkipPossibleComments(&start,cmtmode,1);
    if (start<end) {
      *t=getChar(start++);
      if ((*t==S->User.quotechar)&&(start<end)) { *(++t)=getChar(start++); }
      t++;
    }
  }
  *t=0;
  return s;
}

void SetStandardMode(struct SPECS *P,char *opt) 
{
  P->op_set=DefaultOp;
  P->ext_op_set=DefaultExtOp;
  P->id_set=DefaultId;
  FreeComments(P);
  if (!strcmp(opt,"C")||!strcmp(opt,"cpp")) {
    P->User=KUser; P->Meta=KMeta;
    P->preservelf=1;
    add_comment(P,"ccc",strdup("/*"),strdup("*/"),0,0);
    add_comment(P,"ccc",strdup("//"),strdup("\n"),0,0);
    add_comment(P,"ccc",strdup("\\\n"),strdup(""),0,0);
    add_comment(P,"sss",strdup("\""),strdup("\""),'\\','\n');
    add_comment(P,"sss",strdup("'"),strdup("'"),'\\','\n');
  }
  else if (!strcmp(opt,"TeX")||!strcmp(opt,"tex")) {
    P->User=Tex; P->Meta=Tex;
    P->preservelf=0;
  }
  else if (!strcmp(opt,"HTML")||!strcmp(opt,"html")) {
    P->User=Html; P->Meta=Html;
    P->preservelf=0;
  }
  else if (!strcmp(opt,"default")) {
    P->User=CUser; P->Meta=CMeta;
    P->preservelf=0;
  }
  else if (!strcmp(opt,"Prolog")||!strcmp(opt,"prolog")) {
    P->User=KUser; P->Meta=KMeta;
    P->preservelf=1;
    P->op_set=PrologOp;
    add_comment(P,"css",strdup("\213/*"),strdup("*/"),0,0); /* \!o */ 
    add_comment(P,"cii",strdup("\\\n"),strdup(""),0,0);
    add_comment(P,"css",strdup("%"),strdup("\n"),0,0);
    add_comment(P,"sss",strdup("\""),strdup("\""),0,'\n');
    add_comment(P,"sss",strdup("\207'"),strdup("'"),0,'\n');   /* \!# */
  }
  else bug("unknown standard mode");
}

void ProcessModeCommand(int p1start,int p1end,int p2start,int p2end)
{
  struct SPECS *P;
  char *s,*p,*opt;
  int nargs,check_isdelim;
  char *args[10]; /* can't have more than 10 arguments */
  
  whiteout(&p1start,&p1end);
  if ((p1start==p1end)||(identifierEnd(p1start)!=p1end))
    bug("invalid #mode syntax");
  if (p2start<0) s=strdup("");
  else s=ProcessText(C->buf+p2start,p2end-p2start,FLAG_META);

  /* argument parsing */
  p=s; opt=NULL;
  while (iswhite(*p)) p++;
  if ((*p!='"')&&(*p!=0)) {
    opt=p;
    while ((*p!=0)&&!iswhite(*p)) p++;
    if (*p!=0) {
      *(p++)=0;
      while (iswhite(*p)) p++;
    }
  }
  nargs=0;
  check_isdelim=!idequal(C->buf+p1start,p1end-p1start,"charset");
  while (*p!=0) {
    if (nargs==10) bug("too many arguments in #mode command");
    if (*(p++)!='"') bug("syntax error in #mode command (missing \" or trailing data)");
    args[nargs++]=p;
    p=strnl2(p,check_isdelim);
    while (iswhite(*p)) p++;
  }    

  if (idequal(C->buf+p1start,p1end-p1start,"quote")) {
    if (opt||(nargs>1)) bug("syntax error in #mode quote command");
    if (nargs==0) args[0]="";
    S->stack_next->User.quotechar=args[0][0];
  }
  else if (idequal(C->buf+p1start,p1end-p1start,"comment")) {
    if ((nargs<2)||(nargs>4)) bug("syntax error in #mode comment command");
    if (!opt) opt="ccc";
    if (nargs<3) args[2]="";
    if (nargs<4) args[3]="";
    add_comment(S->stack_next,opt,strdup(args[0]),strdup(args[1]),args[2][0],args[3][0]);
  }
  else if (idequal(C->buf+p1start,p1end-p1start,"string")) {
    if ((nargs<2)||(nargs>4)) bug("syntax error in #mode string command");
    if (!opt) opt="sss";
    if (nargs<3) args[2]="";
    if (nargs<4) args[3]="";
    add_comment(S->stack_next,opt,strdup(args[0]),strdup(args[1]),args[2][0],args[3][0]);
  }
  else if (idequal(C->buf+p1start,p1end-p1start,"save")
	   ||idequal(C->buf+p1start,p1end-p1start,"push")) {
    if ((opt!=NULL)||nargs) bug("too many arguments to #mode save");
    P=CloneSpecs(S->stack_next);
    P->stack_next=S->stack_next;
    S->stack_next=P;
  }
  else if (idequal(C->buf+p1start,p1end-p1start,"restore")
	   ||idequal(C->buf+p1start,p1end-p1start,"pop")) {
    if ((opt!=NULL)||nargs) bug("too many arguments to #mode restore");
    P=S->stack_next->stack_next;
    if (P==NULL) bug("#mode restore without #mode save");
    FreeComments(S->stack_next);
    free(S->stack_next);
    S->stack_next=P;
  }
  else if (idequal(C->buf+p1start,p1end-p1start,"standard")) {
    if ((opt==NULL)||nargs) bug("syntax error in #mode standard");
    SetStandardMode(S->stack_next,opt);
  }
  else if (idequal(C->buf+p1start,p1end-p1start,"user")) {
    if ((opt!=NULL)||(nargs!=9)) bug("#mode user requires 9 arguments");
    S->stack_next->User.mStart=strdup(args[0]);
    S->stack_next->User.mEnd=strdup(args[1]);
    S->stack_next->User.mArgS=strdup(args[2]);
    S->stack_next->User.mArgSep=strdup(args[3]);
    S->stack_next->User.mArgE=strdup(args[4]);
    S->stack_next->User.stackchar=strdup(args[5]);
    S->stack_next->User.unstackchar=strdup(args[6]);
    S->stack_next->User.mArgRef=strdup(args[7]);
    S->stack_next->User.quotechar=args[8][0];
  }
  else if (idequal(C->buf+p1start,p1end-p1start,"meta")) {
    if ((opt!=NULL)&&!nargs&&!strcmp(opt,"user")) 
      S->stack_next->Meta=S->stack_next->User;
    else {
      if ((opt!=NULL)||(nargs!=7)) bug("#mode meta requires 7 arguments");
      S->stack_next->Meta.mStart=strdup(args[0]);
      S->stack_next->Meta.mEnd=strdup(args[1]);
      S->stack_next->Meta.mArgS=strdup(args[2]);
      S->stack_next->Meta.mArgSep=strdup(args[3]);
      S->stack_next->Meta.mArgE=strdup(args[4]);
      S->stack_next->Meta.stackchar=strdup(args[5]);
      S->stack_next->Meta.unstackchar=strdup(args[6]);
    }
  }
  else if (idequal(C->buf+p1start,p1end-p1start,"preservelf")) {
    if ((opt==NULL)||nargs) bug("syntax error in #mode preservelf");
    if (!strcmp(opt,"1")||!strcasecmp(opt,"on")) S->stack_next->preservelf=1;
    else if (!strcmp(opt,"0")||!strcasecmp(opt,"off")) S->stack_next->preservelf=0;
    else bug("#mode preservelf requires on/off argument");
  }
  else if (idequal(C->buf+p1start,p1end-p1start,"nocomment")
	   ||idequal(C->buf+p1start,p1end-p1start,"nostring")) {
    if ((opt!=NULL)||(nargs>1))
      bug("syntax error in #mode nocomment/nostring");
    if (nargs==0) FreeComments(S->stack_next);
    else delete_comment(S->stack_next,strdup(args[0]));
  }
  else if (idequal(C->buf+p1start,p1end-p1start,"charset")) {
    if ((opt==NULL)||(nargs!=1)) bug("syntax error in #mode charset");
    if (!strcasecmp(opt,"op"))
      S->stack_next->op_set=MakeCharsetSubset((unsigned char *)args[0]);
    else if (!strcasecmp(opt,"par"))
      S->stack_next->ext_op_set=MakeCharsetSubset((unsigned char *)args[0]);
    else if (!strcasecmp(opt,"id"))
      S->stack_next->id_set=MakeCharsetSubset((unsigned char *)args[0]);
    else bug("unknown charset subset name in #mode charset");
  }
  else bug("unrecognized #mode command");
       
  free(s);
}

int ParsePossibleMeta()
{
  int cklen,nameend;
  int id,expparams,nparam,i,j;
  int p1start,p1end,p2start,p2end,macend;
  int argc,argb[MAXARGS],arge[MAXARGS];
  char *tmpbuf;

  cklen=1;
  if (!matchStartSequence(S->Meta.mStart,&cklen)) return -1;  
  nameend=identifierEnd(cklen);
  if (nameend&&!getChar(nameend-1)) return -1;
  id=0;
  argc=0; /* for #define with named args */
  if (idequal(C->buf+cklen,nameend-cklen,"define"))      /* check identifier */
    { id=1; expparams=2; argc=1; }
  else if (idequal(C->buf+cklen,nameend-cklen,"undef"))
    { id=2; expparams=1; }
  else if (idequal(C->buf+cklen,nameend-cklen,"ifdef"))
    { id=3; expparams=1; }
  else if (idequal(C->buf+cklen,nameend-cklen,"ifndef"))
    { id=4; expparams=1; }
  else if (idequal(C->buf+cklen,nameend-cklen,"else"))
    { id=5; expparams=0; }
  else if (idequal(C->buf+cklen,nameend-cklen,"endif"))
    { id=6; expparams=0; }
  else if (idequal(C->buf+cklen,nameend-cklen,"include"))
    { id=7; expparams=1; }
  else if (idequal(C->buf+cklen,nameend-cklen,"exec"))
    { id=8; expparams=1; }
  else if (idequal(C->buf+cklen,nameend-cklen,"defeval"))
    { id=9; expparams=2; argc=1; }
  else if (idequal(C->buf+cklen,nameend-cklen,"ifeq"))
    { id=10; expparams=2; }
  else if (idequal(C->buf+cklen,nameend-cklen,"ifneq"))
    { id=11; expparams=2; }
  else if (idequal(C->buf+cklen,nameend-cklen,"eval"))
    { id=12; expparams=1; }
  else if (idequal(C->buf+cklen,nameend-cklen,"if"))
    { id=13; expparams=1; }
  else if (idequal(C->buf+cklen,nameend-cklen,"mode"))
    { id=14; expparams=2; }
  else return -1;

  /* #MODE magic : define "..." to be C-style strings */
  if (id==14) {
    PushSpecs(S);
    S->preservelf=1;
    delete_comment(S,strdup("\""));
    add_comment(S,"sss",strdup("\""),strdup("\""),'\\','\n');
  }

  nparam=findMetaArgs(nameend,&p1start,&p1end,&p2start,&p2end,&macend,&argc,argb,arge);
  if (nparam==-1) return -1; 

  if ((nparam==2)&&iswhitesep(S->Meta.mArgSep))
    if (comment_or_white(p2start,p2end,FLAG_META)) nparam=1;
  if ((nparam==1)&&iswhitesep(S->Meta.mArgS))
    if (comment_or_white(p1start,p1end,FLAG_META)) nparam=0;
  if (expparams&&!nparam) bug("Missing argument in meta-macro");

  switch(id) {
  case 1: /* DEFINE */
    if (!commented[iflevel]) {
      whiteout(&p1start,&p1end); /* recall comments are not allowed here */
      if ((p1start==p1end)||(identifierEnd(p1start)!=p1end)) 
        bug("#define requires an identifier (A-Z,a-z,0-9,_ only)");
      /* buf starts 1 char before the macro */
      i=findIdent(C->buf+p1start,p1end-p1start);
      if (i>=0) delete_macro(i);
      newmacro(C->buf+p1start,p1end-p1start,1);
      if (nparam==1) { p2end=p2start=p1end; }
      replace_definition_with_blank_lines(C->buf+1,C->buf+p2end);
      macros[nmacros].macrotext=remove_comments(p2start,p2end,FLAG_META);
      macros[nmacros].macrolen=strlen(macros[nmacros].macrotext);
      macros[nmacros].defined_in_comment=C->in_comment;

      if (argc) {
        for (j=0;j<argc;j++) whiteout(argb+j,arge+j);
        /* define with one empty argument */
        if ((argc==1)&&(arge[0]==argb[0])) argc=0;
        macros[nmacros].argnames=(char **)malloc((argc+1)*sizeof(char *));
        macros[nmacros].argnames[argc]=NULL;
      }
      macros[nmacros].nnamedargs=argc;
      for (j=0;j<argc;j++) {
        if ((argb[j]==arge[j])||(identifierEnd(argb[j])!=arge[j]))
          bug("#define with named args needs identifiers as arg names");
        macros[nmacros].argnames[j]=malloc(arge[j]-argb[j]+1);
        memcpy(macros[nmacros].argnames[j],C->buf+argb[j],arge[j]-argb[j]);
        macros[nmacros].argnames[j][arge[j]-argb[j]]=0;
      }
      lookupArgRefs(nmacros++);
    } else
      replace_directive_with_blank_line(C->out->f);
    break;
     
  case 2: /* UNDEF */
    replace_directive_with_blank_line(C->out->f);
    if (!commented[iflevel]) {
      if (nparam==2 && WarningLevel > 0)
	warning("Extra argument to #undef ignored");
      whiteout(&p1start,&p1end);
      if ((p1start==p1end)||(identifierEnd(p1start)!=p1end))
        bug("#undef requires an identifier (A-Z,a-z,0-9,_ only)");
      i=findIdent(C->buf+p1start,p1end-p1start);
      if (i>=0) delete_macro(i);
    }
    break;

  case 3: /* IFDEF */
    iflevel++;
    replace_directive_with_blank_line(C->out->f);
    if (iflevel==STACKDEPTH) bug("Too many nested #ifdefs");
    commented[iflevel]=commented[iflevel-1];

    if (!commented[iflevel]) {
      if (nparam==2 && WarningLevel > 0)
	warning("Extra argument to #ifdef ignored");
      whiteout(&p1start,&p1end);
      if ((p1start==p1end)||(identifierEnd(p1start)!=p1end))
	bug("#ifdef requires an identifier (A-Z,a-z,0-9,_ only)");
      i=findIdent(C->buf+p1start,p1end-p1start);
      commented[iflevel]=(i==-1);
    }
    break;

  case 4: /* IFNDEF */
    iflevel++;
    replace_directive_with_blank_line(C->out->f);
    if (iflevel==STACKDEPTH) bug("Too many nested #ifdefs");
    commented[iflevel]=commented[iflevel-1];
    if (!commented[iflevel]) {
      if (nparam==2 && WarningLevel > 0)
	warning("Extra argument to #ifndef ignored");
      whiteout(&p1start,&p1end);
      if ((p1start==p1end)||(identifierEnd(p1start)!=p1end))
        bug("#ifndef requires an identifier (A-Z,a-z,0-9,_ only)");
      i=findIdent(C->buf+p1start,p1end-p1start);
      commented[iflevel]=(i!=-1);
    }
    break;
    
  case 5: /* ELSE */
    replace_directive_with_blank_line(C->out->f);
    if (!commented[iflevel] && (nparam>0) && WarningLevel > 0)
      warning("Extra argument to #else ignored");
    if (iflevel==0) bug("#else without #if");
    if (!commented[iflevel-1]) commented[iflevel]=!commented[iflevel];
    break;

  case 6: /* ENDIF */
    replace_directive_with_blank_line(C->out->f);
    if (!commented[iflevel] && (nparam>0) && WarningLevel > 0)
      warning("Extra argument to #endif ignored");
    if (iflevel==0) bug("#endif without #if");
    iflevel--;
    break;

  case 7: /* INCLUDE */
    if (!commented[iflevel]) {
      struct INPUTCONTEXT *N;
      FILE *f = NULL;
      char *incfile_name;

      if (nparam==2 && WarningLevel > 0)
	warning("Extra argument to #include ignored");
      if (!whiteout(&p1start,&p1end)) bug("Missing file name in #include");
      /* user may put "" or <> */
      if (((getChar(p1start)=='\"')&&(getChar(p1end-1)=='\"'))||
	  ((getChar(p1start)=='<')&&(getChar(p1end-1)=='>')))
	{ p1start++; p1end--; }
      if (p1start>=p1end) bug("Missing file name in #include");
      incfile_name=malloc(p1end-p1start+1);
      /* extract the orig include filename */
      for (i=0;i<p1end-p1start;i++)
	incfile_name[i]=getChar(p1start+i);
      incfile_name[p1end-p1start]=0;

      /* if absolute path name is specified */
      if (incfile_name[0]==SLASH
#ifdef WIN_NT
	  || (isalpha(incfile_name[0]) && incfile_name[1]==':')
#endif
	  )
	f=fopen(incfile_name,"r");
      else /* search current dir, if this search isn't turned off */
	if (!NoCurIncFirst) {
	  f = openInCurrentDir(incfile_name);
	}
       
      for (j=0;(f==NULL)&&(j<nincludedirs);j++) {
	incfile_name =
	  realloc(incfile_name,p1end-p1start+strlen(includedir[j])+2);
	strcpy(incfile_name,includedir[j]);
	incfile_name[strlen(includedir[j])]=SLASH;
	/* extract the orig include filename */
	for (i=0;i<p1end-p1start;i++) 
	  incfile_name[strlen(includedir[j])+1+i]=getChar(p1start+i);
	incfile_name[p1end-p1start+strlen(includedir[j])+1]=0;
	f=fopen(incfile_name,"r");
      }

      /* If didn't find the file and "." is said to be searched last */
      if (f==NULL && CurDirIncLast) {
	incfile_name=realloc(incfile_name,p1end-p1start+1);
	/* extract the orig include filename */
	for (i=0;i<p1end-p1start;i++) 
	  incfile_name[i]=getChar(p1start+i);
	incfile_name[p1end-p1start]=0;
	f = openInCurrentDir(incfile_name);
      }

      if (f==NULL)
	bug("Requested include file not found");

      N=C;
      C=(struct INPUTCONTEXT *)malloc(sizeof(struct INPUTCONTEXT));
      C->in=f;
      C->argc=0;
      C->argv=NULL;
      C->filename=incfile_name;
      C->out=N->out;
      C->lineno=0;
      C->bufsize=80;
      C->len=0;
      C->buf=C->malloced_buf=malloc(C->bufsize);
      C->eof=0;
      C->namedargs=NULL;
      C->in_comment=0;
      C->ambience=FLAG_TEXT;
      PushSpecs(S);
      if (autoswitch) {
	if (!strcmp(incfile_name+strlen(incfile_name)-2,".h")
	    || !strcmp(incfile_name+strlen(incfile_name)-2,".c"))
	  SetStandardMode(S,"C");
      }

      /* Include marker before the included contents */
      write_include_marker(N->out->f, 1, C->filename, "1");
      ProcessContext();
      /* Include marker after the included contents */
      write_include_marker(N->out->f, N->lineno, N->filename, "2");

      /* Need to leave the blank line in lieu of #include, like cpp does */
      replace_directive_with_blank_line(N->out->f);
      free(C);
      PopSpecs();
      C=N;
    } else
      replace_directive_with_blank_line(C->out->f);
    break;

  case 8: /* EXEC */
    if (!commented[iflevel]) {
      if (!execallowed)
	warning("Not allowed to #exec. Command output will be left blank");
      else {
	char *s,*t;
	int c;
	FILE *f;
	s=ProcessText(C->buf+p1start,p1end-p1start,FLAG_META);
	if (nparam==2) {
	  t=ProcessText(C->buf+p2start,p2end-p2start,FLAG_META);
	  i=strlen(s);
	  s=realloc(s,i+strlen(t)+2);
	  s[i]=' ';
	  strcpy(s+i+1,t);
	  free(t);
	}
	f=popen(s,"r");
	free(s);
	if (f==NULL) warning("Cannot #exec. Command not found ?");
	else {
	  while ((c=fgetc(f)) != EOF) outchar((char)c);
	  pclose(f);
	}
      }
    }
    break;

  case 9: /* DEFEVAL */
    if (!commented[iflevel]) {
      whiteout(&p1start,&p1end);
      if ((p1start==p1end)||(identifierEnd(p1start)!=p1end)) 
        bug("#defeval requires an identifier (A-Z,a-z,0-9,_ only)");
      tmpbuf=ProcessText(C->buf+p2start,p2end-p2start,FLAG_META);
      i=findIdent(C->buf+p1start,p1end-p1start);
      if (i>=0) delete_macro(i);
      newmacro(C->buf+p1start,p1end-p1start,1);
      if (nparam==1) { p2end=p2start=p1end; }
      replace_definition_with_blank_lines(C->buf+1,C->buf+p2end);
      macros[nmacros].macrotext=tmpbuf;
      macros[nmacros].macrolen=strlen(macros[nmacros].macrotext);
      macros[nmacros].defined_in_comment=C->in_comment;

      if (argc) {
        for (j=0;j<argc;j++) whiteout(argb+j,arge+j);
        /* define with one empty argument */
        if ((argc==1)&&(arge[0]==argb[0])) argc=0;
        macros[nmacros].argnames=(char **)malloc((argc+1)*sizeof(char *));
        macros[nmacros].argnames[argc]=NULL;
      }
      macros[nmacros].nnamedargs=argc;
      for (j=0;j<argc;j++) {
        if ((argb[j]==arge[j])||(identifierEnd(argb[j])!=arge[j]))
          bug("#defeval with named args needs identifiers as arg names");
        macros[nmacros].argnames[j]=malloc(arge[j]-argb[j]+1);
        memcpy(macros[nmacros].argnames[j],C->buf+argb[j],arge[j]-argb[j]);
        macros[nmacros].argnames[j][arge[j]-argb[j]]=0;
      }
      lookupArgRefs(nmacros++);
    } else
      replace_directive_with_blank_line(C->out->f);
    break;
     
  case 10: /* IFEQ */
    iflevel++;
    replace_directive_with_blank_line(C->out->f);
    if (iflevel==STACKDEPTH) bug("Too many nested #ifeqs");
    commented[iflevel]=commented[iflevel-1];
    if (!commented[iflevel]) {
      char *s,*t;
      if (nparam!=2) bug("#ifeq requires two arguments");
      s=ProcessText(C->buf+p1start,p1end-p1start,FLAG_META);
      t=ProcessText(C->buf+p2start,p2end-p2start,FLAG_META);
      commented[iflevel]=(nowhite_strcmp(s,t)!=0);
      free(s); free(t);
    }
    break;

  case 11: /* IFNEQ */
    iflevel++;
    replace_directive_with_blank_line(C->out->f);
    if (iflevel==STACKDEPTH) bug("Too many nested #ifeqs");
    commented[iflevel]=commented[iflevel-1];
    if (!commented[iflevel]) {
      char *s,*t;
      if (nparam!=2) bug("#ifneq requires two arguments");
      s=ProcessText(C->buf+p1start,p1end-p1start,FLAG_META);
      t=ProcessText(C->buf+p2start,p2end-p2start,FLAG_META);
      commented[iflevel]=(nowhite_strcmp(s,t)==0);
      free(s); free(t);
    }
    break;

  case 12: /* EVAL */
    if (!commented[iflevel]) {
      char *s,*t;
      if (nparam==2) p1end=p2end; /* we really want it all ! */
      s=ArithmEval(p1start,p1end);
      for (t=s;*t;t++) outchar(*t);
      free(s);
    }
    break;

  case 13: /* IF */
    iflevel++;
    replace_directive_with_blank_line(C->out->f);
    if (iflevel==STACKDEPTH) bug("Too many nested #ifs");
    commented[iflevel]=commented[iflevel-1];
    if (!commented[iflevel]) {
      char *s;
      if (nparam==2) p1end=p2end; /* we really want it all ! */
      s=ArithmEval(p1start,p1end);
      commented[iflevel]=((s[0]=='0')&&(s[1]==0));
      free(s);
    }
    break;

  case 14: /* MODE */
    replace_directive_with_blank_line(C->out->f);
    if (nparam==1) p2start=-1;
    if (!commented[iflevel])
      ProcessModeCommand(p1start,p1end,p2start,p2end);
    PopSpecs();
    break;
     
  default: bug("Internal meta-macro identification error");
  }
  shiftIn(macend);
  return 0;
}

int ParsePossibleUser()
{
  int idstart,idend,sh_end,lg_end,macend;
  int argc,id,i,l;
  char *argv[MAXARGS];
  int argb[MAXARGS],arge[MAXARGS];
  struct INPUTCONTEXT *T;

  idstart=1;
  id=0;
  if (!SplicePossibleUser(&idstart,&idend,&sh_end,&lg_end,
                          argb,arge,&argc,1,&id,FLAG_USER))
    return -1;
  if ((sh_end>=0)&&(C->namedargs!=NULL)) {
    i=findNamedArg(C->buf+idstart,idend-idstart);
    if (i>=0) {
      if (i<C->argc) sendout(C->argv[i],strlen(C->argv[i]),0);
      shiftIn(sh_end);
      return 0;
    }
  }

  if (id<0) return -1;
  if (lg_end>=0) macend=lg_end; else { macend=sh_end; argc=0; }

  if (macros[id].nnamedargs==-2) { /* defined(...) macro for arithmetic */
    char *s,*t;
    if (argc!=1) return -1;
    s=remove_comments(argb[0],arge[0],FLAG_USER);
    t=s+strlen(s)-1;
    if (*s!=0) while ((t!=s)&&iswhite(*t)) *(t--)=0;
    t=s; while (iswhite(*t)) t++;
    if (findIdent(t,strlen(t))>=0) outchar('1');
    else outchar('0');
    free(s);
    shiftIn(macend);
    return 0;
  }
  if (!macros[id].macrotext[0]) { /* the empty macro */
    shiftIn(macend);
    return 0;
  }
  
  for (i=0;i<argc;i++)
    argv[i]=ProcessText(C->buf+argb[i],arge[i]-argb[i],FLAG_USER);
  /* process macro text */
  T=C;
  C=(struct INPUTCONTEXT *)malloc(sizeof(struct INPUTCONTEXT));
  C->out=T->out;
  C->in=NULL;
  C->argc=argc;
  C->argv=argv;
  C->filename=T->filename;
  C->lineno=T->lineno;
  if ((macros[id].nnamedargs==-1)&&(lg_end>=0)&&
      (macros[id].define_specs->User.mEnd[0]==0)) {
    /* build an aliased macro call */
    l=strlen(macros[id].macrotext)+2
      +strlen(macros[id].define_specs->User.mArgS)
      +strlen(macros[id].define_specs->User.mArgE)
      +(argc-1)*strlen(macros[id].define_specs->User.mArgSep);
    for (i=0;i<argc;i++) l+=strlen(argv[i]);
    C->buf=C->malloced_buf=malloc(l);
    l=strlen(macros[id].macrotext)+1;
    C->buf[0]='\n';
    strcpy(C->buf+1,macros[id].macrotext);
    while ((l>1)&&iswhite(C->buf[l-1])) l--;
    strcpy(C->buf+l,macros[id].define_specs->User.mArgS);
    for (i=0;i<argc;i++) {
      if (i>0) strcat(C->buf,macros[id].define_specs->User.mArgSep);
      strcat(C->buf,argv[i]);
    }
    strcat(C->buf,macros[id].define_specs->User.mArgE);
  } 
  else {
    C->buf=C->malloced_buf=malloc(strlen(macros[id].macrotext)+2);
    C->buf[0]='\n';
    strcpy(C->buf+1,macros[id].macrotext);
  }
  C->len=strlen(C->buf);
  C->bufsize=C->len+1;
  C->eof=0;
  C->namedargs=macros[id].argnames;
  C->in_comment=macros[id].defined_in_comment;
  C->ambience=FLAG_META; 
  PushSpecs(macros[id].define_specs);
  ProcessContext();
  PopSpecs();
  free(C);
  C=T;
  
  for (i=0;i<argc;i++) free(argv[i]);
  shiftIn(macend);
  return 0;
}

void ParseText()
{
  int l,cs,ce;
  char c,*s;
  struct COMMENT *p;

  /* look for comments first */
  if (!C->in_comment) {
    cs=1;
    for (p=S->comments;p!=NULL;p=p->next)
      if (!(p->flags[C->ambience]&FLAG_IGNORE))
        if (matchStartSequence(p->start,&cs)) {
          l=ce=findCommentEnd(p->end,p->quote,p->warn,cs,p->flags[C->ambience]);
          matchEndSequence(p->end,&l);
          if (p->flags[C->ambience]&OUTPUT_DELIM)
            sendout(C->buf+1,cs-1,0);
          if (p->flags[C->ambience]&PARSE_MACROS) {
            C->in_comment=1;
            s=ProcessText(C->buf+cs,ce-cs,C->ambience);
            if (p->flags[C->ambience]&OUTPUT_TEXT) sendout(s,strlen(s),0);
            C->in_comment=0;
            free(s);
          } 
          else if (p->flags[C->ambience]&OUTPUT_TEXT)
            sendout(C->buf+cs,ce-cs,0);
          if (p->flags[C->ambience]&OUTPUT_DELIM)
            sendout(C->buf+ce,l-ce,0);
          shiftIn(l);
          return;
        }
  }

  if (ParsePossibleMeta()>=0) return;
  if (ParsePossibleUser()>=0) return;
  
  l=1;
  /* MK: changed this If-statement. Check with Denis
     if (matchSequence(S->User.mArgRef,&l)) {
  */
  /* If matching numbered macro argument and inside a macro */
  if (matchSequence(S->User.mArgRef,&l) && C->ambience==FLAG_META) {
    /* Process macro argument referenced as #1,#2,... */
    c=getChar(l);
    if ((c>='1')&&(c<='9')) {
      c=c-'1';
      if (c<C->argc)
        sendout(C->argv[(int)c],strlen(C->argv[(int)c]),0);
      shiftIn(l+1);
      return;
    }
  }
  
  l=identifierEnd(1);
  if (l==1) l=2;
  sendout(C->buf+1,l-1,1);
  shiftIn(l);
}

void ProcessContext()
{
  if (C->len==0) { C->buf[0]='\n'; C->len++; }
  while (!C->eof) ParseText();
  if (C->in!=NULL) fclose(C->in);
  free(C->malloced_buf);
}



/* copy SLASH-terminated name of the directory of fname */
static void getDirname(char *fname, char *dirname)
{
  int i;

  for (i = strlen(fname)-1; i>=0; i--) {
    if (fname[i] == SLASH)
      break;
  }
  if (i >= 0) {
    strncpy(dirname,fname,i);
    dirname[i] = SLASH;
  } else
    /* just a precaution: i must be -1 in this case anyway */
    i = -1;

  dirname[i+1] = '\0';
}

static FILE *openInCurrentDir(char *incfile)
{
  char *absfile =
    (char *)calloc(strlen(C->filename)+strlen(incfile)+1, sizeof(char));
  FILE *f;
  getDirname(C->filename,absfile);
  strcat(absfile,incfile);
  f=fopen(absfile,"r");
  free(absfile);
  return f;
}


void replace_definition_with_blank_lines(char *start, char *end)
{
  if (include_directive_marker != NULL) {
    while (start <= end) {
      if (*start == '\n')
	fprintf(C->out->f,"\n");
      start++;
    }
  }
}

/* insert blank line where the metas IFDEF,ELSE,INCLUDE, etc., stood in the
   input text
*/
void replace_directive_with_blank_line(FILE *f)
{
  if (include_directive_marker != NULL) {
    fprintf(f,"\n");
  }
}


/* If lineno is > 15 digits - the number won't be printed correctly */
void write_include_marker(FILE *f, int lineno, char *filename, char *marker)
{
  static char lineno_buf[MAX_GPP_NUM_SIZE];
  static char *escapedfilename = NULL;

  if (include_directive_marker != NULL) {
#ifdef WIN_NT
    escape_backslashes(filename,&escapedfilename);
#else
    escapedfilename = filename;
#endif
    sprintf(lineno_buf,"%d", lineno);
    fprintf(f, include_directive_marker, lineno_buf, escapedfilename, marker);
  }
}


/* Under windows, files can have backslashes in them. 
   These should be escaped.
*/
void escape_backslashes(char *instr, char **outstr)
{
  int out_idx=0;

  if (*outstr != NULL) free(*outstr);
  *outstr = malloc(2*strlen(instr));

  while (*instr != '\0') {
    if (*instr=='\\') {
      *(*outstr+out_idx) = '\\';
      out_idx++;
    }
    *(*outstr+out_idx) = *instr;
    out_idx++;
    instr++;
  }
  *(*outstr+out_idx) = '\0';
}


/* includemarker_input should have 3 ?-marks, which are replaced with %s.
   Also, @ is replaced with a space. These symbols can be escaped with a
   backslash.
*/
void construct_include_directive_marker(char **include_directive_marker,
					char *includemarker_input)
{
  int len = strlen(includemarker_input);
  char ch;
  int in_idx=0, out_idx=0;
  int quoted = 0;

  /* only 6 extra chars are needed: 3 for the three %'s, 2 for \n, 1 for \0 */
  *include_directive_marker = malloc(len+18);

  ch = *includemarker_input;
  while (ch != '\0' && in_idx < len) {
    if (quoted) {
      *(*include_directive_marker+out_idx) = ch;
      out_idx++;
      quoted = 0;
    } else {
      switch (ch) {
      case '\\':
	quoted = 1;
	break;
      case '@':
	*(*include_directive_marker+out_idx) = ' ';
	out_idx++;
	break;
      case '?':
	*(*include_directive_marker+out_idx) = '%';
	out_idx++;
	*(*include_directive_marker+out_idx) = 's';
	out_idx++;
	break;
      default:
	*(*include_directive_marker+out_idx) = ch;
	out_idx++;
      }
    }

    in_idx++;
    ch = *(includemarker_input+in_idx);
  }

  *(*include_directive_marker+out_idx) = '\n';
  out_idx++;
  *(*include_directive_marker+out_idx) = '\0';
}


int main(int argc,char **argv)
{
  initthings(argc,argv); 
  /* The include marker at the top of the file */
  write_include_marker(C->out->f, 1, C->filename, "");
  ProcessContext();
  fclose(C->out->f);
  return 0;
}
