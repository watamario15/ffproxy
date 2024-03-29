/*
 * ffproxy (c) 2002-2004 Niklas Olmes <niklas@noxa.de>
 * http://faith.eu.org
 * 
 * $Id: request.c,v 2.1 2004/12/31 08:59:15 niklas Exp $
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

#include <stdio.h>
#include <string.h>

#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif 

#include <ctype.h>

#include "req.h"
#include "cfg.h"
#include "msg.h"
#include "alloc.h"
#include "print.h"
#include "http.h"
#include "filter.h"
#include "poll.h"
#include "request.h"
#include "configure.h"

#ifndef ORIGINAL
#include <openssl/sha.h> // SHA Library
#include <time.h>
#endif

static int      read_header(int, struct req *);
static char     sgetc(int);
static size_t   getline_int(int, char[], int);
static int      do_request(int, struct req *);

void
handle_request(int cl, struct clinfo * clinfo)
{
	extern struct cfg config;
	struct req      r;
	char            buf[2048];

keep_alive:
	(void) memset(&r, 0, sizeof(r));
	r.cl = clinfo;

	if (getline_int(cl, buf, sizeof(buf)) < 1)
		*buf = '\0';
	// printf("Req (Line 1): %s\n", buf); // ***** Request Header (Line 1) *****

	if ((http_url(&r, buf)) == 0) {
		int             i;
		r.loop = 0;

		if (r.type == CONNECT) {
			DEBUG(("handle_request => CONNECT request detected"));
			if (read_header(cl, &r) != 0) {
				info("invalid CONNECT header from (%s) [%s]",
					clinfo->name, clinfo->ip);
				err_msg(cl, &r, E_INV);
			} else if (r.port != 443 && ! config.unr_con) {
				info("invalid CONNECT port (%d) for host (%s) from (%s) [%s]",
					r.port, r.host, clinfo->name, clinfo->ip);
				err_msg(cl, &r, E_INV);
			} else if (filter_request(&r) != 0) {
				info("filtered CONNECT request for (%s:%d) from (%s) [%s]",
					r.host, r.port, clinfo->name, clinfo->ip);
			} else {
				// printf("URL(CONNECT): %s\n", r.url); // ***** Requesting URL (CONNECT) *****
				if (config.logrequests) {
					if (r.port == 443)
						info("HTTPS CONNECT to (%s) from (%s) [%s]",
							r.host, clinfo->name, clinfo->ip);
					else
						info("CONNECT to host (%s:%d) from (%s) [%s]",
							r.host, r.port, clinfo->ip);
				}
				i = do_request(cl, &r);
				switch (i) {
				case E_INV:
					info("invalid CONNECT request for (%s:%d) from (%s) [%s]", r.host, r.port, clinfo->name, clinfo->ip);
					break;
				case E_RES:
					info("resolve failure for host (%s) from (%s) [%s]", r.host, clinfo->name, clinfo->ip);
					break;
				case E_CON:
					info("connection failure for host (%s) from (%s) [%s]", r.host, clinfo->name, clinfo->ip);
					break;
				default:
					i = 0;
				}
				if (i != 0) {
					err_msg(cl, &r, i);
					r.kalive = 0;
				}
			}
			i = 0;
			while (r.header[i] != NULL)
				free(r.header[i++]);
			r.header[0] = NULL;
		} else if (read_header(cl, &r) != 0) {
			info("invalid header from (%s) [%s]", clinfo->name, clinfo->ip);
			err_msg(cl, &r, E_INV);

			i = 0;
			while (r.header[i] != NULL)
				free(r.header[i++]);
			r.header[0] = NULL;
		} else if (filter_request(&r) != 0) {
			info("filtered request for URL (%s) from (%s) [%s]", r.url, clinfo->name, clinfo->ip);
			if (r.loop)
				warn("LOOP DETECTED for URL (%s) from (%s) [%s]", r.url, clinfo->name, clinfo->ip);
			else
				err_msg(cl, &r, E_FIL);

			i = 0;
			while (r.header[i] != NULL)
				free(r.header[i++]);
			r.header[0] = NULL;
		} else {
			// printf("URL(Others): %s\n", r.url); // ***** Requesting URL (Others) *****
			if (config.logrequests)
				info("request for URL (%s) from (%s) [%s]", r.url, clinfo->name, clinfo->ip);

			i = do_request(cl, &r);
			switch (i) {
			case E_INV:
				info("invalid request for URL (%s) from (%s) [%s]", r.url, clinfo->name, clinfo->ip);
				break;
			case E_RES:
				info("resolve failure for host (%s) from (%s) [%s]", r.host, clinfo->name, clinfo->ip);
				// Wayback Machine に request 送信/受信
				// これでうまくいったら i = 0;
				break;
			case E_CON:
				info("connection failure for host (%s) from (%s) [%s]", r.host, clinfo->name, clinfo->ip);
				break;
			case E_POST:
				info("failure while post for URL (%s) from (%s) [%s]", r.url, clinfo->name, clinfo->ip);
				break;
			case E_FIL:
				info("filtered request for URL (%s) from (%s) [%s]", r.url, clinfo->name, clinfo->ip);
				break;
			default:
				i = 0;
			}
			if (i != 0) {
				err_msg(cl, &r, i);
				r.kalive = 0;
			}

			i = 0;
			while (r.header[i] != NULL)
				free(r.header[i++]);
			r.header[0] = NULL;

			if (config.kalive && r.kalive && r.clen > 0L)
				goto keep_alive;
		}
	} else {
		if (*buf == '\0') {
			;
		} else {
			info("invalid request from (%s) [%s]", clinfo->name, clinfo->ip);
		}
	}
}

static int
read_header(int cl, struct req * r)
{
	size_t          len, i;
	char            buf[2048];
	char           *b, *p;

	i = 0;
	while ((len = getline_int(cl, buf, sizeof(buf))) > 0 && i < sizeof(r->header) - 1) {
		// printf("Read_Header: %s\n", buf); // ***** Request Header (from line 2) *****
		b = buf;
		while (isspace((int) *b) && *(b++) != '\0');
		if (*b == '\0')
			continue;

		p = (char *) my_alloc(len + 1);
		(void) strcpy(p, b);
		r->header[i] = p;

		DEBUG(("read_header() => entry %d (%s)", i, r->header[i]));

		i++;
		
		if (r->relative && http_rel(r, p) != 0) {
			r->header[i] = NULL;
			return 1;
		}
	}
	r->header[i] = NULL;

	if (i >= sizeof(r->header) - 1)
		return 1;

	return 0;
}

static char
sgetc(int s)
{
	char            c;

	if (read(s, &c, 1) != 1 || c < 1)
		return -1;
	else
		return c;
}

static          size_t
getline_int(int s, char buf[], int len)
{
	int             c;
	size_t          i;

	if (my_poll(s, IN) <= 0)
		return 0;

	i = 0;
	while (--len > 0) {
		c = sgetc(s);
		if (c == '\n' || c == -1)
			break;
		else if (c != '\r')
			buf[i++] = c;
	}
	buf[i] = '\0';

	return i;
}

#ifndef ORIGINAL
int compute_sha256(unsigned char *src, unsigned int src_len, unsigned char *buffer){
	SHA256_CTX c;
	SHA256_Init(&c);
	SHA256_Update(&c, src, src_len);
	SHA256_Final(buffer, &c);
	return 0;
}

void hash2hex(unsigned char hash[32], char hex[65]){
	int hex_index = 0, i;
	char temp[3] = {'\0'};

	hex[0] = '\0';
	for(i=0; i<32; i++){
		sprintf(temp, "%02x", hash[i]);
		strcat(hex, temp);
	}
}

void ExtractHomedir(char dest[PATH_MAX]){
	int i;
	char *temp = getenv("HOME");
	if(!temp){
		dest[0] = '\0';
		return;
	}

	for(i=0; i<strlen(temp); i++){
		if(i>=PATH_MAX){
			dest[0] = '\0';
			break;
		}
		if(temp[i] != ':') dest[i] = temp[i];
		else{
			if(dest[i-1] == '/') dest[i-1] = '\0';
			else dest[i] = '\0';
			break;
		}
	}
}

// <title> から </title> (大文字小文字区別なし)の範囲を取得
static int ExtractTitle(char body[], int len, char title[]) {
	int title_flag = 0 /* タイトル未取得:0, 既取得:1 */, title_index = 0, tag_match = 0, k = 0, l;
	char *tag1 = "<title>", *tag2 = "<TITLE>";
	
	while (k < len) {
		l = tag_match;
		if(tag_match != 7) {
			while (1) {
				if (*(body + k) == *(tag1 + l) || *(body + k) == *(tag2 + l)) {
					k++; l++;
					if (k == len || l == 7) {
						tag_match = l;
						break;
					}
				}
				else {
					tag_match = 0;
					k++;
					break;
				}
			}
		}
		else {
			while (k < len && *(body + k) != '<') {
				title[title_index] = *(body + k);
				title_index++;
				k++;
			}
			if (*(body + k) == '<') {
				title_flag = 1;
				break;
			} else {
				title_flag = 0;
				k++;
				break;
			}
		}
	}
	title[title_index] = '\0';
	return title_flag;
}

// search は小文字で与えること。
static int judge(char *head, char search[]){
	int shortl = strlen(search), longl = strlen(head), i, j;

	for(i=0; i<=longl-shortl; i++){
		if(head[i] == '\r' || head[i] == '\n') return 1;
		for(j=0; j<shortl; j++){
			if(head[j+i] == '\r' || head[j+i] == '\n') return 1;
			if(isalpha(search[j])){
                if(head[j+i] != search[j] && head[j+i] != search[j]-('a'-'A')) break;
            }else{
                if(head[j+i] != search[j]) break;
            }
			if(j == shortl-1) return 0;
		}
	}

	return 1;
}

// word は小文字で与えること。
static char *search(char str[], char word[]){
    int wordl = strlen(word), strl = strlen(str), i=0, j;

    while(1){
        for(j=0; j<wordl; j++){
            if(isalpha(word[j])){
                if(str[j+i] != word[j] && str[j+i] != word[j]-('a'-'A')) break;
            }else{
                if(str[j+i] != word[j]) break;
            }
			if(j == wordl-1) return str+j+i+1;
		}
        i+=j;
        while(str[i] != '\n'){
            if(str[i] == '\0') return NULL;
            i++;
        }
        i++;
    }
	
    return NULL;
}
#endif

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#ifdef HAVE_SYS_SELECT_H
# include <sys/select.h>
#endif
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif

#ifdef HAVE_NETDB_H
# include <netdb.h>
#endif
#include <stdio.h>
#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif
#include <string.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include "dns.h"

static int
do_request(int cl, struct req * r)
{
	extern struct cfg config;
	unsigned long   ip;
	int             s;
	void           *foo;
	size_t          len, i;
	char            buf[4096];

#ifndef ORIGINAL
	char histpath[PATH_MAX], cachepath[PATH_MAX], hexurl[65], title[256], *buf_ptr;
	unsigned char hashurl[32];
	int isCachable = 1, gottenTitle = 0;
	FILE* fp;

	ExtractHomedir(cachepath);
	if(cachepath[0] && PATH_MAX-strlen(cachepath) >= sizeof("/ffproxy/history.csv")+64){
		strcpy(histpath, cachepath);
		strcat(cachepath, "/ffproxy/Cache/");
		strcat(histpath, "/ffproxy/history.csv");
	}else{
		strcpy(cachepath, "/usr/local/etc/ffproxy/Cache/");
		strcpy(histpath, "/usr/local/etc/ffproxy/history.csv");
	}
#endif

	len = 0;
	ip = 0L;
	s = 0;

#ifndef ORIGINAL
	compute_sha256(r->url, strlen(r->url), hashurl);
	hash2hex(hashurl, hexurl);
	strcat(cachepath, hexurl);
	fp = fopen(cachepath, "rb");
	if(fp){
		while((len = fread(buf, 1, sizeof(buf), fp)) > 0) write(cl, buf, len);
		fclose(fp);
		return 0;
	}
#endif

	if (config.use_ipv6 && (config.aux_proxy_ipv6 || *config.proxyhost == '\0')) {
		struct addrinfo hints, *res, *res0;
		char port[6];

		DEBUG(("do_request() => trying ipv6"));

		port[0] = '\0';
		(void) memset(&hints, 0, sizeof(hints));
		hints.ai_family = PF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;

		if (*config.proxyhost != '\0' && config.proxyport) {
			DEBUG(("do_request() => trying ipv6 for proxy %s port %d", config.proxyhost, config.proxyport));
			(void) snprintf(port, 6, "%d", config.proxyport);
			if (getaddrinfo(config.proxyhost, port, &hints, &res)) {
				DEBUG(("do_request() => getaddrinfo() failed for proxy %s", config.proxyhost));

				return E_RES;
			}
		} else {
			(void) snprintf(port, 6, "%d", r->port);
			if (getaddrinfo(r->host, port, &hints, &res)) {
				DEBUG(("do_request() => getaddrinfo() failed for %s", r->host));

				return E_RES;
			}
		}

		s = -1;
		for (res0 = res; res; res = res->ai_next) {
			if ((s = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0)
				continue;
			else if (connect(s, res->ai_addr, res->ai_addrlen) < 0) {
				(void) close(s);
				s = -1;
				continue;
			} else
				break;
		}
		freeaddrinfo(res0);

		if (s == -1) {
			if (*config.proxyhost != '\0' && config.proxyport) {
				DEBUG(("do_request() => socket() or connect() after getaddrinfo() failed for proxy %s port %d", config.proxyhost, config.proxyport));
			} else {
				DEBUG(("do_request() => socket() or connect() after getaddrinfo() failed for %s port %d", r->host, r->port));
			}
			return E_CON;
		}
	} else {
		struct sockaddr_in addr;

		DEBUG(("do_request() => not trying ipv6"));
		
		(void) memset(&addr, 0, sizeof(addr));

		if (*config.proxyhost != '\0' && config.proxyport) {
			DEBUG(("do_request() => using aux proxy w/o trying ipv6"));
			if ((addr.sin_addr.s_addr = resolve(config.proxyhost)) == INADDR_NONE) {
				DEBUG(("do_request() => resolve failure for proxy %s", config.proxyhost));
				return E_RES;
			}
			addr.sin_port = htons(config.proxyport);
			addr.sin_family = AF_INET;
		} else {
			if ((ip = resolve(r->host)) == INADDR_NONE) {
				DEBUG(("do_request() => resolve failure for %s", r->host));
				return E_RES;
			}
			addr.sin_addr.s_addr = ip;
			addr.sin_port = htons(r->port);
			addr.sin_family = AF_INET;
		}

		if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
			DEBUG(("do_request() => socket() failed for %s port %d", r->host, r->port));
			return E_CON;
		} else if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &foo, sizeof(foo)) != 0) {
			DEBUG(("do_request() => setsockopt() failed for %s port %d", r->host, r->port));
			return E_CON;
		} else if (connect(s, (struct sockaddr *) & addr, sizeof(addr)) == -1) {
			DEBUG(("do_request() => connect() failed for %s port %d", r->host, r->port));
			return E_CON;
		}
	}

#ifdef USE_DEBUG
	i = 0;
	DEBUG(("do_request() => header is:"));
	while (r->header[i] != NULL)
		DEBUG(("=> [%s]", r->header[i++]));
#endif

	if (r->vmajor >= 1 && r->vminor >= 0)
		r->vmajor = 1, r->vminor = 0;

	if (config.accel && config.accelusrhost)
		len = snprintf(buf, sizeof(buf),
			       "%s %s HTTP/%d.%d\r\n",
			       ((r->type == GET) ? "GET"
				: ((r->type) == HEAD) ? "HEAD" : "POST"),
			       (*config.proxyhost && config.proxyport) != '\0' ? r->url :  r->urlpath,
			       r->vmajor, r->vminor);
	else if (r->port == 80)
		len = snprintf(buf, sizeof(buf),
			       "%s %s HTTP/%d.%d\r\n"
			       "Host: %s\r\n",
			       ((r->type == GET) ? "GET"
				: ((r->type) == HEAD) ? "HEAD" : "POST"),
			       (*config.proxyhost && config.proxyport) != '\0' ? r->url :  r->urlpath,
			       r->vmajor, r->vminor,
			       r->host);
	else if (r->port == 443 || r->type == CONNECT) {
		*buf = '\0';
		len = 0;
	} else
		len = snprintf(buf, sizeof(buf),
			       "%s %s HTTP/%d.%d\r\n"
			       "Host: %s:%d\r\n",
			       ((r->type == GET) ? "GET"
				: ((r->type) == HEAD) ? "HEAD" : "POST"),
			       (*config.proxyhost && config.proxyport) != '\0' ? r->url :  r->urlpath,
			       r->vmajor, r->vminor,
			       r->host, r->port);

	if (r->type != CONNECT) {
		i = 0;
		while (r->header[i] != NULL) {
			len += strlen(r->header[i]) + strlen("\r\n");
			if (len < sizeof(buf)) {
				(void) strncat(buf, r->header[i++], len);
				(void) strncat(buf, "\r\n", strlen("\r\n"));
			} else {
				DEBUG(("do_request() => header too big"));
				(void) close(s);
				i = 0;
				while (r->header[i] != NULL)
					free(r->header[i++]);
					r->header[0] = NULL;
				return E_INV;
			}
		}
	}
	i = 0;
	while (r->header[i] != NULL)
		free(r->header[i++]);
	r->header[0] = NULL;

	if (r->type != CONNECT) {
		len += strlen("\r\n");
		if (len >= sizeof(buf) - 1) {
			DEBUG(("do_request() => header too big"));
			(void) close(s);
			return E_INV;
		}
		(void) strncat(buf, "\r\n", strlen("\r\n"));

		DEBUG(("do_request() => request ready: type %d url (%s) host (%s) port %d",
		      r->type, r->url, r->host, r->port));
		DEBUG(("=> version maj %d min %d", r->vmajor, r->vminor));
		DEBUG(("=> header: (%s)", buf));

		if (my_poll(s, OUT) <= 0 || write(s, buf, len) < 1) {
			DEBUG(("do_request() => sending request failed"));
			(void) close(s);
			return E_CON;
		}
	}
	if (r->type == POST) {
		long            rest;

		DEBUG(("do_request() => posting data"));

		if ((rest = r->clen) < 0L) {
			DEBUG(("do_request() => post: invalid clen %ld", r->clen));
			(void) close(s);
			return E_POST;
		}
		while (rest > 0L) {
			if (my_poll(cl, OUT) <= 0) {
				(void) close(s);
				return E_POST;
			}
			len = read(cl, buf, sizeof(buf));
			if (len < 1)
				break;
			else
				rest -= len;

			if (my_poll(s, OUT) <= 0 || write(s, buf, len) < 1) {
				DEBUG(("do_request() => post: error writing post data"));
				(void) close(s);
				return E_POST;
			}
		}
		DEBUG(("do_request() => post done"));
	}
	if (r->type != CONNECT) {
		i = 0;
		while ((len = getline_int(s, buf, sizeof(buf))) > 0 && i < sizeof(r->header) - 1) {
			DEBUG(("do_request() => got remote header line: (%s)", buf));
			r->header[i] = (char *) my_alloc(len + 1);
			(void) strcpy(r->header[i++], buf);
		}
		r->header[i] = NULL;

		if (len > 0) {
			DEBUG(("do_request() => remote header too big"));
			(void) close(s);
			i = 0;
			while (r->header[i] != NULL)
				free(r->header[i++]);
			r->header[0] = NULL;
			return E_FIL;
		}
		if (filter_remote(r) != 0) {
			DEBUG(("do_request() => response was filtered"));
			(void) close(s);
			i = 0;
			while (r->header[i] != NULL)
				free(r->header[i++]);
			r->header[0] = NULL;
			return E_FIL;
		}
		*buf = '\0';
		len = 0;
		i = 0;
		while (r->header[i] != NULL) {
			len += strlen(r->header[i]) + strlen("\r\n");
			if (len < sizeof(buf) - 1) {
				(void) strcat(buf, r->header[i++]);
				(void) strcat(buf, "\r\n");
			} else {
				DEBUG(("do_request() => remote header too big (at concatenation)"));
				i = 0;
				while (r->header[i] != NULL)
					free(r->header[i++]);
				r->header[0] = NULL;
				(void) close(s);
				return E_FIL;
			}
		}
		i = 0;
		while (r->header[i] != NULL)
			free(r->header[i++]);
		r->header[0] = NULL;

		len += strlen("\r\n");
		if (len >= sizeof(buf) - 1) {
			DEBUG(("do_request() => remote header too big (at final)"));
			(void) close(s);
			return E_FIL;
		}
		(void) strcat(buf, "\r\n");

		DEBUG(("do_request() => remote header ready: (%s)", buf));

#ifndef ORIGINAL
		// fprintf(stderr, "\n***** HEADER *****\n%s\n", buf); // Response Header
		// Header 解析
		if( (buf_ptr = search(buf, "pragma:")) ) isCachable = judge(buf_ptr, "no-cache");
		if( (buf_ptr = search(buf, "cache-control:")) ){
			isCachable = isCachable && judge(buf_ptr, "private") && judge(buf_ptr, "no-cache") &&
            judge(buf_ptr, "no-store") && judge(buf_ptr, "must-revalidate") && judge(buf_ptr, "proxy-revalidate");
    	}
		
		if(isCachable){ // Header 保存
			if(fp = fopen(cachepath, "wb")){
				fwrite(buf, 1, len, fp);
				fclose(fp);
			}
		}
#endif
		
		if (my_poll(cl, OUT) <= 0 || write(cl, buf, len) < 1) {
			(void) close(s);
			return -1;
		}
	}
	if (r->type == CONNECT) {
 		char *con_est = "HTTP/1.0 200 Connection established\r\n\r\n";
		int max, sel;
		struct timeval to;
		fd_set fdset;

		to.tv_sec = config.to_con;
		to.tv_usec = 0;

#ifndef ORIGINAL
		FILE *fp;
		time_t timer = time(NULL);
		struct tm *local = localtime(&timer);
		if (fp=fopen(histpath, "ab")){
			fprintf(fp, "%d/%d/%d %d:%d,%s,\n", local->tm_year + 1900, local->tm_mon + 1, local->tm_mday, local->tm_hour, local->tm_min, r->url); // CONNECT でも URL だけは記録
			fclose(fp);
		}
#endif

		if (write(cl, con_est, strlen(con_est)) < 1)
			goto c_break;

		if(cl >= s)
			max = cl + 1;
		else
			max = s + 1;

		i = 1;
		sel = 1;
		len = 1;
		while (len > 0 && sel > 0 && i > 0) {
			FD_ZERO(&fdset);
			FD_SET(cl, &fdset);
			FD_SET(s, &fdset);
			sel = select(max, &fdset, (fd_set*) 0, (fd_set*) 0, &to);
			if (FD_ISSET(cl, &fdset)) {
				len = read(cl, buf, sizeof(buf));
				i = write(s, buf, len);
			}
			if (FD_ISSET(s, &fdset)) {
				len = read(s, buf, sizeof(buf));
				i = write(cl, buf, len);
			}
		}
c_break:
		(void) close(s);
		return 0;
	} else if (r->type != HEAD) {
#ifndef ORIGINAL
		if(isCachable) fp = fopen(cachepath, "ab");
#endif
		while (my_poll(s, IN) > 0 && (len = read(s, buf, sizeof(buf))) > 0) {
#ifndef ORIGINAL
			// fprintf(stderr, "\n***** BODY *****\n%s\n", buf); // ***** Response Body *****
			if(!gottenTitle) gottenTitle = ExtractTitle(buf, len, title); // title を取得
			if(fp) fwrite(buf, 1, len, fp); // Body を保存
#endif
			if (my_poll(cl, OUT) <= 0 || write(cl, buf, len) < 1) {
				(void) close(s);
				return -1;
			}
		}
#ifndef ORIGINAL
		if(fp) fclose(fp);
#endif
		(void) close(s);
#ifndef ORIGINAL
		FILE *fp;
		time_t timer = time(NULL);
		struct tm *local = localtime(&timer);
		if (fp=fopen(histpath, "ab")){
			fprintf(fp, "%d/%d/%d %d:%d,%s,\"%s\",\n", local->tm_year + 1900, local->tm_mon + 1, local->tm_mday,
				local->tm_hour, local->tm_min, r->url, title);
			fclose(fp);
		}
#endif
		return 0;
	}
	return 0;
}