BEGIN {
        FS = "[()]";

        print "static struct cmds { const char *cmd; void(*fn)(struct window*); } cmds[] = {";
}

/^CMD/ {
        s = $2;
        sub("^cmd_", "", s);
        gsub("_", "-", s);
        printf("\t{ \"%s\", %s },\n", s, $2);
        next;
}

{
        next;
}

END {
	printf("\t{ NULL, NULL },\n");
	print "};";
}
