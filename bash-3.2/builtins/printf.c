/* printf.c, created from printf.def. */
#line 23 "printf.def"

#line 38 "printf.def"

#include <config.h>

#include "../bashtypes.h"

#include <errno.h>
#if defined (HAVE_LIMITS_H)
#  include <limits.h>
#else
   /* Assume 32-bit ints. */
#  define INT_MAX		2147483647
#  define INT_MIN		(-2147483647-1)
#endif

#if defined (PREFER_STDARG)
#  include <stdarg.h>
#else
#  include <varargs.h>
#endif

#include <stdio.h>
#include <chartypes.h>

#ifdef HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include "../bashansi.h"
#include "../bashintl.h"

#include "../shell.h"
#include "stdc.h"
#include "bashgetopt.h"
#include "common.h"

#if defined (PRI_MACROS_BROKEN)
#  undef PRIdMAX
#endif

#if !defined (PRIdMAX)
#  if HAVE_LONG_LONG
#    define PRIdMAX	"lld"
#  else
#    define PRIdMAX	"ld"
#  endif
#endif

#if !defined (errno)
extern int errno;
#endif

#define PC(c) \
  do { \
    char b[2]; \
    tw++; \
    b[0] = c; b[1] = '\0'; \
    if (vflag) \
      vbadd (b, 1); \
    else \
      putchar (c); \
  } while (0)

#define PF(f, func, id) \
  do { \
    char *b = 0; \
    int nw; \
    clearerr (stdout); \
    if (have_fieldwidth && have_precision) \
      nw = asprintf(&b, fmtcheck(f, "%*.*"id), fieldwidth, precision, func); \
    else if (have_fieldwidth) \
      nw = asprintf(&b, fmtcheck(f, "%*"id), fieldwidth, func); \
    else if (have_precision) \
      nw = asprintf(&b, fmtcheck(f, "%.*"id), precision, func); \
    else \
      nw = asprintf(&b, fmtcheck(f, "%"id), func); \
    tw += nw; \
    if (b) \
      { \
	if (vflag) \
	  (void)vbadd (b, nw); \
	else \
	  (void)fputs (b, stdout); \
	if (ferror (stdout)) \
	  { \
	    sh_wrerror (); \
	    clearerr (stdout); \
	    return (EXECUTION_FAILURE); \
	  } \
	free (b); \
      } \
  } while (0)

/* We free the buffer used by mklong() if it's `too big'. */
#define PRETURN(value) \
  do \
    { \
      if (vflag) \
	{ \
	  bind_variable  (vname, vbuf, 0); \
	  stupidly_hack_special_variables (vname); \
	} \
      if (conv_bufsize > 4096 ) \
	{ \
	  free (conv_buf); \
	  conv_bufsize = 0; \
	  conv_buf = 0; \
	} \
      if (vbsize > 4096) \
	{ \
	  free (vbuf); \
	  vbsize = 0; \
	  vbuf = 0; \
	} \
      fflush (stdout); \
      if (ferror (stdout)) \
	{ \
	  clearerr (stdout); \
	  return (EXECUTION_FAILURE); \
	} \
      return (value); \
    } \
  while (0)

#define SKIP1 "#'-+ 0"
#define LENMODS "hjlLtz"

#ifndef HAVE_ASPRINTF
extern int asprintf __P((char **, const char *, ...)) __attribute__((__format__ (printf, 2, 3)));
#endif

static void printf_erange __P((char *));
static int printstr __P((char *, char *, int, int, int));
static int tescape __P((char *, char *, int *));
static char *bexpand __P((char *, int, int *, int *));
static char *vbadd __P((char *, int));
static char *mklong __P((char *, char *, size_t));
static int getchr __P((void));
static char *getstr __P((void));
static int  getint __P((void));
static intmax_t getintmax __P((void));
static uintmax_t getuintmax __P((void));

#if defined (HAVE_LONG_DOUBLE) && HAVE_DECL_STRTOLD && !defined(STRTOLD_BROKEN)
typedef long double floatmax_t;
#  define FLOATMAX_CONV	"L"
#  define strtofltmax	strtold
#else
typedef double floatmax_t;
#  define FLOATMAX_CONV	""
#  define strtofltmax	strtod
#endif
static floatmax_t getfloatmax __P((void));

static int asciicode __P((void));

static WORD_LIST *garglist;
static int retval;
static int conversion_error;

/* printf -v var support */
static int vflag = 0;
static char *vbuf, *vname;
static size_t vbsize;
static int vblen;

static intmax_t tw;

static char *conv_buf;
static size_t conv_bufsize;

int
printf_builtin (list)
     WORD_LIST *list;
{
  int ch, fieldwidth, precision;
  int have_fieldwidth, have_precision;
  char convch, thisch, nextch, *format, *modstart, *fmt, *start;

  conversion_error = 0;
  retval = EXECUTION_SUCCESS;

  vflag = 0;

  reset_internal_getopt ();
  while ((ch = internal_getopt (list, "v:")) != -1)
    {
      switch (ch)
	{
	case 'v':
	  if (legal_identifier (vname = list_optarg))
	    {
	      vflag = 1;
	      vblen = 0;
	    }
	  else
	    {
	      sh_invalidid (vname);
	      return (EX_USAGE);
	    }
	  break;
	default:
	  builtin_usage ();
	  return (EX_USAGE);
	}
    }
  list = loptend;	/* skip over possible `--' */

  if (list == 0)
    {
      builtin_usage ();
      return (EX_USAGE);
    }

  if (list->word->word == 0 || list->word->word[0] == '\0')
    return (EXECUTION_SUCCESS);

  format = list->word->word;
  tw = 0;

  garglist = list->next;

  /* If the format string is empty after preprocessing, return immediately. */
  if (format == 0 || *format == 0)
    return (EXECUTION_SUCCESS);
	  
  /* Basic algorithm is to scan the format string for conversion
     specifications -- once one is found, find out if the field
     width or precision is a '*'; if it is, gather up value.  Note,
     format strings are reused as necessary to use up the provided
     arguments, arguments of zero/null string are provided to use
     up the format string. */
  do
    {
      tw = 0;
      /* find next format specification */
      for (fmt = format; *fmt; fmt++)
	{
	  precision = fieldwidth = 0;
	  have_fieldwidth = have_precision = 0;

	  if (*fmt == '\\')
	    {
	      fmt++;
	      /* A NULL third argument to tescape means to bypass the
		 special processing for arguments to %b. */
	      fmt += tescape (fmt, &nextch, (int *)NULL);
	      PC (nextch);
	      fmt--;	/* for loop will increment it for us again */
	      continue;
	    }

	  if (*fmt != '%')
	    {
	      PC (*fmt);
	      continue;
	    }

	  /* ASSERT(*fmt == '%') */
	  start = fmt++;

	  if (*fmt == '%')		/* %% prints a % */
	    {
	      PC ('%');
	      continue;
	    }

	  /* found format specification, skip to field width */
	  for (; *fmt && strchr(SKIP1, *fmt); ++fmt)
	    ;

	  /* Skip optional field width. */
	  if (*fmt == '*')
	    {
	      fmt++;
	      have_fieldwidth = 1;
	      fieldwidth = getint ();
	    }
	  else
	    while (DIGIT (*fmt))
	      fmt++;

	  /* Skip optional '.' and precision */
	  if (*fmt == '.')
	    {
	      ++fmt;
	      if (*fmt == '*')
		{
		  fmt++;
		  have_precision = 1;
		  precision = getint ();
		}
	      else
		{
		  /* Negative precisions are allowed but treated as if the
		     precision were missing; I would like to allow a leading
		     `+' in the precision number as an extension, but lots
		     of asprintf/fprintf implementations get this wrong. */
#if 0
		  if (*fmt == '-' || *fmt == '+')
#else
		  if (*fmt == '-')
#endif
		    fmt++;
		  while (DIGIT (*fmt))
		    fmt++;
		}
	    }

	  /* skip possible format modifiers */
	  modstart = fmt;
	  while (*fmt && strchr (LENMODS, *fmt))
	    fmt++;
	    
	  if (*fmt == 0)
	    {
	      builtin_error (_("`%s': missing format character"), start);
	      PRETURN (EXECUTION_FAILURE);
	    }

	  convch = *fmt;
	  thisch = modstart[0];
	  nextch = modstart[1];
	  modstart[0] = convch;
	  modstart[1] = '\0';

	  switch(convch)
	    {
	    case 'c':
	      {
		char p;
		char str[2];

		//rdar://84571510: At least on Apple systems, printing %c with
		// precision and/or field width set is undefined behavior
		// and the compiler won't let us build.
		// So, we wrap the char into a string to get it printed correctly
		// and make fmtcheck() happy, setting *fmt accordingly, then reset
		// *fmt back to its original value.
		p = getchr ();
		str[0] = p;
		str[1] = '\0';
		*fmt = 's';
		PF(start, str, "s");
		*fmt = convch;
		break;
	      }

	    case 's':
	      {
		char *p;

		p = getstr ();
		PF(start, p, "s");
		break;
	      }

	    case 'n':
	      {
		char *var;

		var = getstr ();
		if (var && *var)
		  {
		    if (legal_identifier (var))
		      bind_var_to_int (var, tw);
		    else
		      {
			sh_invalidid (var);
			PRETURN (EXECUTION_FAILURE);
		      }
		  }
		break;
	      }

	    case 'b':		/* expand escapes in argument */
	      {
		char *p, *xp;
		int rlen, r;

		p = getstr ();
		ch = rlen = r = 0;
		xp = bexpand (p, strlen (p), &ch, &rlen);

		if (xp)
		  {
		    /* Have to use printstr because of possible NUL bytes
		       in XP -- printf does not handle that well. */
		    r = printstr (start, xp, rlen, fieldwidth, precision);
		    if (r < 0)
		      {
		        sh_wrerror ();
			clearerr (stdout);
		        retval = EXECUTION_FAILURE;
		      }
		    free (xp);
		  }

		if (ch || r < 0)
		  PRETURN (retval);
		break;
	      }

	    case 'q':		/* print with shell quoting */
	      {
		char *p, *xp;
		int r;

		r = 0;
		p = getstr ();
		if (p && *p == 0)	/* XXX - getstr never returns null */
		  xp = savestring ("''");
		else if (ansic_shouldquote (p))
		  xp = ansic_quote (p, 0, (int *)0);
		else
		  xp = sh_backslash_quote (p);
		if (xp)
		  {
		    /* Use printstr to get fieldwidth and precision right. */
		    r = printstr (start, xp, strlen (xp), fieldwidth, precision);
		    if (r < 0)
		      {
			sh_wrerror ();
			clearerr (stdout);
		      }
		    free (xp);
		  }

		if (r < 0)
		  PRETURN (EXECUTION_FAILURE);
		break;
	      }

	    case 'd':
	    case 'i':
	      {
		char *f;
		long p;
		intmax_t pp;

		p = pp = getintmax ();
		if (p != pp)
		  {
		    f = mklong (start, PRIdMAX, sizeof (PRIdMAX) - 2);
		    PF (f, pp, "jd");
		  }
		else
		  {
		    /* Optimize the common case where the integer fits
		       in "long".  This also works around some long
		       long and/or intmax_t library bugs in the common
		       case, e.g. glibc 2.2 x86.  */
		    f = mklong (start, "l", 1);
		    PF (f, p, "ld");
		  }
		break;
	      }

	    case 'o':
	    case 'u':
	    case 'x':
	    case 'X':
	      {
		char *f;
		unsigned long p;
		uintmax_t pp;

		p = pp = getuintmax ();
		if (p != pp)
		  {
		    f = mklong (start, PRIdMAX, sizeof (PRIdMAX) - 2);
		    PF (f, pp, "ju");
		  }
		else
		  {
		    f = mklong (start, "l", 1);
		    PF (f, p, "lu");
		  }
		break;
	      }

	    case 'e':
	    case 'E':
	    case 'f':
	    case 'F':
	    case 'g':
	    case 'G':
#if defined (HAVE_PRINTF_A_FORMAT)
	    case 'a':
	    case 'A':
#endif
	      {
		char *f;
		floatmax_t p;

		p = getfloatmax ();
		f = mklong (start, FLOATMAX_CONV, sizeof(FLOATMAX_CONV) - 1);
		PF (f, p, "f");
		break;
	      }

	    /* We don't output unrecognized format characters; we print an
	       error message and return a failure exit status. */
	    default:
	      builtin_error (_("`%c': invalid format character"), convch);
	      PRETURN (EXECUTION_FAILURE);
	    }

	  modstart[0] = thisch;
	  modstart[1] = nextch;
	}

      if (ferror (stdout))
	{
	  sh_wrerror ();
	  clearerr (stdout);
	  PRETURN (EXECUTION_FAILURE);
	}
    }
  while (garglist && garglist != list->next);

  if (conversion_error)
    retval = EXECUTION_FAILURE;

  PRETURN (retval);
}

static void
printf_erange (s)
     char *s;
{
  builtin_error ("warning: %s: %s", s, strerror(ERANGE));
}

/* We duplicate a lot of what printf(3) does here. */
static int
printstr (fmt, string, len, fieldwidth, precision)
     char *fmt;			/* format */
     char *string;		/* expanded string argument */
     int len;			/* length of expanded string */
     int fieldwidth;		/* argument for width of `*' */
     int precision;		/* argument for precision of `*' */
{
#if 0
  char *s;
#endif
  int padlen, nc, ljust, i;
  int fw, pr;			/* fieldwidth and precision */

#if 0
  if (string == 0 || *string == '\0')
#else
  if (string == 0 || len == 0)
#endif
    return 0;

#if 0
  s = fmt;
#endif
  if (*fmt == '%')
    fmt++;

  ljust = fw = 0;
  pr = -1;

  /* skip flags */
  while (strchr (SKIP1, *fmt))
    {
      if (*fmt == '-')
	ljust = 1;
      fmt++;
    }

  /* get fieldwidth, if present */
  if (*fmt == '*')
    {
      fmt++;
      fw = fieldwidth;
      if (fw < 0)
	{
	  fw = -fw;
	  ljust = 1;
	}
    }
  else if (DIGIT (*fmt))
    {
      fw = *fmt++ - '0';
      while (DIGIT (*fmt))
	fw = (fw * 10) + (*fmt++ - '0');
    }

  /* get precision, if present */
  if (*fmt == '.')
    {
      fmt++;
      if (*fmt == '*')
	{
	  fmt++;
	  pr = precision;
	}
      else if (DIGIT (*fmt))
	{
	  pr = *fmt++ - '0';
	  while (DIGIT (*fmt))
	    pr = (pr * 10) + (*fmt++ - '0');
	}
    }

#if 0
  /* If we remove this, get rid of `s'. */
  if (*fmt != 'b' && *fmt != 'q')
    {
      internal_error ("format parsing problem: %s", s);
      fw = pr = 0;
    }
#endif

  /* chars from string to print */
  nc = (pr >= 0 && pr <= len) ? pr : len;

  padlen = fw - nc;
  if (padlen < 0)
    padlen = 0;
  if (ljust)
    padlen = -padlen;

  /* leading pad characters */
  for (; padlen > 0; padlen--)
    PC (' ');

  /* output NC characters from STRING */
  for (i = 0; i < nc; i++)
    PC (string[i]);

  /* output any necessary trailing padding */
  for (; padlen < 0; padlen++)
    PC (' ');

  return (ferror (stdout) ? -1 : 0);
}
  
/* Convert STRING by expanding the escape sequences specified by the
   POSIX standard for printf's `%b' format string.  If SAWC is non-null,
   perform the processing appropriate for %b arguments.  In particular,
   recognize `\c' and use that as a string terminator.  If we see \c, set
   *SAWC to 1 before returning.  LEN is the length of STRING. */

/* Translate a single backslash-escape sequence starting at ESTART (the
   character after the backslash) and return the number of characters
   consumed by the sequence.  CP is the place to return the translated
   value.  *SAWC is set to 1 if the escape sequence was \c, since that means
   to short-circuit the rest of the processing.  If SAWC is null, we don't
   do the \c short-circuiting, and \c is treated as an unrecognized escape
   sequence; we also bypass the other processing specific to %b arguments.  */
static int
tescape (estart, cp, sawc)
     char *estart;
     char *cp;
     int *sawc;
{
  register char *p;
  int temp, c, evalue;

  p = estart;

  switch (c = *p++)
    {
#if defined (__STDC__)
      case 'a': *cp = '\a'; break;
#else
      case 'a': *cp = '\007'; break;
#endif

      case 'b': *cp = '\b'; break;

      case 'e':
      case 'E': *cp = '\033'; break;	/* ESC -- non-ANSI */

      case 'f': *cp = '\f'; break;

      case 'n': *cp = '\n'; break;

      case 'r': *cp = '\r'; break;

      case 't': *cp = '\t'; break;

      case 'v': *cp = '\v'; break;

      /* The octal escape sequences are `\0' followed by up to three octal
	 digits (if SAWC), or `\' followed by up to three octal digits (if
	 !SAWC).  As an extension, we allow the latter form even if SAWC. */
      case '0': case '1': case '2': case '3':
      case '4': case '5': case '6': case '7':
	evalue = OCTVALUE (c);
	for (temp = 2 + (!evalue && !!sawc); ISOCTAL (*p) && temp--; p++)
	  evalue = (evalue * 8) + OCTVALUE (*p);
	*cp = evalue & 0xFF;
	break;

      /* And, as another extension, we allow \xNNN, where each N is a
	 hex digit. */
      case 'x':
#if 0
	for (evalue = 0; ISXDIGIT ((unsigned char)*p); p++)
#else
	for (temp = 2, evalue = 0; ISXDIGIT ((unsigned char)*p) && temp--; p++)
#endif
	  evalue = (evalue * 16) + HEXVALUE (*p);
	if (p == estart + 1)
	  {
	    builtin_error ("%s", _("missing hex digit for \\x"));
	    *cp = '\\';
	    return 0;
	  }
	*cp = evalue & 0xFF;
	break;

      case '\\':	/* \\ -> \ */
	*cp = c;
	break;

      /* SAWC == 0 means that \', \", and \? are recognized as escape
	 sequences, though the only processing performed is backslash
	 removal. */
      case '\'': case '"': case '?':
	if (!sawc)
	  *cp = c;
	else
	  {
	    *cp = '\\';
	    return 0;
	  }
	break;

      case 'c':
	if (sawc)
	  {
	    *sawc = 1;
	    break;
	  }
      /* other backslash escapes are passed through unaltered */
      default:
	*cp = '\\';
	return 0;
      }
  return (p - estart);
}

static char *
bexpand (string, len, sawc, lenp)
     char *string;
     int len, *sawc, *lenp;
{
  int temp;
  char *ret, *r, *s, c;

#if 0
  if (string == 0 || *string == '\0')
#else
  if (string == 0 || len == 0)
#endif
    {
      if (sawc)
	*sawc = 0;
      if (lenp)
	*lenp = 0;
      return ((char *)NULL);
    }

  ret = (char *)xmalloc (len + 1);
  for (r = ret, s = string; s && *s; )
    {
      c = *s++;
      if (c != '\\' || *s == '\0')
	{
	  *r++ = c;
	  continue;
	}
      temp = 0;
      s += tescape (s, &c, &temp);
      if (temp)
	{
	  if (sawc)
	    *sawc = 1;
	  break;
	}

      *r++ = c;
    }

  *r = '\0';
  if (lenp)
    *lenp = r - ret;
  return ret;
}

static char *
vbadd (buf, blen)
     char *buf;
     int blen;
{
  size_t nlen;

  nlen = vblen + blen + 1;
  if (nlen >= vbsize)
    {
      vbsize = ((nlen + 63) >> 6) << 6;
      vbuf = (char *)xrealloc (vbuf, vbsize);
    }

  if (blen == 1)
    vbuf[vblen++] = buf[0];
  else
    {
      FASTCOPY (buf, vbuf  + vblen, blen);
      vblen += blen;
    }
  vbuf[vblen] = '\0';

#ifdef DEBUG
  if  (strlen (vbuf) != vblen)
    internal_error  ("printf:vbadd: vblen (%d) != strlen (vbuf) (%d)", vblen, (int)strlen (vbuf));
#endif

  return vbuf;
}

static char *
mklong (str, modifiers, mlen)
     char *str;
     char *modifiers;
     size_t mlen;
{
  size_t len, slen;

  slen = strlen (str);
  len = slen + mlen + 1;

  if (len > conv_bufsize)
    {
      conv_bufsize = (((len + 1023) >> 10) << 10);
      conv_buf = (char *)xrealloc (conv_buf, conv_bufsize);
    }

  FASTCOPY (str, conv_buf, slen - 1);
  FASTCOPY (modifiers, conv_buf + slen - 1, mlen);

  conv_buf[len - 2] = str[slen - 1];
  conv_buf[len - 1] = '\0';
  return (conv_buf);
}

static int
getchr ()
{
  int ret;

  if (garglist == 0)
    return ('\0');

  ret = (int)garglist->word->word[0];
  garglist = garglist->next;
  return ret;
}

static char *
getstr ()
{
  char *ret;

  if (garglist == 0)
    return ("");

  ret = garglist->word->word;
  garglist = garglist->next;
  return ret;
}

static int
getint ()
{
  intmax_t ret;

  ret = getintmax ();

  if (ret > INT_MAX)
    {
      printf_erange (garglist->word->word);
      ret = INT_MAX;
    }
  else if (ret < INT_MIN)
    {
      printf_erange (garglist->word->word);
      ret = INT_MIN;
    }

  return ((int)ret);
}

static intmax_t
getintmax ()
{
  intmax_t ret;
  char *ep;

  if (garglist == 0)
    return (0);

  if (garglist->word->word[0] == '\'' || garglist->word->word[0] == '"')
    return asciicode ();

  errno = 0;
  ret = strtoimax (garglist->word->word, &ep, 0);

  if (*ep)
    {
      sh_invalidnum (garglist->word->word);
      /* POSIX.2 says ``...a diagnostic message shall be written to standard
	 error, and the utility shall not exit with a zero exit status, but
	 shall continue processing any remaining operands and shall write the
         value accumulated at the time the error was detected to standard
	 output.''  Yecch. */
      ret = 0;
      conversion_error = 1;
    }
  else if (errno == ERANGE)
    printf_erange (garglist->word->word);

  garglist = garglist->next;
  return (ret);
}

static uintmax_t
getuintmax ()
{
  uintmax_t ret;
  char *ep;

  if (garglist == 0)
    return (0);

  if (garglist->word->word[0] == '\'' || garglist->word->word[0] == '"')
    return asciicode ();

  errno = 0;
  ret = strtoumax (garglist->word->word, &ep, 0);
  
  if (*ep)
    {
      sh_invalidnum (garglist->word->word);
      /* Same POSIX.2 conversion error requirements as getintmax(). */
      ret = 0;
      conversion_error = 1;
    }
  else if (errno == ERANGE)
    printf_erange (garglist->word->word);

  garglist = garglist->next;
  return (ret);
}

static floatmax_t
getfloatmax ()
{
  floatmax_t ret;
  char *ep;

  if (garglist == 0)
    return (0);

  if (garglist->word->word[0] == '\'' || garglist->word->word[0] == '"')
    return asciicode ();

  errno = 0;
  ret = strtofltmax (garglist->word->word, &ep);

  if (*ep)
    {
      sh_invalidnum (garglist->word->word);
      /* Same thing about POSIX.2 conversion error requirements. */
      ret = 0;
      conversion_error = 1;
    }
  else if (errno == ERANGE)
    printf_erange (garglist->word->word);

  garglist = garglist->next;
  return (ret);
}

/* NO check is needed for garglist here. */
static int
asciicode ()
{
  register int ch;

  ch = garglist->word->word[1];
  garglist = garglist->next;
  return (ch);
}
