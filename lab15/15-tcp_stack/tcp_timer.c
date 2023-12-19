#include "tcp.h"
#include "tcp_timer.h"
#include "tcp_sock.h"
#include "log.h"

#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#define TIMER_TYPE_TIME_WAIT 0
#define TIMER_TYPE_RETRANS 1

#ifndef max
#	define max(x,y) ((x)>(y) ? (x) : (y))
#endif

static struct list_head timer_list;
struct list_head retrans_timer_list;

static pthread_mutex_t timer_list_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t retrans_timer_list_lock = PTHREAD_MUTEX_INITIALIZER;

// scan the timer_list, find the tcp sock which stays for at 2*MSL, release it
void tcp_scan_timer_list()
{
	//fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);
	pthread_mutex_lock(&timer_list_lock);

	struct tcp_timer * timer_p = NULL, * timer_q = NULL;
	list_for_each_entry_safe(timer_p, timer_q, &timer_list, list) {
		if (timer_p->enable) {
			timer_p->timeout -= TCP_TIMER_SCAN_INTERVAL;
			if (timer_p->timeout <= 0) {
				struct tcp_sock * tsk = NULL;
				if (timer_p->type == TIMER_TYPE_TIME_WAIT) {
					// do TCP_TIME_WAIT to TCP_CLOSED
					timer_p->enable = 0;

					tsk = timewait_to_tcp_sock(timer_p);
					//assert(tsk->state == TCP_TIME_WAIT);
					tcp_set_state(tsk, TCP_CLOSED);

					tcp_unhash(tsk);
					tcp_bind_unhash(tsk);
					
					// remove reference from timewait list
					list_delete_entry(&timer_p->list);
					free_tcp_sock(tsk);

					// just leave the closed sock in accept_queue/user
				} else if (timer_p->type == TIMER_TYPE_RETRANS) {
					log(ERROR, "wait timer list entry type error\n");
				}
			}
		}
	}

	pthread_mutex_unlock(&timer_list_lock);
}

// set the timewait timer of a tcp sock, by adding the ti0.0mer into timer_list
void tcp_set_timewait_timer(struct tcp_sock *tsk)
{
	//fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);

	tsk->timewait.enable = 1;
	tsk->timewait.type = TIMER_TYPE_TIME_WAIT;
	tsk->timewait.timeout = TCP_TIMEWAIT_TIMEOUT;

	// refer to this sock in timewait list
	tsk->ref_cnt += 1;
	log(DEBUG, "insert " IP_FMT ":%hu <-> " IP_FMT ":%hu to timewait, ref_cnt += 1", 
			HOST_IP_FMT_STR(tsk->sk_sip), tsk->sk_sport,
			HOST_IP_FMT_STR(tsk->sk_dip), tsk->sk_dport);

	pthread_mutex_lock(&timer_list_lock);
	list_add_tail(&tsk->timewait.list, &timer_list);
	pthread_mutex_unlock(&timer_list_lock);
}

// scan the timer_list periodically by calling tcp_scan_timer_list
void *tcp_timer_thread(void *arg)
{
	init_list_head(&timer_list);
	while (1) {
		usleep(TCP_TIMER_SCAN_INTERVAL);
		tcp_scan_timer_list();
	}

	return NULL;
}


// set the restrans timer of a tcp sock, by adding the timer into timer_list
void tcp_set_retrans_timer(struct tcp_sock *tsk)
{
	if (tsk->retrans_timer.enable) {
		tsk->retrans_timer.timeout = TCP_RETRANS_INTERVAL_INITIAL;
		return;
	}
	tsk->retrans_timer.type = TIMER_TYPE_RETRANS;
	tsk->retrans_timer.enable = 1;
	tsk->retrans_timer.timeout = TCP_RETRANS_INTERVAL_INITIAL;
	tsk->retrans_timer.retrans_time = 0;

	pthread_mutex_lock(&retrans_timer_list_lock);
	list_add_tail(&tsk->retrans_timer.list, &retrans_timer_list);
	pthread_mutex_unlock(&retrans_timer_list_lock);
}

void tcp_update_retrans_timer(struct tcp_sock *tsk)
{
	if (list_empty(&tsk->send_buf) && tsk->retrans_timer.enable) {
		tsk->retrans_timer.enable = 0;
		list_delete_entry(&tsk->retrans_timer.list);
		wake_up(tsk->wait_send);
	}
}

void tcp_unset_retrans_timer(struct tcp_sock *tsk)
{
	if (!list_empty(&tsk->retrans_timer.list)) {
		tsk->retrans_timer.enable = 0;
		list_delete_entry(&tsk->retrans_timer.list);
		wake_up(tsk->wait_send);
	}
	else {
		log(ERROR, "unset an empty retrans timer\n");
	}
}

void tcp_scan_retrans_timer_list(void)
{
	struct tcp_sock *tsk;
	struct tcp_timer *time_entry, *time_q;

	pthread_mutex_lock(&retrans_timer_list_lock);
	
	list_for_each_entry_safe(time_entry, time_q, &retrans_timer_list, list) {
		time_entry->timeout -= TCP_RETRANS_SCAN_INTERVAL;
		tsk = retranstimer_to_tcp_sock(time_entry);
		if (time_entry->timeout <= 0) {
			if(time_entry->retrans_time >= MAX_RETRANS_NUM && tsk->state != TCP_CLOSED){
				list_delete_entry(&time_entry->list);
				if (!tsk->parent) {
					tcp_unhash(tsk);
				}	
				wait_exit(tsk->wait_connect);
				wait_exit(tsk->wait_accept);
				wait_exit(tsk->wait_recv);
				wait_exit(tsk->wait_send);
				
				tcp_set_state(tsk, TCP_CLOSED);
				tcp_send_control_packet(tsk, TCP_RST);
			}
			else if (tsk->state != TCP_CLOSED) {
				time_entry->retrans_time += 1;
				log(DEBUG, "retrans time: %d\n", time_entry->retrans_time);

				tsk->ssthresh = max(((u32)(tsk->cwnd / 2)), 1);
				tsk->cwnd = 1;
				tsk->c_state = LOSS;
				tsk->loss_point = tsk->snd_nxt;
				cnwd_record(tsk);
				
				time_entry->timeout = TCP_RETRANS_INTERVAL_INITIAL * (1 << time_entry->retrans_time);
				//time_entry->timeout = TCP_RETRANS_INTERVAL_INITIAL;
				tcp_retrans_send_buffer(tsk);
			}
		}
	}

	pthread_mutex_unlock(&retrans_timer_list_lock);
}

void *tcp_retrans_timer_thread(void *arg)
{
	init_list_head(&retrans_timer_list);
	while(1){
		usleep(TCP_RETRANS_SCAN_INTERVAL);
		tcp_scan_retrans_timer_list();
	}

	return NULL;
}