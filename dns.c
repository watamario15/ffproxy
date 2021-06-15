/*
 * ffproxy (c) 2002, 2003 Niklas Olmes <niklas@noxa.de>
 * http://faith.eu.org
 * 
 * $Id: dns.c,v 2.1 2004/12/31 08:59:15 niklas Exp $
 * 
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 675
 * Mass Ave, Cambridge, MA 02139, USA.
 */

#include "configure.h"
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif

#include <stdio.h>
#include <string.h>
#ifdef HAVE_NETDB_H
# include <netdb.h>
#endif

#include "req.h"
#include "alloc.h"
#include "print.h"
#include "dns.h"

static char	*ip_to_a(in_addr_t);

in_addr_t
resolve(const char *h)
{
	struct hostent *hp;
	in_addr_t	ip;

	if ((ip = inet_addr(h)) != INADDR_NONE)
		return ip;

	if ((hp = gethostbyname(h)) == NULL)
		return INADDR_NONE;
	else {
		(void) memcpy(&ip, hp->h_addr, hp->h_length);
		return ip;
	}
}

static char    *
ip_to_a(in_addr_t ip)
{
	char           *p;
	struct in_addr  addr;

	addr.s_addr = ip;
	p = inet_ntoa(addr);

	return p;
}

char           *
resolve_to_a(const char *h)
{
	char           *p;

	p = ip_to_a(resolve(h));

	return p;
}

struct clinfo  *
identify(const struct sockaddr * addr, socklen_t salen)
{
	struct clinfo  *host;

	host = (struct clinfo *) my_alloc(sizeof(struct clinfo));
	(void) memset(host, 0, sizeof(struct clinfo));

	if (getnameinfo(addr, salen, host->name, sizeof(host->name), NULL, 0, NI_NAMEREQD))
		*host->name = '\0';
	DEBUG(("identify() => getnameinfo() for Reverse Name returned (%s)", host->name));

	if (getnameinfo(addr, salen, host->ip, sizeof(host->ip), NULL, 0, NI_NUMERICHOST))
		*host->ip = '\0';
	DEBUG(("identify() => getnameinfo() for IP Address returned (%s)", host->ip));

	return host;
}
