/*

1.컴파일
gcc -o jh_echo_server jongho_echo_server.c csapp.c

2.실행
./jh_echo_server 8080

*/
#include "csapp.h"
void echo(int connfd); // 인자 : 연결소켓

int main(int argc, char **argv)
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char client_hostname[MAXLINE], client_port[MAXLINE];

    if (argc !=2){
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    //듣기소켓 = argv[1] (포트번호)
    listenfd =Open_listenfd(argv[1]);
    
    //서버가 명시적으로 종료될 때까지 반복
    while(1){
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA*) &clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen,client_hostname,MAXLINE,
                    client_port,MAXLINE,0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);
            
        echo(connfd);
        Close(connfd);
    }
    exit(0);
}

// 텍스트줄을 읽고 echo해주는 함수
/*
    connfd는 echo 함수가 어떤 클라이언트와 통신해야 하는지를 
    정확히 알려주는 유일한 정보
*/
void echo(int connfd)
{
    size_t n;
    char buf[MAXLINE];
    rio_t rio;

    Rio_readinitb(&rio,connfd);
    while( (n=Rio_readlineb(&rio,buf,MAXLINE)) !=0 ){
        printf("server received %d bytes \n", (int)n);
        Rio_writen(connfd,buf,n);
    }
}