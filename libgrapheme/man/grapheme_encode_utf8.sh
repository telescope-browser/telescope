cat << EOF
.Dd ${MAN_DATE}
.Dt GRAPHEME_ENCODE_UTF8 3
.Os suckless.org
.Sh NAME
.Nm grapheme_encode_utf8
.Nd encode codepoint into UTF-8 string
.Sh SYNOPSIS
.In grapheme.h
.Ft size_t
.Fn grapheme_encode_utf8 "uint_least32_t cp" "char *str" "size_t len"
.Sh DESCRIPTION
The
.Fn grapheme_encode_utf8
function encodes the codepoint
.Va cp
into a UTF-8-string.
If
.Va str
is not
.Dv NULL
and
.Va len
is large enough it writes the UTF-8-string to the memory pointed to by
.Va str .
Otherwise no data is written.
.Sh RETURN VALUES
The
.Fn grapheme_encode_utf8
function returns the length (in bytes) of the UTF-8-string resulting
from encoding
.Va cp ,
even if
.Va len
is not large enough or
.Va str
is
.Dv NULL .
.Sh EXAMPLES
.Bd -literal
/* cc (-static) -o example example.c -lgrapheme */
#include <grapheme.h>
#include <stddef.h>
#include <stdlib.h>

size_t
cps_to_utf8(const uint_least32_t *cp, size_t cplen, char *str, size_t len)
{
	size_t i, off, ret;

	for (i = 0, off = 0; i < cplen; i++, off += ret) {
		if ((ret = grapheme_encode_utf8(cp[i], str + off,
		                                len - off)) > (len - off)) {
			/* buffer too small */
			break;
		}
	}

	return off;
}

size_t
cps_bytelen(const uint_least32_t *cp, size_t cplen)
{
	size_t i, len;

	for (i = 0, len = 0; i < cplen; i++) {
		len += grapheme_encode_utf8(cp[i], NULL, 0);
	}

	return len;
}

char *
cps_to_utf8_alloc(const uint_least32_t *cp, size_t cplen)
{
	char *str;
	size_t len, i, ret, off;

	len = cps_bytelen(cp, cplen);

	if (!(str = malloc(len))) {
		return NULL;
	}

	for (i = 0, off = 0; i < cplen; i++, off += ret) {
		if ((ret = grapheme_encode_utf8(cp[i], str + off,
		                                len - off)) > (len - off)) {
			/* buffer too small */
			break;
		}
	}
	str[off] = '\\\\0';

	return str;
}
.Ed
.Sh SEE ALSO
.Xr grapheme_decode_utf8 3 ,
.Xr libgrapheme 7
.Sh AUTHORS
.An Laslo Hunhold Aq Mt dev@frign.de
EOF
