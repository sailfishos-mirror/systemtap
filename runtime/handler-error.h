/* Format probe handler errors with call-site context (PR34320).
 * Included from io.c after _stp_softerror is defined. */

#ifndef _STAP_HANDLER_ERROR_H_
#define _STAP_HANDLER_ERROR_H_

static void _stp_softerror_handler (struct context *c)
{
	const char *err = c->last_error;
	char buf[MAXSTRINGLEN];
	int inner = -1;
	int i;
	int pos;

	if (!err)
		return;

	for (i = MAXNESTING; i >= 0; i--)
		if (c->last_stmt[i]) {
			inner = i;
			break;
		}

	if (inner < 0) {
		_stp_softerror ("%s", err);
		return;
	}

	pos = snprintf (buf, sizeof (buf), "%s near %s", err,
			c->last_stmt[inner]);
	if (pos < 0 || pos >= (int) sizeof (buf))
		goto emit;

	for (i = inner - 1; i >= 0; i--) {
		if (!c->last_stmt[i])
			continue;
		pos += snprintf (buf + pos, sizeof (buf) - pos,
				 " (called from %s)", c->last_stmt[i]);
		if (pos < 0 || pos >= (int) sizeof (buf))
			goto emit;
	}

	if (c->probe_point)
		snprintf (buf + pos, sizeof (buf) - pos,
			  " in probe %s", c->probe_point);

 emit:
	_stp_softerror ("%s", buf);
}

#endif /* _STAP_HANDLER_ERROR_H_ */
