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
#include "match.h"
#include <regex.h>

static int match_word(const char *glob, const char *word, const char separator);
static char *tar_to_regex(const char *glob);

/*
 * REGEX MATCHING FUNCTIONS
 */

/*
 * Define a specific type to hold error messages in case regex compile/matching
 * fails
 */

typedef char regex_errbuf[STR_SIZE];

/*
 * Validate one regular expression. If the regex is invalid, copy the error
 * message into the supplied regex_errbuf pointer. Also, we want to know whether
 * flags should include REG_NEWLINE (See regcomp(3) for details). Since this is
 * the more frequent case, add REG_NEWLINE to the default flags, and remove it
 * only if match_newline is set to FALSE.
 */

static gboolean do_validate_regex(const char *str, regex_t *regex,
	regex_errbuf *errbuf, gboolean match_newline)
{
	int flags = REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
	int result;

	if (!match_newline)
		CLR(flags, REG_NEWLINE);

	result = regcomp(regex, str, flags);

	if (!result)
		return TRUE;

	regerror(result, regex, *errbuf, SIZEOF(*errbuf));
	return FALSE;
}

/*
 * See if a string matches a regular expression. Return one of MATCH_* defined
 * below. If, for some reason, regexec() returns something other than not 0 or
 * REG_NOMATCH, return MATCH_ERROR and print the error message in the supplied
 * regex_errbuf.
 */

#define MATCH_OK (1)
#define MATCH_NONE (0)
#define MATCH_ERROR (-1)

static int try_match(regex_t *regex, const char *str,
    regex_errbuf *errbuf)
{
    int result = regexec(regex, str, 0, 0, 0);

    switch(result) {
        case 0:
            return MATCH_OK;
        case REG_NOMATCH:
            return MATCH_NONE;
        /* Fall through: something went really wrong */
    }

    regerror(result, regex, *errbuf, SIZEOF(*errbuf));
    return MATCH_ERROR;
}

char *
validate_regexp(
    const char *	regex)
{
    regex_t regc;
    static regex_errbuf errmsg;
    gboolean valid;

    valid = do_validate_regex(regex, &regc, &errmsg, TRUE);

    regfree(&regc);
    return (valid) ? NULL : errmsg;
}

char *
clean_regex(
    const char *	str,
    gboolean		anchor)
{
    char *result;
    int j;
    size_t i;
    result = alloc(2*strlen(str)+3);

    j = 0;
    if (anchor)
	result[j++] = '^';
    for(i=0;i<strlen(str);i++) {
	if(!isalnum((int)str[i]))
	    result[j++]='\\';
	result[j++]=str[i];
    }
    if (anchor)
	result[j++] = '$';
    result[j] = '\0';
    return result;
}

/*
 * Check whether a given character should be escaped (that is, prepended with a
 * backslash), EXCEPT for one character.
 */

static gboolean should_be_escaped_except(char c, char not_this_one)
{
    if (c == not_this_one)
        return FALSE;

    switch (c) {
        case '\\':
        case '^':
        case '$':
        case '?':
        case '*':
        case '[':
        case ']':
        case '.':
        case '/':
            return TRUE;
    }

    return FALSE;
}

/*
 * Take a disk/host expression and turn it into a full-blown amglob (with
 * start and end anchors) following rules in amanda-match(7). The not_this_one
 * argument represents a character which is NOT meant to be special in this
 * case: '/' for disks and '.' for hosts.
 */

static char *full_amglob_from_expression(const char *str, char not_this_one)
{
    const char *src;
    char *result, *dst;

    result = alloc(2 * strlen(str) + 3);
    dst = result;

    *(dst++) = '^';

    for (src = str; *src; src++) {
        if (should_be_escaped_except(*src, not_this_one))
            *(dst++) = '\\';
        *(dst++) = *src;
    }

    *(dst++) = '$';
    *dst = '\0';
    return result;
}

char *
make_exact_host_expression(
    const char *	host)
{
    return full_amglob_from_expression(host, '.');
}

char *
make_exact_disk_expression(
    const char *	disk)
{
    return full_amglob_from_expression(disk, '/');
}

int do_match(const char *regex, const char *str, gboolean match_newline)
{
    regex_t regc;
    int result;
    regex_errbuf errmsg;
    gboolean ok;

    ok = do_validate_regex(regex, &regc, &errmsg, match_newline);

    if (!ok)
        error(_("regex \"%s\": %s"), regex, errmsg);
        /*NOTREACHED*/

    result = try_match(&regc, str, &errmsg);

    if (result == MATCH_ERROR)
        error(_("regex \"%s\": %s"), regex, errmsg);
        /*NOTREACHED*/

    regfree(&regc);

    return result;
}

char *
validate_glob(
    const char *	glob)
{
    char *regex, *ret = NULL;
    regex_t regc;
    static regex_errbuf errmsg;

    regex = glob_to_regex(glob);

    if (!do_validate_regex(regex, &regc, &errmsg, TRUE))
        ret = errmsg;

    regfree(&regc);
    amfree(regex);
    return ret;
}

int
match_glob(
    const char *	glob,
    const char *	str)
{
    char *regex;
    regex_t regc;
    int result;
    regex_errbuf errmsg;
    gboolean ok;

    regex = glob_to_regex(glob);
    ok = do_validate_regex(regex, &regc, &errmsg, TRUE);

    if (!ok)
        error(_("glob \"%s\" -> regex \"%s\": %s"), glob, regex, errmsg);
        /*NOTREACHED*/

    result = try_match(&regc, str, &errmsg);

    if (result == MATCH_ERROR)
        error(_("glob \"%s\" -> regex \"%s\": %s"), glob, regex, errmsg);
        /*NOTREACHED*/

    regfree(&regc);
    amfree(regex);

    return result;
}

/*
 * Macro to tell whether a character is a regex metacharacter. Note that '*'
 * and '?' are NOT included: they are themselves special in globs.
 */

#define IS_REGEX_META(c) ( \
    (c) == '.' || (c) == '(' || (c) == ')' || (c) == '{' || (c) == '}' || \
    (c) == '+' || (c) == '^' || (c) == '$' || (c) == '|' \
)

/*
 * EXPANDING A MATCH TO A REGEX (as per amanda-match(7))
 *
 * The function at the code of this operation is amglob_to_regex(). It
 * takes three arguments: the string to convert, a substitution table and a
 * worst-case expansion.
 *
 * The substitution table, defined right below, is used to replace particular
 * string positions and/or characters. Its fields are:
 * - begin: what the beginnin of the string should be replaced with;
 * - end: what the end of the string should be replaced with;
 * - question_mark: what the question mark ('?') should be replaced with;
 * - star: what the star ('*') should be replaced with;
 * - double_star: what two consecutive stars should be replaced with.
 *
 * Note that apart from double_star, ALL OTHER FIELDS MUST NOT BE NULL
 */

struct subst_table {
    const char *begin;
    const char *end;
    const char *question_mark;
    const char *star;
    const char *double_star;
};

static char *amglob_to_regex(const char *str, struct subst_table *table,
    size_t worst_case)
{
    const char *src;
    char *result, *dst;
    char c;

    /*
     * There are two particular cases when building a regex out of a glob:
     * character classes (anything inside [...] or [!...] and quotes (anything
     * preceded by a backslash). We start with none being true.
     */

    gboolean in_character_class = FALSE, in_quote = FALSE;

    /*
     * Allocate enough space for our string. At worst, the allocated space is
     * the length of the following:
     * - beginning of regex;
     * - size of original string multiplied by worst-case expansion;
     * - end of regex;
     * - final 0.
     */

    result = alloc(strlen(table->begin) + strlen(str) * worst_case
        + strlen(table->end) + 1);

    /*
     * Start by copying the beginning of the regex...
     */

    dst = g_stpcpy(result, table->begin);

    /*
     * ... Now to the meat of it.
     */

    for (src = str; *src; src++) {
        c = *src;

        /*
         * First, check that we're in a character class: each and every
         * character can be copied as is. We only need to be careful is the
         * character is a closing bracket: it will end the character class IF
         * AND ONLY IF it is not preceded by a backslash.
         */

        if (in_character_class) {
            in_character_class = ((c != ']') || (*(src - 1) == '\\'));
            goto straight_copy;
        }

        /*
         * Are we in a quote? If yes, it is really simple: copy the current
         * character, close the quote, the end.
         */

        if (in_quote) {
            in_quote = FALSE;
            goto straight_copy;
        }

        /*
         * The only thing left to handle now is the "normal" case: we are not in
         * a character class nor in a quote.
         */

        if (c == '\\') {
            /*
             * Backslash: append it, and open a new quote.
             */
            in_quote = TRUE;
            goto straight_copy;
        } else if (c == '[') {
            /*
             * Opening bracket: the beginning of a character class.
             *
             * Look ahead the next character: if it's an exclamation mark, then
             * this is a complemented character class; append a caret to make
             * the result string regex-friendly, and forward one character in
             * advance.
             */
            *dst++ = c;
            in_character_class = TRUE;
            if (*(src + 1) == '!') {
                *dst++ = '^';
                src++;
            }
        } else if (IS_REGEX_META(c)) {
            /*
             * Regex metacharacter (except for ? and *, see below): append a
             * backslash, and then the character itself.
             */
            *dst++ = '\\';
            goto straight_copy;
        } else if (c == '?')
            /*
             * Question mark: take the subsitution string out of our subst_table
             * and append it to the string.
             */
            dst = g_stpcpy(dst, table->question_mark);
        else if (c == '*') {
            /*
             * Star: append the subsitution string found in our subst_table.
             * However, look forward the next character: if it's yet another
             * star, then see if there is a substitution string for the double
             * star and append this one instead.
             *
             * FIXME: this means that two consecutive stars in a glob string
             * where there is no substition for double_star can lead to
             * exponential regex execution time: consider [^/]*[^/]*.
             */
            const char *p = table->star;
            if (*(src + 1) == '*' && table->double_star) {
                src++;
                p = table->double_star;
            }
            dst = g_stpcpy(dst, p);
        } else {
            /*
             * Any other character: append each time.
             */
straight_copy:
            *dst++ = c;
        }
    }

    /*
     * Done, now append the end, ONLY if we are not in a quote - a lone
     * backslash at the end of a glob is illegal, just leave it as it, it will
     * make the regex compile fail.
     */

    if (!in_quote)
        dst = g_stpcpy(dst, table->end);
    /*
     * Finalize, return.
     */

    *dst = '\0';
    return result;
}

static struct subst_table glob_subst_stable = {
    "^", /* begin */
    "$", /* end */
    "[^/]", /* question_mark */
    "[^/]*", /* star */
    NULL /* double_star */
};

static size_t glob_worst_case = 5; /* star */

char *
glob_to_regex(
    const char *	glob)
{
    return amglob_to_regex(glob, &glob_subst_stable, glob_worst_case);
}

int
match_tar(
    const char *	glob,
    const char *	str)
{
    char *regex;
    regex_t regc;
    int result;
    regex_errbuf errmsg;
    gboolean ok;

    regex = tar_to_regex(glob);
    ok = do_validate_regex(regex, &regc, &errmsg, TRUE);

    if (!ok)
        error(_("glob \"%s\" -> regex \"%s\": %s"), glob, regex, errmsg);
        /*NOTREACHED*/

    result = try_match(&regc, str, &errmsg);

    if (result == MATCH_ERROR)
        error(_("glob \"%s\" -> regex \"%s\": %s"), glob, regex, errmsg);
        /*NOTREACHED*/

    regfree(&regc);
    amfree(regex);

    return result;
}

static struct subst_table tar_subst_stable = {
    "(^|/)", /* begin */
    "($|/)", /* end */
    "[^/]", /* question_mark */
    ".*", /* star */
    NULL /* double_star */
};

static size_t tar_worst_case = 5; /* begin or end */

static char *
tar_to_regex(
    const char *	glob)
{
    return amglob_to_regex(glob, &tar_subst_stable, tar_worst_case);
}

/*
 * Two utility functions used by match_disk() below: they are used to convert a
 * disk and glob from Windows expressed paths (backslashes) into Unix paths
 * (slashes).
 *
 * Note: the resulting string is dynamically allocated, it is up to the caller
 * to free it.
 *
 * Note 2: UNC in convert_unc_to_unix stands for Uniform Naming Convention.
 */

static char *convert_unc_to_unix(const char *unc)
{
    const char *src;
    char *result, *dst;
    result = alloc(strlen(unc) + 1);
    dst = result;

    for (src = unc; *src; src++)
        *(dst++) = (*src == '\\') ? '/' : *src;

    *dst = '\0';
    return result;
}

static char *convert_winglob_to_unix(const char *glob)
{
    const char *src;
    char *result, *dst;
    result = alloc(strlen(glob) + 1);
    dst = result;

    for (src = glob; *src; src++) {
        if (*src == '\\' && *(src + 1) == '\\') {
            *(dst++) = '/';
            src++;
            continue;
        }
        *(dst++) = *src;
    }
    *dst = '\0';
    return result;
}

/*
 * Check whether a glob passed as an argument to match_word() only looks for the
 * separator
 */

static gboolean glob_is_separator_only(const char *glob, char sep) {
    size_t len = strlen(glob);
    const char len2_1[] = { '^', sep , 0 }, len2_2[] = { sep, '$', 0 },
        len3[] = { '^', sep, '$', 0 };

    switch (len) {
        case 1:
            return (*glob == sep);
        case 2:
            return !(strcmp(glob, len2_1) && strcmp(glob, len2_2));
        case 3:
            return !strcmp(glob, len3);
        default:
            return FALSE;
    }
}

static int
match_word(
    const char *	glob,
    const char *	word,
    const char		separator)
{
    char *regex;
    char *dst;
    size_t  len;
    size_t  lenword;
    char *nword;
    char *nglob;
    const char *src;
    int ret;

    lenword = strlen(word);
    nword = (char *)alloc(lenword + 3);

    dst = nword;
    src = word;
    if(lenword == 1 && *src == separator) {
	*dst++ = separator;
	*dst++ = separator;
    }
    else {
	if(*src != separator)
	    *dst++ = separator;
	while(*src != '\0')
	    *dst++ = *src++;
	if(*(dst-1) != separator)
	    *dst++ = separator;
    }
    *dst = '\0';

    len = strlen(glob);
    nglob = stralloc(glob);

    if(glob_is_separator_only(nglob, separator)) {
        regex = alloc(7); /* Length of what is written below plus '\0' */
        dst = regex;
	*dst++ = '^';
	*dst++ = '\\';
	*dst++ = separator;
	*dst++ = '\\';
	*dst++ = separator;
	*dst++ = '$';
        *dst = '\0';
    } else {
        /*
         * Unlike what happens for tar and disk expressions, here the
         * substitution table needs to be dynamically allocated. When we enter
         * here, we know what the expansions will be for the question mark, the
         * star and the double star, and also the worst case expansion. We
         * calculate the begin and end expansions below.
         */

#define MATCHWORD_STAR_EXPANSION(c) (const char []) { \
    '[', '^', (c), ']', '*', 0 \
}
#define MATCHWORD_QUESTIONMARK_EXPANSION(c) (const char []) { \
    '[', '^', (c), ']', 0 \
}
#define MATCHWORD_DOUBLESTAR_EXPANSION ".*"

        struct subst_table table;
        size_t worst_case = 5;
        const char *begin, *end;
        char *p, *g = nglob;

        /*
         * Calculate the beginning of the regex:
         * - by default, it is an unanchored separator;
         * - if the glob begins with a caret, make that an anchored separator,
         *   and increment g appropriately;
         * - if it begins with a separator, make it the empty string.
         */

        p = nglob;

#define REGEX_BEGIN_FULL(c) (const char[]) { '^', '\\', (c), 0 }
#define REGEX_BEGIN_NOANCHOR(c) (const char[]) { '\\', (c), 0 }
#define REGEX_BEGIN_ANCHORONLY "^" /* Unused, but defined for consistency */
#define REGEX_BEGIN_EMPTY ""

        begin = REGEX_BEGIN_NOANCHOR(separator);

        if (*p == '^') {
            begin = REGEX_BEGIN_FULL(separator);
            p++, g++;
            if (*p == separator)
                g++;
        } else if (*p == separator)
            begin = REGEX_BEGIN_EMPTY;

        /*
         * Calculate the end of the regex:
         * - an unanchored separator by default;
         * - if the last character is a backslash or the separator itself, it
         *   should be the empty string;
         * - if it is a dollar sign, overwrite it with 0 and look at the
         *   character before it: if it is the separator, only anchor at the
         *   end, otherwise, add a separator before the anchor.
         */

        p = &(nglob[strlen(nglob) - 1]);

#define REGEX_END_FULL(c) (const char[]) { '\\', (c), '$', 0 }
#define REGEX_END_NOANCHOR(c) REGEX_BEGIN_NOANCHOR(c)
#define REGEX_END_ANCHORONLY "$"
#define REGEX_END_EMPTY REGEX_BEGIN_EMPTY

        end = REGEX_END_NOANCHOR(separator);

        if (*p == '\\' || *p == separator)
            end = REGEX_END_EMPTY;
        else if (*p == '$') {
            char prev = *(p - 1);
            *p = '\0';
            if (prev == separator)
                end = REGEX_END_ANCHORONLY;
            else
                end = REGEX_END_FULL(separator);
        }

        /*
         * Fill in our substitution table and generate the regex
         */

        table.begin = begin;
        table.end = end;
        table.question_mark = MATCHWORD_QUESTIONMARK_EXPANSION(separator);
        table.star = MATCHWORD_STAR_EXPANSION(separator);
        table.double_star = MATCHWORD_DOUBLESTAR_EXPANSION;

        regex = amglob_to_regex(g, &table, worst_case);
    }

    ret = do_match(regex, nword, TRUE);

    amfree(nword);
    amfree(nglob);
    amfree(regex);

    return ret;
}


int
match_host(
    const char *	glob,
    const char *	host)
{
    char *lglob, *lhost;
    int ret;

    
    lglob = g_ascii_strdown(glob, -1);
    lhost = g_ascii_strdown(host, -1);

    ret = match_word(lglob, lhost, '.');

    amfree(lglob);
    amfree(lhost);
    return ret;
}


int
match_disk(
    const char *	glob,
    const char *	disk)
{
    char *glob2 = NULL, *disk2 = NULL;
    const char *g = glob, *d = disk;
    int result;

    /*
     * Check whether our disk potentially refers to a Windows share (the first
     * two characters are '\' and there is no / in the word at all): if yes,
     * convert all double backslashes to slashes in the glob, and simple
     * backslashes into slashes in the disk, and pass these new strings as
     * arguments instead of the originals.
     */
    gboolean windows_share = !(strncmp(disk, "\\\\", 2) || strchr(disk, '/'));

    if (windows_share) {
        glob2 = convert_winglob_to_unix(glob);
        disk2 = convert_unc_to_unix(disk);
        g = (const char *) glob2;
        d = (const char *) disk2;
    }

    result = match_word(g, d, '/');

    /*
     * We can amfree(NULL), so this is "safe"
     */
    amfree(glob2);
    amfree(disk2);

    return result;
}

static int
alldigits(
    const char *str)
{
    while (*str) {
	if (!isdigit((int)*(str++)))
	    return 0;
    }
    return 1;
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
	goto illegal;
    }
   
    /* strip and ignore an initial "^" */
    if(dateexp[0] == '^') {
	strncpy(mydateexp, dateexp+1, sizeof(mydateexp)-1);
	mydateexp[sizeof(mydateexp)-1] = '\0';
    }
    else {
	strncpy(mydateexp, dateexp, sizeof(mydateexp)-1);
	mydateexp[sizeof(mydateexp)-1] = '\0';
    }

    if(mydateexp[strlen(mydateexp)-1] == '$') {
	match_exact = 1;
	mydateexp[strlen(mydateexp)-1] = '\0';	/* strip the trailing $ */
    }
    else
	match_exact = 0;

    /* a single dash represents a date range */
    if((dash = strchr(mydateexp,'-'))) {
	if(match_exact == 1 || strchr(dash+1, '-')) {
	    goto illegal;
	}

	/* format: XXXYYYY-ZZZZ, indicating dates XXXYYYY to XXXZZZZ */

	len = (size_t)(dash - mydateexp);   /* length of XXXYYYY */
	len_suffix = strlen(dash) - 1;	/* length of ZZZZ */
	if (len_suffix > len) goto illegal;
	len_prefix = len - len_suffix; /* length of XXX */

	dash++;

	strncpy(firstdate, mydateexp, len);
	firstdate[len] = '\0';
	strncpy(lastdate, mydateexp, len_prefix);
	strncpy(&(lastdate[len_prefix]), dash, len_suffix);
	lastdate[len] = '\0';
	if (!alldigits(firstdate) || !alldigits(lastdate))
	    goto illegal;
	if (strncmp(firstdate, lastdate, strlen(firstdate)) > 0)
	    goto illegal;
	return ((strncmp(datestamp, firstdate, strlen(firstdate)) >= 0) &&
		(strncmp(datestamp, lastdate , strlen(lastdate))  <= 0));
    }
    else {
	if (!alldigits(mydateexp))
	    goto illegal;
	if(match_exact == 1) {
	    return (strcmp(datestamp, mydateexp) == 0);
	}
	else {
	    return (strncmp(datestamp, mydateexp, strlen(mydateexp)) == 0);
	}
    }
illegal:
	error(_("Illegal datestamp expression %s"),dateexp);
	/*NOTREACHED*/
}


int
match_level(
    const char *	levelexp,
    const char *	level)
{
    char *dash;
    long int low, hi, level_i;
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

    if(mylevelexp[strlen(mylevelexp)-1] == '$') {
	match_exact = 1;
	mylevelexp[strlen(mylevelexp)-1] = '\0';
    }
    else
	match_exact = 0;

    if((dash = strchr(mylevelexp,'-'))) {
	if(match_exact == 1) {
            goto illegal;
	}

        *dash = '\0';
        if (!alldigits(mylevelexp) || !alldigits(dash+1)) goto illegal;

        errno = 0;
        low = strtol(mylevelexp, (char **) NULL, 10);
        if (errno) goto illegal;
        hi = strtol(dash+1, (char **) NULL, 10);
        if (errno) goto illegal;
        level_i = strtol(level, (char **) NULL, 10);
        if (errno) goto illegal;

	return ((level_i >= low) && (level_i <= hi));
    }
    else {
	if (!alldigits(mylevelexp)) goto illegal;
	if(match_exact == 1) {
	    return (strcmp(level, mylevelexp) == 0);
	}
	else {
	    return (strncmp(level, mylevelexp, strlen(mylevelexp)) == 0);
	}
    }
illegal:
    error(_("Illegal level expression %s"),levelexp);
    /*NOTREACHED*/
}
