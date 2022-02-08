#!/bin/sh

file="${1:?missing input file}"

sed -e '/^$/d'		\
    -e '/^#/d'		\
    -e 's/;.*//'	\
    -e 's/[ \t]*$//'	\
    -e 's/\.\./ /'	\
    "$file"		\
	| awk '
BEGIN {
	print "#include \"utf8.h\""
	print "int is_emoji(uint32_t cp) {"

	e=""
}

{
	if (NF == 1) {
		printf("%sif (cp == 0x%s)", e, $1);
	} else {
		printf("%sif (cp >= 0x%s && cp <= 0x%s)", e, $1, $2);
	}

	print " return 1;"

	e="else "
}

END {
	print "return 0; }"
}
'
