#include "icmp.h"
#include "ip.h"
#include "rtable.h"
#include "arp.h"
#include "base.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// send icmp packet
void icmp_send_packet(const char *in_pkt, int len, u8 type, u8 code)
{
	//fprintf(stderr, "TODO: malloc and send icmp packet.\n");
	struct iphdr *iph =  packet_to_ip_hdr(in_pkt);
	char* ipdata = IP_DATA(iph);

	//length
	int res_len = 0;
	int icmp_len = 0;
	if(type == ICMP_ECHOREPLY){
		icmp_len = ntohs(iph->tot_len) - IP_HDR_SIZE(iph);
	}
	else{
		icmp_len = ICMP_HDR_SIZE + IP_HDR_SIZE(iph) + ICMP_COPIED_DATA_LEN;
	}
	res_len = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + icmp_len;

	//malloc
	char *res = (char *)malloc(res_len);
	memset(res, 0, res_len);
	// init iph
	struct iphdr *res_iph = packet_to_ip_hdr(res);
	if(type == ICMP_ECHOREPLY){
		ip_init_hdr(res_iph, ntohl(iph->daddr), ntohl(iph->saddr), IP_BASE_HDR_SIZE+icmp_len, IPPROTO_ICMP);
	}
	else{
		rt_entry_t *match = longest_prefix_match(ntohl(iph->saddr));
		if(match==NULL){
			free(res);
			return ;
		}
		ip_init_hdr(res_iph, match->iface->ip, ntohl(iph->saddr), IP_BASE_HDR_SIZE+icmp_len, IPPROTO_ICMP);
	}
	// init icmp
	char *res_ipdata = IP_DATA(res_iph);
	struct icmphdr *icmph = (struct icmphdr*)res_ipdata;
	if(type == ICMP_ECHOREPLY){
		memcpy(res_ipdata, ipdata, icmp_len);
	}
	else{
		memcpy(res_ipdata + ICMP_HDR_SIZE, iph, icmp_len-ICMP_HDR_SIZE);
	}

	icmph->type = type;
	icmph->code = code;
	icmph->checksum = icmp_checksum(icmph,icmp_len);
	//send
	ip_send_packet(res, res_len);
	free(res);
}
