/* $Id$ */

/*
 * Copyright (c) 2010-2011 SURFnet bv
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of SURFnet bv nor the names of its contributors 
 *    may be used to endorse or promote products derived from this 
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * The Extensible Ethernet Monitor (EEMO)
 * IP packet handling
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <pcap.h>
#include <arpa/inet.h>
#include "eemo.h"
#include "eemo_list.h"
#include "ip_handler.h"
#include "ether_handler.h"

/* The linked list of IP packet handlers */
eemo_ll_entry* ip_handlers = NULL;

/* IP packet handler comparison */
int eemo_ip_handler_compare(void* elem_data, void* comp_data)
{
	eemo_ip_handler* elem = (eemo_ip_handler*) elem_data;
	u_short which_ip_proto = 0;

	if ((elem_data == NULL) || (comp_data == NULL))
	{
		return 0;
	}

	which_ip_proto = *((u_short*) comp_data);

	if (elem->which_ip_proto == which_ip_proto)
	{
		return 1;
	}

	return 0;
}

/* Find an IP handler for the specified type */
eemo_ip_handler* eemo_find_ip_handler(u_short which_ip_proto)
{
	eemo_ip_handler* rv = NULL;

	if (eemo_ll_find(ip_handlers, (void*) &rv, &eemo_ip_handler_compare, (void*) &which_ip_proto) != ERV_OK)
	{
		/* FIXME: log this */
	}

	return rv;
}

/* Convert IPv4 packet header to host byte order */
void eemo_ipv4_ntoh(eemo_hdr_ipv4* hdr)
{
	hdr->ip4_len 	= ntohs(hdr->ip4_len);
	hdr->ip4_id	= ntohs(hdr->ip4_id);
	hdr->ip4_ofs	= ntohs(hdr->ip4_ofs);
	hdr->ip4_chksum	= ntohs(hdr->ip4_chksum);
}

/* Convert IPv6 packet header to host byte order */
void eemo_ipv6_ntoh(eemo_hdr_ipv6* hdr)
{
	int i = 0;

	hdr->ip6_len	= ntohs(hdr->ip6_len);

	for (i = 0; i < 8; i++)
	{
		hdr->ip6_src[i] = ntohs(hdr->ip6_src[i]);
		hdr->ip6_dst[i] = ntohs(hdr->ip6_dst[i]);
	}
}

/* Handle an IPv4 packet */
eemo_rv eemo_handle_ipv4_packet(eemo_packet_buf* packet, eemo_ether_packet_info ether_info)
{
	u_char version = 0;
	eemo_ip_packet_info ip_info;
	u_short offset = 0;
	u_short ip_proto = 0;
	eemo_ip_handler* handler = NULL;

	/* Clear ip_info structure */
	memset(ip_info.ip_src, 0, NI_MAXHOST);
	memset(ip_info.ip_dst, 0, NI_MAXHOST);

	/* Packets smaller than 1 byte are malformed... */
	if (packet->len < 1)
	{
		/* Packet is malformed */
		return ERV_MALFORMED;
	}

	/* Check the version number */
	version = (packet->data[0] & 0xF0) >> 4;

	if (version == IP_TYPE_V4)
	{
		eemo_hdr_ipv4* hdr = NULL;

		/* Check minimum length */
		if (packet->len < sizeof(eemo_hdr_ipv4))
		{
			/* Packet is malformed */
			return ERV_MALFORMED;
		}

		/* Take the header from the packet */
		hdr = (eemo_hdr_ipv4*) packet->data;

		/* Convert to host byte order */
		eemo_ipv4_ntoh(hdr);

		/* Copy relevant information to the ip_info structure */
		ip_info.fragment_ofs 	= hdr->ip4_ofs & IPV4_FRAGMASK;
		ip_info.is_fragment 	= (ip_info.fragment_ofs > 0) || FLAG_SET(hdr->ip4_ofs, IPV4_MOREFRAG);
		ip_info.more_frags 	= FLAG_SET(hdr->ip4_ofs, IPV4_MOREFRAG);
		ip_info.ip_type 	= IP_TYPE_V4;
		snprintf(ip_info.ip_src, NI_MAXHOST, "%d.%d.%d.%d", hdr->ip4_src[0], hdr->ip4_src[1], hdr->ip4_src[2], hdr->ip4_src[3]);
		snprintf(ip_info.ip_dst, NI_MAXHOST, "%d.%d.%d.%d", hdr->ip4_dst[0], hdr->ip4_dst[1], hdr->ip4_dst[2], hdr->ip4_dst[3]);

		/* Determine the protocol type */
		ip_proto = hdr->ip4_proto;

		/* Determine the offset */
		offset = sizeof(eemo_hdr_ipv4);
	}
	else
	{
		/* Eek! Unknown IP version number */
		return ERV_MALFORMED;
	}

	/* See if there is a handler for this type of packet */
	handler = eemo_find_ip_handler(ip_proto);

	if ((handler != NULL) && (handler->handler_fn != NULL))
	{
		eemo_rv rv = ERV_OK;
		eemo_packet_buf* ip_data = 
			eemo_pbuf_new(&packet->data[offset], packet->len - offset);

		if (ip_data == NULL)
		{
			return ERV_MEMORY;
		}

		rv = (handler->handler_fn)(ip_data, ip_info);

		eemo_pbuf_free(ip_data);

		return rv;
	}

	return ERV_SKIPPED;
}

/* Handle an IPv6 packet */
eemo_rv eemo_handle_ipv6_packet(eemo_packet_buf* packet, eemo_ether_packet_info ether_info)
{
	u_char version = 0;
	eemo_ip_packet_info ip_info;
	u_short offset = 0;
	u_short ip_proto = 0;
	eemo_ip_handler* handler = NULL;

	/* Clear ip_info structure */
	memset(ip_info.ip_src, 0, NI_MAXHOST);
	memset(ip_info.ip_dst, 0, NI_MAXHOST);

	/* Packets smaller than 1 byte are malformed... */
	if (packet->len < 1)
	{
		/* Packet is malformed */
		return ERV_MALFORMED;
	}

	/* Check the version number */
	version = (packet->data[0] & 0xF0) >> 4;

	if (version == IP_TYPE_V6)
	{
		eemo_hdr_ipv6* hdr = NULL;

		/* Check minimum length */
		if (packet->len < sizeof(eemo_hdr_ipv6))
		{
			/* Packet is malformed */
			return ERV_MALFORMED;
		}

		/* Take the header from the packet */
		hdr = (eemo_hdr_ipv6*) packet->data;

		/* Convert to host byte order */
		eemo_ipv6_ntoh(hdr);

		/* Copy relevant information to the ip_info structure */
		ip_info.fragment_ofs	= 0; /* currently not supported */
		ip_info.is_fragment	= 0; /* currently not supported */
		ip_info.more_frags 	= 0; /* currently not supported */
		ip_info.ip_type		= IP_TYPE_V6;
		snprintf(ip_info.ip_src, NI_MAXHOST, "%x:%x:%x:%x:%x:%x:%x:%x",
			hdr->ip6_src[0], hdr->ip6_src[1], hdr->ip6_src[2], hdr->ip6_src[3], 
			hdr->ip6_src[4], hdr->ip6_src[5], hdr->ip6_src[6], hdr->ip6_src[7]);
		snprintf(ip_info.ip_dst, NI_MAXHOST, "%x:%x:%x:%x:%x:%x:%x:%x",
			hdr->ip6_dst[0], hdr->ip6_dst[1], hdr->ip6_dst[2], hdr->ip6_dst[3], 
			hdr->ip6_dst[4], hdr->ip6_dst[5], hdr->ip6_dst[6], hdr->ip6_dst[7]);

		/* Determine the protocol type */
		ip_proto = hdr->ip6_next_hdr;

		/* Determine the offset */
		offset = sizeof(eemo_hdr_ipv6);
	}
	else
	{
		/* Eek! Unknown IP version number */
		return ERV_MALFORMED;
	}

	/* See if there is a handler for this type of packet */
	handler = eemo_find_ip_handler(ip_proto);

	if ((handler != NULL) && (handler->handler_fn != NULL))
	{
		eemo_rv rv = ERV_OK;
		eemo_packet_buf* ip_data = 
			eemo_pbuf_new(&packet->data[offset], packet->len - offset);

		if (ip_data == NULL)
		{
			return ERV_MEMORY;
		}

		rv = (handler->handler_fn)(ip_data, ip_info);

		eemo_pbuf_free(ip_data);

		return rv;
	}

	return ERV_SKIPPED;
}

/* Register an IP handler */
eemo_rv eemo_reg_ip_handler(u_short which_ip_proto, eemo_ip_handler_fn handler_fn)
{
	eemo_ip_handler* new_handler = NULL;
	eemo_rv rv = ERV_OK;

	/* Check if a handler for the specified type already exists */
	if (eemo_find_ip_handler(which_ip_proto) != NULL)
	{
		/* A handler for this type has already been registered */
		return ERV_HANDLER_EXISTS;
	}

	/* Create a new handler entry */
	new_handler = (eemo_ip_handler*) malloc(sizeof(eemo_ip_handler));

	if (new_handler == NULL)
	{
		/* Not enough memory */
		return ERV_MEMORY;
	}

	new_handler->which_ip_proto = which_ip_proto;
	new_handler->handler_fn = handler_fn;

	/* Register the new handler */
	if ((rv = eemo_ll_append(&ip_handlers, (void*) new_handler)) != ERV_OK)
	{
		/* FIXME: log this */
		free(new_handler);
	}

	return rv;
}

/* Unregister an IP handler */
eemo_rv eemo_unreg_ip_handler(u_short which_ip_proto)
{
	return eemo_ll_remove(&ip_handlers, &eemo_ip_handler_compare, (void*) &which_ip_proto);
}

/* Initialise IP handling */
eemo_rv eemo_init_ip_handler(void)
{
	eemo_rv rv = ERV_OK;

	/* Register IPv4 packet handler */
	rv = eemo_reg_ether_handler(ETHER_IPV4, &eemo_handle_ipv4_packet);

	if (rv != ERV_OK)
	{
		return rv;
	}

	/* Register IPv6 packet handler */
	rv = eemo_reg_ether_handler(ETHER_IPV6, &eemo_handle_ipv6_packet);

	if (rv != ERV_OK)
	{
		eemo_unreg_ether_handler(ETHER_IPV4);
	}

	return rv;
}

/* Clean up */
void eemo_ip_handler_cleanup(void)
{
	/* Clean up the list of IP packet handlers */
	if (eemo_ll_free(&ip_handlers) != ERV_OK)
	{
		/* FIXME: log this */
	}

	/* Unregister the Ethernet handler for IPv4 and IPv6 packets */
	eemo_unreg_ether_handler(ETHER_IPV4);
	eemo_unreg_ether_handler(ETHER_IPV6);
}
