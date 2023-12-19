#include "tcp.h"
#include "tcp_sock.h"
#include "tcp_timer.h"

#include "log.h"
#include "ring_buffer.h"

#include <stdlib.h>

#ifdef LOG_AS_PPT
struct timeval start;
#endif

// update the snd_wnd of tcp_sock
//
// if the snd_wnd before updating is zero, notify tcp_sock_send (wait_send)
static inline void tcp_update_window(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	u16 old_snd_wnd = tsk->snd_wnd;
	tsk->adv_wnd = cb->rwnd;
	tsk->snd_wnd = min(tsk->adv_wnd, tsk->cwnd * TCP_MSS);;
	if (old_snd_wnd == 0)
		wake_up(tsk->wait_send);
}

// update the snd_wnd safely: cb->ack should be between snd_una and snd_nxt
static inline void tcp_update_window_safe(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	if (less_or_equal_32b(tsk->snd_una, cb->ack) && less_or_equal_32b(cb->ack, tsk->snd_nxt))
		tcp_update_window(tsk, cb);
}

#ifndef max
#	define max(x,y) ((x)>(y) ? (x) : (y))
#endif

// check whether the sequence number of the incoming packet is in the receiving
// window
static inline int is_tcp_seq_valid(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	u32 rcv_end = tsk->rcv_nxt + max(tsk->rcv_wnd, 1);
	if (less_than_32b(cb->seq, rcv_end) && less_or_equal_32b(tsk->rcv_nxt, cb->seq_end)) {
		return 1;
	}
	else {
		fprintf(stderr, "%d %d %d\n",cb->seq, tsk->rcv_nxt, rcv_end);
		log(ERROR, "received packet with invalid seq, drop it.");
		return 0;
	}
}

// handle the recv data from TCP packet
int handle_tcp_recv_data(struct tcp_sock *tsk, struct tcp_cb * cb) {
	if (cb->pl_len <= 0) {
		return 0;
	}

	pthread_mutex_lock(&tsk->rcv_buf_lock);

	while (ring_buffer_full(tsk->rcv_buf)) {
		log(DEBUG, "ring_buffer is full.");
		pthread_mutex_unlock(&tsk->rcv_buf_lock);
		sleep_on(tsk->wait_recv);
	}

	tcp_recv_ofo_buffer_add_packet(tsk, cb);
	pthread_mutex_unlock(&tsk->rcv_buf_lock);

	tcp_move_recv_ofo_buffer(tsk);
	pthread_mutex_lock(&tsk->rcv_buf_lock);

	tsk->rcv_wnd = ring_buffer_free(tsk->rcv_buf);

	tcp_update_send_buffer(tsk, cb->ack);
	tcp_update_retrans_timer(tsk);

	//fprintf(stderr, "%d %d\n",cb->seq, tsk->rcv_nxt);
	tcp_send_control_packet(tsk, TCP_ACK);
		
	wake_up(tsk->wait_recv);

	pthread_mutex_unlock(&tsk->rcv_buf_lock);

	return 1;
}

void tcp_congestion_control(struct tcp_sock *tsk, struct tcp_cb *cb, char *packet) {
	//printf("0 %d\n", tsk->c_state);
	int ack_valid = tcp_update_send_buffer(tsk, cb->ack);

	switch (tsk->c_state) {
		case OPEN: {
			if (tsk->cwnd < tsk->ssthresh)
				tsk->cwnd += 1;
			else
				tsk->cwnd += 1.0 / tsk->cwnd;
			if (!ack_valid) {
				tsk->dupacks++;
				tsk->c_state = DISORDER;
			}
			break;
		}
		case DISORDER: {
			if (tsk->cwnd < tsk->ssthresh)
				tsk->cwnd += 1;
			else
				tsk->cwnd += 1.0 / tsk->cwnd;
			if (!ack_valid) {
				tsk->dupacks++;
				if (tsk->dupacks >= 3) {
					tsk->ssthresh = max((u32)(tsk->cwnd / 2), 1);
					tsk->cwnd -= 0.5;
					tsk->cwnd_flag = 0;
					tsk->recovery_point = tsk->snd_nxt;
					tsk->c_state = RECOVERY;
					tcp_retrans_send_buffer(tsk);
				}
			}
			break;
		}
		case LOSS: {
			if (tsk->cwnd < tsk->ssthresh)
				tsk->cwnd += 1;
			else
				tsk->cwnd += 1.0 / tsk->cwnd;

			if (ack_valid) {
				if (cb->ack >= tsk->loss_point) {
					tsk->c_state = OPEN;
					tsk->dupacks = 0;
				}
			}
			else {
				tsk->dupacks++;
			}
			break;
		}
		case RECOVERY: {
			if (tsk->cwnd > tsk->ssthresh && tsk->cwnd_flag == 0)
				tsk->cwnd -= 0.5;
			else
				tsk->cwnd_flag = 1;

			if (ack_valid) {
				if (cb->ack < tsk->recovery_point) {
					tcp_retrans_send_buffer(tsk);
				}
				else {
					tsk->c_state = OPEN;
					tsk->dupacks = 0;
				}
			}
			else {
				tsk->dupacks++;
				wake_up(tsk->wait_send);
			}
			break;
		}
		default:
			break;
	}

	tcp_update_retrans_timer(tsk);
}

// Process the incoming packet according to TCP state machine. 
void tcp_process(struct tcp_sock *tsk, struct tcp_cb *cb, char *packet)
{
	//fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);

	if (!tsk) {
	    log(ERROR, "No process listening!!!\n");
		tcp_send_reset(cb);
        return;
	}
	if (cb->flags & TCP_RST) {
		tcp_set_state(tsk, TCP_CLOSED);
		// release TCP socket

		tcp_unhash(tsk);
		tcp_bind_unhash(tsk);

		return;
	}

	switch(tsk->state){
		case(TCP_LISTEN):{
			if (cb->flags == TCP_SYN) {
				// alloc child sock
				struct tcp_sock * child_tsk = alloc_tcp_sock();
				child_tsk->parent = tsk;
				tsk->ref_cnt += 1;

				child_tsk->local.ip = cb->daddr;
				child_tsk->local.port = cb->dport;
				child_tsk->peer.ip = cb->saddr;
				child_tsk->peer.port = cb->sport;

				child_tsk->iss = tcp_new_iss();
				child_tsk->snd_nxt = child_tsk->iss;
				child_tsk->rcv_nxt = cb->seq_end;

				tcp_set_state(child_tsk, TCP_SYN_RECV);

				tcp_hash(child_tsk);
				init_list_head(&child_tsk->bind_hash_list);
				
				log(DEBUG, "Pass " IP_FMT ":%hu <-> " IP_FMT ":%hu from process to listen_queue", 
						HOST_IP_FMT_STR(child_tsk->sk_sip), child_tsk->sk_sport,
						HOST_IP_FMT_STR(child_tsk->sk_dip), child_tsk->sk_dport);
				list_add_tail(&child_tsk->list, &tsk->listen_queue);

				// send SYN + ACK
				tcp_send_control_packet(child_tsk, TCP_SYN | TCP_ACK);
			}
			else{
				log(DEBUG, "Current state is TCP_LISTEN but recv not SYN");
			}
			break;
		}
		case(TCP_SYN_SENT):{
			if (cb->flags == (TCP_SYN | TCP_ACK)) {
				tsk->rcv_nxt = cb->seq_end;

				tcp_update_window_safe(tsk, cb);
				tsk->snd_una = greater_than_32b(cb->ack, tsk->snd_una) ? cb->ack : tsk->snd_una;

				tcp_unset_retrans_timer(tsk);
				tcp_update_send_buffer(tsk, cb->ack);

				tcp_set_state(tsk, TCP_ESTABLISHED);

				// send ACK;
				tcp_send_control_packet(tsk, TCP_ACK);

			#ifdef LOG_AS_PPT
				gettimeofday(&start, NULL);
			#else
				//create a thread to record cwnd size
				pthread_t cwnd_record;
				pthread_create(&cwnd_record, NULL, tcp_cwnd_thread, (void *)tsk);
			#endif
				
				wake_up(tsk->wait_connect);

			} else if (cb->flags == TCP_SYN) {
				tsk->rcv_nxt = cb->seq_end;

				tcp_set_state(tsk, TCP_SYN_RECV);
				// send SYN + ACK;
				tcp_send_control_packet(tsk, TCP_SYN | TCP_ACK);
			}
			else{
				log(DEBUG, "Current state is TCP_SYN_SENT but recv not SYN or SYN|ACK");
			}
			break;
		}
		case(TCP_SYN_RECV):{
			if (cb->flags == TCP_ACK) {
				if (!is_tcp_seq_valid(tsk, cb)) {
					tcp_send_control_packet(tsk, TCP_ACK);
					return;
				}
				tsk->rcv_nxt = cb->seq_end;

				tcp_update_window_safe(tsk, cb);
				tsk->snd_una = greater_than_32b(cb->ack, tsk->snd_una) ? cb->ack : tsk->snd_una;;

				if (tsk->parent) {
					if (tcp_sock_accept_queue_full(tsk->parent)) {
						tcp_set_state(tsk, TCP_CLOSED);
						// send RST
						tcp_send_control_packet(tsk, TCP_RST);

						tcp_unhash(tsk);
						tcp_bind_unhash(tsk);

						// remove from listen list
						list_delete_entry(&tsk->list);
						free_tcp_sock(tsk);
						log(DEBUG, "tcp_sock accept queue is full, so the tsk should be freed.");

					} 
					else {
						tcp_set_state(tsk, TCP_ESTABLISHED);
						tcp_sock_accept_enqueue(tsk);

						tcp_unset_retrans_timer(tsk);
						tcp_update_send_buffer(tsk, cb->ack);

						// wake up user process for accept
						wake_up(tsk->parent->wait_accept);
					}
				}
				else {
					log(ERROR, "tsk->parent is NULL\n");
				}
			}
			else{
				log(DEBUG, "Current state is TCP_SYN_RECV but recv not ACK");
			}
			break;
		}
		case(TCP_ESTABLISHED):{
			if (less_than_32b(cb->seq, tsk->rcv_nxt)) {
				tcp_send_control_packet(tsk, TCP_ACK);
				log(DEBUG, "received packet had been received before, drop it.");
				return;
			}

			if (!is_tcp_seq_valid(tsk, cb)) {
				tcp_send_control_packet(tsk, TCP_ACK);
				log(DEBUG, "received packet invalid, drop it.");
				return;
			}

			if (cb->flags & TCP_ACK) {
				tcp_update_window_safe(tsk, cb);
				tsk->snd_una = greater_than_32b(cb->ack, tsk->snd_una) ? cb->ack : tsk->snd_una;;
			}

			if (cb->flags & TCP_FIN) {
				tcp_update_send_buffer(tsk, cb->ack);
				tcp_update_retrans_timer(tsk);
				if (tsk->retrans_timer.enable) {
					log(ERROR, "still have no ack packet before close wait\n");
				}

				tcp_set_state(tsk, TCP_CLOSE_WAIT);
				handle_tcp_recv_data(tsk, cb);

				// send ACK;
				tsk->rcv_nxt = cb->seq_end;
				tcp_send_control_packet(tsk, TCP_ACK);
				
				wake_up(tsk->wait_recv);
			} 
			else {
				if (cb->pl_len != 0) {
					handle_tcp_recv_data(tsk, cb);
					//tcp_send_control_packet(tsk, TCP_ACK);
				}
				else{
					tsk->rcv_nxt = cb->seq_end;
					if (cb->ack > tsk->snd_una) {
						tsk->retrans_timer.retrans_time = 0;
						tsk->retrans_timer.timeout = TCP_RETRANS_INTERVAL_INITIAL;
					}
					//log(DEBUG, "%d %d\n", tsk->snd_una, cb->ack);
					tsk->snd_una = cb->ack;
					tcp_update_window_safe(tsk, cb);
					tcp_congestion_control(tsk, cb, packet);
					//tcp_update_send_buffer(tsk, cb->ack);
					//tcp_update_retrans_timer(tsk);
				}
			}
			break;
		}
		case(TCP_FIN_WAIT_1):{
			if (!is_tcp_seq_valid(tsk, cb)) {
				tcp_send_control_packet(tsk, TCP_ACK);
				return;
			}

			// do something but not this stage
			tsk->rcv_nxt = cb->seq_end;

			if (cb->flags & TCP_ACK) {
				tcp_update_send_buffer(tsk, cb->ack);
				tcp_unset_retrans_timer(tsk);

				tcp_update_window_safe(tsk, cb);
				tsk->snd_una = cb->ack;
			}

			if ((cb->flags & TCP_FIN) && (cb->flags & TCP_ACK) && tsk->snd_nxt == tsk->snd_una) {
				tcp_set_state(tsk, TCP_TIME_WAIT);
				tcp_set_timewait_timer(tsk);

				// send ACK;
				tcp_send_control_packet(tsk, TCP_ACK);

			} else if ((cb->flags & TCP_ACK) && tsk->snd_nxt == tsk->snd_una) {
				tcp_set_state(tsk, TCP_FIN_WAIT_2);

			} else if (cb->flags & TCP_FIN) {
				tcp_set_state(tsk, TCP_CLOSING);

				// send ACK;
				tcp_send_control_packet(tsk, TCP_ACK);
			}
			break;
		}
		case(TCP_FIN_WAIT_2):{
			if (!is_tcp_seq_valid(tsk, cb)) {
				tcp_send_control_packet(tsk, TCP_ACK);
				return;
			}

			// do something but not this stage
			tsk->rcv_nxt = cb->seq_end;

			if (cb->flags & TCP_ACK) {
				tcp_update_window_safe(tsk, cb);
				tsk->snd_una = cb->ack;
			}

			if (cb->flags & TCP_FIN) {
				tcp_set_state(tsk, TCP_TIME_WAIT);
				tcp_set_timewait_timer(tsk);

				// send ACK;
				tcp_send_control_packet(tsk, TCP_ACK);
			}
			break;
		}
		case(TCP_CLOSING):{
			if (!is_tcp_seq_valid(tsk, cb)) {
				tcp_send_control_packet(tsk, TCP_ACK);
				return;
			}

			// do something but not this stage
			tsk->rcv_nxt = cb->seq_end;

			if (cb->flags & TCP_ACK) {
				tcp_update_window_safe(tsk, cb);
				tsk->snd_una = cb->ack;
				
				tcp_set_state(tsk, TCP_TIME_WAIT);
				tcp_set_timewait_timer(tsk);
			}
			break;
		}
		case(TCP_TIME_WAIT):{
			log(DEBUG, "receive a packet of a TCP_TIME_WAIT sock.");
			// do something but not this stage
			break;
		}
		case(TCP_CLOSE_WAIT):{
			log(DEBUG, "receive a packet of a TCP_CLOSE_WAIT sock.");
			// nothing to do;
			break;
		}
		case(TCP_LAST_ACK):{
			if (!is_tcp_seq_valid(tsk, cb)) {
				tcp_send_control_packet(tsk, TCP_ACK);
				return;
			}

			tsk->rcv_nxt = cb->seq_end;

			if (cb->flags & TCP_ACK) {
				tcp_update_window_safe(tsk, cb);
				tsk->snd_una = cb->ack;
			}

			if ((cb->flags & TCP_ACK) && tsk->snd_nxt == tsk->snd_una) {
				tcp_update_send_buffer(tsk, cb->ack);
				tcp_unset_retrans_timer(tsk);
				tcp_set_state(tsk, TCP_CLOSED);
				tsk->rcv_nxt = cb->seq;
				tsk->snd_una = cb->ack;

				tcp_set_state(tsk, TCP_CLOSED);

				// release the sock
				tcp_unhash(tsk);
				tcp_bind_unhash(tsk);

				free_tcp_sock(tsk);
			}
			break;
		}
		case(TCP_CLOSED):{
			log(DEBUG, "this socket is closed");

			// release the sock
			tcp_unhash(tsk);
			tcp_bind_unhash(tsk);
			break;
		}
		default:{
			log(DEBUG, "TCP state default when processing the incoming packet");
			break;
		}
	}

	cnwd_record(tsk);
}

