BEGIN {
	FS = "[(,)]";

	print "#include \"telescope.h\""
	print "#include \"cmd.h\""
	print "struct cmd cmds[] = {";
}

/^CMD/ {
	s = $2;
	sub("^cmd_", "", s);
	gsub("_", "-", s);
	printf("\t{ \"%s\", %s },\n", s, $2);
	next;
}

/^DEFALIAS/ {
	s = $2;
	d = $3;
	printf("\t{ \"%s\", %s },\n", s, d);
	next
}

{
	next;
}

END {
	printf("\t{ NULL, NULL },\n");
	print "};";
}
