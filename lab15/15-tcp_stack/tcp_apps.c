#include "tcp_sock.h"

#include "log.h"

#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#ifdef LOG_AS_PPT
extern struct timeval start;
#endif
//char filename[100];

// return the interval in us
long get_interval(struct timeval tv_start,struct timeval tv_end){
    long start_us = tv_start.tv_sec * 1000000 + tv_start.tv_usec;
    long end_us   = tv_end.tv_sec   * 1000000 + tv_end.tv_usec;
    return end_us - start_us;
}

// tcp server application, listens to port (specified by arg) and serves only one
// connection request
void *tcp_server(void *arg)
{
	u16 port = *(u16 *)arg;
	struct tcp_sock *tsk = alloc_tcp_sock();

	struct sock_addr addr;
	addr.ip = htonl(0);
	addr.port = port;
	if (tcp_sock_bind(tsk, &addr) < 0) {
		log(ERROR, "tcp_sock bind to port %hu failed", ntohs(port));
		exit(1);
	}

	if (tcp_sock_listen(tsk, 3) < 0) {
		log(ERROR, "tcp_sock listen failed");
		exit(1);
	}

	log(DEBUG, "listen to port %hu.", ntohs(port));

	struct tcp_sock *csk = tcp_sock_accept(tsk);

	log(DEBUG, "accept a connection.");

	char rbuf[1001];
	char wbuf[1024];
	int rlen = 0;
	while (1) {
		rlen = tcp_sock_read(csk, rbuf, 1000);
		if (rlen == 0) {
			log(DEBUG, "tcp_sock_read return 0, finish transmission.");
			break;
		} 
		else if (rlen > 0) {
			rbuf[rlen] = '\0';
			sprintf(wbuf, "server echoes: %s", rbuf);
			if (tcp_sock_write(csk, wbuf, strlen(wbuf)) < 0) {
				log(DEBUG, "tcp_sock_write return a negative value, something goes wrong.");
				exit(1);
			}
		}
		else {
			log(DEBUG, "tcp_sock_read return a negative value, something goes wrong.");
			exit(1);
		}
	}

	log(DEBUG, "close this connection.");

	tcp_sock_close(csk);
	
	return NULL;
}

// tcp client application, connects to server (ip:port specified by arg), each
// time sends one bulk of data and receives one bulk of data 
void *tcp_client(void *arg)
{
	struct sock_addr *skaddr = arg;

	struct tcp_sock *tsk = alloc_tcp_sock();

	if (tcp_sock_connect(tsk, skaddr) < 0) {
		log(ERROR, "tcp_sock connect to server ("IP_FMT":%hu)failed.", \
				NET_IP_FMT_STR(skaddr->ip), ntohs(skaddr->port));
		exit(1);
	}

	char *data = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
	int dlen = strlen(data);
	char *wbuf = malloc(dlen+1);
	char rbuf[1001];
	int rlen = 0;

	int n = 10;
	for (int i = 0; i < n; i++) {
		memcpy(wbuf, data+i, dlen-i);
		if (i > 0) memcpy(wbuf+(dlen-i), data, i);

		int slen;
		if ((slen = tcp_sock_write(tsk, wbuf, dlen)) < 0)
			break;

		rlen = tcp_sock_read(tsk, rbuf, 1000);
		if (rlen == 0) {
			log(DEBUG, "tcp_sock_read return 0, finish transmission.");
			break;
		}
		else if (rlen > 0) {
			rbuf[rlen] = '\0';
			fprintf(stdout, "%s\n", rbuf);
		}
		else {
			log(DEBUG, "tcp_sock_read return a negative value, something goes wrong.");
			exit(1);
		}
		sleep(1);
	}

	tcp_sock_close(tsk);

	free(wbuf);

	return NULL;
}

// tcp server application, listens to port (specified by arg) and serves only one
// connection request
void *tcp_server_file(void *arg)
{
	FILE * f = fopen("server-output.dat", "wb");
	if (!f) {
		log(ERROR, "open file server-output.dat failed");
	}
	log(DEBUG, "open file server-output.dat");

	struct timeval tv_start, tv_end;

	u16 port = *(u16 *)arg;
	struct tcp_sock *tsk = alloc_tcp_sock();

	struct sock_addr addr;
	addr.ip = htonl(0);
	addr.port = port;
	if (tcp_sock_bind(tsk, &addr) < 0) {
		log(ERROR, "tcp_sock bind to port %hu failed", ntohs(port));
		exit(1);
	}

	if (tcp_sock_listen(tsk, 3) < 0) {
		log(ERROR, "tcp_sock listen failed");
		exit(1);
	}

	log(DEBUG, "listen to port %hu.", ntohs(port));

	struct tcp_sock *csk = tcp_sock_accept(tsk);

	log(DEBUG, "accept a connection.");
	gettimeofday(&tv_start,NULL);

	char data_buf[20030];
	int data_len = 0;
	while (1) {
		data_len = tcp_sock_read(csk, data_buf, 20024);
		if (data_len > 0) {
			fwrite(data_buf, 1, data_len, f);
		} else {
			log(DEBUG, "peer closed.");
			break;
		}
	}

	gettimeofday(&tv_end,NULL);
	long time_res = get_interval(tv_start,tv_end);
	time_res /= 1000000;
	fprintf(stderr, "used time: %ld s\n", time_res);

	fclose(f);
	log(DEBUG, "close this connection.");

	tcp_sock_close(csk);
	
	return NULL;
}

// tcp client application, connects to server (ip:port specified by arg), each
// time sends one bulk of data and receives one bulk of data 
void *tcp_client_file(void *arg)
{
	FILE * f = fopen("client-input.dat", "rb");
	if (!f) {
		log(ERROR, "open file client-input.dat failed");
	}
	log(DEBUG, "open file client-input.dat");

	struct sock_addr *skaddr = arg;

	struct tcp_sock *tsk = alloc_tcp_sock();

	if (tcp_sock_connect(tsk, skaddr) < 0) {
		log(ERROR, "tcp_sock connect to server ("IP_FMT":%hu)failed.", \
				NET_IP_FMT_STR(skaddr->ip), ntohs(skaddr->port));
		exit(1);
	}

	log(DEBUG, "connect success.");

	char data_buf[20030];
	int data_len = 0;

	int send_size = 0;
	while (1) {
		data_len = fread(data_buf, 1, 20024, f);
		if (data_len > 0) {
			send_size += data_len;
			log(DEBUG, "sent %d Bytes", send_size);
			tcp_sock_write(tsk, data_buf, data_len);
		} else {
			log(DEBUG, "the file has been sent completely.");
			break;
		}
		usleep(10000);
	}

	fclose(f);
	tcp_sock_close(tsk);

	return NULL;
}

void *tcp_cwnd_thread(void *arg) {
	struct tcp_sock *tsk = (struct tcp_sock *)arg;
	FILE *fp = fopen("cwnd.txt", "w");
	
	int time_us = 0;
	while (tsk->state == TCP_ESTABLISHED && time_us < 1000000) {
		usleep(500);
		time_us += 500;
		fprintf(fp, "%d %f %f\n", time_us, tsk->cwnd, tsk->cwnd * TCP_MSS);
	}
	fclose(fp);
	return NULL;
}

void cnwd_record(struct tcp_sock *tsk) {
#ifdef LOG_AS_PPT  
    struct timeval current;
    gettimeofday(&current, NULL);
    long duration = 1000000 * ( current.tv_sec - start.tv_sec ) + current.tv_usec - start.tv_usec;
    char line[100];
    sprintf(line, "%d %f %f\n", duration, tsk->cwnd, tsk->cwnd * TCP_MSS);
    fwrite(line, sizeof(char), strlen(line), "cwnd.txt");
#else
	return;
#endif
}