#include "nat.h"
#include "ip.h"
#include "icmp.h"
#include "tcp.h"
#include "rtable.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define MAX_LINE_LEN 100

static struct nat_table nat;

// get the interface from iface name
static iface_info_t *if_name_to_iface(char *if_name)
{
	char *name_end = if_name;
	while (*name_end != ' ' && *name_end != '\n' && *name_end != '\0') name_end++;
	*name_end = '\0';

	iface_info_t *iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		if (strcmp(iface->name, if_name) == 0)
			return iface;
	}

	log(ERROR, "Could not find the desired interface according to if_name '%s'", if_name);
	return NULL;
}

// get int from string ip
static u32 ip_to_u32(char *ip){
	u32 u32_ip = 0;
	int ip_values[4];

	sscanf(ip, "%d.%d.%d.%d", &ip_values[0], &ip_values[1], &ip_values[2], &ip_values[3]);

	for (int i = 0; i < 4; i++) {
		u32_ip <<= 8;
		u32_ip |= ip_values[i];
	}

    return u32_ip;
}

// determine the direction of the packet, DIR_IN / DIR_OUT / DIR_INVALID
static int get_packet_direction(char *packet)
{
	//fprintf(stdout, "TODO: determine the direction of this packet.\n");

	struct iphdr *ip = packet_to_ip_hdr(packet);
	u32 saddr = ntohl(ip->saddr);
	u32 daddr = ntohl(ip->daddr);
	rt_entry_t *src_entry = longest_prefix_match(saddr);
	rt_entry_t *dst_entry = longest_prefix_match(daddr);

	if ((src_entry->iface == nat.internal_iface) && (dst_entry->iface == nat.external_iface)) {
		return DIR_OUT;
	}
	else if ((src_entry->iface == nat.external_iface) && (daddr == nat.external_iface->ip)) {
		return DIR_IN;
	}
	else{
		return DIR_INVALID;
	}
}

static u8 rmt_hash(u32 addr, u16 port) {
    char str[6];
    memset(str, 0, 6 * sizeof(char));
    memcpy(str, &addr, sizeof(u32));
    memcpy(str + 4, &port, sizeof(u16));

	u8 res = hash8(str, 6);
    return res;
}

// do translation for the packet: replace the ip/port, recalculate ip & tcp
// checksum, update the statistics of the tcp connection
void do_translation(iface_info_t *iface, char *packet, int len, int dir)
{
	//fprintf(stdout, "TODO: do translation for this packet.\n");
	struct iphdr *iphdr = packet_to_ip_hdr(packet);
	struct tcphdr *tcphdr = packet_to_tcp_hdr(packet);

    u32 daddr = ntohl(iphdr->daddr);
    u32 saddr = ntohl(iphdr->saddr);
    u32 raddr = (dir == DIR_IN) ? saddr : daddr;
    u16 sport = ntohs(tcphdr->sport);
    u16 dport = ntohs(tcphdr->dport);
    u16 rport = (dir == DIR_IN) ? sport : dport;

    u8 idx = rmt_hash(raddr, rport);
    struct list_head *head = &nat.nat_mapping_list[idx];
    struct nat_mapping *entry;

    pthread_mutex_lock(&nat.lock);
    list_for_each_entry(entry, head, list) {
        if (raddr != entry->remote_ip || rport != entry->remote_port){
			continue;
		}

		int clear = (tcphdr->flags & TCP_RST) ? 1 : 0;

        if (dir == DIR_IN) {
            if (daddr != entry->external_ip || dport != entry->external_port){
				continue;
			}
            iphdr->daddr = htonl(entry->internal_ip);
            tcphdr->dport = htons(entry->internal_port);

			entry->conn.external_fin = (tcphdr->flags & TCP_FIN) ? 1 : 0;
            entry->conn.external_seq_end = tcp_seq_end(iphdr, tcphdr);
            if (tcphdr->flags & TCP_ACK){
				entry->conn.external_ack = tcphdr->ack;
			}
        } 
		else {
            if (saddr != entry->internal_ip || sport != entry->internal_port){
				 continue;
			}
            iphdr->saddr = htonl(entry->external_ip);
            tcphdr->sport = htons(entry->external_port);
            entry->conn.internal_fin = (tcphdr->flags & TCP_FIN) ? 1 : 0;
            entry->conn.internal_seq_end = tcp_seq_end(iphdr, tcphdr);
            if (tcphdr->flags & TCP_ACK){
				entry->conn.internal_ack = tcphdr->ack;
			}
        }

        pthread_mutex_unlock(&nat.lock);

		entry->update_time = time(NULL);
        tcphdr->checksum = tcp_checksum(iphdr, tcphdr);
        iphdr->checksum = ip_checksum(iphdr);
        ip_send_packet(packet, len);

		if (clear) {
			nat.assigned_ports[entry->external_port] = 0;
			list_delete_entry(&(entry->list));
			free(entry);
		}
        return;
    }

	if ((tcphdr->flags & TCP_SYN) == 0) {
        fprintf(stderr, "Invalid packet!\n");
        icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH);
        free(packet);
        pthread_mutex_unlock(&nat.lock);
        return;
    }

    if (dir == DIR_IN) {
		struct dnat_rule *rule;
        list_for_each_entry(rule, &nat.rules, list) {
            if (daddr == rule->external_ip && dport == rule->external_port) {
                struct nat_mapping *new_entry = (struct nat_mapping *) malloc(sizeof(struct nat_mapping));
                list_add_tail(&new_entry->list, head);

                new_entry->remote_ip = raddr;
                new_entry->remote_port = rport;
                new_entry->external_ip = rule->external_ip;
                new_entry->external_port = rule->external_port;
                new_entry->internal_ip = rule->internal_ip;
                new_entry->internal_port = rule->internal_port;

                new_entry->conn.external_fin = ((tcphdr->flags & TCP_FIN) != 0);
                new_entry->conn.external_seq_end = tcp_seq_end(iphdr, tcphdr);
                if (tcphdr->flags & TCP_ACK) new_entry->conn.external_ack = tcphdr->ack;

                new_entry->update_time = time(NULL);
                pthread_mutex_unlock(&nat.lock);

                iphdr->daddr = htonl(rule->internal_ip);
                tcphdr->dport = htons(rule->internal_port);
                tcphdr->checksum = tcp_checksum(iphdr, tcphdr);
                iphdr->checksum = ip_checksum(iphdr);
                ip_send_packet(packet, len);
                return;
            }
        }
    }
	else {
        u16 pid;
        for (pid = NAT_PORT_MIN; pid <= NAT_PORT_MAX; ++pid) {
            if (!nat.assigned_ports[pid]) {
                struct nat_mapping *new_entry = (struct nat_mapping *) malloc(sizeof(struct nat_mapping));
                list_add_tail(&new_entry->list, head);

                new_entry->remote_ip = raddr;
                new_entry->remote_port = rport;
                new_entry->external_ip = nat.external_iface->ip;
                new_entry->external_port = pid;
                new_entry->internal_ip = saddr;
                new_entry->internal_port = sport;

                new_entry->conn.internal_fin = ((tcphdr->flags & TCP_FIN) != 0);
                new_entry->conn.internal_seq_end = tcp_seq_end(iphdr, tcphdr);
                if (tcphdr->flags & TCP_ACK){
					new_entry->conn.internal_ack = tcphdr->ack;
				}

                new_entry->update_time = time(NULL);
                pthread_mutex_unlock(&nat.lock);

                iphdr->saddr = htonl(new_entry->external_ip);
                tcphdr->sport = htons(new_entry->external_port);
                tcphdr->checksum = tcp_checksum(iphdr, tcphdr);
                iphdr->checksum = ip_checksum(iphdr);
                ip_send_packet(packet, len);
                return;
            }
        }
    }

    log(DEBUG, "No available port!\n");
    icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH);
    free(packet);
}

void nat_translate_packet(iface_info_t *iface, char *packet, int len)
{
	int dir = get_packet_direction(packet);
	if (dir == DIR_INVALID) {
		log(ERROR, "invalid packet direction, drop it.");
		icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH);
		free(packet);
		return ;
	}

	struct iphdr *ip = packet_to_ip_hdr(packet);
	if (ip->protocol != IPPROTO_TCP) {
		log(ERROR, "received non-TCP packet (0x%0hhx), drop it", ip->protocol);
		free(packet);
		return ;
	}

	do_translation(iface, packet, len, dir);
}

// check whether the flow is finished according to FIN bit and sequence number
// XXX: seq_end is calculated by `tcp_seq_end` in tcp.h
static int is_flow_finished(struct nat_connection *conn)
{
    return (conn->internal_fin && conn->external_fin) && \
            (conn->internal_ack >= conn->external_seq_end) && \
            (conn->external_ack >= conn->internal_seq_end);
}

// nat timeout thread: find the finished flows, remove them and free port
// resource
void *nat_timeout()
{
	while (1) {
		//fprintf(stdout, "TODO: sweep finished flows periodically.\n");
		sleep(1);
		pthread_mutex_lock(&nat.lock);
		time_t now = time(NULL);

		for (int i = 0;i < HASH_8BITS;i++) {
			struct nat_mapping *map_entry = NULL, *map_q = NULL;
			list_for_each_entry_safe(map_entry, map_q, &(nat.nat_mapping_list[i]), list) {
				if ((now - map_entry->update_time > TCP_ESTABLISHED_TIMEOUT) || is_flow_finished(&(map_entry->conn))) {
					log(DEBUG, "remove map entry, port: %d\n", map_entry->external_port);
					nat.assigned_ports[map_entry->external_port] = 0;
					list_delete_entry(&(map_entry->list));
					free(map_entry);
				}
			}
		}

		pthread_mutex_unlock(&nat.lock);
	}

	return NULL;
}

int parse_config(const char *filename)
{
	char *line = (char *)malloc(MAX_LINE_LEN);
	FILE *fp = fopen(filename, "r");

	if (!fp) {
		fprintf(stderr, "config file do not exist\n");
		free(line);
		return -1;
	}

	while (fgets(line, MAX_LINE_LEN, fp)) {
		char *name_end = line;
		
		if (line[0] == 'i') {
			char* internal =line + 16;
			nat.internal_iface = if_name_to_iface(internal);
			log(DEBUG, "internal_iface: "IP_FMT"\n", HOST_IP_FMT_STR(nat.internal_iface->ip));
			continue;
		}
		else if (line[0] == 'e') {
			char* external =line + 16;
			nat.external_iface = if_name_to_iface(external);
			log(DEBUG, "external_iface: "IP_FMT"\n", HOST_IP_FMT_STR(nat.external_iface->ip));
			continue;
		}
		else if (line[0] == 'd') {
			struct dnat_rule *new_rule = (struct dnat_rule *)malloc(sizeof(struct dnat_rule));
			memset(new_rule, 0, sizeof(struct dnat_rule));

			char* drule =line + 12;
			new_rule->external_ip = ip_to_u32(drule);

			while(*drule != ':')
				drule++;
			drule++;
			new_rule->external_port = atoi(drule);

			while(*drule != '-')
				drule++;
			drule += 3;
			new_rule->internal_ip = ip_to_u32(drule);

			while(*drule != ':')
				drule++;
			drule++;
			new_rule->internal_port = atoi(drule);

			init_list_head(&new_rule->list);
			list_add_tail(&new_rule->list, &nat.rules);

			nat.assigned_ports[new_rule->external_port] = 1;

			log(DEBUG, "dnat_rule: "IP_FMT" %d "IP_FMT" %d\n", HOST_IP_FMT_STR(new_rule->external_ip),
			 new_rule->external_port, HOST_IP_FMT_STR(new_rule->internal_ip), new_rule->internal_port);

			continue;
		}
    }

	fclose(fp);
	free(line);

	return 0;
}

// initialize
void nat_init(const char *config_file)
{
	memset(&nat, 0, sizeof(nat));

	for (int i = 0; i < HASH_8BITS; i++)
		init_list_head(&nat.nat_mapping_list[i]);

	init_list_head(&nat.rules);

	// seems unnecessary
	memset(nat.assigned_ports, 0, sizeof(nat.assigned_ports));

	parse_config(config_file);

	pthread_mutex_init(&nat.lock, NULL);

	pthread_create(&nat.thread, NULL, nat_timeout, NULL);
}

void nat_exit()
{
	//fprintf(stdout, "TODO: release all resources allocated.\n");
	for (int i = 0;i < HASH_8BITS;i++) {
		struct nat_mapping *map_entry = NULL, *map_q = NULL;
		list_for_each_entry_safe(map_entry, map_q, &(nat.nat_mapping_list[i]), list) {
			list_delete_entry(&(map_entry->list));
			free(map_entry);
		}
	}

	struct dnat_rule *rule = NULL, *rule_q = NULL;
	list_for_each_entry_safe(rule, rule_q, &nat.rules, list) {
		list_delete_entry(&(rule->list));
		free(rule);
	}
}
