/*
 * Copyright (c) 2003-2004 E. Will et al.
 * Copyright (c) 2005-2008 Atheme Development Group
 * Copyright (c) 2008 ShadowIRCd Development Group
 * Rights to this code are documented in doc/LICENSE.
 *
 * This file contains protocol support for shadowircd.
 *
 * $Id$
 */

#include "atheme.h"
#include "uplink.h"
#include "pmodule.h"
#include "protocol/shadowircd5.h"

DECLARE_MODULE_V1("protocol/shadowircd5", TRUE, _modinit, NULL, "$Id$", "ShadowIRCd Development Group <http://www.shadowircd.net>");

/* *INDENT-OFF* */

ircd_t ShadowIRCd = {
        "ShadowIRCd 5.x",		/* IRCd name */
        "$$",                           /* TLD Prefix, used by Global. */
        TRUE,                           /* Whether or not we use IRCNet/TS6 UID */
        FALSE,                          /* Whether or not we use RCOMMAND */
        FALSE,                          /* Whether or not we support channel owners. */
        FALSE,                          /* Whether or not we support channel protection. */
        FALSE,                          /* Whether or not we support halfops. */
	FALSE,				/* Whether or not we use P10 */
	FALSE,				/* Whether or not we use vHosts. */
	CMODE_EXLIMIT | CMODE_PERM,	/* Oper-only cmodes */
        0,                              /* Integer flag for owner channel flag. */
        0,                              /* Integer flag for protect channel flag. */
        0,                              /* Integer flag for halfops. */
        "+",                            /* Mode we set for owner. */
        "+",                            /* Mode we set for protect. */
        "+",                            /* Mode we set for halfops. */
	PROTOCOL_CHARYBDIS,		/* Protocol type */
	CMODE_PERM,                     /* Permanent cmodes */
	"beIq",                         /* Ban-like cmodes */
	'e',                            /* Except mchar */
	'I',                            /* Invex mchar */
	IRCD_CIDR_BANS | IRCD_HOLDNICK  /* Flags */
};

struct cmode_ shadowircd_mode_list[] = {
  { 'i', CMODE_INVITE },
  { 'm', CMODE_MOD    },
  { 'n', CMODE_NOEXT  },
  { 'p', CMODE_PRIV   },
  { 's', CMODE_SEC    },
  { 't', CMODE_TOPIC  },
  { 'c', CMODE_NOCOLOR},		/* chm_nocolour.c */
  { 'r', CMODE_REGONLY},
  { 'z', CMODE_OPMOD  },
  { 'g', CMODE_FINVITE},
  { 'L', CMODE_EXLIMIT},
  { 'P', CMODE_PERM   },
  { 'F', CMODE_FTARGET},
  { 'Q', CMODE_DISFWD },
  { 'M', CMODE_IMMUNE },		/* chm_no_oper_kick.c */
  { 'C', CMODE_NOCTCP },		/* chm_noctcp.c */
  { 'A', CMODE_ADMINONLY },		/* chm_adminonly.c */
  { 'O', CMODE_OPERONLY },		/* chm_operonly.c */
  { 'z', CMODE_SSLONLY },		/* chm_sslonly.c */
  { 'D', CMODE_NOACTIONS },		/* chm_noactions.c */
  { 'X', CMODE_NOSPAM },		/* chm_nospam.c */
  { 'S', CMODE_SCOLOR },		/* chm_stripcolour.c */
  { 'T', CMODE_NONOTICE },		/* chm_nonotice.c */
  { 'R', CMODE_MODNOREG },		/* chm_quietunreg.c */
  { '\0', 0 }
};

static boolean_t check_forward(const char *, channel_t *, mychan_t *, user_t *, myuser_t *);
static boolean_t check_jointhrottle(const char *, channel_t *, mychan_t *, user_t *, myuser_t *);

struct extmode shadowircd_ignore_mode_list[] = {
  { 'f', check_forward },
  { 'j', check_jointhrottle },
  { '\0', 0 }
};

struct cmode_ shadowircd_status_mode_list[] = {
  { 'o', CMODE_OP    },
  { 'v', CMODE_VOICE },
  { '\0', 0 }
};

struct cmode_ shadowircd_prefix_mode_list[] = {
  { '@', CMODE_OP    },
  { '+', CMODE_VOICE },
  { '\0', 0 }
};

struct cmode_ shadowircd_user_mode_list[] = {
  { 'm', UF_IMMUNE   },			/* immune.c */
  { 'i', UF_INVIS    },
  { 'o', UF_IRCOP    },
  { '\0', 0 }
};

static boolean_t use_rserv_support = FALSE;
static boolean_t use_tb = FALSE;
static boolean_t use_euid = FALSE;

static void server_eob(server_t *s);
static server_t *sid_find(char *name);

static char ts6sid[3 + 1] = "";

/* *INDENT-ON* */

/* ircd allows forwards to existing channels; the target channel must be
 * +F or the setter must have ops in it */
static boolean_t check_forward(const char *value, channel_t *c, mychan_t *mc, user_t *u, myuser_t *mu)
{
	channel_t *target_c;
	mychan_t *target_mc;
	chanuser_t *target_cu;

	if (*value != '#' || strlen(value) > 50)
		return FALSE;
	if (u == NULL && mu == NULL)
		return TRUE;
	target_c = channel_find(value);
	target_mc = mychan_find(value);
	if (target_c == NULL && target_mc == NULL)
		return FALSE;
	if (target_c != NULL && target_c->modes & CMODE_FTARGET)
		return TRUE;
	if (target_mc != NULL && target_mc->mlock_on & CMODE_FTARGET)
		return TRUE;
	if (u != NULL)
	{
		target_cu = chanuser_find(target_c, u);
		if (target_cu != NULL && target_cu->modes & CMODE_OP)
			return TRUE;
		if (chanacs_user_flags(target_mc, u) & CA_SET)
			return TRUE;
	}
	else if (mu != NULL)
		if (chanacs_find(target_mc, mu, CA_SET))
			return TRUE;
	return FALSE;
}

static boolean_t check_jointhrottle(const char *value, channel_t *c, mychan_t *mc, user_t *u, myuser_t *mu)
{
	const char *p, *arg2;

	p = value, arg2 = NULL;
	while (*p != '\0')
	{
		if (*p == ':')
		{
			if (arg2 != NULL)
				return FALSE;
			arg2 = p + 1;
		}
		else if (!isdigit(*p))
			return FALSE;
		p++;
	}
	if (arg2 == NULL)
		return FALSE;
	if (p - arg2 > 10 || arg2 - value - 1 > 10 || !atoi(value) || !atoi(arg2))
		return FALSE;
	return TRUE;
}

/* this may be slow, but it is not used much */
/* returns TRUE if it matches, FALSE if not */
/* note that the host part matches differently from a regular ban */
static boolean_t extgecos_match(const char *mask, user_t *u)
{
	char hostgbuf[NICKLEN+USERLEN+HOSTLEN+GECOSLEN];
	char realgbuf[NICKLEN+USERLEN+HOSTLEN+GECOSLEN];

	snprintf(hostgbuf, sizeof hostgbuf, "%s!%s@%s#%s", u->nick, u->user, u->vhost, u->gecos);
	snprintf(realgbuf, sizeof realgbuf, "%s!%s@%s#%s", u->nick, u->user, u->host, u->gecos);
	return !match(mask, hostgbuf) || !match(mask, realgbuf);
}

static node_t *shadowircd_next_matching_ban(channel_t *c, user_t *u, int type, node_t *first)
{
	chanban_t *cb;
	node_t *n;
	char hostbuf[NICKLEN+USERLEN+HOSTLEN];
	char realbuf[NICKLEN+USERLEN+HOSTLEN];
	char ipbuf[NICKLEN+USERLEN+HOSTLEN];
	const char *p;
	boolean_t negate, matched;
	int exttype;
	channel_t *target_c;

	snprintf(hostbuf, sizeof hostbuf, "%s!%s@%s", u->nick, u->user, u->vhost);
	snprintf(realbuf, sizeof realbuf, "%s!%s@%s", u->nick, u->user, u->host);
	/* will be nick!user@ if ip unknown, doesn't matter */
	snprintf(ipbuf, sizeof ipbuf, "%s!%s@%s", u->nick, u->user, u->ip);
	LIST_FOREACH(n, first)
	{
		cb = n->data;

		if (cb->type == type &&
				(!match(cb->mask, hostbuf) || !match(cb->mask, realbuf) || !match(cb->mask, ipbuf) || !match_cidr(cb->mask, ipbuf)))
			return n;
		if (cb->type == type && cb->mask[0] == '$')
		{
			p = cb->mask + 1;
			negate = *p == '~';
			if (negate)
				p++;
			exttype = *p++;
			if (exttype == '\0')
				continue;
			/* check parameter */
			if (*p++ != ':')
				p = NULL;
			switch (exttype)
			{
				case 'a':
					matched = u->myuser != NULL && !(u->myuser->flags & MU_WAITAUTH) && (p == NULL || !match(p, u->myuser->name));
					break;
				case 'c':
					if (p == NULL)
						continue;
					target_c = channel_find(p);
					if (target_c == NULL || (target_c->modes & (CMODE_PRIV | CMODE_SEC)))
						continue;
					matched = chanuser_find(target_c, u) != NULL;
					break;
				case 'o':
					matched = is_ircop(u);
					break;
				case 'r':
					if (p == NULL)
						continue;
					matched = !match(p, u->gecos);
					break;
				case 's':
					if (p == NULL)
						continue;
					matched = !match(p, u->server->name);
					break;
				case 'x':
					if (p == NULL)
						continue;
					matched = extgecos_match(p, u);
					break;
				default:
					continue;
			}
			if (negate ^ matched)
				return n;
		}
	}
	return NULL;
}

static boolean_t shadowircd_is_valid_host(const char *host)
{
	const char *p;

	for (p = host; *p != '\0'; p++)
		if (!((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') ||
					(*p >= 'a' && *p <= 'z') || *p == '.'
					|| *p == ':' || *p == '-'))
			return FALSE;
	return TRUE;
}

/* login to our uplink */
static unsigned int shadowircd_server_login(void)
{
	int ret = 1;

	if (!me.numeric)
	{
		ircd->uses_uid = FALSE;
		ret = sts("PASS %s :TS", curr_uplink->pass);
	}
	else if (strlen(me.numeric) == 3 && isdigit(*me.numeric))
	{
		ircd->uses_uid = TRUE;
		ret = sts("PASS %s TS 6 :%s", curr_uplink->pass, me.numeric);
	}
	else
	{
		slog(LG_ERROR, "Invalid numeric (SID) %s", me.numeric);
	}
	if (ret == 1)
		return 1;

	me.bursting = TRUE;

	sts("CAPAB :QS EX IE KLN UNKLN ENCAP TB SERVICES EUID");
	sts("SERVER %s 1 :%s", me.name, me.desc);
	sts("SVINFO %d 3 0 :%lu", ircd->uses_uid ? 6 : 5,
			(unsigned long)CURRTIME);

	return 0;
}

/* introduce a client */
static void shadowircd_introduce_nick(user_t *u)
{
	const char *omode = is_ircop(u) ? "o" : "";

	if (ircd->uses_uid && use_euid)
		sts(":%s EUID %s 1 %lu +i%s%sS %s %s 0 %s * * :%s", me.numeric, u->nick, (unsigned long)u->ts, omode, chansvs.fantasy ? "" : "D", u->user, u->host, u->uid, u->gecos);
	else if (ircd->uses_uid)
		sts(":%s UID %s 1 %lu +i%s%sS %s %s 0 %s :%s", me.numeric, u->nick, (unsigned long)u->ts, omode, chansvs.fantasy ? "" : "D", u->user, u->host, u->uid, u->gecos);
	else
		sts("NICK %s 1 %lu +i%s%sS %s %s %s :%s", u->nick, (unsigned long)u->ts, omode, chansvs.fantasy ? "" : "D", u->user, u->host, me.name, u->gecos);
}

/* invite a user to a channel */
static void shadowircd_invite_sts(user_t *sender, user_t *target, channel_t *channel)
{
	sts(":%s INVITE %s %s", CLIENT_NAME(sender), CLIENT_NAME(target), channel->name);
}

static void shadowircd_quit_sts(user_t *u, const char *reason)
{
	if (!me.connected)
		return;

	sts(":%s QUIT :%s", CLIENT_NAME(u), reason);
}

/* WALLOPS wrapper */
static void shadowircd_wallops_sts(const char *text)
{
	sts(":%s WALLOPS :%s", ME, text);
}

/* join a channel */
static void shadowircd_join_sts(channel_t *c, user_t *u, boolean_t isnew, char *modes)
{
	if (isnew)
		sts(":%s SJOIN %lu %s %s :@%s", ME, (unsigned long)c->ts,
				c->name, modes, CLIENT_NAME(u));
	else
		sts(":%s SJOIN %lu %s + :@%s", ME, (unsigned long)c->ts,
				c->name, CLIENT_NAME(u));
}

static void shadowircd_chan_lowerts(channel_t *c, user_t *u)
{
	slog(LG_DEBUG, "shadowircd_chan_lowerts(): lowering TS for %s to %lu",
			c->name, (unsigned long)c->ts);
	sts(":%s SJOIN %lu %s %s :@%s", ME, (unsigned long)c->ts, c->name,
				channel_modes(c, TRUE), CLIENT_NAME(u));
	if (ircd->uses_uid)
		chanban_clear(c);
}

/* kicks a user from a channel */
static void shadowircd_kick(char *from, char *channel, char *to, char *reason)
{
	channel_t *chan = channel_find(channel);
	user_t *user = user_find(to);
	user_t *from_p = user_find(from);

	if (!chan || !user)
		return;

	if (chan->ts != 0 || chanuser_find(chan, from_p))
		sts(":%s KICK %s %s :%s", CLIENT_NAME(from_p), channel, CLIENT_NAME(user), reason);
	else
		sts(":%s KICK %s %s :%s", ME, channel, CLIENT_NAME(user), reason);

	chanuser_delete(chan, user);
}

/* PRIVMSG wrapper */
static void shadowircd_msg(const char *from, const char *target, const char *fmt, ...)
{
	va_list ap;
	char buf[BUFSIZE];
	user_t *u = user_find(from);
	user_t *t = user_find(target);

	if (!u)
		return;

	va_start(ap, fmt);
	vsnprintf(buf, BUFSIZE, fmt, ap);
	va_end(ap);

	/* If this is to a channel, it's the snoop channel so chanserv
	 * is on it -- jilles
	 *
	 * Well, now it's operserv, but yes it's safe to assume that
	 * the source would be able to send to whatever target it is 
	 * sending to. --nenolod
	 */
	sts(":%s PRIVMSG %s :%s", CLIENT_NAME(u), t ? CLIENT_NAME(t) : target, buf);
}

/* NOTICE wrapper */
static void shadowircd_notice_user_sts(user_t *from, user_t *target, const char *text)
{
	sts(":%s NOTICE %s :%s", from ? CLIENT_NAME(from) : ME, CLIENT_NAME(target), text);
}

static void shadowircd_notice_global_sts(user_t *from, const char *mask, const char *text)
{
	node_t *n;
	tld_t *tld;

	if (!strcmp(mask, "*"))
	{
		LIST_FOREACH(n, tldlist.head)
		{
			tld = n->data;
			sts(":%s NOTICE %s*%s :%s", from ? CLIENT_NAME(from) : ME, ircd->tldprefix, tld->name, text);
		}
	}
	else
		sts(":%s NOTICE %s%s :%s", from ? CLIENT_NAME(from) : ME, ircd->tldprefix, mask, text);
}

static void shadowircd_notice_channel_sts(user_t *from, channel_t *target, const char *text)
{
	if (from == NULL || chanuser_find(target, from))
		sts(":%s NOTICE %s :%s", from ? CLIENT_NAME(from) : ME, target->name, text);
	else
		sts(":%s NOTICE %s :[%s:%s] %s", ME, target->name, from->nick, target->name, text);
}

static void shadowircd_wallchops(user_t *sender, channel_t *channel, const char *message)
{
	if (chanuser_find(channel, sender))
		sts(":%s NOTICE @%s :%s", CLIENT_NAME(sender), channel->name,
				message);
	else /* do not join for this, everyone would see -- jilles */
		generic_wallchops(sender, channel, message);
}

/* numeric wrapper */
static void shadowircd_numeric_sts(char *from, int numeric, char *target, char *fmt, ...)
{
	va_list ap;
	char buf[BUFSIZE];
	user_t *t = user_find(target);

	va_start(ap, fmt);
	vsnprintf(buf, BUFSIZE, fmt, ap);
	va_end(ap);

	sts(":%s %d %s %s", ME, numeric, CLIENT_NAME(t), buf);
}

/* KILL wrapper */
static void shadowircd_kill_id_sts(user_t *killer, const char *id, const char *reason)
{
	if (killer != NULL)
		sts(":%s KILL %s :%s!%s (%s)", CLIENT_NAME(killer), id, killer->host, killer->nick, reason);
	else
		sts(":%s KILL %s :%s (%s)", ME, id, me.name, reason);
}

/* PART wrapper */
static void shadowircd_part_sts(channel_t *c, user_t *u)
{
	sts(":%s PART %s", CLIENT_NAME(u), c->name);
}

/* server-to-server KLINE wrapper */
static void shadowircd_kline_sts(char *server, char *user, char *host, long duration, char *reason)
{
	if (!me.connected)
		return;

	sts(":%s KLINE %s %ld %s %s :%s", CLIENT_NAME(opersvs.me->me), server, duration, user, host, reason);
}

/* server-to-server UNKLINE wrapper */
static void shadowircd_unkline_sts(char *server, char *user, char *host)
{
	if (!me.connected)
		return;

	sts(":%s UNKLINE %s %s %s", CLIENT_NAME(opersvs.me->me), server, user, host);
	sts(":%s ENCAP %s REHASH REJECTCACHE", ME, server);
}

/* topic wrapper */
static void shadowircd_topic_sts(channel_t *c, const char *setter, time_t ts, time_t prevts, const char *topic)
{
	int joined = 0;

	if (!me.connected || !c)
		return;

	/* If possible, try to use TB
	 * Note that because TOPIC does not contain topicTS, it may be
	 * off a few seconds on other servers, hence the 60 seconds here.
	 * -- jilles */
	if (use_tb && *topic != '\0')
	{
		/* Restoring old topic */
		if (ts < prevts || prevts == 0)
		{
			if (prevts != 0 && ts + 60 > prevts)
				ts = prevts - 60;
			sts(":%s TB %s %lu %s :%s", ME, c->name, (unsigned long)ts, setter, topic);
			c->topicts = ts;
			return;
		}
		/* Tweaking a topic */
		else if (ts == prevts)
		{
			ts -= 60;
			sts(":%s TB %s %lu %s :%s", ME, c->name, (unsigned long)ts, setter, topic);
			c->topicts = ts;
			return;
		}
	}
	/* We have to be on channel to change topic.
	 * We cannot nicely change topic from the server:
	 * :server.name TOPIC doesn't propagate and TB requires
	 * us to specify an older topicts.
	 * -- jilles
	 */
	if (!chanuser_find(c, chansvs.me->me))
	{
		sts(":%s SJOIN %lu %s + :@%s", ME, (unsigned long)c->ts, c->name, CLIENT_NAME(chansvs.me->me));
		joined = 1;
	}
	sts(":%s TOPIC %s :%s", CLIENT_NAME(chansvs.me->me), c->name, topic);
	if (joined)
		sts(":%s PART %s :Topic set", CLIENT_NAME(chansvs.me->me), c->name);
	c->topicts = CURRTIME;
}

/* mode wrapper */
static void shadowircd_mode_sts(char *sender, channel_t *target, char *modes)
{
	user_t *u = user_find(sender);

	if (!me.connected || !u)
		return;

	if (ircd->uses_uid)
		sts(":%s TMODE %lu %s %s", CLIENT_NAME(u), (unsigned long)target->ts, target->name, modes);
	else
		sts(":%s MODE %s %s", CLIENT_NAME(u), target->name, modes);
}

/* ping wrapper */
static void shadowircd_ping_sts(void)
{
	if (!me.connected)
		return;

	sts("PING :%s", me.name);
}

/* protocol-specific stuff to do on login */
static void shadowircd_on_login(char *origin, char *user, char *wantedhost)
{
	user_t *u = user_find(origin);

	if (!me.connected || !use_rserv_support || !u)
		return;

	sts(":%s ENCAP * SU %s %s", ME, CLIENT_NAME(u), user);
}

/* protocol-specific stuff to do on login */
static boolean_t shadowircd_on_logout(char *origin, char *user, char *wantedhost)
{
	user_t *u = user_find(origin);

	if (!me.connected || !use_rserv_support || !u)
		return FALSE;

	sts(":%s ENCAP * SU %s", ME, CLIENT_NAME(u));
	return FALSE;
}

/* XXX we don't have an appropriate API for this, what about making JUPE
 * serverside like in P10?
 *       --nenolod
 */
static void shadowircd_jupe(const char *server, const char *reason)
{
	if (!me.connected)
		return;

	server_delete(server);
	sts(":%s SQUIT %s :%s", CLIENT_NAME(opersvs.me->me), server, reason);
	sts(":%s SERVER %s 2 :(H) %s", me.name, server, reason);
}

static void shadowircd_sethost_sts(char *source, char *target, char *host)
{
	user_t *tu = user_find(target);

	if (!tu)
		return;

	if (use_euid)
		sts(":%s CHGHOST %s :%s", ME, CLIENT_NAME(tu), host);
	else
		sts(":%s ENCAP * CHGHOST %s :%s", ME, tu->nick, host);
}

static void shadowircd_fnc_sts(user_t *source, user_t *u, char *newnick, int type)
{
	/* XXX assumes the server will accept this -- jilles */
	sts(":%s ENCAP %s RSFNC %s %s %lu %lu", ME,
			u->server->name,
			CLIENT_NAME(u), newnick,
			(unsigned long)(CURRTIME - 60),
			(unsigned long)u->ts);
}

static void shadowircd_svslogin_sts(char *target, char *nick, char *user, char *host, char *login)
{
	user_t *tu = user_find(target);
	server_t *s;

	if(tu)
		s = tu->server;
	else if(ircd->uses_uid) /* Non-announced UID - must be a SASL client. */
		s = sid_find(target);
	else
		return;

	sts(":%s ENCAP %s SVSLOGIN %s %s %s %s %s", ME, s->name,
			target, nick, user, host, login);
}

static void shadowircd_sasl_sts(char *target, char mode, char *data)
{
	server_t *s = sid_find(target);

	if(s == NULL)
		return;

	if (saslsvs.me == NULL)
	{
		slog(LG_ERROR, "shadowircd_sasl_sts(): saslserv does not exist!");
		return;
	}

	sts(":%s ENCAP %s SASL %s %s %c %s", ME, s->name,
			saslsvs.me->uid,
			target,
			mode,
			data);
}

static void shadowircd_holdnick_sts(user_t *source, int duration, const char *nick, myuser_t *account)
{
	if (use_euid)
		sts(":%s ENCAP * NICKDELAY %d %s", ME, duration, nick);
	else
	{
		if (duration == 0)
			return; /* can't do this safely */
		sts(":%s ENCAP * RESV %d %s 0 :Reserved by %s for nickname owner (%s)",
				CLIENT_NAME(source), duration > 300 ? 300 : duration,
				nick, source->nick,
				account != NULL ? account->name : nick);
	}
}

static void m_topic(sourceinfo_t *si, int parc, char *parv[])
{
	channel_t *c = channel_find(parv[0]);

	if (c == NULL)
		return;

	handle_topic_from(si, c, si->su->nick, CURRTIME, parv[1]);
}

static void m_tb(sourceinfo_t *si, int parc, char *parv[])
{
	channel_t *c = channel_find(parv[0]);
	time_t ts = atol(parv[1]);

	if (c == NULL)
		return;

	if (c->topic != NULL && c->topicts <= ts)
	{
		slog(LG_DEBUG, "m_tb(): ignoring newer topic on %s", c->name);
		return;
	}

	handle_topic_from(si, c, parc > 3 ? parv[2] : si->s->name, ts, parv[parc - 1]);
}

static void m_ping(sourceinfo_t *si, int parc, char *parv[])
{
	/* reply to PING's */
	sts(":%s PONG %s %s", ME, me.name, parv[0]);
}

static void m_pong(sourceinfo_t *si, int parc, char *parv[])
{
	server_t *s;

	/* someone replied to our PING */
	if (!parv[0])
		return;
	s = server_find(parv[0]);
	if (s == NULL)
		return;
	handle_eob(s);

	if (irccasecmp(me.actual, parv[0]))
		return;

	me.uplinkpong = CURRTIME;

	/* -> :test.projectxero.net PONG test.projectxero.net :shrike.malkier.net */
	if (me.bursting)
	{
#ifdef HAVE_GETTIMEOFDAY
		e_time(burstime, &burstime);

		slog(LG_INFO, "m_pong(): finished synching with uplink (%d %s)", (tv2ms(&burstime) > 1000) ? (tv2ms(&burstime) / 1000) : tv2ms(&burstime), (tv2ms(&burstime) > 1000) ? "s" : "ms");

		wallops("Finished synching to network in %d %s.", (tv2ms(&burstime) > 1000) ? (tv2ms(&burstime) / 1000) : tv2ms(&burstime), (tv2ms(&burstime) > 1000) ? "s" : "ms");
#else
		slog(LG_INFO, "m_pong(): finished synching with uplink");
		wallops("Finished synching to network.");
#endif

		me.bursting = FALSE;
	}
}

static void m_privmsg(sourceinfo_t *si, int parc, char *parv[])
{
	if (parc != 2)
		return;

	handle_message(si, parv[0], FALSE, parv[1]);
}

static void m_notice(sourceinfo_t *si, int parc, char *parv[])
{
	if (parc != 2)
		return;

	handle_message(si, parv[0], TRUE, parv[1]);
}

static void m_sjoin(sourceinfo_t *si, int parc, char *parv[])
{
	/* -> :proteus.malkier.net SJOIN 1073516550 #shrike +tn :@sycobuny @+rakaur */

	channel_t *c;
	boolean_t keep_new_modes = TRUE;
	unsigned int userc;
	char *userv[256];
	unsigned int i;
	time_t ts;
	char *p;

	/* :origin SJOIN ts chan modestr [key or limits] :users */
	c = channel_find(parv[1]);
	ts = atol(parv[0]);

	if (!c)
	{
		slog(LG_DEBUG, "m_sjoin(): new channel: %s", parv[1]);
		c = channel_add(parv[1], ts, si->s);
	}

	if (ts == 0 || c->ts == 0)
	{
		if (c->ts != 0)
			slog(LG_INFO, "m_sjoin(): server %s changing TS on %s from %lu to 0", si->s->name, c->name, (unsigned long)c->ts);
		c->ts = 0;
		hook_call_event("channel_tschange", c);
	}
	else if (ts < c->ts)
	{
		chanuser_t *cu;
		node_t *n;

		/* the TS changed.  a TS change requires the following things
		 * to be done to the channel:  reset all modes to nothing, remove
		 * all status modes on known users on the channel (including ours),
		 * and set the new TS.
		 *
		 * if the source does TS6, also remove all bans
		 * note that JOIN does not do this
		 */

		clear_simple_modes(c);
		if (si->s->sid != NULL)
			chanban_clear(c);

		LIST_FOREACH(n, c->members.head)
		{
			cu = (chanuser_t *)n->data;
			if (cu->user->server == me.me)
			{
				/* it's a service, reop */
				sts(":%s PART %s :Reop", CLIENT_NAME(cu->user), c->name);
				sts(":%s SJOIN %lu %s + :@%s", ME, (unsigned long)ts, c->name, CLIENT_NAME(cu->user));
				cu->modes = CMODE_OP;
			}
			else
				cu->modes = 0;
		}

		slog(LG_DEBUG, "m_sjoin(): TS changed for %s (%lu -> %lu)", c->name, (unsigned long)c->ts, (unsigned long)ts);

		c->ts = ts;
		hook_call_event("channel_tschange", c);
	}
	else if (ts > c->ts)
		keep_new_modes = FALSE;

	if (keep_new_modes)
		channel_mode(NULL, c, parc - 3, parv + 2);

	userc = sjtoken(parv[parc - 1], ' ', userv);

	if (keep_new_modes)
		for (i = 0; i < userc; i++)
			chanuser_add(c, userv[i]);
	else
		for (i = 0; i < userc; i++)
		{
			p = userv[i];
			while (*p == '@' || *p == '%' || *p == '+')
				p++;
			/* XXX for TS5 we should mark them deopped
			 * if they were opped and drop modes from them
			 * -- jilles */
			chanuser_add(c, p);
		}

	if (c->nummembers == 0 && !(c->modes & ircd->perm_mode))
		channel_delete(c);
}

static void m_join(sourceinfo_t *si, int parc, char *parv[])
{
	/* -> :1JJAAAAAB JOIN 1127474195 #test +tn */
	boolean_t keep_new_modes = TRUE;
	node_t *n, *tn;
	channel_t *c;
	chanuser_t *cu;
	time_t ts;

	/* JOIN 0 is really a part from all channels */
	/* be sure to allow joins to TS 0 channels -- jilles */
	if (parv[0][0] == '0' && parc <= 2)
	{
		LIST_FOREACH_SAFE(n, tn, si->su->channels.head)
		{
			cu = (chanuser_t *)n->data;
			chanuser_delete(cu->chan, si->su);
		}
		return;
	}

	/* :user JOIN ts chan modestr [key or limits] */
	c = channel_find(parv[1]);
	ts = atol(parv[0]);

	if (!c)
	{
		slog(LG_DEBUG, "m_join(): new channel: %s", parv[1]);
		c = channel_add(parv[1], ts, si->su->server);
	}

	if (ts == 0 || c->ts == 0)
	{
		if (c->ts != 0)
			slog(LG_INFO, "m_join(): server %s changing TS on %s from %lu to 0", si->su->server->name, c->name, (unsigned long)c->ts);
		c->ts = 0;
		hook_call_event("channel_tschange", c);
	}
	else if (ts < c->ts)
	{
		/* the TS changed.  a TS change requires the following things
		 * to be done to the channel:  reset all modes to nothing, remove
		 * all status modes on known users on the channel (including ours),
		 * and set the new TS.
		 */
		clear_simple_modes(c);

		LIST_FOREACH(n, c->members.head)
		{
			cu = (chanuser_t *)n->data;
			if (cu->user->server == me.me)
			{
				/* it's a service, reop */
				sts(":%s PART %s :Reop", CLIENT_NAME(cu->user), c->name);
				sts(":%s SJOIN %lu %s + :@%s", ME, (unsigned long)ts, c->name, CLIENT_NAME(cu->user));
				cu->modes = CMODE_OP;
			}
			else
				cu->modes = 0;
		}
		slog(LG_DEBUG, "m_join(): TS changed for %s (%lu -> %lu)", c->name, (unsigned long)c->ts, (unsigned long)ts);
		c->ts = ts;
		hook_call_event("channel_tschange", c);
	}
	else if (ts > c->ts)
		keep_new_modes = FALSE;

	if (keep_new_modes)
		channel_mode(NULL, c, parc - 2, parv + 2);

	chanuser_add(c, CLIENT_NAME(si->su));
}

static void m_bmask(sourceinfo_t *si, int parc, char *parv[])
{
	unsigned int ac, i;
	char *av[256];
	channel_t *c = channel_find(parv[1]);
	int type;

	/* :1JJ BMASK 1127474361 #services b :*!*@*evil* *!*eviluser1@* */
	if (!c)
	{
		slog(LG_DEBUG, "m_bmask(): got bmask for unknown channel");
		return;
	}

	if (atol(parv[0]) > c->ts)
		return;
	
	type = *parv[2];
	if (!strchr(ircd->ban_like_modes, type))
	{
		slog(LG_DEBUG, "m_bmask(): got unknown type '%c'", type);
		return;
	}

	ac = sjtoken(parv[parc - 1], ' ', av);

	for (i = 0; i < ac; i++)
		chanban_add(c, av[i], type);
}

static void m_part(sourceinfo_t *si, int parc, char *parv[])
{
	int chanc;
	char *chanv[256];
	int i;

	chanc = sjtoken(parv[0], ',', chanv);
	for (i = 0; i < chanc; i++)
	{
		slog(LG_DEBUG, "m_part(): user left channel: %s -> %s", si->su->nick, chanv[i]);

		chanuser_delete(channel_find(chanv[i]), si->su);
	}
}

static void m_nick(sourceinfo_t *si, int parc, char *parv[])
{
	server_t *s;
	user_t *u;

	/* got the right number of args for an introduction? */
	if (parc == 8)
	{
		s = server_find(parv[6]);
		if (!s)
		{
			slog(LG_DEBUG, "m_nick(): new user on nonexistant server: %s", parv[6]);
			return;
		}

		slog(LG_DEBUG, "m_nick(): new user on `%s': %s", s->name, parv[0]);

		u = user_add(parv[0], parv[4], parv[5], NULL, NULL, NULL, parv[7], s, atoi(parv[2]));
		if (u == NULL)
			return;

		user_mode(u, parv[3]);

		/* If server is not yet EOB we will do this later.
		 * This avoids useless "please identify" -- jilles */
		if (s->flags & SF_EOB)
			handle_nickchange(user_find(parv[0]));
	}

	/* if it's only 2 then it's a nickname change */
	else if (parc == 2)
	{
                if (!si->su)
                {       
                        slog(LG_DEBUG, "m_nick(): server trying to change nick: %s", si->s != NULL ? si->s->name : "<none>");
                        return;
                }

		slog(LG_DEBUG, "m_nick(): nickname change from `%s': %s", si->su->nick, parv[0]);

		if (user_changenick(si->su, parv[0], atoi(parv[1])))
			return;

		/* It could happen that our PING arrived late and the
		 * server didn't acknowledge EOB yet even though it is
		 * EOB; don't send double notices in that case -- jilles */
		if (si->su->server->flags & SF_EOB)
			handle_nickchange(si->su);
	}
	else
	{
		int i;
		slog(LG_DEBUG, "m_nick(): got NICK with wrong number of params");

		for (i = 0; i < parc; i++)
			slog(LG_DEBUG, "m_nick():   parv[%d] = %s", i, parv[i]);
	}
}

static void m_uid(sourceinfo_t *si, int parc, char *parv[])
{
	server_t *s;
	user_t *u;

	/* got the right number of args for an introduction? */
	if (parc == 9)
	{
		s = si->s;
		slog(LG_DEBUG, "m_uid(): new user on `%s': %s", s->name, parv[0]);

		u = user_add(parv[0], parv[4], parv[5], NULL, parv[6], parv[7], parv[8], s, atoi(parv[2]));
		if (u == NULL)
			return;

		user_mode(u, parv[3]);

		/* If server is not yet EOB we will do this later.
		 * This avoids useless "please identify" -- jilles
		 */
		if (s->flags & SF_EOB)
			handle_nickchange(user_find(parv[0]));
	}
	else
	{
		int i;
		slog(LG_DEBUG, "m_uid(): got UID with wrong number of params");

		for (i = 0; i < parc; i++)
			slog(LG_DEBUG, "m_uid():   parv[%d] = %s", i, parv[i]);
	}
}

static void m_euid(sourceinfo_t *si, int parc, char *parv[])
{
	server_t *s;
	user_t *u;

	/* got the right number of args for an introduction? */
	if (parc >= 11)
	{
		s = si->s;
		slog(LG_DEBUG, "m_euid(): new user on `%s': %s", s->name, parv[0]);

		u = user_add(parv[0],				/* nick */
			parv[4],				/* user */
			*parv[8] != '*' ? parv[8] : parv[5],	/* hostname */
			parv[5],				/* hostname (visible) */
			parv[6],				/* ip */
			parv[7],				/* uid */
			parv[parc - 1],				/* gecos */
			s,					/* object parent (server) */
			atoi(parv[2]));				/* hopcount */
		if (u == NULL)
			return;

		user_mode(u, parv[3]);
		if (*parv[9] != '*')
			handle_burstlogin(u, parv[9]);

		/* server_eob() cannot know if a user was introduced
		 * with NICK/UID or EUID and handle_nickchange() must
		 * be called exactly once for each new user -- jilles */
		if (s->flags & SF_EOB)
			handle_nickchange(u);
	}
	else
	{
		int i;
		slog(LG_DEBUG, "m_euid(): got EUID with wrong number of params");

		for (i = 0; i < parc; i++)
			slog(LG_DEBUG, "m_euid():   parv[%d] = %s", i, parv[i]);
	}
}

static void m_quit(sourceinfo_t *si, int parc, char *parv[])
{
	slog(LG_DEBUG, "m_quit(): user leaving: %s", si->su->nick);

	/* user_delete() takes care of removing channels and so forth */
	user_delete(si->su);
}

static void m_mode(sourceinfo_t *si, int parc, char *parv[])
{
	if (*parv[0] == '#')
		channel_mode(NULL, channel_find(parv[0]), parc - 1, &parv[1]);
	else
		user_mode(user_find(parv[0]), parv[1]);
}

static void m_tmode(sourceinfo_t *si, int parc, char *parv[])
{
	channel_t *c;

	/* -> :1JJAAAAAB TMODE 1127511579 #new +o 2JJAAAAAB */
	c = channel_find(parv[1]);
	if (c == NULL)
	{
		slog(LG_DEBUG, "m_tmode(): nonexistent channel %s", parv[1]);
		return;
	}

	if (atol(parv[0]) > c->ts)
		return;

	channel_mode(NULL, c, parc - 2, &parv[2]);
}

static void m_kick(sourceinfo_t *si, int parc, char *parv[])
{
	user_t *u = user_find(parv[1]);
	channel_t *c = channel_find(parv[0]);

	/* -> :rakaur KICK #shrike rintaun :test */
	slog(LG_DEBUG, "m_kick(): user was kicked: %s -> %s", parv[1], parv[0]);

	if (!u)
	{
		slog(LG_DEBUG, "m_kick(): got kick for nonexistant user %s", parv[1]);
		return;
	}

	if (!c)
	{
		slog(LG_DEBUG, "m_kick(): got kick in nonexistant channel: %s", parv[0]);
		return;
	}

	if (!chanuser_find(c, u))
	{
		slog(LG_DEBUG, "m_kick(): got kick for %s not in %s", u->nick, c->name);
		return;
	}

	chanuser_delete(c, u);

	/* if they kicked us, let's rejoin */
	if (is_internal_client(u))
	{
		slog(LG_DEBUG, "m_kick(): %s got kicked from %s; rejoining", u->nick, parv[0]);
		join(parv[0], u->nick);
	}
}

static void m_kill(sourceinfo_t *si, int parc, char *parv[])
{
	handle_kill(si, parv[0], parc > 1 ? parv[1] : "<No reason given>");
}

static void m_squit(sourceinfo_t *si, int parc, char *parv[])
{
	slog(LG_DEBUG, "m_squit(): server leaving: %s from %s", parv[0], parv[1]);
	server_delete(parv[0]);
}

static void m_server(sourceinfo_t *si, int parc, char *parv[])
{
	server_t *s;

	slog(LG_DEBUG, "m_server(): new server: %s", parv[0]);
	s = handle_server(si, parv[0], si->s || !ircd->uses_uid ? NULL : ts6sid, atoi(parv[1]), parv[2]);

	if (s != NULL && s->uplink != me.me)
	{
		/* elicit PONG for EOB detection; pinging uplink is
		 * already done elsewhere -- jilles
		 */
		sts(":%s PING %s %s", ME, me.name, s->name);
	}
}

static void m_sid(sourceinfo_t *si, int parc, char *parv[])
{
	/* -> :1JJ SID file. 2 00F :telnet server */
	server_t *s;

	slog(LG_DEBUG, "m_sid(): new server: %s", parv[0]);
	s = handle_server(si, parv[0], parv[2], atoi(parv[1]), parv[3]);

	if (s != NULL && s->uplink != me.me)
	{
		/* elicit PONG for EOB detection; pinging uplink is
		 * already done elsewhere -- jilles
		 */
		sts(":%s PING %s %s", ME, me.name, s->sid);
	}
}

static void m_stats(sourceinfo_t *si, int parc, char *parv[])
{
	handle_stats(si->su, parv[0][0]);
}

static void m_admin(sourceinfo_t *si, int parc, char *parv[])
{
	handle_admin(si->su);
}

static void m_version(sourceinfo_t *si, int parc, char *parv[])
{
	handle_version(si->su);
}

static void m_info(sourceinfo_t *si, int parc, char *parv[])
{
	handle_info(si->su);
}

static void m_whois(sourceinfo_t *si, int parc, char *parv[])
{
	handle_whois(si->su, parv[1]);
}

static void m_trace(sourceinfo_t *si, int parc, char *parv[])
{
	handle_trace(si->su, parv[0], parc >= 2 ? parv[1] : NULL);
}

static void m_away(sourceinfo_t *si, int parc, char *parv[])
{
	handle_away(si->su, parc >= 1 ? parv[0] : NULL);
}

static void m_pass(sourceinfo_t *si, int parc, char *parv[])
{
	/* TS5: PASS mypassword :TS
	 * TS6: PASS mypassword TS 6 :sid */
	if (strcmp(curr_uplink->pass, parv[0]))
	{
		slog(LG_INFO, "m_pass(): password mismatch from uplink; aborting");
		runflags |= RF_SHUTDOWN;
	}

	if (ircd->uses_uid && parc > 3 && atoi(parv[2]) >= 6)
		strlcpy(ts6sid, parv[3], sizeof(ts6sid));
	else
	{
		if (ircd->uses_uid)
		{
			slog(LG_INFO, "m_pass(): uplink does not support TS6, falling back to TS5");
			ircd->uses_uid = FALSE;
		}
		ts6sid[0] = '\0';
	}
}

static void m_error(sourceinfo_t *si, int parc, char *parv[])
{
	slog(LG_INFO, "m_error(): error from server: %s", parv[0]);
}

static void m_encap(sourceinfo_t *si, int parc, char *parv[])
{
	user_t *u;

	if (match(parv[0], me.name))
		return;
	if (!irccasecmp(parv[1], "LOGIN"))
	{
		/* :jilles ENCAP * LOGIN jilles */
		if (!use_rserv_support)
			return;
		if (parc < 3)
			return;
		u = si->su;
		if (u == NULL)
			return;
		/* We used to throw out LOGINs outside of a burst,
		 * but we can't do that anymore since it's used for
		 * SASL users.
		 * --gxti
		 */
		handle_burstlogin(u, parv[2]);
	}
	else if (!irccasecmp(parv[1], "REALHOST"))
	{
		/* :1JJAAAAAC ENCAP * REALHOST localhost.stack.nl */
		if (parc < 3)
			return;
		u = si->su;
		if (u == NULL)
			return;
		strlcpy(u->host, parv[2], HOSTLEN);
	}
	else if (!irccasecmp(parv[1], "CHGHOST"))
	{
		if (parc < 4)
			return;
		u = user_find(parv[2]);
		if (u == NULL)
			return;
		strlcpy(u->vhost, parv[3], HOSTLEN);
		slog(LG_DEBUG, "m_encap(): chghost %s -> %s", u->nick,
				u->vhost);
	}
	else if (!irccasecmp(parv[1], "SASL"))
	{
		/* :08C ENCAP * SASL 08CAAAAAE * S d29vTklOSkFTAGRhdGEgaW4gZmlyc3QgbGluZQ== */
		sasl_message_t smsg;

		if (parc < 6)
			return;

		smsg.uid = parv[2];
		smsg.mode = *parv[4];
		smsg.buf = parv[5];
		hook_call_event("sasl_input", &smsg);
	}
}

static void m_signon(sourceinfo_t *si, int parc, char *parv[])
{
	user_t *u;
	node_t *n, *tn;
	myuser_t *old, *new;

	if((u = user_find(parv[0])) == NULL)
		return;

	/* NICK */
	if (user_changenick(u, parv[0], atoi(parv[3])))
		return;

	handle_nickchange(u); /* If they're logging out, this will bug them about identifying. Or something. */

	/* USER */
	strlcpy(u->user, parv[1], USERLEN);

	/* HOST */
	strlcpy(u->vhost, parv[2], HOSTLEN);

	/* LOGIN */
	if(*parv[4] == '*') /* explicitly unchanged */
		return;
	if(u->myuser == NULL && !strcmp(parv[4], "0")) /* both unset */
		return;
	
	old = u->myuser;
	new = myuser_find(parv[4]);
	if(old == new)
		return;

	if(old)
	{
		old->lastlogin = CURRTIME;
		LIST_FOREACH_SAFE(n, tn, old->logins.head)
		{
			if(n->data == u)
			{
				node_del(n, &old->logins);
				node_free(n);
				break;
			}
		}
		u->myuser = NULL;
	}

	if(new)
	{
		if (is_soper(new))
			snoop("SOPER: \2%s\2 as \2%s\2", u->nick, new->name);

		myuser_notice(nicksvs.nick, new, "%s!%s@%s has just authenticated as you (%s)", u->nick, u->user, u->vhost, new->name);

		u->myuser = new;
		n = node_create();
		node_add(u, n, &new->logins);
	}
}

static void m_capab(sourceinfo_t *si, int parc, char *parv[])
{
	char *p;

	use_euid = FALSE;
	use_rserv_support = FALSE;
	use_tb = FALSE;
	for (p = strtok(parv[0], " "); p != NULL; p = strtok(NULL, " "))
	{
		if (!irccasecmp(p, "EUID"))
		{
			slog(LG_DEBUG, "m_capab(): uplink supports EUID, enabling support.");
			use_euid = TRUE;
		}
		if (!irccasecmp(p, "SERVICES"))
		{
			slog(LG_DEBUG, "m_capab(): uplink has rserv extensions, enabling support.");
			use_rserv_support = TRUE;
		}
		if (!irccasecmp(p, "TB"))
		{
			slog(LG_DEBUG, "m_capab(): uplink does topic bursting, using if appropriate.");
			use_tb = TRUE;
		}
	}

	/* Now we know whether or not we should enable services support,
	 * so burst the clients.
	 *       --nenolod
	 */
	services_init();
}

static void m_chghost(sourceinfo_t *si, int parc, char *parv[])
{
	user_t *u = user_find(parv[0]);

	if (!u)
		return;

	strlcpy(u->vhost, parv[1], HOSTLEN);
}

static void m_motd(sourceinfo_t *si, int parc, char *parv[])
{
	handle_motd(si->su);
}

/* Server ended their burst: warn all their users if necessary -- jilles */
static void server_eob(server_t *s)
{
	node_t *n;

	LIST_FOREACH(n, s->userlist.head)
	{
		handle_nickchange((user_t *)n->data);
	}
}

static server_t *sid_find(char *name)
{
	char sid[4];
	strlcpy(sid, name, 4);
	return server_find(sid);
}

void _modinit(module_t * m)
{
	/* Symbol relocation voodoo. */
	server_login = &shadowircd_server_login;
	introduce_nick = &shadowircd_introduce_nick;
	quit_sts = &shadowircd_quit_sts;
	wallops_sts = &shadowircd_wallops_sts;
	join_sts = &shadowircd_join_sts;
	chan_lowerts = &shadowircd_chan_lowerts;
	kick = &shadowircd_kick;
	msg = &shadowircd_msg;
	notice_user_sts = &shadowircd_notice_user_sts;
	notice_global_sts = &shadowircd_notice_global_sts;
	notice_channel_sts = &shadowircd_notice_channel_sts;
	wallchops = &shadowircd_wallchops;
	numeric_sts = &shadowircd_numeric_sts;
	kill_id_sts = &shadowircd_kill_id_sts;
	part_sts = &shadowircd_part_sts;
	kline_sts = &shadowircd_kline_sts;
	unkline_sts = &shadowircd_unkline_sts;
	topic_sts = &shadowircd_topic_sts;
	mode_sts = &shadowircd_mode_sts;
	ping_sts = &shadowircd_ping_sts;
	ircd_on_login = &shadowircd_on_login;
	ircd_on_logout = &shadowircd_on_logout;
	jupe = &shadowircd_jupe;
	sethost_sts = &shadowircd_sethost_sts;
	fnc_sts = &shadowircd_fnc_sts;
	invite_sts = &shadowircd_invite_sts;
	holdnick_sts = &shadowircd_holdnick_sts;
	svslogin_sts = &shadowircd_svslogin_sts;
	sasl_sts = &shadowircd_sasl_sts;
	next_matching_ban = &shadowircd_next_matching_ban;
	is_valid_host = &shadowircd_is_valid_host;

	mode_list = shadowircd_mode_list;
	ignore_mode_list = shadowircd_ignore_mode_list;
	status_mode_list = shadowircd_status_mode_list;
	prefix_mode_list = shadowircd_prefix_mode_list;
	user_mode_list = shadowircd_user_mode_list;

	ircd = &ShadowIRCd;

	pcommand_add("PING", m_ping, 1, MSRC_USER | MSRC_SERVER);
	pcommand_add("PONG", m_pong, 1, MSRC_SERVER);
	pcommand_add("PRIVMSG", m_privmsg, 2, MSRC_USER);
	pcommand_add("NOTICE", m_notice, 2, MSRC_UNREG | MSRC_USER | MSRC_SERVER);
	pcommand_add("SJOIN", m_sjoin, 4, MSRC_SERVER);
	pcommand_add("PART", m_part, 1, MSRC_USER);
	pcommand_add("NICK", m_nick, 2, MSRC_USER | MSRC_SERVER);
	pcommand_add("QUIT", m_quit, 1, MSRC_USER);
	pcommand_add("MODE", m_mode, 2, MSRC_USER | MSRC_SERVER);
	pcommand_add("KICK", m_kick, 2, MSRC_USER | MSRC_SERVER);
	pcommand_add("KILL", m_kill, 1, MSRC_USER | MSRC_SERVER);
	pcommand_add("SQUIT", m_squit, 1, MSRC_USER | MSRC_SERVER);
	pcommand_add("SERVER", m_server, 3, MSRC_UNREG | MSRC_SERVER);
	pcommand_add("STATS", m_stats, 2, MSRC_USER);
	pcommand_add("ADMIN", m_admin, 1, MSRC_USER);
	pcommand_add("VERSION", m_version, 1, MSRC_USER);
	pcommand_add("INFO", m_info, 1, MSRC_USER);
	pcommand_add("WHOIS", m_whois, 2, MSRC_USER);
	pcommand_add("TRACE", m_trace, 1, MSRC_USER);
	pcommand_add("AWAY", m_away, 0, MSRC_USER);
	pcommand_add("JOIN", m_join, 1, MSRC_USER);
	pcommand_add("PASS", m_pass, 1, MSRC_UNREG);
	pcommand_add("ERROR", m_error, 1, MSRC_UNREG | MSRC_SERVER);
	pcommand_add("TOPIC", m_topic, 2, MSRC_USER);
	pcommand_add("TB", m_tb, 3, MSRC_SERVER);
	pcommand_add("ENCAP", m_encap, 2, MSRC_USER | MSRC_SERVER);
	pcommand_add("CAPAB", m_capab, 1, MSRC_UNREG);
	pcommand_add("UID", m_uid, 9, MSRC_SERVER);
	pcommand_add("BMASK", m_bmask, 4, MSRC_SERVER);
	pcommand_add("TMODE", m_tmode, 3, MSRC_USER | MSRC_SERVER);
	pcommand_add("SID", m_sid, 4, MSRC_SERVER);
	pcommand_add("CHGHOST", m_chghost, 2, MSRC_USER | MSRC_SERVER);
	pcommand_add("MOTD", m_motd, 1, MSRC_USER);
	pcommand_add("SIGNON", m_signon, 5, MSRC_USER);
	pcommand_add("EUID", m_euid, 11, MSRC_SERVER);

	hook_add_event("server_eob");
	hook_add_hook("server_eob", (void (*)(void *))server_eob);

	m->mflags = MODTYPE_CORE;

	pmodule_loaded = TRUE;
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */