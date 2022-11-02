if [ "$ENCODING" = "utf8" ]; then
	UNIT="byte"
	ARRAYTYPE="UTF-8-encoded string"
	SUFFIX="_utf8"
	ANTISUFFIX=""
	DATATYPE="char"
else
	UNIT="codepoint"
	ARRAYTYPE="codepoint array"
	SUFFIX=""
	ANTISUFFIX="_utf8"
	DATATYPE="uint_least32_t"
fi

cat << EOF
.Dd ${MAN_DATE}
.Dt GRAPHEME_IS_$(printf "%s%s" "$CASE" "$SUFFIX" | tr [:lower:] [:upper:]) 3
.Os suckless.org
.Sh NAME
.Nm grapheme_is_${CASE}${SUFFIX}
.Nd check if ${ARRAYTYPE} is ${CASE}
.Sh SYNOPSIS
.In grapheme.h
.Ft size_t
.Fn grapheme_is_${CASE}${SUFFIX} "const ${DATATYPE} *str" "size_t len" "size_t *caselen"
.Sh DESCRIPTION
The
.Fn grapheme_is_${CASE}${SUFFIX}
function checks if the ${ARRAYTYPE}
.Va str
is ${CASE} and writes the length of the matching ${CASE}-sequence to the integer pointed to by
.Va caselen ,
unless
.Va caselen
is set to
.Dv NULL .
.Pp
If
.Va len
is set to
.Dv SIZE_MAX
(stdint.h is already included by grapheme.h) the ${ARRAYTYPE}
.Va src
is interpreted to be NUL-terminated and processing stops when a
NUL-byte is encountered.
.Pp
For $(if [ "$ENCODING" != "utf8" ]; then printf "UTF-8-encoded"; else printf "non-UTF-8"; fi) input data
.Xr grapheme_is_${CASE}${ANTISUFFIX} 3
can be used instead.
.Sh RETURN VALUES
The
.Fn grapheme_is_${CASE}${SUFFIX}
function returns
.Dv true
if the ${ARRAYTYPE}
.Va str
is ${CASE}, otherwise
.Dv false .
.Sh SEE ALSO
.Xr grapheme_is_${CASE}${ANTISUFFIX} 3 ,
.Xr libgrapheme 7
.Sh STANDARDS
.Fn grapheme_is_${CASE}${SUFFIX}
is compliant with the Unicode ${UNICODE_VERSION} specification.
.Sh AUTHORS
.An Laslo Hunhold Aq Mt dev@frign.de
EOF
