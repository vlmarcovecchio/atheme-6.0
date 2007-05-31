/*
 * Copyright (c) 2005 William Pitcock, et al.
 * Rights to this code are as documented in doc/LICENSE.
 *
 * This file contains code for the CService QUIET/UNQUIET function.
 *
 * $Id: quiet.c 8251 2007-05-12 21:10:06Z jilles $
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"chanserv/quiet", FALSE, _modinit, _moddeinit,
	"$Id: quiet.c 8251 2007-05-12 21:10:06Z jilles $",
	"Atheme Development Group <http://www.atheme.org>"
);

static void cs_cmd_quiet(sourceinfo_t *si, int parc, char *parv[]);
static void cs_cmd_unquiet(sourceinfo_t *si, int parc, char *parv[]);

command_t cs_quiet = { "QUIET", N_("Sets a quiet on a channel."),
                        AC_NONE, 2, cs_cmd_quiet };
command_t cs_unquiet = { "UNQUIET", N_("Removes a quiet on a channel."),
			AC_NONE, 2, cs_cmd_unquiet };


list_t *cs_cmdtree;
list_t *cs_helptree;

void _modinit(module_t *m)
{
	MODULE_USE_SYMBOL(cs_cmdtree, "chanserv/main", "cs_cmdtree");
	MODULE_USE_SYMBOL(cs_helptree, "chanserv/main", "cs_helptree");

	if (!strchr(ircd->ban_like_modes, 'q'))
	{
		slog(LG_INFO, "Module %s requires a ban-like mode +q, refusing to load.", m->header->name);
		m->mflags = MODTYPE_FAIL;
		return;
	}

        command_add(&cs_quiet, cs_cmdtree);
	command_add(&cs_unquiet, cs_cmdtree);

	help_addentry(cs_helptree, "QUIET", "help/cservice/quiet", NULL);
	help_addentry(cs_helptree, "UNQUIET", "help/cservice/unquiet", NULL);
}

void _moddeinit()
{
	command_delete(&cs_quiet, cs_cmdtree);
	command_delete(&cs_unquiet, cs_cmdtree);

	help_delentry(cs_helptree, "QUIET");
	help_delentry(cs_helptree, "UNQUIET");
}

static void cs_cmd_quiet(sourceinfo_t *si, int parc, char *parv[])
{
	char *channel = parv[0];
	char *target = parv[1];
	channel_t *c = channel_find(channel);
	mychan_t *mc = mychan_find(channel);
	user_t *tu;

	if (!channel || !target)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "QUIET");
		command_fail(si, fault_needmoreparams, _("Syntax: QUIET <#channel> <nickname|hostmask>"));
		return;
	}

	if (!mc)
	{
		command_fail(si, fault_nosuch_target, _("\2%s\2 is not registered."), channel);
		return;
	}

	if (!c)
	{
		command_fail(si, fault_nosuch_target, _("\2%s\2 is currently empty."), channel);
		return;
	}

	if (!si->smu)
	{
		command_fail(si, fault_noprivs, _("You are not logged in."));
		return;
	}

	if (!chanacs_source_has_flag(mc, si, CA_REMOVE))
	{
		command_fail(si, fault_noprivs, _("You are not authorized to perform this operation."));
		return;
	}
	
	if (metadata_find(mc, METADATA_CHANNEL, "private:close:closer"))
	{
		command_fail(si, fault_noprivs, _("\2%s\2 is closed."), channel);
		return;
	}

	if (validhostmask(target))
	{
		modestack_mode_param(chansvs.nick, c, MTYPE_ADD, 'q', target);
		chanban_add(c, target, 'q');
		logcommand(si, CMDLOG_DO, "%s QUIET %s", mc->name, target);
		if (!chanuser_find(mc->chan, si->su))
			command_success_nodata(si, _("Quieted \2%s\2 on \2%s\2."), target, channel);
		return;
	}
	else if ((tu = user_find_named(target)))
	{
		char hostbuf[BUFSIZE];

		hostbuf[0] = '\0';

		strlcat(hostbuf, "*!*@", BUFSIZE);
		strlcat(hostbuf, tu->vhost, BUFSIZE);

		modestack_mode_param(chansvs.nick, c, MTYPE_ADD, 'q', hostbuf);
		chanban_add(c, hostbuf, 'q');
		logcommand(si, CMDLOG_DO, "%s QUIET %s (for user %s!%s@%s)", mc->name, hostbuf, tu->nick, tu->user, tu->vhost);
		if (!chanuser_find(mc->chan, si->su))
			command_success_nodata(si, _("Quieted \2%s\2 on \2%s\2."), target, channel);
		return;
	}
	else
	{
		command_fail(si, fault_badparams, _("Invalid nickname/hostmask provided: \2%s\2"), target);
		command_fail(si, fault_badparams, _("Syntax: QUIET <#channel> <nickname|hostmask>"));
		return;
	}
}

static void cs_cmd_unquiet(sourceinfo_t *si, int parc, char *parv[])
{
        char *channel = parv[0];
        char *target = parv[1];
        channel_t *c = channel_find(channel);
	mychan_t *mc = mychan_find(channel);
	user_t *tu;
	chanban_t *cb;

	if (!channel)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "UNQUIET");
		command_fail(si, fault_needmoreparams, _("Syntax: UNQUIET <#channel> <nickname|hostmask>"));
		return;
	}

	if (!target)
	{
		if (si->su == NULL)
		{
			command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "UNQUIET");
			command_fail(si, fault_needmoreparams, _("Syntax: UNQUIET <#channel> <nickname|hostmask>"));
			return;
		}
		target = si->su->nick;
	}

	if (!mc)
	{
		command_fail(si, fault_nosuch_target, _("\2%s\2 is not registered."), channel);
		return;
	}

	if (!c)
	{
		command_fail(si, fault_nosuch_target, _("\2%s\2 is currently empty."), channel);
		return;
	}

	if (!si->smu)
	{
		command_fail(si, fault_noprivs, _("You are not logged in."));
		return;
	}

	if (!chanacs_source_has_flag(mc, si, CA_REMOVE))
	{
		command_fail(si, fault_noprivs, _("You are not authorized to perform this operation."));
		return;
	}

	if ((tu = user_find_named(target)))
	{
		node_t *n, *tn;
		char hostbuf[BUFSIZE], hostbuf2[BUFSIZE], hostbuf3[BUFSIZE];
		int count = 0;

		snprintf(hostbuf, BUFSIZE, "%s!%s@%s", tu->nick, tu->user, tu->host);
		snprintf(hostbuf2, BUFSIZE, "%s!%s@%s", tu->nick, tu->user, tu->vhost);
		/* will be nick!user@ if ip unknown, doesn't matter */
		snprintf(hostbuf3, BUFSIZE, "%s!%s@%s", tu->nick, tu->user, tu->ip);

		LIST_FOREACH_SAFE(n, tn, c->bans.head)
		{
			cb = n->data;

			if (cb->type != 'q')
				continue;
			slog(LG_DEBUG, "cs_unquiet(): iterating %s on %s", cb->mask, c->name);

			if (!match(cb->mask, hostbuf) || !match(cb->mask, hostbuf2) || !match(cb->mask, hostbuf3) || !match_cidr(cb->mask, hostbuf3))
			{
				logcommand(si, CMDLOG_DO, "%s UNQUIET %s (for user %s)", mc->name, cb->mask, hostbuf2);
				modestack_mode_param(chansvs.nick, c, MTYPE_DEL, 'q', cb->mask);
				chanban_delete(cb);
				count++;
			}
		}
		if (count > 0)
			command_success_nodata(si, _("Unquieted \2%s\2 on \2%s\2 (%d ban%s removed)."),
				target, channel, count, (count != 1 ? "s" : ""));
		else
			command_success_nodata(si, _("No quiets found matching \2%s\2 on \2%s\2."), target, channel);
		return;
	}
	else if ((cb = chanban_find(c, target, 'q')) != NULL || validhostmask(target))
	{
		if (cb)
		{
			modestack_mode_param(chansvs.nick, c, MTYPE_DEL, 'q', target);
			chanban_delete(cb);
			logcommand(si, CMDLOG_DO, "%s UNQUIET %s", mc->name, target);
			if (!chanuser_find(mc->chan, si->su))
				command_success_nodata(si, _("Unquieted \2%s\2 on \2%s\2."), target, channel);
		}
		else
			command_fail(si, fault_nosuch_key, _("No such quiet \2%s\2 on \2%s\2."), target, channel);

		return;
	}
        else
        {
		command_fail(si, fault_badparams, _("Invalid nickname/hostmask provided: \2%s\2"), target);
		command_fail(si, fault_badparams, _("Syntax: UNQUIET <#channel> [nickname|hostmask]"));
		return;
        }
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */