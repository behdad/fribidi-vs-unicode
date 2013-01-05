#include <fribidi.h>
#include <glib.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>

static FriBidiCharType
parse_char_type (const char *s, int len)
{
#define MATCH(name, value) \
    if (!strncmp (name, s, len) && name[len] == '\0') return value;

    MATCH ("L",   FRIBIDI_TYPE_LTR);
    MATCH ("R",   FRIBIDI_TYPE_RTL);
    MATCH ("AL",  FRIBIDI_TYPE_AL);
    MATCH ("EN",  FRIBIDI_TYPE_EN);
    MATCH ("AN",  FRIBIDI_TYPE_AN);
    MATCH ("ES",  FRIBIDI_TYPE_ES);
    MATCH ("ET",  FRIBIDI_TYPE_ET);
    MATCH ("CS",  FRIBIDI_TYPE_CS);
    MATCH ("NSM", FRIBIDI_TYPE_NSM);
    MATCH ("BN",  FRIBIDI_TYPE_BN);
    MATCH ("B",   FRIBIDI_TYPE_BS);
    MATCH ("S",   FRIBIDI_TYPE_SS);
    MATCH ("WS",  FRIBIDI_TYPE_WS);
    MATCH ("ON",  FRIBIDI_TYPE_ON);
    MATCH ("LRE", FRIBIDI_TYPE_LRE);
    MATCH ("RLE", FRIBIDI_TYPE_RLE);
    MATCH ("LRO", FRIBIDI_TYPE_LRO);
    MATCH ("RLO", FRIBIDI_TYPE_RLO);
    MATCH ("PDF", FRIBIDI_TYPE_PDF);

    g_assert_not_reached ();
}

static FriBidiStrIndex *
parse_reorder_line (const char *line,
		    FriBidiStrIndex *len)
{
    GArray *map;
    FriBidiStrIndex l;
    char *end;

    if (!strncmp (line, "@Reorder:", 9))
	line += 9;

    map = g_array_new (FALSE, FALSE, sizeof (FriBidiStrIndex));

    for(; errno = 0, l = strtol (line, &end, 10), line != end && errno != EINVAL; line = end) {
	g_array_append_val (map, l);
    }

    *len = map->len;
    return (FriBidiStrIndex *) g_array_free (map, FALSE);
}

static FriBidiCharType *
parse_test_line (const char *line,
	         FriBidiStrIndex *len,
		 int *base_dir_flags)
{
    GArray *types;
    FriBidiCharType c;
    const char *end;

    types = g_array_new (FALSE, FALSE, sizeof (FriBidiCharType));

    for(;;) {
	while (isspace (*line))
	    line++;
	end = line;
	while (isalpha (*end))
	    end++;
	if (line == end)
	    break;

	c = parse_char_type (line, end - line);
	g_array_append_val (types, c);

	line = end;
    }

    if (*line == ';')
	line++;
    *base_dir_flags = strtol (line, NULL, 10);

    *len = types->len;
    return (FriBidiCharType *) g_array_free (types, FALSE);
}

int
main (int argc, char **argv)
{
    GIOChannel *channel;
    GIOStatus status;
    GError *error;
    gchar *line = NULL;
    gsize length, terminator_pos;
    FriBidiStrIndex *expected_ltor = NULL;
    FriBidiStrIndex expected_ltor_len = 0;
    FriBidiStrIndex *ltor = NULL;
    FriBidiStrIndex ltor_len = 0;
    FriBidiCharType *types = NULL;
    FriBidiStrIndex types_len = 0;
    FriBidiLevel *levels = NULL;
    int base_dir_flags, base_dir_mode;
    int numerrs = 0;
    int line_no = 0;
    gboolean debug = FALSE;
    const char *filename;
    int next_arg;

    if (argc < 2) {
	g_printerr ("usage: %s [--debug] test-file-name\n", argv[0]);
	exit (1);
    }

    next_arg = 1;
    if (!strcmp (argv[next_arg], "--debug")) {
	debug = TRUE;
	next_arg++;
    }

    filename = argv[next_arg++];

    error = NULL;
    channel = g_io_channel_new_file (filename, "r", &error);
    if (!channel) {
	g_printerr ("%s\n", error->message);
	exit (1);
    }

    while (TRUE) {
	error = NULL;
	g_free (line);
	status = g_io_channel_read_line (channel, &line, &length, &terminator_pos, &error);
	switch (status) {
        case G_IO_STATUS_ERROR:
            g_printerr ("%s\n", error->message);
            exit (1);

        case G_IO_STATUS_EOF:
	    goto done;

        case G_IO_STATUS_AGAIN:
            continue;

        case G_IO_STATUS_NORMAL:
            line[terminator_pos] = '\0';
            break;
	}

	line_no++;

	if (line[0] == '#' || line[0] == '\0')
	    continue;

	if (line[0] == '@')
	{
	    if (!strncmp (line, "@Reorder:", 9)) {
		g_free (expected_ltor);
		expected_ltor = parse_reorder_line (line, &expected_ltor_len);
		continue;
	    }
	    continue;
	}

	/* Test line */
	g_free (types);
	types = parse_test_line (line, &types_len, &base_dir_flags);

	g_free (levels);
	levels = g_malloc (sizeof (FriBidiLevel) * types_len);

	g_free (ltor);
	ltor = g_malloc (sizeof (FriBidiStrIndex) * types_len);

	/* Test it */
	for (base_dir_mode = 0; base_dir_mode < 3; base_dir_mode++) {
	    FriBidiParType base_dir;
	    int i, j;
	    gboolean matches;

	    if ((base_dir_flags & (1<<base_dir_mode)) == 0)
		continue;

	    switch (base_dir_mode) {
	    case 0: base_dir = FRIBIDI_PAR_ON;  break;
	    case 1: base_dir = FRIBIDI_PAR_LTR; break;
	    case 2: base_dir = FRIBIDI_PAR_RTL; break;
	    }

	    fribidi_get_par_embedding_levels (types, types_len,
					      &base_dir,
					      levels);

	    for (i = 0; i < types_len; i++)
	        ltor[i] = i;

	    fribidi_reorder_line (0 /*FRIBIDI_FLAG_REORDER_NSM*/,
				  types, types_len,
				  0, base_dir,
				  levels,
				  NULL,
				  ltor);

	    j = 0;
	    for (i = 0; i < types_len; i++)
	    	if (!FRIBIDI_IS_EXPLICIT_OR_BN (types[ltor[i]]))
		    ltor[j++] = ltor[i];
	    ltor_len = j;

	    /* Compare */
	    matches = TRUE;
	    if (ltor_len != expected_ltor_len)
		matches = FALSE;
	    if (matches)
		for (i = 0; i < ltor_len; i++)
		    if (ltor[i] != expected_ltor[i]) {
			matches = FALSE;
			break;
		    }
	    if (!matches) {
		numerrs++;
		g_printerr ("failure on line %d\n", line_no);
		g_printerr ("input is: %s\n", line);
		g_printerr ("base dir: %s\n", base_dir_mode==0 ? "auto"
					    : base_dir_mode==1 ? "LTR" : "RTL");
		g_printerr ("expected:");
		for (i = 0; i < expected_ltor_len; i++)
		    g_printerr (" %d", expected_ltor[i]);
		g_printerr ("\n");
		g_printerr ("returned:");
		for (i = 0; i < ltor_len; i++)
		    g_printerr (" %d", ltor[i]);
		g_printerr ("\n");

		if (debug) {
		    FriBidiParType base_dir;

		    fribidi_set_debug (1);

		    switch (base_dir_mode) {
		    case 0: base_dir = FRIBIDI_PAR_ON;  break;
		    case 1: base_dir = FRIBIDI_PAR_LTR; break;
		    case 2: base_dir = FRIBIDI_PAR_RTL; break;
		    }

		    fribidi_get_par_embedding_levels (types, types_len,
						      &base_dir,
						      levels);

		    fribidi_set_debug (0);
		}

		g_printerr ("\n");
	    }
	}
    }

done:
    g_free (ltor);
    g_free (levels);
    g_free (expected_ltor);
    g_free (types);
    g_free (line);
    g_io_channel_unref (channel);
    if (error)
	g_error_free (error);

    if (numerrs)
	g_printerr ("%d errors\n", numerrs);
    return numerrs;
}
