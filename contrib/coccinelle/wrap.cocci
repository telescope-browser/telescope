@@
expression x, y;
statement S;
@@
-if ((x = strdup(y)) == NULL)
- S
+x = xstrdup(y);

@@
expression x, y;
statement S;
@@
-x = strdup(y);
-if (x == NULL)
- S
+x = xstrdup(y);

@@
expression x, y;
@@
-x = strdup(y);
+x = xstrdup(y);

@@
expression x, y, z;
statement S;
@@
-if ((x = strndup(y, z)) == NULL)
- S
+x = xstrndup(y, z);

@@
expression x, y;
statement S;
@@
-if ((x = malloc(y)) == NULL)
- S
+x = xmalloc(y);

@@
expression x, y, z;
statement S;
@@
-if ((x = calloc(y, z)) == NULL)
- S
+x = xcalloc(y, z);

@@
expression x, y, z;
statement S;
@@
-if ((x = realloc(y, z)) == NULL)
- S
+x = xrealloc(y, z);

@@
expression x, y, z;
statement S;
@@
-x = realloc(y, z);
- S
+x = xrealloc(y, z);

@@
expression w, x, y, z;
statement S;
@@
-if ((x = reallocarray(w, y, z)) == NULL)
- S
+x = xreallocarray(w, y, z);

@@
expression w, x, y, z;
@@
-x = reallocarray(w, y, z);
+x = xreallocarray(w, y, z);

@@
expression fmt;
expression list args;
statement S;
@@
-if (asprintf(fmt, args) == -1) S
+xasprintf(fmt, args);

@@
expression x, fmt;
expression list args;
@@
-x = asprintf(fmt, args);
+xasprintf(fmt, args);
