#include "csapp.h" 

int main(int argc, char **argv)
{
    int clientfd;
    char *host, *port , buf[MAXLINE];
    rio_t rio;

    if (argc !=3){ //인자가 3개가 아닐때 예외처리
        fprintf(stderr, "usage: %s <host> <port>\n",argv[0]);
        exit(0);
    }
    host = argv[1]; // 호스트 이름
    port = argv[2]; // 포트,서비스네임

    //헬퍼함수 - csapp.c 에 정의
    //래퍼함수로, 오류 처리 자동화 
    // gettdaddrinfo -> socket() -> connect() 실행
    clientfd = Open_clientfd(host,port);
    Rio_readinitb(&rio,clientfd);

    while(Fgets(buf,MAXLINE,stdin) != NULL){
        Rio_writen(clientfd,buf,strlen(buf));
        Rio_readlineb(&rio,buf,MAXLINE);
        Fputs(buf,stdout);
    }
    Close(clientfd);
    exit(0);
}