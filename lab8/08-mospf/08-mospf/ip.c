#include "ip.h"
#include "icmp.h"
#include "arpcache.h"
#include "rtable.h"
#include "arp.h"

#include "mospf_proto.h"
#include "mospf_daemon.h"

#include "log.h"

#include <stdlib.h>
#include <assert.h>

// handle ip packet
//
// If the packet is ICMP echo request and the destination IP address is equal to
// the IP address of the iface, send ICMP echo reply; otherwise, forward the
// packet.
void handle_ip_packet(iface_info_t *iface, char *packet, int len)
{
	struct iphdr *iph = packet_to_ip_hdr(packet);
	u32 daddr = ntohl(iph->daddr);
	if (daddr == iface->ip) {
		if (iph->protocol == IPPROTO_ICMP) {
			struct icmphdr *icmp = (struct icmphdr *)IP_DATA(iph);
			if (icmp->type == ICMP_ECHOREQUEST) {
				icmp_send_packet(packet, len, ICMP_ECHOREPLY, 0);
			}
		}
		else if (iph->protocol == IPPROTO_MOSPF) {
			handle_mospf_packet(iface, packet, len);
		}

		free(packet);
	}
	else if (iph->daddr == htonl(MOSPF_ALLSPFRouters)) {
		assert(iph->protocol == IPPROTO_MOSPF);
		handle_mospf_packet(iface, packet, len);

		free(packet);
	}
	else {
		iph->ttl --;
		if (iph->ttl <= 0) { //ICMP TTL equals 0 during transit
			icmp_send_packet(packet, len, ICMP_TIME_EXCEEDED, ICMP_EXC_TTL);
			free(packet);
			return;
		}

		//checksum
		iph->checksum = ip_checksum(iph);
		//lookup rtable
		rt_entry_t *match = longest_prefix_match(daddr);
		if(match == NULL){
			icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_NET_UNREACH);
			free(packet);
			return ;
		}
		//get next ip addr
		u32 next_ip;
		if(match->gw){
			next_ip = match->gw;
		}
		else{
			next_ip = daddr;
		}
		//forward
		iface_send_packet_by_arp(match->iface, next_ip, packet, len);
	}
}
