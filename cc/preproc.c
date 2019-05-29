//
//  preproc.c - Tiny C Compiler for Sanos
// 
//  Copyright (c) 2001-2004 Fabrice Bellard
//  Copyright (c) 2011-2012 Michael Ringgaard
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//

#include "cc.h"

//#define PARSE_DEBUG
//#define PP_DEBUG

BufferedFile *file;
int ch;
int tok;
CValue tokc;
CString tokcstr;
int tok_flags;
int parse_flags;

int last_line_num, last_ind;
TokenSym **table_ident;
TokenSym *hash_ident[TOK_HASH_SIZE];
char token_buf[STRING_MAX_SIZE + 1];
CType func_old_type;
Sym *global_stack, *local_stack;
Sym *define_stack;
Sym *global_label_stack, *local_label_stack;

int total_lines;
int total_bytes;

int *macro_ptr, *macro_ptr_allocated;
int *unget_saved_macro_ptr;
int unget_saved_buffer[TOK_MAX_SIZE + 1];
int unget_buffer_enabled;

// NB: The content of this string encodes token numbers
static char tok_two_chars[] = 
  "<=\236>=\235!=\225&&\240||\241++\244--\242==\224<<\1>>\2+=\253-=\255*=\252/=\257%=\245&=\246^=\336|=\374->\313..\250##\266";

void skip(int c) {
  if (tok != c) error("'%c' expected", c);
  next();
}

// Allocate a new token
TokenSym *tok_alloc_new(TokenSym **pts, const char *str, int len) {
  TokenSym *ts, **ptable;
  int i;

  if (tok_ident >= SYM_FIRST_ANOM) error("memory full");

  // Expand token table if needed
  i = tok_ident - TOK_IDENT;
  if ((i % TOK_ALLOC_INCR) == 0) {
    ptable = tcc_realloc(table_ident, (i + TOK_ALLOC_INCR) * sizeof(TokenSym *));
    if (!ptable) error("memory full");
    table_ident = ptable;
  }

  ts = tcc_malloc(sizeof(TokenSym) + len);		//dcm: allocate extra space for the variable length string at the end.
  table_ident[i] = ts;
  ts->tok = tok_ident++;
  ts->sym_define = NULL;
  ts->sym_label = NULL;
  ts->sym_struct = NULL;
  ts->sym_identifier = NULL;
  ts->len = len;
  ts->hash_next = NULL;
  memcpy(ts->str, str, len);
  ts->str[len] = '\0';
  *pts = ts;
  return ts;
}

#define TOK_HASH_INIT 1
#define TOK_HASH_FUNC(h, c) ((h) * 263 + (c))

// Find a token and add it if not found
TokenSym *tok_alloc(const char *str, int len) {
  TokenSym *ts, **pts;
  int i;
  unsigned int h;

  // Compute token hash value  
  h = TOK_HASH_INIT;
  for (i = 0; i < len; i++) {
    h = TOK_HASH_FUNC(h, ((unsigned char *)str)[i]);
  }
  h &= (TOK_HASH_SIZE - 1);

  // Find token in hash table
  pts = &hash_ident[h];
  for (;;) {
    ts = *pts;
    if (!ts) break;
    if (ts->len == len && !memcmp(ts->str, str, len)) return ts;
    pts = &(ts->hash_next);
  }

  // Not found, allocate new token
  return tok_alloc_new(pts, str, len);
}

// TODO: buffer overflow
// TODO: float tokens
char *get_tok_str(int v, CValue *cv) {
  static char buf[STRING_MAX_SIZE + 1];
  static CString cstr_buf;
  CString *cstr;
  unsigned char *q;
  char *p;
  int i, len;

  // NOTE: to go faster, we give a fixed buffer for small strings
  cstr_reset(&cstr_buf);
  cstr_buf.data = buf;
  cstr_buf.size_allocated = sizeof(buf);
  p = buf;

  switch (v) {
    case TOK_CINT:
    case TOK_CUINT:
      // TODO: not quite exact, but only useful for testing
      sprintf(p, "%u", cv->ui);
      break;
    case TOK_CLLONG:
    case TOK_CULLONG:
      // TODO: not quite exact, but only useful for testing
      sprintf(p, "%Lu", cv->ull);
      break;
    case TOK_CCHAR:
    case TOK_LCHAR:
      cstr_ccat(&cstr_buf, '\'');
      add_char(&cstr_buf, cv->i);
      cstr_ccat(&cstr_buf, '\'');
      cstr_ccat(&cstr_buf, '\0');
      break;
    case TOK_PPNUM:
      cstr = cv->cstr;
      len = cstr->size - 1;
      for (i = 0; i < len; i++) {
        add_char(&cstr_buf, ((unsigned char *)cstr->data)[i]);
      }
      cstr_ccat(&cstr_buf, '\0');
      break;
    case TOK_STR:
    case TOK_LSTR:
      cstr = cv->cstr;
      cstr_ccat(&cstr_buf, '\"');
      if (v == TOK_STR) {
        len = cstr->size - 1;
        for (i = 0; i < len; i++) {
          add_char(&cstr_buf, ((unsigned char *) cstr->data)[i]);
        }
      } else {
        len = (cstr->size / sizeof(nwchar_t)) - 1;
        for (i = 0;i < len; i++) {
          add_char(&cstr_buf, ((nwchar_t *) cstr->data)[i]);
        }
      }
      cstr_ccat(&cstr_buf, '\"');
      cstr_ccat(&cstr_buf, '\0');
      break;
    case TOK_LT:
      v = '<';
      goto addv;
    case TOK_GT:
      v = '>';
      goto addv;
    case TOK_ULT:
      return strcpy(p, "<(u)");
    case TOK_ULE:
      return strcpy(p, "<=(u)");
    case TOK_UGT:
      return strcpy(p, ">(u)");
    case TOK_UGE:
      return strcpy(p, ">=(u)");
    case TOK_DOTS:
      return strcpy(p, "...");
    case TOK_A_SHL:
      return strcpy(p, "<<=");
    case TOK_A_SAR:
      return strcpy(p, ">>=");
    default:
      if (v < TOK_IDENT) {
        // Search in two bytes table
        q = tok_two_chars;
        while (*q) {
          if (q[2] == v) {
            *p++ = q[0];
            *p++ = q[1];
            *p = '\0';
            return buf;
          }
          q += 3;
        }
      addv:
        *p++ = v;
        *p = '\0';
      } else if (v < tok_ident) {
        return table_ident[v - TOK_IDENT]->str;
      } else if (v >= SYM_FIRST_ANOM) {
        // Special name for anonymous symbol
        sprintf(p, "L.%u", v - SYM_FIRST_ANOM);
      } else {
        // Should never happen
        return NULL;
      }
      break;
  }

  return cstr_buf.data;
}

BufferedFile *tcc_open(TCCState *s1, const char *filename) {
  int fd;
  BufferedFile *bf;

  if (strcmp(filename, "-") == 0) {
    fd = 0;
    filename = "stdin";
  } else {
    fd = open(filename, O_RDONLY | O_BINARY);
  }
  if ((verbose == 2 && fd >= 0) || verbose == 3) {
    printf("%s %*s%s\n", fd < 0 ? "nf" : "->",
           (s1->include_stack_ptr - s1->include_stack), "", filename);
  }
  if (fd < 0) return NULL;
  bf = tcc_malloc(sizeof(BufferedFile));
  bf->fd = fd;
  bf->buf_ptr = bf->buffer;
  bf->buf_end = bf->buffer;
  bf->buffer[0] = CH_EOB; // put eob symbol		//dcm: this is defined as a \ - no wonder there's weird code around dealing with "\"
  pstrcpy(bf->filename, sizeof(bf->filename), filename);
  bf->line_num = 1;
  bf->ifndef_macro = 0;
  bf->ifdef_stack_ptr = s1->ifdef_stack_ptr;
  return bf;
}

void tcc_close(BufferedFile *bf) {
  total_lines += bf->line_num;
  close(bf->fd);
  tcc_free(bf);
}

// Fill input buffer and peek next char
//dcm: only ever called by handle_eob()
int tcc_peekc_slow(BufferedFile *bf) {
  int len;
  
  // Only tries to read if really end of buffer
  if (bf->buf_ptr >= bf->buf_end) {
    if (bf->fd != -1) {
#ifdef PARSE_DEBUG
      len = 8;
#else
      len = IO_BUF_SIZE;
#endif
      len = read(bf->fd, bf->buffer, len);	//dcm: this is the only place that the BufferedFile->buffer is replenished
      if (len < 0) len = 0;
    } else {
      len = 0;
    }
    total_bytes += len;
    bf->buf_ptr = bf->buffer;
    bf->buf_end = bf->buffer + len;
    *bf->buf_end = CH_EOB;					//dcm: the buffer is terminated with CH_EOB 
  }

  if (bf->buf_ptr < bf->buf_end) {			//dcm: given that a CH_EOB is already inserted, I don't understand why this code is necessary.
    return bf->buf_ptr[0];
  } else {
    bf->buf_ptr = bf->buf_end;
    return CH_EOF;							//dcm: No wait, this is a CH_EOF, not CH_EOB! Now I'm really confused.
  }
}

// Return the current character, handling end of block if necessary
// (but not stray)
int handle_eob(void) {
  return tcc_peekc_slow(file);
}

// Read next char from current input file and handle end of input buffer //dcm: by reading the file to replenish the buffer
void finp(void) {
  ch = *(++(file->buf_ptr));
  // End of buffer/file handling
  if (ch == CH_EOB) ch = handle_eob();
}

// Handle '\[\r]\n'
int handle_stray_noerror(void) {
  while (ch == '\\') {
    finp();
    if (ch == '\n') {
      file->line_num++;
      finp();
    } else if (ch == '\r') {
      finp();
      if (ch != '\n') goto fail;
      file->line_num++;
      finp();
    } else {
    fail:
      return 1;
    }
  }
  return 0;
}

void handle_stray(void) {
  if (handle_stray_noerror()) error("stray '\\' in program");
}

// Skip the stray and handle the \\n case. Output an error if
// incorrect char after the stray
int handle_stray1(uint8_t *p) {
  int c;

  if (p >= file->buf_end) {
    file->buf_ptr = p;
    c = handle_eob();	//dcm: read from the file as necessary
    p = file->buf_ptr;
    if (c == '\\') goto parse_stray;
  } else {
  parse_stray:
    file->buf_ptr = p;
    ch = *p;
    handle_stray();
    p = file->buf_ptr;
    c = *p;
  }
  return c;
}

// Handle just the EOB case, but not stray
#define PEEKC_EOB(c, p) \
{ \
  p++; \
  c = *p; \
  if (c == '\\') { \
    file->buf_ptr = p; \
    c = handle_eob(); \
    p = file->buf_ptr; \
  } \
}

// Handle the complicated stray case.
//dcm: strange that it doesn't check for end-of-buffer. But handle)stray1() does, so I think it's okay.
//dcm: Update: it does check - EOB = '\'!
#define PEEKC(c, p) \
{ \
  p++; \
  c = *p; \
  if (c == '\\') { \
    c = handle_stray1(p); \
    p = file->buf_ptr; \
  } \
}

// Input with '\[\r]\n' handling. Note that this function cannot
// handle other characters after '\', so you cannot call it inside
// strings or comments
void minp(void) {
  finp();
  if (ch == '\\') handle_stray();
}

// Single line C++ comments
uint8_t *parse_line_comment(uint8_t *p) {
  int c;

  p++;
  for (;;) {
    c = *p;
  redo:
    if (c == '\n' || c == CH_EOF) {
      break;
    } else if (c == '\\') {
      file->buf_ptr = p;
      c = handle_eob();		//dcm: read from the file as necessary
      p = file->buf_ptr;
      if (c == '\\') {
        PEEKC_EOB(c, p);
        if (c == '\n') {
          file->line_num++;
          PEEKC_EOB(c, p);
        } else if (c == '\r') {
          PEEKC_EOB(c, p);
          if (c == '\n') {
            file->line_num++;
            PEEKC_EOB(c, p);
          }
        }
      } else {
        goto redo;
      }
    } else {
      p++;
    }
  }
  return p;
}

// C comments
uint8_t *parse_comment(uint8_t *p) {
  int c;
  
  p++;
  for (;;) {
    // Fast skip loop
    for (;;) {
      c = *p;
      if (c == '\n' || c == '*' || c == '\\') break;
      p++;
      c = *p;
      if (c == '\n' || c == '*' || c == '\\') break;
      p++;
    }
    // Now we can handle all the cases
    if (c == '\n') {
      file->line_num++;
      p++;
    } else if (c == '*') {
      p++;
      for (;;) {
        c = *p;
        if (c == '*') {
          p++;
        } else if (c == '/') {
          goto end_of_comment;
        } else if (c == '\\') {
          file->buf_ptr = p;
		  c = handle_eob();	//dcm: read from the file as necessary
          p = file->buf_ptr;
          if (c == '\\') {
            // Skip '\[\r]\n', otherwise just skip the stray
            while (c == '\\') {
              PEEKC_EOB(c, p);
              if (c == '\n') {
                file->line_num++;
                PEEKC_EOB(c, p);
              } else if (c == '\r') {
                PEEKC_EOB(c, p);
                if (c == '\n') {
                  file->line_num++;
                  PEEKC_EOB(c, p);
                }
              } else {
                goto after_star;
              }
            }
          }
        } else {
          break;
        }
      }
    after_star: ;
    } else {
      // stray, eob or eof
      file->buf_ptr = p;
	  c = handle_eob();		//dcm: read from the file as necessary
      p = file->buf_ptr;
      if (c == CH_EOF) {
        error("unexpected end of file in comment");
      } else if (c == '\\') {
        p++;
      }
    }
  }
 end_of_comment:
  p++;
  return p;
}

void skip_spaces(void) {
  while (is_space(ch)) minp();
}

// Parse a string without interpreting escapes
uint8_t *parse_pp_string(uint8_t *p, int sep, CString *str) {
  int c;
  p++;
  for (;;) {
    c = *p;
    if (c == sep) {
      break;
    } else if (c == '\\') {
      file->buf_ptr = p;
      c = handle_eob();		//dcm: read from the file as necessary
      p = file->buf_ptr;
      if (c == CH_EOF) {
      unterminated_string:
        // TODO: indicate line number of start of string
        error("missing terminating %c character", sep);
      } else if (c == '\\') {
        // Escape: just skip \[\r]\n
        PEEKC_EOB(c, p);
        if (c == '\n') {
          file->line_num++;
          p++;
        } else if (c == '\r') {
          PEEKC_EOB(c, p);
          if (c != '\n') expect("'\n' after '\r'");
          file->line_num++;
          p++;
        } else if (c == CH_EOF) {
          goto unterminated_string;
        } else {
          if (str) {
            cstr_ccat(str, '\\');
            cstr_ccat(str, c);
          }
          p++;
        }
      }
    } else if (c == '\n') {
      file->line_num++;
      goto add_char;
    } else if (c == '\r') {
      PEEKC_EOB(c, p);
      if (c != '\n') {
        if (str) cstr_ccat(str, '\r');
      } else {
        file->line_num++;
        goto add_char;
      }
    } else {
    add_char:
      if (str) cstr_ccat(str, c);
      p++;
    }
  }
  p++;
  return p;
}

// Skip block of text until #else, #elif or #endif. skip also pairs of #if/#endif
void preprocess_skip(void) {
  int a, start_of_line, c, in_warn_or_error;
  uint8_t *p;

  p = file->buf_ptr;
  a = 0;
redo_start:
  start_of_line = 1;
  in_warn_or_error = 0;
  for (;;) {
  redo_no_start:
    c = *p;
    switch (c) {
      case ' ': case '\t': case '\f': case '\v': case '\r':
        p++;
        goto redo_no_start;
      case '\n':
        file->line_num++;
        p++;
        goto redo_start;
      case '\\':
        file->buf_ptr = p;
        c = handle_eob();		//dcm: read from the file as necessary
        if (c == CH_EOF) {
          expect("#endif");
        } else if (c == '\\') {
          ch = file->buf_ptr[0];
          handle_stray_noerror();
        }
        p = file->buf_ptr;
        goto redo_no_start;
      // Skip strings
      case '\"': case '\'':
        if (in_warn_or_error) goto _default;
        p = parse_pp_string(p, c, NULL);
        break;
      // Skip comments
      case '/':
        if (in_warn_or_error) goto _default;
        file->buf_ptr = p;
        ch = *p;
        minp();
        p = file->buf_ptr;
        if (ch == '*') {
          p = parse_comment(p);
        } else if (ch == '/') {
          p = parse_line_comment(p);
        }
        break;
      case '#':
        p++;
        if (start_of_line) {
          file->buf_ptr = p;
          next_nomacro();
          p = file->buf_ptr;
          if (a == 0 && (tok == TOK_ELSE || tok == TOK_ELIF || tok == TOK_ENDIF)) goto done;
          if (tok == TOK_IF || tok == TOK_IFDEF || tok == TOK_IFNDEF) {
            a++;
          } else if (tok == TOK_ENDIF) {
            a--;
          } else if( tok == TOK_ERROR || tok == TOK_WARNING) {
            in_warn_or_error = 1;
          }
        }
        break;
      _default:
      default:
        p++;
        break;
    }
    start_of_line = 0;
  }

 done:
  file->buf_ptr = p;
}

// ParseState handling

// TODO: currently, no include file info is stored. Thus, we cannot display
// accurate messages if the function or data definition spans multiple files.

// Save current parse state in 's'
void save_parse_state(ParseState *s) {
  s->line_num = file->line_num;
  s->macro_ptr = macro_ptr;
  s->tok = tok;
  s->tokc = tokc;
}

// Restore parse state from 's'
void restore_parse_state(ParseState *s) {
  file->line_num = s->line_num;
  macro_ptr = s->macro_ptr;
  tok = s->tok;
  tokc = s->tokc;
}

// Return the number of additional 'ints' necessary to store the token
int tok_ext_size(int t) {
  switch (t) {
    // 4 bytes
    case TOK_CINT:
    case TOK_CUINT:
    case TOK_CCHAR:
    case TOK_LCHAR:
    case TOK_CFLOAT:
    case TOK_LINENUM:
      return 1;
    case TOK_STR:
    case TOK_LSTR:
    case TOK_PPNUM:
      error("unsupported token");
      return 1;
    case TOK_CDOUBLE:
    case TOK_CLLONG:
    case TOK_CULLONG:
      return 2;
    case TOK_CLDOUBLE:
      return LDOUBLE_SIZE / 4;
    default:
      return 0;
  }
}


//dcm: TokenString handling - creating, growing the int *str structure in powers of 2, adding tokens
//dcm: markers (TOK_LINENUM) are added to the TokenString to record the line number when it changes
//dcm: TokenStrings are used in 8 different places for a variety of purposes - macros, inline functions, etc.
//dcm: There's no gen. purpose routine for reading a token - it seems to be handled "by hand" by reading
//dcm: the .str element.

// Token string handling
void tok_str_new(TokenString *s) {
  s->str = NULL;
  s->len = 0;
  s->allocated_len = 0;
  s->last_line_num = -1;
}

void tok_str_free(int *str) {
  tcc_free(str);
}

int *tok_str_realloc(TokenString *s) {	//dcm: internal fn - only ever called by TokenString functions.
  int *str, len;

  if (s->allocated_len == 0) {
    len = 8;
  } else {
    len = s->allocated_len * 2;
  }
  str = tcc_realloc(s->str, len * sizeof(int));
  if (!str) error("memory full");
  s->allocated_len = len;
  s->str = str;
  return str;
}

void tok_str_add(TokenString *s, int t) {	//dcm: called from all over the place, including compiler.c
  int len, *str;

  len = s->len;
  str = s->str;
  if (len >= s->allocated_len) str = tok_str_realloc(s);
  str[len++] = t;
  s->len = len;
}

//dcm: equivalent routine for reading back is get_tok_str() which strangely, is right at the top of the top of this file
void tok_str_add_ex(TokenString *s, int t, CValue *cv) {	//dcm:  called from elsewhere in preproc.c only
  int len, *str;

  len = s->len;
  str = s->str;

  // Allocate space for worst case
  if (len + TOK_MAX_SIZE > s->allocated_len) str = tok_str_realloc(s);
  str[len++] = t;
  switch (t) {
    case TOK_CINT:
    case TOK_CUINT:
    case TOK_CCHAR:
    case TOK_LCHAR:
    case TOK_CFLOAT:
    case TOK_LINENUM:
      str[len++] = cv->tab[0];
      break;
    case TOK_PPNUM:
    case TOK_STR:
    case TOK_LSTR: {
      int nb_words;
      CString *cstr;

      nb_words = (sizeof(CString) + cv->cstr->size + 3) >> 2;
      while ((len + nb_words) > s->allocated_len) str = tok_str_realloc(s);
      cstr = (CString *)(str + len);
      cstr->data = NULL;
      cstr->size = cv->cstr->size;
      cstr->data_allocated = NULL;
      cstr->size_allocated = cstr->size;
      memcpy((char *)cstr + sizeof(CString), cv->cstr->data, cstr->size);
      len += nb_words;
      break;
    }
    case TOK_CDOUBLE:
    case TOK_CLLONG:
    case TOK_CULLONG:
#if LDOUBLE_SIZE == 8
    case TOK_CLDOUBLE:
#endif
      str[len++] = cv->tab[0];
      str[len++] = cv->tab[1];
      break;
#if LDOUBLE_SIZE == 12
    case TOK_CLDOUBLE:
      str[len++] = cv->tab[0];
      str[len++] = cv->tab[1];
      str[len++] = cv->tab[2];
#elif LDOUBLE_SIZE != 8
#error add long double size support
#endif
      break;
  }
  s->len = len;
}

// Add the current parse token in token string 's'
void tok_str_add_tok(TokenString *s) {		//dcm: called from a number of places in compiler.c, and once in preproc.c
  CValue cval;

  // Save line number info
  if (file->line_num != s->last_line_num) {
    s->last_line_num = file->line_num;
    cval.i = s->last_line_num;
    tok_str_add_ex(s, TOK_LINENUM, &cval);
  }
  tok_str_add_ex(s, tok, &tokc);
}

#if LDOUBLE_SIZE == 12
#define LDOUBLE_GET(p, cv)                  \
    cv.tab[0] = p[0];                       \
    cv.tab[1] = p[1];                       \
    cv.tab[2] = p[2];
#elif LDOUBLE_SIZE == 8
#define LDOUBLE_GET(p, cv)                  \
    cv.tab[0] = p[0];                       \
    cv.tab[1] = p[1];
#else
#error add long double size support
#endif


// Get a token from an integer array and increment pointer
// accordingly. We code it as a macro to avoid pointer aliasing.
#define TOK_GET(t, p, cv)                              \
{                                                      \
  t = *p++;                                            \
  switch (t) {                                         \
    case TOK_CINT:                                     \
    case TOK_CUINT:                                    \
    case TOK_CCHAR:                                    \
    case TOK_LCHAR:                                    \
    case TOK_CFLOAT:                                   \
    case TOK_LINENUM:                                  \
      cv.tab[0] = *p++;                                \
      break;                                           \
    case TOK_STR:                                      \
    case TOK_LSTR:                                     \
    case TOK_PPNUM:                                    \
      cv.cstr = (CString *)p;                          \
      cv.cstr->data = (char *)p + sizeof(CString);     \
      p += (sizeof(CString) + cv.cstr->size + 3) >> 2; \
      break;                                           \
    case TOK_CDOUBLE:                                  \
    case TOK_CLLONG:                                   \
    case TOK_CULLONG:                                  \
      cv.tab[0] = p[0];                                \
      cv.tab[1] = p[1];                                \
      p += 2;                                          \
      break;                                           \
    case TOK_CLDOUBLE:                                 \
      LDOUBLE_GET(p, cv);                              \
      p += LDOUBLE_SIZE / 4;                           \
      break;                                           \
    default:                                           \
      break;                                           \
  }                                                    \
}

// Defines handling
void define_push(int v, int macro_type, int *str, Sym *first_arg) {
  Sym *s;

  s = sym_push2(&define_stack, v, macro_type, (int)str);
  s->next = first_arg;
  table_ident[v - TOK_IDENT]->sym_define = s;
}

// Undefined a define symbol. Its name is just set to zero
void define_undef(Sym *s) {
  int v;
  v = s->v;
  if (v >= TOK_IDENT && v < tok_ident) {
    table_ident[v - TOK_IDENT]->sym_define = NULL;
  }
  s->v = 0;
}

Sym *define_find(int v) {
  v -= TOK_IDENT;
  if ((unsigned)v >= (unsigned)(tok_ident - TOK_IDENT)) return NULL;
  return table_ident[v]->sym_define;
}

// Free define stack until top reaches 'b'
void free_defines(Sym *b) {
  Sym *top, *top1;
  int v;

  top = define_stack;
  while (top != b) {
    top1 = top->prev;
    // Do not free args or predefined defines
    if (top->c) tok_str_free((int *)top->c);
    v = top->v;
    if (v >= TOK_IDENT && v < tok_ident) {
      table_ident[v - TOK_IDENT]->sym_define = NULL;
    }
    sym_free(top);
    top = top1;
  }
  define_stack = b;
}

// Evaluate an #if/#elif expression
int expr_preprocess(void) {
  int c, t;
  TokenString str;
  
  tok_str_new(&str);
  while (tok != TOK_LINEFEED && tok != TOK_EOF) {
    next(); // Do macro subst
    if (tok == TOK_DEFINED) {
      next_nomacro();
      t = tok;
      if (t == '(') next_nomacro();
      c = define_find(tok) != 0;
      if (t == '(') next_nomacro();
      tok = TOK_CINT;
      tokc.i = c;
    } else if (tok >= TOK_IDENT) {
      // if undefined macro
      tok = TOK_CINT;
      tokc.i = 0;
    }
    tok_str_add_tok(&str);
  }
  tok_str_add(&str, -1); // Simulate end of file
  tok_str_add(&str, 0);
  
  // Now evaluate C constant expression
  macro_ptr = str.str;
  next();
  c = expr_const();
  macro_ptr = NULL;
  tok_str_free(str.str);
  return c != 0;
}

#if defined(PARSE_DEBUG) || defined(PP_DEBUG)
void tok_print(int *str) {
  int t;
  CValue cval;

  while (1) {
    TOK_GET(t, str, cval);
    if (!t) break;
    printf(" %s", get_tok_str(t, &cval));
  }
  printf("\n");
}

//dcm: I added this just to test that I'd understood what was going on with macro expansion
void para_print(Sym *head) {
	printf("Parameters: ( ");
	while (head) {
		printf(" %s", get_tok_str(head->v & ~SYM_FIELD, NULL));
		head = head->next;
	}
	printf(")\n");
}
#endif

// Parse after #define
//dcm: Description of #define: https://gcc.gnu.org/onlinedocs/cpp/Macros.html#Macros
void parse_define(void) {
  Sym *s, *first, **ps;
  int v, t, varg, is_vaargs, c;
  TokenString str;
  
  v = tok;		//dcm: this is the token immediately following the #define - ie the define's name
  if (v < TOK_IDENT) {
    error("invalid macro name '%s'", get_tok_str(tok, &tokc));
  }
  // TODO: should check if same macro (ANSI)
  first = NULL;				//dcm: first is the head of a linked list of the formal parameters to the function-like macro
  t = MACRO_OBJ;			//dcm: assume it's an object-like macro
  // '(' must be just after macro definition for MACRO_FUNC
  c = file->buf_ptr[0];		//dcm: get a pointer to the first ch immediateley following the identifier just scanned.
  if (c == '\\') {
    c = handle_stray1(file->buf_ptr);
  }
  if (c == '(') {		//dcm: the open paren must immediately follow the identifier. It's a function-like macro
    next_nomacro();		//dcm: scan over the '('
    next_nomacro();		//dcm: read the first param 
    ps = &first;
    while (tok != ')') {
      varg = tok;
      next_nomacro();
      is_vaargs = 0;

	  //dcm: now check for the 2x va_arg alternatives:
      if (varg == TOK_DOTS) {		//dcm: va_args can be either (a) ... (in which case you use __VAR_ARGS__ in the body) 
        varg = TOK___VA_ARGS__;
        is_vaargs = 1;
      } else if (tok == TOK_DOTS) {	//dcm: or (b) name... (in which case you use name in the body)
        is_vaargs = 1;
        next_nomacro();
      }
      if (varg < TOK_IDENT) {
        error("badly punctuated parameter list");
      }

	  //dcm: create the sym for the param add it to the linked list. varg is the token representing the param.
	  //dcm: verified with this debug: printf("Test; param is %s\n", get_tok_str(varg, NULL));
      s = sym_push2(&define_stack, varg | SYM_FIELD, is_vaargs, 0);

	  //dcm: add the symbol to the end of the linked list that has first as the head. Not sure why it's added to 2x stacks/queues
      *ps = s;				
      ps = &s->next;
      if (tok != ',') break;
      next_nomacro();
    }
    t = MACRO_FUNC;
  }
  tok_str_new(&str);	//dcm: create a new TokenString into which we put the "body" of the macro
  next_nomacro();		//dcm: skip over either the ')' for the func-like macro case, or the macro name for the object-like macro case

  // EOF testing necessary for '-D' handling
  //dcm: read in the tokens defining the macro "body" and add them to the TokenString
  while (tok != TOK_LINEFEED && tok != TOK_EOF) {
    tok_str_add_ex(&str, tok, &tokc);
    next_nomacro();
  }
  tok_str_add(&str, 0);		//dcm: add a terminator to the TokenString

#ifdef PP_DEBUG
  printf("define %s %d: ", get_tok_str(v, NULL), t);
  tok_print(str.str);			//dcm: doesn't print the params - useful learning exercise?
  para_print(first);			//dcm: my attempt to print the params. 
#endif

  define_push(v, t, str.str, first);	//dcm: finally push the macro name (v), its type (t), the tokens (str.str) and the optional chain of paras (first)
}

int hash_cached_include(int type, const char *filename) {
  const unsigned char *s;
  unsigned int h;

  h = TOK_HASH_INIT;
  h = TOK_HASH_FUNC(h, type);
  s = filename;
  while (*s) {
    h = TOK_HASH_FUNC(h, *s);
    s++;
  }
  h &= (CACHED_INCLUDES_HASH_SIZE - 1);
  return h;
}

// TODO: use a token or a hash table to accelerate matching?
CachedInclude *search_cached_include(TCCState *s1, int type, const char *filename) {
  CachedInclude *e;
  int i, h;
  h = hash_cached_include(type, filename);
  i = s1->cached_includes_hash[h];
  for (;;) {
    if (i == 0) break;
    e = s1->cached_includes[i - 1];
    if (e->type == type && !strcmp(e->filename, filename)) return e;
    i = e->hash_next;
  }
  return NULL;
}

void add_cached_include(TCCState *s1, int type, const char *filename, int ifndef_macro) {
  CachedInclude *e;
  int h;

  if (search_cached_include(s1, type, filename))return;
#ifdef INC_DEBUG
  printf("adding cached '%s' %s\n", filename, get_tok_str(ifndef_macro, NULL));
#endif
  e = tcc_malloc(sizeof(CachedInclude) + strlen(filename));
  if (!e) return;
  e->type = type;
  strcpy(e->filename, filename);
  e->ifndef_macro = ifndef_macro;
  dynarray_add((void ***)&s1->cached_includes, &s1->nb_cached_includes, e);

  // Add in hash table
  h = hash_cached_include(type, filename);
  e->hash_next = s1->cached_includes_hash[h];
  s1->cached_includes_hash[h] = s1->nb_cached_includes;
}

void pragma_parse(TCCState *s1) {
  int val;

  next();
  if (tok == TOK_pack) {
    next();
    skip('(');
    if (tok == TOK_ASM_pop) {
      next();
      if (s1->pack_stack_ptr <= s1->pack_stack) {
      stk_error:
        error("out of pack stack");
      }
      s1->pack_stack_ptr--;
    } else {
      val = 0;
      if (tok != ')') {
        if (tok == TOK_ASM_push) {
          next();
          if (s1->pack_stack_ptr >= s1->pack_stack + PACK_STACK_SIZE - 1) goto stk_error;
          s1->pack_stack_ptr++;
          skip(',');
        }
        if (tok != TOK_CINT) {
        pack_error:
          error("invalid pack pragma");
        }
        val = tokc.i;
        if (val < 1 || val > 16 || (val & (val - 1)) != 0) goto pack_error;
        next();
      }
      *s1->pack_stack_ptr = val;
      skip(')');
    }
  }
}

// is_bof is true if first non space token at beginning of file
//dcm: care required - this is in a recursive call chain
//dcm: called from just one location: next_nomacro1() - when '#' is encountered.
void preprocess(int is_bof) {
  TCCState *s1 = tcc_state;
  int size, i, c, n, saved_parse_flags;
  char buf[1024], *q;
  char buf1[1024];
  BufferedFile *f;
  Sym *s;
  CachedInclude *e;
  
  saved_parse_flags = parse_flags;
  parse_flags = PARSE_FLAG_PREPROCESS | PARSE_FLAG_TOK_NUM | PARSE_FLAG_LINEFEED;
  next_nomacro();
 redo:
  switch (tok) {
    case TOK_DEFINE:
      next_nomacro();
      parse_define();
      break;
    case TOK_UNDEF:
      next_nomacro();
      s = define_find(tok);
      // Undefine symbol by putting an invalid name
      if (s) define_undef(s);
      break;
    case TOK_INCLUDE:
    case TOK_INCLUDE_NEXT:
      ch = file->buf_ptr[0];
      // TODO: incorrect if comments: use next_nomacro with a special mode
      skip_spaces();
      if (ch == '<') {
        c = '>';
        goto read_name;
      } else if (ch == '\"') {
        c = ch;
      read_name:
        finp();
        q = buf;
        while (ch != c && ch != '\n' && ch != CH_EOF) {
          if ((q - buf) < sizeof(buf) - 1)
            *q++ = ch;
          if (ch == '\\') {
            if (handle_stray_noerror() == 0) --q;
          } else {
            finp();
          }
        }
        *q = '\0';
        minp();
      } else {
        // Computed #include: either we have only strings or we have anything enclosed in '<>'
		//dcm:  it doesn't start with " or < so process here. I don't understand what this could be??? Maybe by
		//dcm: calling next() it deals with macro expansion within the #include stmt?
		//dcm: But https://gcc.gnu.org/onlinedocs/cpp/Include-Syntax.html#Include-Syntax explicitly says macros aren't expanded.
		//dcm: But there's implementation-defined expansion: https://gcc.gnu.org/onlinedocs/cpp/Computed-Includes.html#Computed-Includes.
		//dcm: This matches the comment above about computed #include.
		//dcm: None of the sanos code uses this feature.
        next();
        buf[0] = '\0';
        if (tok == TOK_STR) {
          while (tok != TOK_LINEFEED) {
            if (tok != TOK_STR) {
            include_syntax:
              error("'#include' expects \"FILENAME\" or <FILENAME>");
            }
            pstrcat(buf, sizeof(buf), (char *)tokc.cstr->data);
            next();
          }
          c = '\"';
        } else {
          int len;
          while (tok != TOK_LINEFEED) {
            pstrcat(buf, sizeof(buf), get_tok_str(tok, &tokc));
            next();
          }
          len = strlen(buf);
          // Check syntax and remove '<>'
          if (len < 2 || buf[0] != '<' || buf[len - 1] != '>') goto include_syntax;
          memmove(buf, buf + 1, len - 2);
          buf[len - 2] = '\0';
          c = '>';
        }
      }

      e = search_cached_include(s1, c, buf);
      if (e && define_find(e->ifndef_macro)) {
        // No need to parse the include because the 'ifndef macro' is defined
#ifdef INC_DEBUG
        printf("%s: skipping %s\n", file->filename, buf);
#endif
      } else {
        if (s1->include_stack_ptr >= s1->include_stack + INCLUDE_STACK_SIZE) {
          error("#include recursion too deep");
        }

        // Push current file onto stack
        // TODO: fix current line init
        *s1->include_stack_ptr++ = file;
        if (c == '\"') {
          // First search in current dir if "header.h"
          size = tcc_basename(file->filename) - file->filename;
          if (size > sizeof(buf1) - 1) size = sizeof(buf1) - 1;
          memcpy(buf1, file->filename, size);
          buf1[size] = '\0';
          pstrcat(buf1, sizeof(buf1), buf);
          f = tcc_open(s1, buf1);
          if (f) {
            if (tok == TOK_INCLUDE_NEXT) {
              tok = TOK_INCLUDE;
            } else {
              goto found;
            }
          }
        }

        // Now search in all the include paths
        n = s1->nb_include_paths + s1->nb_sysinclude_paths;
        for (i = 0; i < n; i++) {
          const char *path;
          if (i < s1->nb_include_paths) {
            path = s1->include_paths[i];
          } else {
            path = s1->sysinclude_paths[i - s1->nb_include_paths];
          }
          pstrcpy(buf1, sizeof(buf1), path);
          pstrcat(buf1, sizeof(buf1), "/");
          pstrcat(buf1, sizeof(buf1), buf);
          f = tcc_open(s1, buf1);
          if (f) {
            if (tok == TOK_INCLUDE_NEXT) {	//dcm: for desription of include_next see https://gcc.gnu.org/onlinedocs/cpp/Wrapper-Headers.html
              tok = TOK_INCLUDE;
            } else {
              goto found;
            }
          }
        }
        --s1->include_stack_ptr;
        error("include file '%s' not found", buf);
        break;
      found:
#ifdef INC_DEBUG
        printf("%s: including %s\n", file->filename, buf1);
#endif
        f->inc_type = c;
        pstrcpy(f->inc_filename, sizeof(f->inc_filename), buf);
        file = f;

        // Add include file debug info
        if (do_debug) {
          put_stabs(file->filename, N_BINCL, 0, 0, 0);
        }
        tok_flags |= TOK_FLAG_BOF | TOK_FLAG_BOL;
        ch = file->buf_ptr[0];
        goto done;
      }
      break;
    case TOK_IFNDEF:
      c = 1;
      goto do_ifdef;
    case TOK_IF:
      c = expr_preprocess();
      goto do_if;
    case TOK_IFDEF:
      c = 0;
    do_ifdef:
      next_nomacro();
      if (tok < TOK_IDENT) error("invalid argument for '#if%sdef'", c ? "n" : "");
      if (is_bof) {
        if (c) {
#ifdef INC_DEBUG
          printf("#ifndef %s\n", get_tok_str(tok, NULL));
#endif
          file->ifndef_macro = tok;
        }
      }
      c = (define_find(tok) != 0) ^ c;
    do_if:
      if (s1->ifdef_stack_ptr >= s1->ifdef_stack + IFDEF_STACK_SIZE) error("memory full");
      *s1->ifdef_stack_ptr++ = c;
      goto test_skip;
    case TOK_ELSE:
      if (s1->ifdef_stack_ptr == s1->ifdef_stack) error("#else without matching #if");
      if (s1->ifdef_stack_ptr[-1] & 2) error("#else after #else");
      c = (s1->ifdef_stack_ptr[-1] ^= 3);
      goto test_skip;
    case TOK_ELIF:
      if (s1->ifdef_stack_ptr == s1->ifdef_stack) error("#elif without matching #if");
      c = s1->ifdef_stack_ptr[-1];
      if (c > 1) error("#elif after #else");
      // Last #if/#elif expression was true; skip
      if (c == 1) goto skip;
      c = expr_preprocess();
      s1->ifdef_stack_ptr[-1] = c;
    test_skip:
      if (!(c & 1)) {
      skip:
        preprocess_skip();
        is_bof = 0;
        goto redo;
      }
      break;
    case TOK_ENDIF:
      if (s1->ifdef_stack_ptr <= file->ifdef_stack_ptr) error("#endif without matching #if");
      s1->ifdef_stack_ptr--;
      // '#ifndef macro' was at the start of file. Now we check if
      // an '#endif' is exactly at the end of file
      if (file->ifndef_macro &&
        s1->ifdef_stack_ptr == file->ifdef_stack_ptr) {
        file->ifndef_macro_saved = file->ifndef_macro;
        //  Need to set to zero to avoid false matches if another
        // #ifndef at middle of file
        file->ifndef_macro = 0;
        while (tok != TOK_LINEFEED) next_nomacro();
        tok_flags |= TOK_FLAG_ENDIF;
        goto done;
      }
      break;
    case TOK_LINE:
      next();
      if (tok != TOK_CINT) error("#line");
      file->line_num = tokc.i - 1; // The line number will be incremented after
      next();
      if (tok != TOK_LINEFEED) {
        if (tok != TOK_STR) error("#line");
        pstrcpy(file->filename, sizeof(file->filename), (char *) tokc.cstr->data);
      }
      break;
    case TOK_ERROR:
    case TOK_WARNING:
      c = tok;
      ch = file->buf_ptr[0];
      skip_spaces();
      q = buf;
      while (ch != '\n' && ch != CH_EOF) {
        if ((q - buf) < sizeof(buf) - 1) *q++ = ch;
        if (ch == '\\') {
          if (handle_stray_noerror() == 0) --q;
        } else {
          finp();
        }
      }
      *q = '\0';
      if (c == TOK_ERROR) {
        error("#error %s", buf);
      } else {
        warning("#warning %s", buf);
      }
      break;
    case TOK_PRAGMA:
      pragma_parse(s1);
      break;
    default:
      if (tok == TOK_LINEFEED || tok == '!' || tok == TOK_CINT) {
        // '!' is ignored to allow C scripts. numbers are ignored
        // to emulate cpp behaviour
      } else {
        if (!(saved_parse_flags & PARSE_FLAG_ASM_COMMENTS)) {
          warning("Ignoring unknown preprocessing directive #%s", get_tok_str(tok, &tokc));
        }
      }
      break;
  }

  // Ignore other preprocess commands or #! for C scripts
  while (tok != TOK_LINEFEED) next_nomacro();

 done:
  parse_flags = saved_parse_flags;
}

// Evaluate escape codes in a string
void parse_escape_string(CString *outstr, const uint8_t *buf, int is_long) {
  int c, n;
  const uint8_t *p;

  p = buf;
  for (;;) {
    c = *p;
    if (c == '\0') break;
    if (c == '\\') {
      p++;
      // Escape
      c = *p;
      switch (c) {
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7':
          // At most three octal digits
          n = c - '0';
          p++;
          c = *p;
          if (is_oct(c)) {
            n = n * 8 + c - '0';
            p++;
            c = *p;
            if (is_oct(c)) {
              n = n * 8 + c - '0';
              p++;
            }
          }
          c = n;
          goto add_char_nonext;
        case 'x':
        case 'u':
        case 'U':
          p++;
          n = 0;
          for (;;) {
            c = *p;
            if (c >= 'a' && c <= 'f') {
              c = c - 'a' + 10;
            } else if (c >= 'A' && c <= 'F') {
              c = c - 'A' + 10;
            } else if (is_num(c)) {
              c = c - '0';
            } else {
              break;
            }
            n = n * 16 + c;
            p++;
          }
          c = n;
          goto add_char_nonext;
        case 'a':
          c = '\a';
          break;
        case 'b':
          c = '\b';
          break;
        case 'f':
          c = '\f';
          break;
        case 'n':
          c = '\n';
          break;
        case 'r':
          c = '\r';
          break;
        case 't':
          c = '\t';
          break;
        case 'v':
          c = '\v';
          break;
        case 'e':
          c = 27;
          break;
        case '\'':
        case '\"':
        case '\\': 
        case '?':
          break;
        default:
          if (c >= '!' && c <= '~') {
            warning("unknown escape sequence: \'\\%c\'", c);
          } else {
            warning("unknown escape sequence: \'\\x%x\'", c);
          }
      }
    }
    p++;
  add_char_nonext:
    if (!is_long) {
      cstr_ccat(outstr, c);
    } else {
      cstr_wccat(outstr, c);
    }
  }

  // Add a trailing '\0'
  if (!is_long) {
    cstr_ccat(outstr, '\0');
  } else {
    cstr_wccat(outstr, '\0');
  }
}

#define BN_SIZE 2

// bn = (bn << shift) | or_val
void bn_lshift(unsigned int *bn, int shift, int or_val) {
  int i;
  unsigned int v;
  for (i = 0; i < BN_SIZE; i++) {
    v = bn[i];
    bn[i] = (v << shift) | or_val;
    or_val = v >> (32 - shift);
  }
}

void bn_zero(unsigned int *bn) {
  int i;
  for (i = 0; i < BN_SIZE; i++) bn[i] = 0;
}

// Parse number in null terminated string 'p' and return it in the current token
void parse_number(const char *p) {
  int b, t, shift, frac_bits, s, exp_val, ch;
  char *q;
  unsigned int bn[BN_SIZE];
  double d;

  // Number
  q = token_buf;
  ch = *p++;
  t = ch;
  ch = *p++;
  *q++ = t;
  b = 10;
  if (t == '.') {
    goto float_frac_parse;
  } else if (t == '0') {
    if (ch == 'x' || ch == 'X') {
      q--;
      ch = *p++;
      b = 16;
    } else if (ch == 'b' || ch == 'B') {
      q--;
      ch = *p++;
      b = 2;
    }
  }

  // Parse all digits. Cannot check octal numbers at this stage
  // because of floating point constants.
  while (1) {
    if (ch >= 'a' && ch <= 'f') {
      t = ch - 'a' + 10;
    } else if (ch >= 'A' && ch <= 'F') {
      t = ch - 'A' + 10;
    } else if (is_num(ch)) {
      t = ch - '0';
    } else {
      break;
    }

    if (t >= b) break;
    if (q >= token_buf + STRING_MAX_SIZE) {
    num_too_long:
      error("number too long");
    }
    *q++ = ch;
    ch = *p++;
  }
  if (ch == '.' ||
    ((ch == 'e' || ch == 'E') && b == 10) ||
    ((ch == 'p' || ch == 'P') && (b == 16 || b == 2))) {
    if (b != 10) {
      // NOTE: strtox should support that for hex numbers, but
      // non ISOC99 libcs do not support it, so we prefer to do
      // it by hand
      // hexadecimal or binary floats
      // TODO: handle overflows
      *q = '\0';
      if (b == 16) {
        shift = 4;
      } else {
        shift = 2;
      }
      bn_zero(bn);
      q = token_buf;
      while (1) {
        t = *q++;
        if (t == '\0') {
          break;
        } else if (t >= 'a') {
          t = t - 'a' + 10;
        } else if (t >= 'A') {
          t = t - 'A' + 10;
        } else {
          t = t - '0';
        }
        bn_lshift(bn, shift, t);
      }
      frac_bits = 0;
      if (ch == '.') {
        ch = *p++;
        while (1) {
          t = ch;
          if (t >= 'a' && t <= 'f') {
            t = t - 'a' + 10;
          } else if (t >= 'A' && t <= 'F') {
            t = t - 'A' + 10;
          } else if (t >= '0' && t <= '9') {
            t = t - '0';
          } else {
            break;
          }
          if (t >= b) error("invalid digit");
          bn_lshift(bn, shift, t);
          frac_bits += shift;
          ch = *p++;
        }
      }
      if (ch != 'p' && ch != 'P') expect("exponent");
      ch = *p++;
      s = 1;
      exp_val = 0;
      if (ch == '+') {
        ch = *p++;
      } else if (ch == '-') {
        s = -1;
        ch = *p++;
      }
      if (ch < '0' || ch > '9') expect("exponent digits");
      while (ch >= '0' && ch <= '9') {
        exp_val = exp_val * 10 + ch - '0';
        ch = *p++;
      }
      exp_val = exp_val * s;
      
      // Now we can generate the number
      // TODO: should patch float number directly
      d = (double) bn[1] * 4294967296.0 + (double) bn[0];
      d = ldexp(d, exp_val - frac_bits);
      t = to_upper(ch);
      if (t == 'F') {
        ch = *p++;
        tok = TOK_CFLOAT;
        // float, should handle overflow
        tokc.f = (float)d;
      } else if (t == 'L') {
        ch = *p++;
        tok = TOK_CLDOUBLE;
        // TODO: not large enough
        tokc.ld = (long double)d;
      } else {
        tok = TOK_CDOUBLE;
        tokc.d = d;
      }
    } else {
      // Decimal floats
      if (ch == '.') {
        if (q >= token_buf + STRING_MAX_SIZE) goto num_too_long;
        *q++ = ch;
        ch = *p++;
      float_frac_parse:
        while (ch >= '0' && ch <= '9') {
          if (q >= token_buf + STRING_MAX_SIZE) goto num_too_long;
          *q++ = ch;
          ch = *p++;
        }
      }
      if (ch == 'e' || ch == 'E') {
        if (q >= token_buf + STRING_MAX_SIZE) goto num_too_long;
        *q++ = ch;
        ch = *p++;
        if (ch == '-' || ch == '+') {
          if (q >= token_buf + STRING_MAX_SIZE) goto num_too_long;
          *q++ = ch;
          ch = *p++;
        }
        if (ch < '0' || ch > '9') expect("exponent digits");
        while (ch >= '0' && ch <= '9') {
          if (q >= token_buf + STRING_MAX_SIZE) goto num_too_long;
          *q++ = ch;
          ch = *p++;
        }
      }
      *q = '\0';
      t = to_upper(ch);
      errno = 0;
      if (t == 'F') {
        ch = *p++;
        tok = TOK_CFLOAT;
        tokc.f = strtof(token_buf, NULL);
      } else if (t == 'L') {
        ch = *p++;
        tok = TOK_CLDOUBLE;
        tokc.ld = strtold(token_buf, NULL);
      } else {
        tok = TOK_CDOUBLE;
        tokc.d = strtod(token_buf, NULL);
      }
    }
  } else {
    uint64_t n, n1;
    int lcount, ucount;

    // Integer number
    *q = '\0';
    q = token_buf;
    if (b == 10 && *q == '0') {
      b = 8;
      q++;
    }
    n = 0;
    while (1) {
      t = *q++;
      // No need for checks except for base 10 / 8 errors
      if (t == '\0') {
        break;
      } else if (t >= 'a') {
        t = t - 'a' + 10;
      } else if (t >= 'A') {
        t = t - 'A' + 10;
      } else {
        t = t - '0';
        if (t >= b) error("invalid digit");
      }
      n1 = n;
      n = n * b + t;
      // Detect overflow
      // TODO: this test is not reliable
      if (n < n1) error("integer constant overflow");
    }
    
    // TODO: not exactly ANSI compliant
    if ((n & UINT64_C(0xffffffff00000000)) != 0) {
      if ((n >> 63) != 0) {
        tok = TOK_CULLONG;
      } else {
        tok = TOK_CLLONG;
      }
    } else if (n > 0x7fffffff) {
      tok = TOK_CUINT;
    } else {
      tok = TOK_CINT;
    }

    // Handle MSVC integer size extension
    if (ch == 'i' || ch == 'u' && *p == 'i') {
     int sgn = 1;
     int bits = 0;
     if (ch == 'u') {
       sgn = 0;
       p++;
     }
     ch = *p++;
     while (is_num(ch)) {
       bits = bits * 10 + (ch - '0');
       ch = *p++;
     }
     if (bits == 64) {
       tok = sgn ? TOK_CLLONG : TOK_CULLONG;
     } else if (bits == 32 || bits == 16 || bits == 8) {
       tok = sgn ? TOK_CINT : TOK_CUINT;
     } else {
       error("illegal number of bits in integer constant");
     }
    }

    lcount = 0;
    ucount = 0;
    for (;;) {
      t = to_upper(ch);
      if (t == 'L') {
        if (lcount >= 2) error("three 'l's in integer constant");
        lcount++;
        if (lcount == 2) {
          if (tok == TOK_CINT) {
            tok = TOK_CLLONG;
          } else if (tok == TOK_CUINT) {
            tok = TOK_CULLONG;
          }
        }
        ch = *p++;
      } else if (t == 'U') {
        if (ucount >= 1) error("two 'u's in integer constant");
        ucount++;
        if (tok == TOK_CINT) {
          tok = TOK_CUINT;
        } else if (tok == TOK_CLLONG) {
          tok = TOK_CULLONG;
        }
        ch = *p++;
      } else {
        break;
      }
    }
    if (tok == TOK_CINT || tok == TOK_CUINT) {
      tokc.ui = (unsigned int) n;
    } else {
      tokc.ull = n;
    }
  }
}

#define PARSE2(c1, tok1, c2, tok2) \
  case c1:                         \
    PEEKC(c, p);                   \
    if (c == c2) {                 \
      p++;                         \
      tok = tok2;                  \
    } else {                       \
      tok = tok1;                  \
    }                              \
    break;

// Return next token without macro substitution
void next_nomacro1(void) {
  int t, c, is_long;
  TokenSym *ts;
  uint8_t *p, *p1;
  unsigned int h;

  p = file->buf_ptr;	//dcm: get the pointer to the next ch from the BufferedFile.
 redo_no_start:
  c = *p;				//dcm: now retrieve the ch
  switch (c) {
    case ' ': case '\t': case '\f': case '\v': case '\r':	//dcm: skip white space
      p++;
      goto redo_no_start;

    case '\\':		//dcm: this is CH_EOB ch. The initial empty buffer is initialised to this, and a CH_EOB is added to the end of the buffer.
      // First look if it is in fact an end of buffer
      if (p >= file->buf_end) {
        file->buf_ptr = p;
        handle_eob();			//dcm: read from the file as necessary
        p = file->buf_ptr;
        if (p >= file->buf_end) {
          goto parse_eof;
        } else {
          goto redo_no_start;
        }
      } else {
        file->buf_ptr = p;
        ch = *p;
        handle_stray();
        p = file->buf_ptr;
        goto redo_no_start;
      }
    parse_eof:		//dcm: we've reached the end of file after the "\", now what do we do?
      {
        TCCState *s1 = tcc_state;
        if ((parse_flags & PARSE_FLAG_LINEFEED) && !(tok_flags & TOK_FLAG_EOF)) {
          tok_flags |= TOK_FLAG_EOF;
          tok = TOK_LINEFEED;
          goto keep_tok_flags;
        } else if (s1->include_stack_ptr == s1->include_stack || !(parse_flags & PARSE_FLAG_PREPROCESS)) {
          // No include left; end of file
          tok = TOK_EOF;
        } else {
          tok_flags &= ~TOK_FLAG_EOF;
          // Pop include file
        
          // Test if previous '#endif' was after a #ifdef at start of file
          if (tok_flags & TOK_FLAG_ENDIF) {
#ifdef INC_DEBUG
            printf("#endif %s\n", get_tok_str(file->ifndef_macro_saved, NULL));
#endif
            add_cached_include(s1, file->inc_type, file->inc_filename, file->ifndef_macro_saved);
          }

          // Add end of include file debug info
          if (do_debug) {
            put_stabd(N_EINCL, 0, 0);
          }

          // Pop include stack
          tcc_close(file);
          s1->include_stack_ptr--;
          file = *s1->include_stack_ptr;
          p = file->buf_ptr;
          goto redo_no_start;
        }
      }
      break;

    case '\n':
      file->line_num++;
      tok_flags |= TOK_FLAG_BOL;	//dcm:  "beginning of line before"
      p++;
      if ((parse_flags & PARSE_FLAG_LINEFEED) == 0) goto redo_no_start;	//dcm: treat as white space and skip
      tok = TOK_LINEFEED;
      goto keep_tok_flags;

    case '#':
      // TODO: simplify
      PEEKC(c, p);		//dcm: move to the next ch
      if ((tok_flags & TOK_FLAG_BOL) && (parse_flags & PARSE_FLAG_PREPROCESS)) {
        file->buf_ptr = p;		//dcm: update the BufferedFile info.
        preprocess(tok_flags & TOK_FLAG_BOF);	//dcm: ha, preprocess() calls next_nomacro() which calls next_nomacro1() so this is recursive
        p = file->buf_ptr;
        goto redo_no_start;		//dcm: treat as white space & skip
      } else {
        if (c == '#') {
          p++;
          tok = TOK_TWOSHARPS;
        } else {
          if (parse_flags & PARSE_FLAG_ASM_COMMENTS) {	//dcm: special # processing when compiling asm.
            p = parse_line_comment(p - 1);
            goto redo_no_start;							//dcm: treat as white space
          } else {
            tok = '#';
          }
        }
      }
      break;

    case 'a': case 'b': case 'c': case 'd':
    case 'e': case 'f': case 'g': case 'h':
    case 'i': case 'j': case 'k': case 'l':
    case 'm': case 'n': case 'o': case 'p':
    case 'q': case 'r': case 's': case 't':
    case 'u': case 'v': case 'w': case 'x':
    case 'y': case 'z': 
    case 'A': case 'B': case 'C': case 'D':
    case 'E': case 'F': case 'G': case 'H':
    case 'I': case 'J': case 'K': 
    case 'M': case 'N': case 'O': case 'P':
    case 'Q': case 'R': case 'S': case 'T':
    case 'U': case 'V': case 'W': case 'X':
    case 'Y': case 'Z': 
    case '_':
    parse_ident_fast:
      p1 = p;
      h = TOK_HASH_INIT;
      h = TOK_HASH_FUNC(h, c);
      p++;
      for (;;) {
        c = *p;
        if (!is_idnum(c)) break;
        h = TOK_HASH_FUNC(h, c);
        p++;
      }
      if (c != '\\') {
        TokenSym **pts;
        int len;

        // Fast case: no stray found, so we have the full token and we have already hashed it
        len = p - p1;
        h &= (TOK_HASH_SIZE - 1);
        pts = &hash_ident[h];
        for (;;) {				//dcm: Dupl. of lookup intok_alloc()
          ts = *pts;
          if (!ts) break;
          if (ts->len == len && !memcmp(ts->str, p1, len)) goto token_found;
          pts = &(ts->hash_next);
        }
        ts = tok_alloc_new(pts, p1, len);
      token_found: ;
      } else {
        // Slower case
        cstr_reset(&tokcstr);
        while (p1 < p) {
          cstr_ccat(&tokcstr, *p1);
          p1++;
        }
        p--;
        PEEKC(c, p);
      parse_ident_slow:
        while (is_idnum(c)) {
          cstr_ccat(&tokcstr, c);
          PEEKC(c, p);
        }
        ts = tok_alloc(tokcstr.data, tokcstr.size);
      }
      tok = ts->tok;
      break;

    case 'L':
      t = p[1];
      if (t != '\\' && t != '\'' && t != '\"') {
        // Fast case
        goto parse_ident_fast;
      } else {
        PEEKC(c, p);
        if (c == '\'' || c == '\"') {
          is_long = 1;
          goto str_const;
        } else {
          cstr_reset(&tokcstr);
          cstr_ccat(&tokcstr, 'L');
          goto parse_ident_slow;
        }
      }
      break;

    case '0': case '1': case '2': case '3':
    case '4': case '5': case '6': case '7':
    case '8': case '9':
      cstr_reset(&tokcstr);
      // After the first digit, accept digits, alpha, '.' or sign if prefixed by 'eEpP'
    parse_num:
      for (;;) {
        t = c;
        cstr_ccat(&tokcstr, c);
        PEEKC(c, p);
        if (!(is_num(c) || is_id(c) || c == '.' ||
           ((c == '+' || c == '-') && 
            (t == 'e' || t == 'E' || t == 'p' || t == 'P')))) {
          break;
        }
      }

      // Add a trailing '\0' to ease parsing
      cstr_ccat(&tokcstr, '\0');
      tokc.cstr = &tokcstr;
      tok = TOK_PPNUM;
      break;

    case '.':
      // Special dot handling because it can also start a number
      PEEKC(c, p);
      if (is_num(c)) {
        cstr_reset(&tokcstr);
        cstr_ccat(&tokcstr, '.');
        goto parse_num;
      } else if (c == '.') {
        PEEKC(c, p);
        if (c != '.') expect("'.'");
        PEEKC(c, p);
        tok = TOK_DOTS;
      } else {
        tok = '.';
      }
      break;

    case '\'':
    case '\"':
      is_long = 0;
    str_const: {
      CString str;
      int sep;

      sep = c;

      // Parse the string
      cstr_new(&str);
      p = parse_pp_string(p, sep, &str);
      cstr_ccat(&str, '\0');

      // Evaluate the escape (should be done as TOK_PPNUM)
      cstr_reset(&tokcstr);
      parse_escape_string(&tokcstr, str.data, is_long);
      cstr_free(&str);

      if (sep == '\'') {
        int char_size;
        // TODO: make it portable
        if (!is_long) {
          char_size = 1;
        } else {
          char_size = sizeof(nwchar_t);
        }

        if (tokcstr.size <= char_size) error("empty character constant");

        if (!is_long) {
          if (tokcstr.size > 2) {
            // Multi-character character constant (MSVC extension)
            unsigned char *p = tokcstr.data;
            tokc.i = 0;
            while (*p) tokc.i = tokc.i << 8 | *p++; 
          } else {
            tokc.i = *(int8_t *)tokcstr.data;
          }
          tok = TOK_CCHAR;
        } else {
          if (tokcstr.size > 2 * sizeof(nwchar_t)) warning("multi-character character constant");
          tokc.i = *(nwchar_t *)tokcstr.data;
          tok = TOK_LCHAR;
        }
      } else {
        tokc.cstr = &tokcstr;
        if (!is_long) {
          tok = TOK_STR;
        } else {
          tok = TOK_LSTR;
        }
      }
      break;
    }

    case '<':
      PEEKC(c, p);
      if (c == '=') {
        p++;
        tok = TOK_LE;
      } else if (c == '<') {
        PEEKC(c, p);
        if (c == '=') {
          p++;
          tok = TOK_A_SHL;
        } else {
          tok = TOK_SHL;
        }
      } else {
        tok = TOK_LT;
      }
      break;
    
    case '>':
      PEEKC(c, p);
      if (c == '=') {
        p++;
        tok = TOK_GE;
      } else if (c == '>') {
        PEEKC(c, p);
        if (c == '=') {
          p++;
          tok = TOK_A_SAR;
        } else {
          tok = TOK_SAR;
        }
      } else {
        tok = TOK_GT;
      }
      break;
    
    case '&':
      PEEKC(c, p);
      if (c == '&') {
        p++;
        tok = TOK_LAND;
      } else if (c == '=') {
        p++;
        tok = TOK_A_AND;
      } else {
        tok = '&';
      }
      break;
    
    case '|':
      PEEKC(c, p);
      if (c == '|') {
        p++;
        tok = TOK_LOR;
      } else if (c == '=') {
        p++;
        tok = TOK_A_OR;
      } else {
        tok = '|';
      }
      break;

    case '+':
      PEEKC(c, p);
      if (c == '+') {
        p++;
        tok = TOK_INC;
      } else if (c == '=') {
        p++;
        tok = TOK_A_ADD;
      } else {
        tok = '+';
      }
      break;
    
    case '-':
      PEEKC(c, p);
      if (c == '-') {
        p++;
        tok = TOK_DEC;
      } else if (c == '=') {
        p++;
        tok = TOK_A_SUB;
      } else if (c == '>') {
        p++;
        tok = TOK_ARROW;
      } else {
        tok = '-';
      }
      break;

    PARSE2('!', '!', '=', TOK_NE)
    PARSE2('=', '=', '=', TOK_EQ)
    PARSE2('*', '*', '=', TOK_A_MUL)
    PARSE2('%', '%', '=', TOK_A_MOD)
    PARSE2('^', '^', '=', TOK_A_XOR)
    
    case '/':
      // Comment or operator
      PEEKC(c, p);
      if (c == '*') {
        p = parse_comment(p);
        goto redo_no_start;
      } else if (c == '/') {
        p = parse_line_comment(p);
        goto redo_no_start;
      } else if (c == '=') {
        p++;
        tok = TOK_A_DIV;
      } else {
        tok = '/';
      }
      break;
    
    case '(': case ')': case '[': case ']': case '{':  case '}': 
    case ',': case ';': case ':': case '?': case '~': 
    case '$': case '@': // Only used in assembler
      // Simple token
      tok = c;
      p++;
      break;

    default:
      error("unrecognized character \\x%02x", c);
  }
  tok_flags = 0;
keep_tok_flags:
  file->buf_ptr = p;
#if defined(PARSE_DEBUG)
  printf("token = %s\n", get_tok_str(tok, &tokc));
#endif
}

// Return next token without macro substitution. Can read input from macro_ptr buffer
void next_nomacro(void) {
  //dcm: macro_ptr is set/restored in lots of places - allows parsing of different lists of tokens at different times
  //dcm: using macro_ptr is the way that macros are expanded. Prime place is in macro_subst(). That's also where nested macros are dealt with
  if (macro_ptr) {		
  redo:
    tok = *macro_ptr;
    if (tok) {
      TOK_GET(tok, macro_ptr, tokc);
      if (tok == TOK_LINENUM) {
        file->line_num = tokc.i;
        goto redo;
      }
    }
  } else {
    next_nomacro1();
  }
}

// Substitute args in macro_str and return allocated string
int *macro_arg_subst(Sym **nested_list, int *macro_str, Sym *args) {
  int *st, last_tok, t, notfirst;
  Sym *s;
  CValue cval;
  TokenString str;
  CString cstr;

  tok_str_new(&str);
  last_tok = 0;
  while (1) {
    TOK_GET(t, macro_str, cval);	//dcm: read the next token into t, cval from macro_str
    if (!t) break;
    if (t == '#') {
      // Stringize
	  //dcm:  see https://gcc.gnu.org/onlinedocs/cpp/Stringizing.html#Stringizing
	  //dcm: "when a macro para is used with a leading '#' replace with the literal text of the argument"
      TOK_GET(t, macro_str, cval);	//dcm: read the arg-name
      if (!t) break;
      s = sym_find2(args, t);		//dcm: look up the arg
      if (s) {
        cstr_new(&cstr);
        st = (int *)s->c;
        notfirst = 0;
        while (*st) {				//dcm: turn the tokens into an optionally space separated string
          if (notfirst) cstr_ccat(&cstr, ' ');
          TOK_GET(t, st, cval);
          cstr_cat(&cstr, get_tok_str(t, &cval));	//dcm: needs to be a valid string, so need to backslash escape any embedded quotes & all backslashes
#ifndef PP_NOSPACES
          notfirst = 1;
#endif
        }
        cstr_ccat(&cstr, '\0');
#ifdef PP_DEBUG
        printf("stringize: %s\n", (char *)cstr.data);
#endif
        // Add string
        cval.cstr = &cstr;
        tok_str_add_ex(&str, TOK_STR, &cval);
        cstr_free(&cstr);
      } else {
        tok_str_add_ex(&str, t, &cval);	//dcm: if # wasn't followed by an arg-name, just output it as is, minus the '#'
      }
    } else if (t >= TOK_IDENT) {
      s = sym_find2(args, t);		//dcm: search the args linked-list for t
      if (s) {
        st = (int *)s->c;
		//dcm: https://gcc.gnu.org/onlinedocs/cpp/Concatenation.html#Concatenation
		//dcm: ## performs token pasting - on expansion the 2 tokens on either side are combined into a single token
		//dcm: param expansion happens first, before concatenation. But actual args are NOT macro expanded 1st
		//dcm: the combined token should be valid otherwise an error should be given
        // If '##' is present before or after, no arg substitution
        if (*macro_str == TOK_TWOSHARPS || last_tok == TOK_TWOSHARPS) {
          // Special case for var arg macros: ## eats the ',' if empty VA_ARGS variable.
          // TODO: test of the ',' is not 100% reliable. should fix it to avoid security problems
		  //dcm: For this case see https://gcc.gnu.org/onlinedocs/cpp/Variadic-Macros.html#Variadic-Macros
		  //dcm: if LHS of ## is empty, ignore the prior ','
          if (s->type.t && last_tok == TOK_TWOSHARPS && str.len >= 2 && str.str[str.len - 2] == ',') {
            if (*st == 0) {
              // Suppress ',' '##'
              str.len -= 2;
            } else {
              // Suppress '##' and add variable
              str.len--;
              goto add_var;
            }
          } else {
            int t1;
          add_var:
            for (;;) {
              TOK_GET(t1, st, cval);
              if (!t1) break;
              tok_str_add_ex(&str, t1, &cval);
            }
          }
        } else {
		  //dcm: finally - this is the typical case where a para is substitued. The other cases have all been dealt with
          // NOTE: the stream cannot be read when macro substituing an argument
          macro_subst(&str, nested_list, st, NULL);
        }
      } else {
		//dcm:  the identifier is not a parameter to the macro
        tok_str_add(&str, t);
      }
    } else {
	  //dcm: it's all other tokens (ie not an identifier o # or ##)
      tok_str_add_ex(&str, t, &cval);
    }
    last_tok = t;
  }


  tok_str_add(&str, 0);		//dcm: terminate the token string
  return str.str;
}

static char const ab_month_name[12][4] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

// Perform macro substitution of current token with macro 's' and add
// result to (tok_str,tok_len). 'nested_list' is the list of all macros 
// we got inside to avoid recursing. Return non-zero if no substitution 
// needs to be done.
//dcm: can_read_stream is a stack of saved states which is used whenever
//dcm: a macro needs to be recursively expanded whilst aleady expanding a macro
//dcm: the stack is pushed/popped inside macro_subst()
int macro_subst_tok(TokenString *tok_str, Sym **nested_list, Sym *s, struct macro_level **can_read_stream) {
  Sym *args, *sa, *sa1;
  int mstr_allocated, parlevel, *mstr, t, t1;
  TokenString str;
  char *cstrval;
  CValue cval;
  CString cstr;
  char buf[32];
  
  // If symbol is a macro, prepare substitution special macros
  if (tok == TOK___LINE__) {
    snprintf(buf, sizeof(buf), "%d", file->line_num);
    cstrval = buf;
    t1 = TOK_PPNUM;
    goto add_cstr1;
  } else if (tok == TOK___FILE__) {
    cstrval = file->filename;
    goto add_cstr;
  } else if (tok == TOK___DATE__ || tok == TOK___TIME__) {
    time_t ti;
    struct tm *tm;

    time(&ti);
    tm = localtime(&ti);
    if (tok == TOK___DATE__) {
      snprintf(buf, sizeof(buf), "%s %2d %d", ab_month_name[tm->tm_mon], tm->tm_mday, tm->tm_year + 1900);
    } else {
      snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    }
    cstrval = buf;
  add_cstr:
    t1 = TOK_STR;
  add_cstr1:
    cstr_new(&cstr);
    cstr_cat(&cstr, cstrval);
    cstr_ccat(&cstr, '\0');
    cval.cstr = &cstr;
    tok_str_add_ex(tok_str, t1, &cval);
    cstr_free(&cstr);
  } else {

	//dcm:  It's user-provided macro; time to do the macro expansion
    mstr = (int *)s->c;		//dcm: pointer to the tokens that are the body of the macro
    mstr_allocated = 0;
    if (s->type.t == MACRO_FUNC) {		//dcm: the alternative value for a macro is MACRO_OBJ
      // NOTE: we do not use next_nomacro to avoid eating the
      // next token. TODO: find better solution
    redo:
      if (macro_ptr) {
        t = *macro_ptr;
        if (t == 0 && can_read_stream) {	//dcm: if can_read_stream isn't NULL, it means there's a recursive macro expansion underway
          // End of macro stream: we must look at the token after in the file
          struct macro_level *ml = *can_read_stream;
          macro_ptr = NULL;
          if (ml) {
            macro_ptr = ml->p;
            ml->p = NULL;
            *can_read_stream = ml -> prev;
          }
          goto redo;
        }
      } else {
        // TODO: incorrect with comments
        ch = file->buf_ptr[0];
        while (is_space(ch) || ch == '\n') minp();
        t = ch;
      }
      if (t != '(') return -1; // No macro subst
              
      // Argument macro
      next_nomacro();	//dcm: read the '('
      next_nomacro();	//dcm: read the first actual para
      args = NULL;
      sa = s->next;		//dcm: get the linked list of formal parameters

      // NOTE: empty args are allowed, except if no args
      for (;;) {	//dcm: loop processing each argument in turn
        // handle '()' case
        if (!args && !sa && tok == ')') break;
        if (!sa) error("macro '%s' used with too many args", get_tok_str(s->v, 0));

        tok_str_new(&str);
        parlevel = 0;
		//dcm: see https://gcc.gnu.org/onlinedocs/cpp/Macro-Arguments.html#Macro-Arguments
		//dcm: leading, trailing whitespace in each argument is removed, 
		//dcm: all whitespace between tokens of an argument are reduced to a single space
		//dcm: parentheses within each argument must balance & a comma within parentheses don't end argument
		//dcm: No rqmt for square brackets or braces to balance & don't affect comma
        // NOTE: non zero sa->t indicates VA_ARGS
        while ((parlevel > 0 || (tok != ')' && (tok != ',' || sa->type.t))) && tok != -1) {
          if (tok == '(') {
            parlevel++;
          } else if (tok == ')') {
            parlevel--;
          }
          if (tok != TOK_LINEFEED) tok_str_add_ex(&str, tok, &tokc);
          next_nomacro();
        }
        tok_str_add(&str, 0);	//dcm: terminate the TokenString
        sym_push2(&args, sa->v & ~SYM_FIELD, sa->type.t, (int)str.str);		//dcm: push this argument string onto &args
        sa = sa->next;
        if (tok == ')') {
          // special case for gcc var args: add an empty var arg argument if it is omitted
          if (sa && sa->type.t) {
            continue;
          } else {
            break;
          }
        }
        if (tok != ',') expect(",");
        next_nomacro();
      }		//dcm: end of argument processing loop

      if (sa) {
        error("macro '%s' used with too few args", get_tok_str(s->v, 0));
      }

      // Now subst each arg
      mstr = macro_arg_subst(nested_list, mstr, args);

      // Free memory
      sa = args;
      while (sa) {
        sa1 = sa->prev;
        tok_str_free((int *)sa->c);
        sym_free(sa);
        sa = sa1;
      }
      mstr_allocated = 1;
    }

	//dcm: add this macro to the list of "active", nested macros. Needed to prevent infinite recursive expansion
	//dcm: presence on the list is tested in macro_subst()
    sym_push2(nested_list, s->v, 0, 0);
    macro_subst(tok_str, nested_list, mstr, can_read_stream);

    // Pop nested defined symbol
    sa1 = *nested_list;
    *nested_list = sa1->prev;
    sym_free(sa1);
    if (mstr_allocated) tok_str_free(mstr);
  }
  return 0;
}

// Handle the '##' operator. Return NULL if no '##' seen. Otherwise
// return the resulting string (which must be freed).
//dcm: only ever called from macro_subst(), which means macro_str is only ever the actual para to a macro.
//dcm: by the way, this whole chunk of code is extremely smelly. It's a shame that we can't do the concat and then
//dcm: re-run the lexer over the concatenated strings & completely remove the need for this altogether
int *macro_twosharps(const int *macro_str) {
  TokenSym *ts;
  const int *macro_ptr1, *start_macro_ptr, *ptr, *saved_macro_ptr;
  int t;
  const char *p1, *p2;
  CValue cval;
  TokenString macro_str1;
  CString cstr;

  start_macro_ptr = macro_str;
  // Search the first '##'
  for (;;) {
    macro_ptr1 = macro_str;
    TOK_GET(t, macro_str, cval);
    // Nothing more to do if end of string
    if (t == 0) return NULL;
    if (*macro_str == TOK_TWOSHARPS) break;		//dcm: this is testing if the NEXT token is a ##
  }

  // We saw '##', so we need more processing to handle it
  cstr_new(&cstr);
  tok_str_new(&macro_str1);
  tok = t;
  tokc = cval;

  // Add all tokens seen so far
  for (ptr = start_macro_ptr; ptr < macro_ptr1;) {
    TOK_GET(t, ptr, cval);
    tok_str_add_ex(&macro_str1, t, &cval);
  }
  saved_macro_ptr = macro_ptr;

  // TODO: get rid of the use of macro_ptr here
  macro_ptr = (int *)macro_str;
  for (;;) {
    while (*macro_ptr == TOK_TWOSHARPS) {
      macro_ptr++;
      macro_ptr1 = macro_ptr;
      t = *macro_ptr;		//dcm: see if there's a token following the ##
      if (t) {
        TOK_GET(t, macro_ptr, cval);	//dcm: there is a token, so read it properly
        // We concatenate the two tokens if we have an identifier or a preprocessing number
        cstr_reset(&cstr);				//dcm: cstr is a string var that's re-used each time thru. the loop. Set it back to ""
        p1 = get_tok_str(tok, &tokc);	//dcm: get the string for the token prior to the ## & append it to cstr
        cstr_cat(&cstr, p1);
        p2 = get_tok_str(t, &cval);		//dcm: get the string for the token following the ## & append it to cstr
        cstr_cat(&cstr, p2);
        cstr_ccat(&cstr, '\0');			//dcm: terminate the string
        
		//dcm: now that the 2 strings have been "blindly" concatenated see if the resulting string is something "new":
		//dcm: eg. 2x numbers making a new number, 2 identifiers making a new valid identifier.
        if ((tok >= TOK_IDENT || tok == TOK_PPNUM) && (t >= TOK_IDENT || t == TOK_PPNUM)) {
          if (tok == TOK_PPNUM) {
            // If number, then create a number token
            // NOTE: no need to allocate because tok_str_add_ex() does it
            cstr_reset(&tokcstr);
            tokcstr = cstr;
            cstr_new(&cstr);
            tokc.cstr = &tokcstr;
          } else {
            // If identifier, we must do a test to validate we have a correct identifier
            if (t == TOK_PPNUM) {
              const char *p;
              int c;

              p = p2;
              for (;;) {
                c = *p;
                if (c == '\0') break;
                p++;
                if (!is_num(c) && !is_id(c)) goto error_pasting;
              }
            }
            ts = tok_alloc(cstr.data, strlen(cstr.data));
            tok = ts->tok; // Modify current token
          }
        } else {
		  //dcm: Ok, we've now dealt with numbers and identifiers. Check all other valid symbol concats 
		  //dcm: eg >> and = and various 2 char concats such as <= 

          const char *str = cstr.data;
          const unsigned char *q;

          // We look for a valid token
          // TODO: do more extensive checks
          if (!strcmp(str, ">>=")) {
            tok = TOK_A_SAR;
          } else if (!strcmp(str, "<<=")) {
            tok = TOK_A_SHL;
          } else if (strlen(str) == 2) {
            // Search in two bytes table
            q = tok_two_chars;
            for (;;) {
              if (!*q) goto error_pasting;
              if (q[0] == str[0] && q[1] == str[1]) break;
              q += 3;
            }
            tok = q[2];
          } else {
          error_pasting:
            // NOTE: because get_tok_str use a static buffer, we must save it
            cstr_reset(&cstr);
            p1 = get_tok_str(tok, &tokc);
            cstr_cat(&cstr, p1);
            cstr_ccat(&cstr, '\0');
            p2 = get_tok_str(t, &cval);
            warning("pasting \"%s\" and \"%s\" does not give a valid preprocessing token", cstr.data, p2);
            // Cannot merge tokens: just add them separately
            tok_str_add_ex(&macro_str1, tok, &tokc);
            // TODO: free associated memory?
            tok = t;
            tokc = cval;
          }
        }
      }
    }
    tok_str_add_ex(&macro_str1, tok, &tokc);
    next_nomacro();
    if (tok == 0) break;
  }
  macro_ptr = (int *)saved_macro_ptr;
  cstr_free(&cstr);
  tok_str_add(&macro_str1, 0);
  return macro_str1.str;
}

// Perform macro substitution of macro_str and add result to
// (tok_str,tok_len). 'nested_list' is the list of all macros 
// we got inside to avoid recursing.
//dcm: called from macro_arg_subst() each time a formal-para has been found in the body of the macro 
//dcm: tok_str is the macro-expansion accomplished so far.
//dcm: macro_str is the actual-para to be substituted
//dcm: macro names are added/removed to nested_list by macro_subst_tok()
void macro_subst(TokenString *tok_str, Sym **nested_list, const int *macro_str, struct macro_level ** can_read_stream) {
  Sym *s;
  int *macro_str1;
  const int *ptr;
  int t, ret;
  CValue cval;
  struct macro_level ml;
  
  // First scan for '##' operator handling
  ptr = macro_str;
  macro_str1 = macro_twosharps(ptr);	//dcm: returns with a newer version of ptr with ## concat having taken place
  if (macro_str1) ptr = macro_str1;
  while (1) {
    // NOTE: ptr == NULL can only happen if tokens are read from file stream due to a macro function call
    if (ptr == NULL) break;
    TOK_GET(t, ptr, cval);
    if (t == 0) break;
    s = define_find(t);		//dcm: is the symbol a define?
    if (s != NULL) {		//dcm: why, yes it is
      // If nested substitution, do nothing
      if (sym_find2(*nested_list, t)) goto no_subst;

	  //dcm: Ok, set things up to read from this new macro instead by setting macro_ptr to its TokenString.
	  //dcm: Note - macro_ptr is used by next_nomacro() to decide where to read tokens from.
	  //dcm: Lots of fiddling to remember where we're currently processing 
	  //dcm: (needs to be a stack since it's recursive) and to
	  //dcm: alter where tokens are read to the newly encountered macro
      ml.p = macro_ptr;		//dcm: add this TokenStream to the can_read_stream
      if (can_read_stream) {
        ml.prev = *can_read_stream; 
        *can_read_stream = &ml;
      }
      macro_ptr = (int *) ptr;
      tok = t;
      ret = macro_subst_tok(tok_str, nested_list, s, can_read_stream); //dcm: recursive call.

	  //dcm: now that that substitution has completed, restore the prior saved state
      ptr = (int *) macro_ptr;
      macro_ptr = ml.p;
      if (can_read_stream && *can_read_stream == &ml) {
        *can_read_stream = ml.prev;
      }
      if (ret != 0) goto no_subst;
    } else {
    no_subst:
      tok_str_add_ex(tok_str, t, &cval);
    }
  }
  if (macro_str1) tok_str_free(macro_str1);
}

// Return next token with macro substitution
void next(void) {
  Sym *nested_list, *s;
  TokenString str;
  struct macro_level *ml;

 redo:
  next_nomacro();
  if (!macro_ptr) {
    // If not reading from macro substituted string, then try to substitute macros
    if (tok >= TOK_IDENT && (parse_flags & PARSE_FLAG_PREPROCESS)) {
      s = define_find(tok);		//dcm: returns with a pointer to the define. (as set up by define_push()
      if (s) {
        // We have a macro: try to substitute
        tok_str_new(&str);
        nested_list = NULL;
        ml = NULL;
        if (macro_subst_tok(&str, &nested_list, s, &ml) == 0) {
          // Substitution done, maybe empty
          tok_str_add(&str, 0);
          macro_ptr = str.str;
          macro_ptr_allocated = str.str;
          goto redo;
        }
      }
    }
  } else {
    if (tok == 0) {
      // End of macro or end of unget buffer
      if (unget_buffer_enabled) {
        macro_ptr = unget_saved_macro_ptr;
        unget_buffer_enabled = 0;
      } else {
        // End of macro string: free it
        tok_str_free(macro_ptr_allocated);
        macro_ptr = NULL;
      }
      goto redo;
    }
  }
  
  // Convert preprocessor tokens into C tokens
  if (tok == TOK_PPNUM && (parse_flags & PARSE_FLAG_TOK_NUM)) {
    parse_number((char *)tokc.cstr->data);
  }
}

// Push back current token and set current token to 'last_tok'. Only
// identifier case handled for labels.
void unget_tok(int last_tok) {
  int i, n;
  int *q;
  unget_saved_macro_ptr = macro_ptr;
  unget_buffer_enabled = 1;
  q = unget_saved_buffer;
  macro_ptr = q;
  *q++ = tok;
  n = tok_ext_size(tok) - 1;
  for (i = 0; i < n; i++) *q++ = tokc.tab[i];
  *q = 0; // End of token string
  tok = last_tok;
}

