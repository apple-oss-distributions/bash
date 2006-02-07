/*
 * mksyntax.c - construct shell syntax table for fast char attribute lookup.
 */

/* Copyright (C) 2000 Free Software Foundation, Inc.

   This file is part of GNU Bash, the Bourne Again SHell.

   Bash is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2, or (at your option) any later
   version.

   Bash is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License along
   with Bash; see the file COPYING.  If not, write to the Free Software
   Foundation, 59 Temple Place, Suite 330, Boston, MA 02111 USA. */

#include "config.h"

#include <stdio.h>
#include "bashansi.h"
#include "chartypes.h"
#include <errno.h>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include "syntax.h"

extern int optind;
extern char *optarg;

#ifndef errno
extern int errno;
#endif

#ifndef HAVE_STRERROR
extern char *strerror();
#endif

struct wordflag {
	int	flag;
	char	*fstr;
} wordflags[] = {
	{ CWORD,	"CWORD" },
	{ CSHMETA,	"CSHMETA" },
	{ CSHBRK,	"CSHBRK" },
	{ CBACKQ,	"CBACKQ" },
	{ CQUOTE,	"CQUOTE" },
	{ CSPECL,	"CSPECL" },
	{ CEXP,		"CEXP" },
	{ CBSDQUOTE,	"CBSDQUOTE" },
	{ CBSHDOC,	"CBSHDOC" },
	{ CGLOB,	"CGLOB" },
	{ CXGLOB,	"CXGLOB" },
	{ CXQUOTE,	"CXQUOTE" },
	{ CSPECVAR,	"CSPECVAR" },
	{ CSUBSTOP,	"CSUBSTOP" },
};
	
#define N_WFLAGS	(sizeof (wordflags) / sizeof (wordflags[0]))
#define SYNSIZE		256

int	lsyntax[SYNSIZE];
int	debug;
char	*progname;

char	preamble[] = "\
/*\n\
 * This file was generated by mksyntax.  DO NOT EDIT.\n\
 */\n\
\n";

char	includes[] = "\
#include \"stdc.h\"\n\
#include \"syntax.h\"\n\n";

static void
usage()
{
  fprintf (stderr, "%s: usage: %s [-d] [-o filename]\n", progname, progname);
  exit (2);
}

#ifdef INCLUDE_UNUSED
static int
getcflag (s)
     char *s;
{
  int i;

  for (i = 0; i < N_WFLAGS; i++)
    if (strcmp (s, wordflags[i].fstr) == 0)
      return wordflags[i].flag;
  return -1;
}
#endif

static char *
cdesc (i)
     int i;
{
  static char xbuf[16];

  if (i == ' ')
    return "SPC";
  else if (ISPRINT (i))
    {
      xbuf[0] = i;
      xbuf[1] = '\0';
      return (xbuf);
    }
  else if (i == CTLESC)
    return "CTLESC";
  else if (i == CTLNUL)
    return "CTLNUL";
  else if (i == '\033')		/* ASCII */
    return "ESC";

  xbuf[0] = '\\';
  xbuf[2] = '\0';
    
  switch (i)
    {
    case '\a': xbuf[1] = 'a'; break;
    case '\v': xbuf[1] = 'v'; break;
    case '\b': xbuf[1] = 'b'; break;
    case '\f': xbuf[1] = 'f'; break;
    case '\n': xbuf[1] = 'n'; break;
    case '\r': xbuf[1] = 'r'; break;
    case '\t': xbuf[1] = 't'; break;
    default: sprintf (xbuf, "%d", i); break;
    }

  return xbuf;	
}

static char *
getcstr (f)
     int f;
{
  int i;

  for (i = 0; i < N_WFLAGS; i++)
    if (f == wordflags[i].flag)
      return (wordflags[i].fstr);
  return ((char *)NULL);
}

static void
addcstr (str, flag)
     char *str;
     int flag;
{
  char *s, *fstr;
  unsigned char uc;

  for (s = str; s && *s; s++)
    {
      uc = *s;

      if (debug)
	{
	  fstr = getcstr (flag);
	  fprintf(stderr, "added %s for character %s\n", fstr, cdesc(uc));
	}
	
      lsyntax[uc] |= flag;
    }
}

static void
addcchar (c, flag)
     unsigned char c;
     int flag;
{
  char *fstr;

  if (debug)
    {
      fstr = getcstr (flag);
      fprintf (stderr, "added %s for character %s\n", fstr, cdesc(c));
    }
  lsyntax[c] |= flag;
}

/* load up the correct flag values in lsyntax */
static void
load_lsyntax ()
{
  /* shell metacharacters */
  addcstr (shell_meta_chars, CSHMETA);

  /* shell word break characters */
  addcstr (shell_break_chars, CSHBRK);

  addcchar ('`', CBACKQ);

  addcstr (shell_quote_chars, CQUOTE);

  addcchar (CTLESC, CSPECL);
  addcchar (CTLNUL, CSPECL);

  addcstr (shell_exp_chars, CEXP);

  addcstr (slashify_in_quotes, CBSDQUOTE);
  addcstr (slashify_in_here_document, CBSHDOC);

  addcstr (shell_glob_chars, CGLOB);

#if defined (EXTENDED_GLOB)
  addcstr (ext_glob_chars, CXGLOB);
#endif

  addcstr (shell_quote_chars, CXQUOTE);
  addcchar ('\\', CXQUOTE);

  addcstr ("@*#?-$!", CSPECVAR);	/* omits $0...$9 and $_ */

  addcstr ("-=?+", CSUBSTOP);		/* OP in ${paramOPword} */
}

static void
dump_lflags (fp, ind)
     FILE *fp;
     int ind;
{
  int xflags, first, i;

  xflags = lsyntax[ind];
  first = 1;

  if (xflags == 0)
    fputs (wordflags[0].fstr, fp);
  else
    {
      for (i = 1; i < N_WFLAGS; i++)
	if (xflags & wordflags[i].flag)
	  {
	    if (first)
	      first = 0;
	    else
	      putc ('|', fp);
	    fputs (wordflags[i].fstr, fp);
  	  }
    }
}

static void
wcomment (fp, i)
     FILE *fp;
     int i;
{
  fputs ("\t\t/* ", fp);

  fprintf (fp, "%s", cdesc(i));
      
  fputs (" */", fp);
}

static void
dump_lsyntax (fp)
     FILE *fp;
{
  int i;

  fprintf (fp, "const int sh_syntaxtab[%d] = {\n", SYNSIZE);

  for (i = 0; i < SYNSIZE; i++)
    {
      putc ('\t', fp);
      dump_lflags (fp, i);
      putc (',', fp);
      wcomment (fp, i);
      putc ('\n', fp);
    }

  fprintf (fp, "};\n");
}

int
main(argc, argv)
     int argc;
     char **argv;
{
  int opt, i;
  char *filename;
  FILE *fp;

  if ((progname = strrchr (argv[0], '/')) == 0)
    progname = argv[0];
  else
    progname++;

  filename = (char *)NULL;
  debug = 0;

  while ((opt = getopt (argc, argv, "do:")) != EOF)
    {
      switch (opt)
	{
	case 'd':
	  debug = 1;
	  break;
	case 'o':
	  filename = optarg;
	  break;
	default:
	  usage();
	}
    }

  argc -= optind;
  argv += optind;

  if (filename)
    {
      fp = fopen (filename, "w");
      if (fp == 0)
	{
	  fprintf (stderr, "%s: %s: cannot open: %s\n", progname, filename, strerror(errno));
	  exit (1);
	}
    }
  else
    {
      filename = "stdout";
      fp = stdout;
    }


  for (i = 0; i < SYNSIZE; i++)
    lsyntax[i] = CWORD;

  load_lsyntax ();

  fprintf (fp, "%s\n", preamble);
  fprintf (fp, "%s\n", includes);

  dump_lsyntax (fp);

  if (fp != stdout)
    fclose (fp);
  exit (0);
}


#if !defined (HAVE_STRERROR)

#include <bashtypes.h>
#ifndef _MINIX
#  include <sys/param.h>
#endif

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif

/* Return a string corresponding to the error number E.  From
   the ANSI C spec. */
#if defined (strerror)
#  undef strerror
#endif

char *
strerror (e)
     int e;
{
  static char emsg[40];
#if defined (HAVE_SYS_ERRLIST)
  extern int sys_nerr;
  extern char *sys_errlist[];

  if (e > 0 && e < sys_nerr)
    return (sys_errlist[e]);
  else
#endif /* HAVE_SYS_ERRLIST */
    {
      sprintf (emsg, "Unknown system error %d", e);
      return (&emsg[0]);
    }
}
#endif /* HAVE_STRERROR */