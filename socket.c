/*
 * ffproxy (c) 2002, 2003 Niklas Olmes <niklas@noxa.de>
 * http://faith.eu.org
 * 
 * $Id: socket.c,v 2.1 2004/12/31 08:59:15 niklas Exp $
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

#include <stdio.h>

#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif
#include <string.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_NETDB_H
# include <netdb.h>
#endif
#include <poll.h>

#include "req.h"
#include "cfg.h"
#include "print.h"
#include "request.h"
#include "dns.h"
#include "access.h"
#include "socket.h"

#define DFLT_PORT	8080
#ifndef INFTIM
#define INFTIM	-1
#endif

void
open_socket(void)
{
	extern struct cfg config;
	struct sockaddr claddr;
	struct addrinfo hints[2], *res;
	struct clinfo  *clinfo;
	struct pollfd	s[2];
	socklen_t       claddr_len;
	pid_t           pid;
	void           *foo;
	char		strport[6];
	char           *ip_add;
	int             st, cl, i;
	int		num_fd;
	int		isipv4;

	if (config.port == 0)
		config.port = DFLT_PORT;
	(void) snprintf(strport, sizeof(strport), "%d", config.port);

	num_fd = 0;
	if (config.bind_ipv4)
		num_fd++;
	if (config.bind_ipv6)
		num_fd++;
		
	i = 0;
	(void) memset(s, 0, sizeof(s));
	s[0].fd = s[1].fd = 0;
	while (i < num_fd) {
		(void) memset(&hints[i], 0, sizeof(struct addrinfo));
		hints[i].ai_family = (i == 0 && config.bind_ipv4) ? PF_INET : PF_INET6;
		hints[i].ai_socktype = SOCK_STREAM;
		hints[i].ai_flags = AI_PASSIVE;
		if (i == 0 && config.bind_ipv4) {
			if (*config.ipv4 == '\0')
				ip_add = NULL;
			else
				ip_add = config.ipv4;
		} else {
			if (*config.ipv6 == '\0')
				ip_add = NULL;
			else
				ip_add = config.ipv6;
		}
		if (getaddrinfo(ip_add, strport, &hints[i], &res))
			fatal("getaddrinfo() failed for (%s) %s", ip_add, (i == 0 && config.bind_ipv4) ? "IPv4" : "IPv6");
		if ((s[i].fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0) {
			if (i == 1 || (config.ipv6 && !config.ipv4))
				fatal("socket() failed for IPv6, perhaps your system does not support IPv6.\nTry -B or `bind_ipv6 no' to disable IPv6 binding.\nError message");
			else
				fatal("socket() failed for IPv4");
		}
		if (setsockopt(s[i].fd, SOL_SOCKET, SO_REUSEADDR, &foo, sizeof(foo)) != 0) {
			(void) close(s[i].fd);
			fatal("setsockopt() failed for (%s) %s", ip_add, (i == 0 && config.bind_ipv4) ? "IPv4" : "IPv6");
		}
		if (bind(s[i].fd, (struct sockaddr *) res->ai_addr, res->ai_addrlen) < 0) {
			(void) close(s[i].fd);
#if defined(__linux__)
			if (i == 1)
				fatal("could not bind to IPv6, possibly because of\nLinux's ``feature'' to bind to IPv4 also.\nTry -b or binding to specific IPv6 address via -C\nif you're using IPv6 with Linux 2.4\nError message");
#endif /* __linux__ */
			fatal("bind() failed for (%s) %s", ip_add, (i == 0 && config.bind_ipv4) ? "IPv4" : "IPv6");
		}
		if (listen(s[i].fd, config.backlog) != 0) {
			(void) close(s[i].fd);
			fatal("listen() failed for (%s) %s",ip_add, (i == 0 && config.bind_ipv4) ? "IPv4" : "IPv6");
		}
		freeaddrinfo(res);
	
		s[i].events = POLLIN;
		i++;
	}
	
	if (config.bind_ipv4)
		info("waiting for requests on %s port %d (IPv4)", *config.ipv4 ? config.ipv4 : "(any)", config.port);
	if (config.bind_ipv6)
		info("waiting for requests on %s port %d (IPv6)", *config.ipv6 ? config.ipv6 : "(any)", config.port);

	claddr_len = sizeof(claddr);
	config.ccount = 0;
	cl = 0;
	isipv4 = config.bind_ipv4;

	for (;;) {
		if (config.ccount >= config.childs) {
			(void) usleep(50000);
			continue;
		}
		if (num_fd == 2) {
			i = poll(s, 2, INFTIM);
			if (i < 1) {
				continue;
			} else {
				if (s[0].revents == POLLIN) {
					st = s[0].fd;
					isipv4 = 1;
				} else {
					st = s[1].fd;
					isipv4 = 0;
				}
			}
		} else
			st = s[0].fd;
		if ((cl = accept(st, (struct sockaddr *) & claddr, &claddr_len)) == -1) {
			DEBUG(("open_socket() => accept() failed"));
			continue;
		}

		DEBUG(("open_socket() => connection, checking access"));
		clinfo = identify(&claddr, (socklen_t) isipv4 ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6));
		if (check_access(clinfo) != 0) {
			DEBUG(("open_socket() => no access"));
			if (config.logrequests)
				info("connection attempt from (%s) [%s], ACCESS DENIED", clinfo->name, clinfo->ip);
			free(clinfo);
			(void) close(cl);
			continue;
		}
		if (config.logrequests)
			info("connection attempt from (%s) [%s], access granted", clinfo->name, clinfo->ip);

		if ((pid = fork()) == -1) {
			DEBUG(("open_socket() => fork() failed"));
			free(clinfo);
			(void) close(cl);
			continue;
		} else if (pid == 0) {
			(void) close(s[0].fd);
			if (num_fd == 2)
				(void) close(s[1].fd);
			setup_log_slave();
			handle_request(cl, clinfo);
			free(clinfo);
			(void) close(cl);
			exit(0);
		} else {
			free(clinfo);
			config.ccount++;
			(void) close(cl);
		}
	}

	/* NOTREACHED */
}
