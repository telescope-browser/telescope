if [ "$ENCODING" = "utf8" ]; then
	UNIT="byte"
	SUFFIX="_utf8"
	ANTISUFFIX=""
else
	UNIT="codepoint"
	SUFFIX=""
	ANTISUFFIX="_utf8"
fi

cat << EOF
.Dd ${MAN_DATE}
.Dt GRAPHEME_NEXT_$(printf "%s_break%s" "$TYPE" "$SUFFIX" | tr [:lower:] [:upper:]) 3
.Os suckless.org
.Sh NAME
.Nm grapheme_next_${TYPE}_break${SUFFIX}
.Nd determine ${UNIT}-offset to next ${REALTYPE} break
.Sh SYNOPSIS
.In grapheme.h
.Ft size_t
.Fn grapheme_next_${TYPE}_break${SUFFIX} "const $(if [ "$ENCODING" = "utf8" ]; then printf "char"; else printf "uint_least32_t"; fi) *str" "size_t len"
.Sh DESCRIPTION
The
.Fn grapheme_next_${TYPE}_break${SUFFIX}
function computes the offset (in ${UNIT}s) to the next ${REALTYPE}
break (see
.Xr libgrapheme 7 )
in the $(if [ "$ENCODING" = "utf8" ]; then printf "UTF-8-encoded string"; else printf "codepoint array"; fi)
.Va str
of length
.Va len .$(if [ "$TYPE" != "line" ]; then printf "\nIf a ${REALTYPE} begins at
.Va str
this offset is equal to the length of said ${REALTYPE}."; fi)
.Pp
If
.Va len
is set to
.Dv SIZE_MAX
(stdint.h is already included by grapheme.h) the string
.Va str
is interpreted to be NUL-terminated and processing stops when
a $(if [ "$ENCODING" = "utf8" ]; then printf "NUL-byte"; else printf "codepoint with the value 0"; fi) is encountered.
.Pp
For $(if [ "$ENCODING" != "utf8" ]; then printf "UTF-8-encoded"; else printf "non-UTF-8"; fi) input
data$(if [ "$TYPE" = "character" ] && [ "$ENCODING" = "utf8" ]; then printf "\n.Xr grapheme_is_character_break 3 and"; fi)
.Xr grapheme_next_${TYPE}_break${ANTISUFFIX} 3
can be used instead.
.Sh RETURN VALUES
The
.Fn grapheme_next_${TYPE}_break${SUFFIX}
function returns the offset (in ${UNIT}s) to the next ${REALTYPE}
break in
.Va str
or 0 if
.Va str
is
.Dv NULL .
EOF

if [ "$ENCODING" = "utf8" ]; then
cat << EOF
.Sh EXAMPLES
.Bd -literal
/* cc (-static) -o example example.c -lgrapheme */
#include <grapheme.h>
#include <stdint.h>
#include <stdio.h>

int
main(void)
{
	/* UTF-8 encoded input */
	char *s = "T\\\\xC3\\\\xABst \\\\xF0\\\\x9F\\\\x91\\\\xA8\\\\xE2\\\\x80\\\\x8D\\\\xF0"
	          "\\\\x9F\\\\x91\\\\xA9\\\\xE2\\\\x80\\\\x8D\\\\xF0\\\\x9F\\\\x91\\\\xA6 \\\\xF0"
	          "\\\\x9F\\\\x87\\\\xBA\\\\xF0\\\\x9F\\\\x87\\\\xB8 \\\\xE0\\\\xA4\\\\xA8\\\\xE0"
	          "\\\\xA5\\\\x80 \\\\xE0\\\\xAE\\\\xA8\\\\xE0\\\\xAE\\\\xBF!";
	size_t ret, len, off;

	printf("Input: \\\\"%s\\\\"\\\\n", s);

	/* print each ${REALTYPE} with byte-length */
	printf("${REALTYPE}s in NUL-delimited input:\\\\n");
	for (off = 0; s[off] != '\\\\0'; off += ret) {
		ret = grapheme_next_${TYPE}_break_utf8(s + off, SIZE_MAX);
		printf("%2zu bytes | %.*s\\\\n", ret, (int)ret, s + off);
	}
	printf("\\\\n");

	/* do the same, but this time string is length-delimited */
	len = 17;
	printf("${REALTYPE}s in input delimited to %zu bytes:\\\\n", len);
	for (off = 0; off < len; off += ret) {
		ret = grapheme_next_${TYPE}_break_utf8(s + off, len - off);
		printf("%2zu bytes | %.*s\\\\n", ret, (int)ret, s + off);
	}

	return 0;
}
.Ed
EOF
fi

cat << EOF
.Sh SEE ALSO$(if [ "$TYPE" = "character" ] && [ "$ENCODING" != "utf8" ]; then printf "\n.Xr grapheme_is_character_break 3 ,"; fi)
.Xr grapheme_next_${TYPE}_break${ANTISUFFIX} 3 ,
.Xr libgrapheme 7
.Sh STANDARDS
.Fn grapheme_next_${TYPE}_break${SUFFIX}
is compliant with the Unicode ${UNICODE_VERSION} specification.
.Sh AUTHORS
.An Laslo Hunhold Aq Mt dev@frign.de
EOF
