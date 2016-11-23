﻿/*	Copyright 2012, 2016 Christoph Gärtner
	Distributed under the Boost Software License, Version 1.0
*/

#include "entities.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define UNICODE_MAX 0x10FFFFul

static const char *const NAMED_ENTITIES[][2] = {
	{ "AElig;", "\xC3\x86" },
	{ "Aacute;", "\xC3\x81" },
	{ "Acirc;", "\xC3\x82" },
	{ "Agrave;", "\xC3\x80" },
	{ "Alpha;", "\xCE\x91" },
	{ "Aring;", "\xC3\x85" },
	{ "Atilde;", "\xC3\x83" },
	{ "Auml;", "\xC3\x84" },
	{ "Beta;", "\xCE\x92" },
	{ "Ccedil;", "\xC3\x87" },
	{ "Chi;", "\xCE\xA7" },
	{ "Dagger;", "\xE2\x80\xA1" },
	{ "Delta;", "\xCE\x94" },
	{ "ETH;", "\xC3\x90" },
	{ "Eacute;", "\xC3\x89" },
	{ "Ecirc;", "\xC3\x8A" },
	{ "Egrave;", "\xC3\x88" },
	{ "Epsilon;", "\xCE\x95" },
	{ "Eta;", "\xCE\x97" },
	{ "Euml;", "\xC3\x8B" },
	{ "Gamma;", "\xCE\x93" },
	{ "Iacute;", "\xC3\x8D" },
	{ "Icirc;", "\xC3\x8E" },
	{ "Igrave;", "\xC3\x8C" },
	{ "Iota;", "\xCE\x99" },
	{ "Iuml;", "\xC3\x8F" },
	{ "Kappa;", "\xCE\x9A" },
	{ "Lambda;", "\xCE\x9B" },
	{ "Mu;", "\xCE\x9C" },
	{ "Ntilde;", "\xC3\x91" },
	{ "Nu;", "\xCE\x9D" },
	{ "OElig;", "\xC5\x92" },
	{ "Oacute;", "\xC3\x93" },
	{ "Ocirc;", "\xC3\x94" },
	{ "Ograve;", "\xC3\x92" },
	{ "Omega;", "\xCE\xA9" },
	{ "Omicron;", "\xCE\x9F" },
	{ "Oslash;", "\xC3\x98" },
	{ "Otilde;", "\xC3\x95" },
	{ "Ouml;", "\xC3\x96" },
	{ "Phi;", "\xCE\xA6" },
	{ "Pi;", "\xCE\xA0" },
	{ "Prime;", "\xE2\x80\xB3" },
	{ "Psi;", "\xCE\xA8" },
	{ "Rho;", "\xCE\xA1" },
	{ "Scaron;", "\xC5\xA0" },
	{ "Sigma;", "\xCE\xA3" },
	{ "THORN;", "\xC3\x9E" },
	{ "Tau;", "\xCE\xA4" },
	{ "Theta;", "\xCE\x98" },
	{ "Uacute;", "\xC3\x9A" },
	{ "Ucirc;", "\xC3\x9B" },
	{ "Ugrave;", "\xC3\x99" },
	{ "Upsilon;", "\xCE\xA5" },
	{ "Uuml;", "\xC3\x9C" },
	{ "Xi;", "\xCE\x9E" },
	{ "Yacute;", "\xC3\x9D" },
	{ "Yuml;", "\xC5\xB8" },
	{ "Zeta;", "\xCE\x96" },
	{ "aacute;", "\xC3\xA1" },
	{ "acirc;", "\xC3\xA2" },
	{ "acute;", "\xC2\xB4" },
	{ "aelig;", "\xC3\xA6" },
	{ "agrave;", "\xC3\xA0" },
	{ "alefsym;", "\xE2\x84\xB5" },
	{ "alpha;", "\xCE\xB1" },
	{ "amp;", "&" },
	{ "and;", "\xE2\x88\xA7" },
	{ "ang;", "\xE2\x88\xA0" },
	{ "apos;", "'" },
	{ "aring;", "\xC3\xA5" },
	{ "asymp;", "\xE2\x89\x88" },
	{ "atilde;", "\xC3\xA3" },
	{ "auml;", "\xC3\xA4" },
	{ "bdquo;", "\xE2\x80\x9E" },
	{ "beta;", "\xCE\xB2" },
	{ "brvbar;", "\xC2\xA6" },
	{ "bull;", "\xE2\x80\xA2" },
	{ "cap;", "\xE2\x88\xA9" },
	{ "ccedil;", "\xC3\xA7" },
	{ "cedil;", "\xC2\xB8" },
	{ "cent;", "\xC2\xA2" },
	{ "chi;", "\xCF\x87" },
	{ "circ;", "\xCB\x86" },
	{ "clubs;", "\xE2\x99\xA3" },
	{ "cong;", "\xE2\x89\x85" },
	{ "copy;", "\xC2\xA9" },
	{ "crarr;", "\xE2\x86\xB5" },
	{ "cup;", "\xE2\x88\xAA" },
	{ "curren;", "\xC2\xA4" },
	{ "dArr;", "\xE2\x87\x93" },
	{ "dagger;", "\xE2\x80\xA0" },
	{ "darr;", "\xE2\x86\x93" },
	{ "deg;", "\xC2\xB0" },
	{ "delta;", "\xCE\xB4" },
	{ "diams;", "\xE2\x99\xA6" },
	{ "divide;", "\xC3\xB7" },
	{ "eacute;", "\xC3\xA9" },
	{ "ecirc;", "\xC3\xAA" },
	{ "egrave;", "\xC3\xA8" },
	{ "empty;", "\xE2\x88\x85" },
	{ "emsp;", "\xE2\x80\x83" },
	{ "ensp;", "\xE2\x80\x82" },
	{ "epsilon;", "\xCE\xB5" },
	{ "equiv;", "\xE2\x89\xA1" },
	{ "eta;", "\xCE\xB7" },
	{ "eth;", "\xC3\xB0" },
	{ "euml;", "\xC3\xAB" },
	{ "euro;", "\xE2\x82\xAC" },
	{ "exist;", "\xE2\x88\x83" },
	{ "fnof;", "\xC6\x92" },
	{ "forall;", "\xE2\x88\x80" },
	{ "frac12;", "\xC2\xBD" },
	{ "frac14;", "\xC2\xBC" },
	{ "frac34;", "\xC2\xBE" },
	{ "frasl;", "\xE2\x81\x84" },
	{ "gamma;", "\xCE\xB3" },
	{ "ge;", "\xE2\x89\xA5" },
	{ "gt;", ">" },
	{ "hArr;", "\xE2\x87\x94" },
	{ "harr;", "\xE2\x86\x94" },
	{ "hearts;", "\xE2\x99\xA5" },
	{ "hellip;", "\xE2\x80\xA6" },
	{ "iacute;", "\xC3\xAD" },
	{ "icirc;", "\xC3\xAE" },
	{ "iexcl;", "\xC2\xA1" },
	{ "igrave;", "\xC3\xAC" },
	{ "image;", "\xE2\x84\x91" },
	{ "infin;", "\xE2\x88\x9E" },
	{ "int;", "\xE2\x88\xAB" },
	{ "iota;", "\xCE\xB9" },
	{ "iquest;", "\xC2\xBF" },
	{ "isin;", "\xE2\x88\x88" },
	{ "iuml;", "\xC3\xAF" },
	{ "kappa;", "\xCE\xBA" },
	{ "lArr;", "\xE2\x87\x90" },
	{ "lambda;", "\xCE\xBB" },
	{ "lang;", "\xE3\x80\x88" },
	{ "laquo;", "\xC2\xAB" },
	{ "larr;", "\xE2\x86\x90" },
	{ "lceil;", "\xE2\x8C\x88" },
	{ "ldquo;", "\xE2\x80\x9C" },
	{ "le;", "\xE2\x89\xA4" },
	{ "lfloor;", "\xE2\x8C\x8A" },
	{ "lowast;", "\xE2\x88\x97" },
	{ "loz;", "\xE2\x97\x8A" },
	{ "lrm;", "\xE2\x80\x8E" },
	{ "lsaquo;", "\xE2\x80\xB9" },
	{ "lsquo;", "\xE2\x80\x98" },
	{ "lt;", "<" },
	{ "macr;", "\xC2\xAF" },
	{ "mdash;", "\xE2\x80\x94" },
	{ "micro;", "\xC2\xB5" },
	{ "middot;", "\xC2\xB7" },
	{ "minus;", "\xE2\x88\x92" },
	{ "mu;", "\xCE\xBC" },
	{ "nabla;", "\xE2\x88\x87" },
	{ "nbsp;", "  " },
	{ "ndash;", "\xE2\x80\x93" },
	{ "ne;", "\xE2\x89\xA0" },
	{ "ni;", "\xE2\x88\x8B" },
	{ "not;", "\xC2\xAC" },
	{ "notin;", "\xE2\x88\x89" },
	{ "nsub;", "\xE2\x8A\x84" },
	{ "ntilde;", "\xC3\xB1" },
	{ "nu;", "\xCE\xBD" },
	{ "oacute;", "\xC3\xB3" },
	{ "ocirc;", "\xC3\xB4" },
	{ "oelig;", "\xC5\x93" },
	{ "ograve;", "\xC3\xB2" },
	{ "oline;", "\xE2\x80\xBE" },
	{ "omega;", "\xCF\x89" },
	{ "omicron;", "\xCE\xBF" },
	{ "oplus;", "\xE2\x8A\x95" },
	{ "or;", "\xE2\x88\xA8" },
	{ "ordf;", "\xC2\xAA" },
	{ "ordm;", "\xC2\xBA" },
	{ "oslash;", "\xC3\xB8" },
	{ "otilde;", "\xC3\xB5" },
	{ "otimes;", "\xE2\x8A\x97" },
	{ "ouml;", "\xC3\xB6" },
	{ "para;", "\xC2\xB6" },
	{ "part;", "\xE2\x88\x82" },
	{ "permil;", "\xE2\x80\xB0" },
	{ "perp;", "\xE2\x8A\xA5" },
	{ "phi;", "\xCF\x86" },
	{ "pi;", "\xCF\x80" },
	{ "piv;", "\xCF\x96" },
	{ "plusmn;", "\xC2\xB1" },
	{ "pound;", "\xC2\xA3" },
	{ "prime;", "\xE2\x80\xB2" },
	{ "prod;", "\xE2\x88\x8F" },
	{ "prop;", "\xE2\x88\x9D" },
	{ "psi;", "\xCF\x88" },
	{ "quot;", "\"" },
	{ "rArr;", "\xE2\x87\x92" },
	{ "radic;", "\xE2\x88\x9A" },
	{ "rang;", "\xE3\x80\x89" },
	{ "raquo;", "\xC2\xBB" },
	{ "rarr;", "\xE2\x86\x92" },
	{ "rceil;", "\xE2\x8C\x89" },
	{ "rdquo;", "\xE2\x80\x9D" },
	{ "real;", "\xE2\x84\x9C" },
	{ "reg;", "\xC2\xAE" },
	{ "rfloor;", "\xE2\x8C\x8B" },
	{ "rho;", "\xCF\x81" },
	{ "rlm;", "\xE2\x80\x8F" },
	{ "rsaquo;", "\xE2\x80\xBA" },
	{ "rsquo;", "\xE2\x80\x99" },
	{ "sbquo;", "\xE2\x80\x9A" },
	{ "scaron;", "\xC5\xA1" },
	{ "sdot;", "\xE2\x8B\x85" },
	{ "sect;", "\xC2\xA7" },
	{ "shy;", "\xC2\xAD" },
	{ "sigma;", "\xCF\x83" },
	{ "sigmaf;", "\xCF\x82" },
	{ "sim;", "\xE2\x88\xBC" },
	{ "spades;", "\xE2\x99\xA0" },
	{ "sub;", "\xE2\x8A\x82" },
	{ "sube;", "\xE2\x8A\x86" },
	{ "sum;", "\xE2\x88\x91" },
	{ "sup1;", "\xC2\xB9" },
	{ "sup2;", "\xC2\xB2" },
	{ "sup3;", "\xC2\xB3" },
	{ "sup;", "\xE2\x8A\x83" },
	{ "supe;", "\xE2\x8A\x87" },
	{ "szlig;", "\xC3\x9F" },
	{ "tau;", "\xCF\x84" },
	{ "there4;", "\xE2\x88\xB4" },
	{ "theta;", "\xCE\xB8" },
	{ "thetasym;", "\xCF\x91" },
	{ "thinsp;", "\xE2\x80\x89" },
	{ "thorn;", "\xC3\xBE" },
	{ "tilde;", "\xCB\x9C" },
	{ "times;", "\xC3\x97" },
	{ "trade;", "\xE2\x84\xA2" },
	{ "uArr;", "\xE2\x87\x91" },
	{ "uacute;", "\xC3\xBA" },
	{ "uarr;", "\xE2\x86\x91" },
	{ "ucirc;", "\xC3\xBB" },
	{ "ugrave;", "\xC3\xB9" },
	{ "uml;", "\xC2\xA8" },
	{ "upsih;", "\xCF\x92" },
	{ "upsilon;", "\xCF\x85" },
	{ "uuml;", "\xC3\xBC" },
	{ "weierp;", "\xE2\x84\x98" },
	{ "xi;", "\xCE\xBE" },
	{ "yacute;", "\xC3\xBD" },
	{ "yen;", "\xC2\xA5" },
	{ "yuml;", "\xC3\xBF" },
	{ "zeta;", "\xCE\xB6" },
	{ "zwj;", "\xE2\x80\x8D" },
	{ "zwnj;", "\xE2\x80\x8C" }
};

static int cmp(const void *key, const void *value)
{
	return strncmp((const char *)key, *(const char *const *)value,
		strlen(*(const char *const *)value));
}

static const char *get_named_entity(const char *name)
{
	const char *const *entity = (const char *const *)bsearch(name,
		NAMED_ENTITIES, sizeof NAMED_ENTITIES / sizeof *NAMED_ENTITIES,
		sizeof *NAMED_ENTITIES, cmp);

	return entity ? entity[1] : NULL;
}

static size_t putc_utf8(unsigned long cp, char *buffer)
{
	unsigned char *bytes = (unsigned char *)buffer;

	if (cp <= 0x007Ful)
	{
		bytes[0] = (unsigned char)cp;
		return 1;
	}

	if (cp <= 0x07FFul)
	{
		bytes[1] = (unsigned char)((2 << 6) | (cp & 0x3F));
		bytes[0] = (unsigned char)((6 << 5) | (cp >> 6));
		return 2;
	}

	if (cp <= 0xFFFFul)
	{
		bytes[2] = (unsigned char)((2 << 6) | (cp & 0x3F));
		bytes[1] = (unsigned char)((2 << 6) | ((cp >> 6) & 0x3F));
		bytes[0] = (unsigned char)((14 << 4) | (cp >> 12));
		return 3;
	}

	if (cp <= 0x10FFFFul)
	{
		bytes[3] = (unsigned char)((2 << 6) | (cp & 0x3F));
		bytes[2] = (unsigned char)((2 << 6) | ((cp >> 6) & 0x3F));
		bytes[1] = (unsigned char)((2 << 6) | ((cp >> 12) & 0x3F));
		bytes[0] = (unsigned char)((30 << 3) | (cp >> 18));
		return 4;
	}

	return 0;
}

static bool parse_entity(
	const char *current, char **to, const char **from)
{
	const char *end = strchr(current, ';');
	if (!end) return 0;

	if (current[1] == '#')
	{
		char *tail = NULL;
		int errno_save = errno;
		bool hex = current[2] == 'x' || current[2] == 'X';

		errno = 0;
		unsigned long cp = strtoul(
			current + (hex ? 3 : 2), &tail, hex ? 16 : 10);

		bool fail = errno || tail != end || cp > UNICODE_MAX;
		errno = errno_save;
		if (fail) return 0;

		*to += putc_utf8(cp, *to);
		*from = end + 1;

		return 1;
	}
	else
	{
		const char *entity = get_named_entity(&current[1]);
		if (!entity) return 0;

		size_t len = strlen(entity);
		memcpy(*to, entity, len);

		*to += len;
		*from = end + 1;

		return 1;
	}
}

size_t decode_html_entities_utf8(char *dest, const char *src)
{
	if (!src) src = dest;

	char *to = dest;
	const char *from = src;

	for (const char *current; (current = strchr(from, '&'));)
	{
		memmove(to, from, (size_t)(current - from));
		to += current - from;

		if (parse_entity(current, &to, &from))
			continue;

		from = current;
		*to++ = *from++;
	}

	size_t remaining = strlen(from);

	memmove(to, from, remaining);
	to += remaining;
	*to = 0;

	return (size_t)(to - dest);
}
