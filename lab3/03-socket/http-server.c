#include <errno.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <resolv.h>
#include <pthread.h>
#include "openssl/ssl.h"
#include "openssl/bio.h"
#include "openssl/err.h"

void *handle_http_request(void *arg);
void *handle_https_request(void *arg);
void *HTTP_SERVER(void *arg);
void *HTTPS_SERVER(void *arg);

int main()
{   
    pthread_t thread1, thread2;

    if (pthread_create(&thread1, NULL, HTTP_SERVER, NULL) != 0)
    {
        perror("Thread creation failed");
        return -1;
    }
    
    if (pthread_create(&thread2, NULL, HTTPS_SERVER, NULL) != 0)
    {
        perror("Thread creation failed");
        return -1;
    }
    
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    return 0;
}

void *HTTP_SERVER(void *arg)
{   
    int port = 80;

    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Create socket failed");
        exit(1);
    }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("bind failed");
        exit(1);
    }

    listen(sockfd, 128);

    while (1)
    {
        struct sockaddr_in c_addr;
        socklen_t addr_len;

        int request = accept(sockfd, (struct sockaddr *)&c_addr, &addr_len);
        if (request < 0)
        {
            perror("Accept failed");
            exit(1);
        }

        pthread_t new_thread;

        if ((pthread_create(&new_thread, NULL, (void *)handle_http_request, (void *)&request)) != 0)
        {
            perror("Create handle_http_request thread failed");
            exit(1);
        }
    }

    close(sockfd);
    return NULL;
}


void *HTTPS_SERVER(void *arg)
{
    int port = 443;

    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    const SSL_METHOD *method = TLSv1_2_server_method();
    SSL_CTX *ctx = SSL_CTX_new(method);

    // load certificate and private key
    if (SSL_CTX_use_certificate_file(ctx, "./keys/cnlab.cert", SSL_FILETYPE_PEM) <= 0)
    {
        perror("load cert failed");
        exit(1);
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, "./keys/cnlab.prikey", SSL_FILETYPE_PEM) <= 0)
    {
        perror("load prikey failed");
        exit(1);
    }

    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Create socket failed");
        exit(1);
    }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("bind failed");
        exit(1);
    }

    listen(sockfd, 10);
   
    while (1)
    {
        struct sockaddr_in c_addr;
        socklen_t addr_len;

        int request = accept(sockfd, (struct sockaddr *)&c_addr, &addr_len);
        if (request < 0)
        {
            perror("Accept failed");
            exit(1);
        }

        SSL *ssl = SSL_new(ctx);
        SSL_set_fd(ssl, request);

        pthread_t new_thread;

        if ((pthread_create(&new_thread, NULL, (void *)handle_https_request, (void *)ssl)) != 0)
        {
            perror("Create handle_http_request thread failed");
            exit(1);
        }
    }

    close(sockfd);
    SSL_CTX_free(ctx);
    return NULL;
}


void *handle_http_request(void *arg)
{  
    pthread_detach(pthread_self());

    int request = *(int *)arg;

    char *recv_buff = (char *)malloc(2000 * sizeof(char));
    memset(recv_buff, 0, 2000);
    char *send_buff = (char *)malloc(6000 * sizeof(char));

    int request_len;
    request_len = recv(request, recv_buff, 2000, 0);
    if (request_len < 0)
    {
        fprintf(stderr, "recv failed\n");
        exit(1);
    }

    char *req_get = strstr(recv_buff, "GET");
    if (req_get)
    {
        char *iterator;
        iterator = req_get + 4;

        char *temp_url = (char *)malloc(50 * sizeof(char));
        char *http_version = (char *)malloc(9 * sizeof(char));
        char *host = (char *)malloc(100 * sizeof(char));
        int relative_url;

        relative_url = ((*iterator) == '/');

        int i;
        for (i = 0; (*iterator) != ' '; iterator++, i++)
        {
            temp_url[i] = *iterator;
        }
        temp_url[i] = '\0';
        iterator++;
        
        for (i = 0; (*iterator) != '\r'; iterator++, i++)
        {
            http_version[i] = *iterator;
        }
        http_version[i] = '\0';
        
        if (relative_url)
        {
            iterator = strstr(recv_buff, "Host:");
            if(!iterator){
                perror("Not found Host");
                exit(1);
            }
            iterator += 6;

            for (int i = 0; (*iterator) != '\r'; iterator++, i++)
            {
                host[i] = *iterator;
            }
            host[i] = '\0';
        }

        memset(send_buff, 0, 6000);
        strcat(send_buff, http_version);
        strcat(send_buff, " 301 Moved Permanently\r\nLocation: ");
        strcat(send_buff, "https://");

        if (relative_url)
        {
            strcat(send_buff, host);
            strcat(send_buff, temp_url);
        }
        else
        {
            strcat(send_buff, &temp_url[7]);
        }
        strcat(send_buff, "\r\n\r\n\r\n\r\n");

        if ((send(request, send_buff, strlen(send_buff), 0)) < 0)
        {
            fprintf(stderr, "send failed");
            exit(1);
        }

        free(temp_url);
        free(http_version);
        free(host);
    }

    free(send_buff);
    free(recv_buff);

    close(request);
    return NULL;
}


void *handle_https_request(void *arg)
{   
    pthread_detach(pthread_self());

    SSL *ssl = (SSL *)arg;
    if (SSL_accept(ssl) == -1)
    {
        perror("SSL_accept failed");
        exit(1);
    }

    char *recv_buff = (char *)malloc(2000 * sizeof(char));
    char *send_buff = (char *)malloc(6000 * sizeof(char));
    int keep_alive = 1;

    while (keep_alive)
    {
        memset(recv_buff, 0, 2000);
        int request_len = SSL_read(ssl, recv_buff, 2000);
        if (request_len < 0)
        {
            fprintf(stderr, "SSL_read failed\n");
            exit(1);
        }

        if(recv_buff[0] == '\0')
        {
            break;
        }

        char *temp_url = (char *)malloc(50 * sizeof(char));
        char *http_version = (char *)malloc(9 * sizeof(char));
        char *file_path = (char *)malloc(100 * sizeof(char));

        char *req_get = strstr(recv_buff, "GET");
        if (req_get)
        {
            char *iterator;
            iterator = req_get + 4;

            int relative_url;
            int range = 0;
            int range_begin, range_end;

            relative_url = (*(iterator) == '/');

            int i;
            for (i = 0; *iterator != ' '; iterator++, i++)
            {
                temp_url[i] = *iterator;
            }
            temp_url[i] = '\0';
            iterator++;

            for (i = 0; *iterator != '\r'; iterator++, i++)
            {
                http_version[i] = *iterator;
            }
            http_version[i] = '\0';

            if(iterator = (strstr(recv_buff, "Range:")))
            {
                iterator += 13;
                range = 1;

                range_begin = 0;
                while(*iterator >= '0' && *iterator <= '9')
                {
                    range_begin = range_begin * 10 + (*iterator) - '0';
                    iterator++;
                }
                iterator++;

                if(*iterator < '0' || *iterator > '9')
                {
						range_end = -1;
				}
                else{
					range_end = 0;
					while(*iterator >= '0' && *iterator <= '9')
                    {
						range_end = range_end * 10 + (*iterator)-'0';
						iterator++;
					}
				}
            }

            if(iterator = (strstr(recv_buff, "Connection:")))
            {
                iterator += 12;
                if(*iterator == 'k'){
                    keep_alive = 1;
                }
                else if(*iterator == 'c'){
                    keep_alive = 0;
                }
            }

            file_path[0] = '.';
            file_path[1] = '\0';
            if(relative_url){
                strcat(file_path, temp_url);
            }
            else
            {
                i = 0;
                int count = 3;
                while(count){
                    if(temp_url[i] == '/'){
                        count--;
                    }
                    i++;
                }
                strcat(file_path, temp_url + i);
            }


            FILE *fp = fopen(file_path, "r");
            if(fp == NULL)
            {   
                memset(send_buff, 0, 6000);
				strcat(send_buff, http_version);
				strcat(send_buff, " 404 Not Found\r\n\r\n\r\n\r\n");
				SSL_write(ssl, send_buff, strlen(send_buff));
                
				break;
            }
            else
            {
                char header[200] = {0};
				strcat(header,http_version);

				if(range){
					strcat(header, " 206 Partial Content\r\n");
                }
				else{
					strcat(header, " 200 OK\r\n");
                }
                    
				int size,begin;
				if(range){
					if(range_end==-1){
						fseek(fp,0L,SEEK_END);
						size = ftell(fp) - range_begin + 1;
						begin = range_begin;
					}
                    else{
						size = range_end - range_begin + 1;
						begin = range_begin;
					}
				}
                else{
					fseek(fp,0L,SEEK_END);
					size = ftell(fp);
					begin = 0;
				}
				// static  using content-length
				strcat(header, "Content-Length: ");
				fseek(fp,begin,0);
            
				char str_size[64] = {0};	
                sprintf(str_size, "%d", size);

				char response[size + 200];
				memset(response,0, size + 200);
				strcat(response, header);
				strcat(response, str_size);

				strcat(response,"\r\nConnection: ");
				if(keep_alive)
					strcat(response, "keep-alive");
				else
					strcat(response, "close");

				strcat(response, "\r\n\r\n");
				fread(&(response[strlen(response)]), 1, size, fp);
				SSL_write(ssl,response,strlen(response));

				fclose(fp);

				if(range==1 && range_end==-1)
					break;
            }
        }

        free(temp_url);
        free(http_version);
        free(file_path);
        
    }

    free(send_buff);
    free(recv_buff);

    int requst = SSL_get_fd(ssl);
	SSL_free(ssl);
	close(requst);
    return NULL;
}

