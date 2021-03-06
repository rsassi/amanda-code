/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1998 University of Maryland at College Park
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of U.M. not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  U.M. makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * U.M. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL U.M.
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors: the Amanda Development Team.  Its members are listed in a
 * file named AUTHORS, in the root directory of this distribution.
 */
/*
 * $Id: match.c,v 1.23 2006/05/25 01:47:12 johnfranks Exp $
 *
 * functions for checking and matching regular expressions
 */

#include "amanda.h"
#include <regex.h>

static int match_word(const char *glob, const char *word, const char separator);

char *
validate_regexp(
    const char *	regex)
{
    regex_t regc;
    int result;
    static char errmsg[STR_SIZE];

    if ((result = regcomp(&regc, regex,
			  REG_EXTENDED|REG_NOSUB|REG_NEWLINE)) != 0) {
      regerror(result, &regc, errmsg, SIZEOF(errmsg));
      return errmsg;
    }

    regfree(&regc);

    return NULL;
}

char *
clean_regex(
    const char *	regex)
{
    char *result;
    int j;
    size_t i;
    result = alloc(2*strlen(regex)+1);

    for(i=0,j=0;i<strlen(regex);i++) {
	if(!isalnum((int)regex[i]))
	    result[j++]='\\';
	result[j++]=regex[i];
    }
    result[j] = '\0';
    return result;
}

int
match(
    const char *	regex,
    const char *	str)
{
    regex_t regc;
    int result;
    char errmsg[STR_SIZE];

    if((result = regcomp(&regc, regex,
			 REG_EXTENDED|REG_NOSUB|REG_NEWLINE)) != 0) {
        regerror(result, &regc, errmsg, SIZEOF(errmsg));
	error(_("regex \"%s\": %s"), regex, errmsg);
	/*NOTREACHED*/
    }

    if((result = regexec(&regc, str, 0, 0, 0)) != 0
       && result != REG_NOMATCH) {
        regerror(result, &regc, errmsg, SIZEOF(errmsg));
	error(_("regex \"%s\": %s"), regex, errmsg);
	/*NOTREACHED*/
    }

    regfree(&regc);

    return result == 0;
}

char *
validate_glob(
    const char *	glob)
{
    char *regex;
    regex_t regc;
    int result;
    static char errmsg[STR_SIZE];

    regex = glob_to_regex(glob);
    if ((result = regcomp(&regc, regex,
			  REG_EXTENDED|REG_NOSUB|REG_NEWLINE)) != 0) {
      regerror(result, &regc, errmsg, SIZEOF(errmsg));
      amfree(regex);
      return errmsg;
    }

    regfree(&regc);
    amfree(regex);

    return NULL;
}

int
match_glob(
    const char *	glob,
    const char *	str)
{
    char *regex;
    regex_t regc;
    int result;
    char errmsg[STR_SIZE];

    regex = glob_to_regex(glob);
    if((result = regcomp(&regc, regex,
			 REG_EXTENDED|REG_NOSUB|REG_NEWLINE)) != 0) {
        regerror(result, &regc, errmsg, SIZEOF(errmsg));
	error(_("glob \"%s\" -> regex \"%s\": %s"), glob, regex, errmsg);
	/*NOTREACHED*/
    }

    if((result = regexec(&regc, str, 0, 0, 0)) != 0
       && result != REG_NOMATCH) {
        regerror(result, &regc, errmsg, SIZEOF(errmsg));
	error(_("glob \"%s\" -> regex \"%s\": %s"), glob, regex, errmsg);
	/*NOTREACHED*/
    }

    regfree(&regc);
    amfree(regex);

    return result == 0;
}

char *
glob_to_regex(
    const char *	glob)
{
    char *regex;
    char *r;
    size_t len;
    int ch;
    int last_ch;

    /*
     * Allocate an area to convert into.  The worst case is a five to
     * one expansion.
     */
    len = strlen(glob);
    regex = alloc(1 + len * 5 + 1 + 1);

    /*
     * Do the conversion:
     *
     *  ?      -> [^/]
     *  *      -> [^/]*
     *  [!...] -> [^...]
     *
     * The following are given a leading backslash to protect them
     * unless they already have a backslash:
     *
     *   ( ) { } + . ^ $ |
     *
     * Put a leading ^ and trailing $ around the result.  If the last
     * non-escaped character is \ leave the $ off to cause a syntax
     * error when the regex is compiled.
     */

    r = regex;
    *r++ = '^';
    last_ch = '\0';
    for (ch = *glob++; ch != '\0'; last_ch = ch, ch = *glob++) {
	if (last_ch == '\\') {
	    *r++ = (char)ch;
	    ch = '\0';			/* so last_ch != '\\' next time */
	} else if (last_ch == '[' && ch == '!') {
	    *r++ = '^';
	} else if (ch == '\\') {
	    *r++ = (char)ch;
	} else if (ch == '*' || ch == '?') {
	    *r++ = '[';
	    *r++ = '^';
	    *r++ = '/';
	    *r++ = ']';
	    if (ch == '*') {
		*r++ = '*';
	    }
	} else if (ch == '('
		   || ch == ')'
		   || ch == '{'
		   || ch == '}'
		   || ch == '+'
		   || ch == '.'
		   || ch == '^'
		   || ch == '$'
		   || ch == '|') {
	    *r++ = '\\';
	    *r++ = (char)ch;
	} else {
	    *r++ = (char)ch;
	}
    }
    if (last_ch != '\\') {
	*r++ = '$';
    }
    *r = '\0';

    return regex;
}


int
match_tar(
    const char *	glob,
    const char *	str)
{
    char *regex;
    regex_t regc;
    int result;
    char errmsg[STR_SIZE];

    regex = tar_to_regex(glob);
    if((result = regcomp(&regc, regex,
			 REG_EXTENDED|REG_NOSUB|REG_NEWLINE)) != 0) {
        regerror(result, &regc, errmsg, SIZEOF(errmsg));
	error(_("glob \"%s\" -> regex \"%s\": %s"), glob, regex, errmsg);
	/*NOTREACHED*/
    }

    if((result = regexec(&regc, str, 0, 0, 0)) != 0
       && result != REG_NOMATCH) {
        regerror(result, &regc, errmsg, SIZEOF(errmsg));
	error(_("glob \"%s\" -> regex \"%s\": %s"), glob, regex, errmsg);
	/*NOTREACHED*/
    }

    regfree(&regc);
    amfree(regex);

    return result == 0;
}

char *
tar_to_regex(
    const char *	glob)
{
    char *regex;
    char *r;
    size_t len;
    int ch;
    int last_ch;

    /*
     * Allocate an area to convert into.  The worst case is a five to
     * one expansion.
     */
    len = strlen(glob);
    regex = alloc(1 + len * 5 + 1 + 1);

    /*
     * Do the conversion:
     *
     *  ?      -> [^/]
     *  *      -> .*
     *  [!...] -> [^...]
     *
     * The following are given a leading backslash to protect them
     * unless they already have a backslash:
     *
     *   ( ) { } + . ^ $ |
     *
     * Put a leading ^ and trailing $ around the result.  If the last
     * non-escaped character is \ leave the $ off to cause a syntax
     * error when the regex is compiled.
     */

    r = regex;
    *r++ = '^';
    last_ch = '\0';
    for (ch = *glob++; ch != '\0'; last_ch = ch, ch = *glob++) {
	if (last_ch == '\\') {
	    *r++ = (char)ch;
	    ch = '\0';			/* so last_ch != '\\' next time */
	} else if (last_ch == '[' && ch == '!') {
	    *r++ = '^';
	} else if (ch == '\\') {
	    *r++ = (char)ch;
	} else if (ch == '*') {
	    *r++ = '.';
	    *r++ = '*';
	} else if (ch == '?') {
	    *r++ = '[';
	    *r++ = '^';
	    *r++ = '/';
	    *r++ = ']';
	} else if (ch == '('
		   || ch == ')'
		   || ch == '{'
		   || ch == '}'
		   || ch == '+'
		   || ch == '.'
		   || ch == '^'
		   || ch == '$'
		   || ch == '|') {
	    *r++ = '\\';
	    *r++ = (char)ch;
	} else {
	    *r++ = (char)ch;
	}
    }
    if (last_ch != '\\') {
	*r++ = '$';
    }
    *r = '\0';

    return regex;
}


static int
match_word(
    const char *	glob,
    const char *	word,
    const char		separator)
{
    char *regex;
    char *r;
    size_t  len;
    int  ch;
    int  last_ch;
    int  next_ch;
    size_t  lenword;
    char *nword;
    char *nglob;
    char *g; 
    const char *w;
    int  i;

    lenword = strlen(word);
    nword = (char *)alloc(lenword + 3);

    r = nword;
    w = word;
    if(lenword == 1 && *w == separator) {
	*r++ = separator;
	*r++ = separator;
    }
    else {
	if(*w != separator)
	    *r++ = separator;
	while(*w != '\0')
	    *r++ = *w++;
	if(*(r-1) != separator)
	    *r++ = separator;    
    }
    *r = '\0';

    /*
     * Allocate an area to convert into.  The worst case is a six to
     * one expansion.
     */
    len = strlen(glob);
    regex = (char *)alloc(1 + len * 6 + 1 + 1 + 2 + 2);
    r = regex;
    nglob = stralloc(glob);
    g = nglob;

    if((len == 1 && nglob[0] == separator) ||
       (len == 2 && nglob[0] == '^' && nglob[1] == separator) ||
       (len == 2 && nglob[0] == separator && nglob[1] == '$') ||
       (len == 3 && nglob[0] == '^' && nglob[1] == separator &&
        nglob[2] == '$')) {
	*r++ = '^';
	*r++ = '\\';
	*r++ = separator;
	*r++ = '\\';
	*r++ = separator;
	*r++ = '$';
    }
    else {
	/*
	 * Do the conversion:
	 *
	 *  ?      -> [^\separator]
	 *  *      -> [^\separator]*
	 *  [!...] -> [^...]
	 *  **     -> .*
	 *
	 * The following are given a leading backslash to protect them
	 * unless they already have a backslash:
	 *
	 *   ( ) { } + . ^ $ |
	 *
	 * If the last
	 * non-escaped character is \ leave it to cause a syntax
	 * error when the regex is compiled.
	 */

	if(*g == '^') {
	    *r++ = '^';
	    *r++ = '\\';	/* escape the separator */
	    *r++ = separator;
	    g++;
	    if(*g == separator) g++;
	}
	else if(*g != separator) {
	    *r++ = '\\';	/* add a leading \separator */
	    *r++ = separator;
	}
	last_ch = '\0';
	for (ch = *g++; ch != '\0'; last_ch = ch, ch = *g++) {
	    next_ch = *g;
	    if (last_ch == '\\') {
		*r++ = (char)ch;
		ch = '\0';		/* so last_ch != '\\' next time */
	    } else if (last_ch == '[' && ch == '!') {
		*r++ = '^';
	    } else if (ch == '\\') {
		*r++ = (char)ch;
	    } else if (ch == '*' || ch == '?') {
		if(ch == '*' && next_ch == '*') {
		    *r++ = '.';
		    g++;
		}
		else {
		    *r++ = '[';
		    *r++ = '^';
		    *r++ = '\\';
		    *r++ = separator;
		    *r++ = ']';
		}
		if (ch == '*') {
		    *r++ = '*';
		}
	    } else if (ch == '$' && next_ch == '\0') {
		if(last_ch != separator) {
		    *r++ = '\\';
		    *r++ = separator;
		}
		*r++ = (char)ch;
	    } else if (   ch == '('
		       || ch == ')'
		       || ch == '{'
		       || ch == '}'
		       || ch == '+'
		       || ch == '.'
		       || ch == '^'
		       || ch == '$'
		       || ch == '|') {
		*r++ = '\\';
		*r++ = (char)ch;
	    } else {
		*r++ = (char)ch;
	    }
	}
	if(last_ch != '\\') {
	    if(last_ch != separator && last_ch != '$') {
		*r++ = '\\';
		*r++ = separator;		/* add a trailing \separator */
	    }
	}
    }
    *r = '\0';

    i = match(regex,nword);

    amfree(nword);
    amfree(nglob);
    amfree(regex);
    return i;
}


int
match_host(
    const char *	glob,
    const char *	host)
{
    char *lglob, *lhost;
    char *c;
    const char *d;
    int i;

    
    lglob = (char *)alloc(strlen(glob)+1);
    c = lglob, d=glob;
    while( *d != '\0')
	*c++ = (char)tolower(*d++);
    *c = *d;

    lhost = (char *)alloc(strlen(host)+1);
    c = lhost, d=host;
    while( *d != '\0')
	*c++ = (char)tolower(*d++);
    *c = *d;

    i = match_word(lglob, lhost, (int)'.');
    amfree(lglob);
    amfree(lhost);
    return i;
}


int
match_disk(
    const char *	glob,
    const char *	disk)
{
    return match_word(glob, disk, '/');
}

int
match_datestamp(
    const char *	dateexp,
    const char *	datestamp)
{
    char *dash;
    size_t len, len_suffix;
    size_t len_prefix;
    char firstdate[100], lastdate[100];
    char mydateexp[100];
    int match_exact;

    if(strlen(dateexp) >= 100 || strlen(dateexp) < 1) {
	error(_("Illegal datestamp expression %s"),dateexp);
	/*NOTREACHED*/
    }
   
    if(dateexp[0] == '^') {
	strncpy(mydateexp, dateexp+1, strlen(dateexp)-1); 
	mydateexp[strlen(dateexp)-1] = '\0';
    }
    else {
	strncpy(mydateexp, dateexp, strlen(dateexp));
	mydateexp[strlen(dateexp)] = '\0';
    }

    if(mydateexp[strlen(mydateexp)] == '$') {
	match_exact = 1;
	mydateexp[strlen(mydateexp)] = '\0';
    }
    else
	match_exact = 0;

    if((dash = strchr(mydateexp,'-'))) {
	if(match_exact == 1) {
	    error(_("Illegal datestamp expression %s"),dateexp);
	    /*NOTREACHED*/
	}
	len = (size_t)(dash - mydateexp);
	len_suffix = strlen(dash) - 1;
	len_prefix = len - len_suffix;

	dash++;
	strncpy(firstdate, mydateexp, len);
	firstdate[len] = '\0';
	strncpy(lastdate, mydateexp, len_prefix);
	strncpy(&(lastdate[len_prefix]), dash, len_suffix);
	lastdate[len] = '\0';
	return ((strncmp(datestamp, firstdate, strlen(firstdate)) >= 0) &&
		(strncmp(datestamp, lastdate , strlen(lastdate))  <= 0));
    }
    else {
	if(match_exact == 1) {
	    return (strcmp(datestamp, mydateexp) == 0);
	}
	else {
	    return (strncmp(datestamp, mydateexp, strlen(mydateexp)) == 0);
	}
    }
}


int
match_level(
    const char *	levelexp,
    const char *	level)
{
    char *dash;
    size_t len, len_suffix;
    size_t len_prefix;
    char lowend[100], highend[100];
    char mylevelexp[100];
    int match_exact;

    if(strlen(levelexp) >= 100 || strlen(levelexp) < 1) {
	error(_("Illegal level expression %s"),levelexp);
	/*NOTREACHED*/
    }
   
    if(levelexp[0] == '^') {
	strncpy(mylevelexp, levelexp+1, strlen(levelexp)-1); 
	mylevelexp[strlen(levelexp)-1] = '\0';
    }
    else {
	strncpy(mylevelexp, levelexp, strlen(levelexp));
	mylevelexp[strlen(levelexp)] = '\0';
    }

    if(mylevelexp[strlen(mylevelexp)] == '$') {
	match_exact = 1;
	mylevelexp[strlen(mylevelexp)] = '\0';
    }
    else
	match_exact = 0;

    if((dash = strchr(mylevelexp,'-'))) {
	if(match_exact == 1) {
	    error(_("Illegal level expression %s"),levelexp);
	    /*NOTREACHED*/
	}
	len = (size_t)(dash - mylevelexp);
	len_suffix = strlen(dash) - 1;
	len_prefix = len - len_suffix;

	dash++;
	strncpy(lowend, mylevelexp, len);
	lowend[len] = '\0';
	strncpy(highend, mylevelexp, len_prefix);
	strncpy(&(highend[len_prefix]), dash, len_suffix);
	highend[len] = '\0';
	return ((strncmp(level, lowend, strlen(lowend)) >= 0) &&
		(strncmp(level, highend , strlen(highend))  <= 0));
    }
    else {
	if(match_exact == 1) {
	    return (strcmp(level, mylevelexp) == 0);
	}
	else {
	    return (strncmp(level, mylevelexp, strlen(mylevelexp)) == 0);
	}
    }
}
