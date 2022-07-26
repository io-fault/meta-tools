#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <stdbool.h>
#include <clang-c/CXString.h>

#define chrcmp(I, C) (*((char *) I) == C)
#define quote(CSTR) "\x22" CSTR "\x22"
#define COMMA "\\" "u002c"
#define QUOTE "\\" "u0022"
#define ESCAPE "\\" "u005c"
#define TAB "\\" "t"

int
print_attribute(FILE *fp, char *attrid, char *str)
{
	if (str == NULL)
		return(1);

	fprintf(fp, quote("%s") ":" quote("%s") ",", attrid, str);
	return(0);
}

int
print_attributes_open(FILE *fp)
{
	fputs("{", fp);
	return(0);
}

int
print_attributes_close(FILE *fp)
{
	fputs("}", fp);
	return(0);
}

int
print_number_attribute(FILE *fp, char *attrid, unsigned long n)
{
	fprintf(fp, quote("%s") ":%lu,", attrid, n);
	return(0);
}

int
print_number(FILE *fp, char *attrid, unsigned long n)
{
	fprintf(fp, "%lu,", n);
	return(0);
}

int
print_expression_open(FILE *fp, unsigned long ln, unsigned long cn)
{
	fprintf(fp, "\n\t[[%lu,%lu]", ln, cn);
	return(0);
}

int
print_expression_node(FILE *fp, const char *ntype, unsigned long ln, unsigned long cn)
{
	fprintf(fp, ",[" quote("%s") ",%lu,%lu]", ntype, ln, cn);
	return(0);
}

int
print_expression_close(FILE *fp)
{
	fputs("],", fp);
	return(0);
}

int
print_string_attribute(FILE *fp, char *attrid, CXString cx)
{
	int r;
	const char *s;

	s = clang_getCString(cx);
	r = print_attribute(fp, attrid, (char *) s);
	clang_disposeString(cx);

	return(r);
}

int
print_string(FILE *fp, char *string, int pcount)
{
	if (pcount)
		fprintf(fp, "," quote("%s"), string);
	else
		fprintf(fp, quote("%s"), string);

	return(0);
}

int
print_identifier(FILE *fp, char *str)
{
	return print_attribute(fp, "identifier", str);
}

unsigned long
skip(intptr_t ip, unsigned long offset)
{
	/* Skip leading decoration if any */
	if (chrcmp(ip+offset, '/') && chrcmp(ip+offset+1, '/'))
	{
		offset += 2;
		if (chrcmp(ip+offset, ' '))
			++offset;
	}
	else if (chrcmp(ip+offset, ' ') && chrcmp(ip+offset+1, '*') && chrcmp(ip+offset+2, ' '))
	{
		offset += 3;
	}
	else if (chrcmp(ip+offset, '*') && chrcmp(ip+offset+1, ' '))
	{
		offset += 2;
	}

	return(offset);
}

/**
	// Recognize retained indentation and overwrite leading decorations.
*/
unsigned long
rewrite(intptr_t ip, unsigned long offset, unsigned long *out)
{
	char *str = (char *) ip;
	unsigned long start, edge, i, delta;

	start = offset;
	while (chrcmp(ip+start, '\t'))
		++start;

	edge = skip(ip, start);

	/* Rewrite everything as tabs. */
	for (i = start; i < edge; ++i)
		str[i] = '\t';

	delta = start - offset;
	edge = edge - delta;

	if (delta > 0)
		*out = edge - 1;
	else
		*out = edge;

	return(edge);
}

/**
	// Print comment content skipping common indentation and common decorations.
*/
int
print_text(FILE *fp, char *str, bool skip_last)
{
	intptr_t ip = (intptr_t) str;
	intptr_t eol = NULL;
	unsigned long x = 0, y = 0;
	unsigned long il = 0;
	bool single = true;

	/* Align at EOL. */
	if (strncmp("/**", str, 3) == 0)
	{
		x += 3;
		while (!chrcmp(ip+x, '\0'))
		{
			if (chrcmp(ip+x, '\n'))
			{
				++x;
				single = false;
				break;
			}

			++x;
		}
	}

	/* Calculate indentation level from second line. */
	if (single)
	{
		while (chrcmp(ip+x, ' '))
		{
			++x;
		}
	}
	else
	{
		while (chrcmp(ip+x, '\t'))
		{
			++il;
			++x;
		}

		x = skip(ip, x);
	}

	y = x;
	while (!chrcmp(ip+y, '\0'))
	{
		/*
			// ip+y guaranteed not to extend beyond &str.
		*/
		switch (str[y])
		{
			/* JSON Comma Escape */
			case ',':
				fwrite((char *) ip+x, 1, y-x, fp);
				fputs(COMMA, fp);
				x = y+1;
			break;

			/* JSON quotation escape */
			case '"':
				fwrite((char *) ip+x, 1, y-x, fp);
				fputs(QUOTE, fp);
				x = y+1;
			break;

			/* JSON backslash escape */
			case '\\':
				fwrite((char *) ip+x, 1, y-x, fp);
				fputs(ESCAPE, fp);
				x = y+1;
			break;

			/* Tab */
			case '\t':
				fwrite((char *) ip+x, 1, y-x, fp);
				fputs(TAB, fp);
				x = y+1;
			break;

			/* Line Segmentation and Indentation Control */
			case '\n':
			{
				unsigned long i = il;

				fwrite((char *) ip+x, 1, y-x, fp);
				fputs(quote(","), fp);
				x = y+1;

				while (chrcmp(ip+x, '\n'))
				{
					/* Successive newlines */
					fputs(quote(","), fp);
					++x;
					++y;
				}

				/* Skip leading common indentation. */
				while (i)
				{
					if (!chrcmp(ip+x, '\t'))
						break;

					--i;
					++x;
				}

				/* Skip leading decoration if any and rewrite indentation. */
				x = rewrite(ip, x, &y);
			}
			break;

			default:
				/* writes deferred until escape or newline */
			break;
		}

		y++;
	}

	if (single || !skip_last)
	{
		fwrite((char *) ip+x, 1, y-x, fp);
		return(1);
	}

	return(0);
}

int
print_open(FILE *fp, char *eid)
{
	fprintf(fp, "[" quote("%s") ",", eid);
	return(0);
}

int
print_open_empty(FILE *fp, char *eid)
{
	fprintf(fp, "[" quote("%s") ",[],", eid);
	return(0);
}

int
print_enter(FILE *fp)
{
	fprintf(fp, "[");
	return(0);
}

int
print_exit(FILE *fp)
{
	fputs("],", fp);
	return(0);
}

int
print_exit_final(FILE *fp)
{
	fputs("]", fp);
	return(0);
}

int
print_close_empty(FILE *fp, char *eid)
{
	fputs("],", fp);
	return(0);
}

int
print_close(FILE *fp, char *eid)
{
	fputs("],", fp);
	return(0);
}

int
print_close_final(FILE *fp, char *eid)
{
	fputs("]", fp);
	return(0);
}

int
print_close_no_attributes(FILE *fp, char *eid)
{
	fputs("{}],", fp);
	return(0);
}
