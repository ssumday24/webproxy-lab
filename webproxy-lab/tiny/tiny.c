/*
0. 디렉토리 이동 
cd webproxy-lab/tiny

1. 컴파일
gcc -o tiny tiny.c csapp.c -lpthread -Wall

2. 서버 실행
./tiny 8000

3. 웹 브라우저 접속
http://localhost:8000
http://localhost:8000/home.html

안될시 쿠키,캐시 삭제후 실행

cd webproxy-lab/tiny && gcc -o tiny tiny.c csapp.c -lpthread -Wall && ./tiny 8000
*/

#include "csapp.h" 

// 함수선언
void doit(int fd); // HTTP 트랜잭션 처리
void read_requesthdrs(rio_t *rp); // (편의상 요청헤더 무시)
int parse_uri(char *uri, char *filename, char *cgiargs); // URI 파싱 (파일명, CGI 인자)
void serve_static(int fd, char *filename, int filesize); // 정적 파일 서비스
void get_filetype(char *filename, char *filetype); // 파일 타입(MIME) 얻기
void serve_dynamic(int fd, char *filename, char *cgiargs); // 동적 CGI 프로그램 서비스
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg); // 클라이언트 오류 응답

// 1. tiny main 
int main(int argc, char **argv)
{
    int listenfd, connfd; // 듣기 소켓, 연결 소켓
    //듣기 소켓은 서버당 1개만 존재
    char hostname[MAXLINE], port[MAXLINE]; // 클라이언트 호스트명, 포트명 저장 버퍼
    socklen_t clientlen; // 클라이언트 주소 구조체 크기
    struct sockaddr_storage clientaddr; // 클라이언트 소켓 주소 (IPv4/IPv6 겸용)

    // 명령줄 인자 개수 확인 (포트 번호 하나만 받는지)
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]); // 사용법 안내
        exit(1); // 프로그램 종료
    }

    // 듣기 소켓 생성 및 초기화 (socket, bind, listen 포함)
    // argv[1]로 받은 포트 사용
    listenfd = Open_listenfd(argv[1]);

    // 무한 루프: 클라이언트 연결 계속 처리
    while (1) {
        clientlen = sizeof(clientaddr);
        
        // 클라이언트 연결 요청 수락
        // listenfd로 들어온 요청 accept, 새 연결 소켓 connfd 반환
        // 클라이언트 주소 정보는 clientaddr에 저장
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        // 연결된 클라이언트 정보(호스트명, 포트명) 얻어 출력
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        // HTTP 트랜잭션 처리
        doit(connfd); 

        // 클라이언트와의 연결 소켓 닫기
        Close(connfd);
    }
    // return 0; // 무한 루프 때문에 도달 X
}

// 2. doit  - 한개의 HTTP 트랜잭션 처리

void doit(int fd)
{
    int is_static;    //요청이 정적인지 동적 인지 판별
    struct stat sbuf; 

    //버퍼들
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    // 파일 경로와 CGI 프로그램 인자를 저장할 버퍼
    char filename[MAXLINE], cgiargs[MAXLINE];

    rio_t rio; // (Robust I/O)를 위한 버퍼 구조체

    // 요청 라인과 헤더를 읽기 
    Rio_readinitb(&rio, fd); 
    Rio_readlineb(&rio, buf, MAXLINE); // 클라이언트로부터 HTTP 요청의 첫 번째 줄(요청 라인)을 읽어 buf에 저장
    printf("Request headers:\n"); 
    printf("%s", buf); // 읽어들인 [요청 라인 (Request Line)] 출력 

    sscanf(buf, "%s %s %s", method, uri, version);

    // HTTP 메서드 검사: Tiny 웹 서버는 GET과 HEAD 메서드만 지원
    if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) { // 메서드가 GET 또는 HEAD 둘 다 아니면
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method"); 
        return; // 함수 종료
    }
    read_requesthdrs(&rio); // 나머지 HTTP 요청 헤더들을 읽어들임 (이 예제에서는 특별히 처리하지 않고 버림)

    is_static = parse_uri(uri, filename, cgiargs);
    
    // stat 함수가 실패하면 (파일이 존재하지 않거나 접근 불가)
    if (stat(filename, &sbuf) < 0) { 
        clienterror(fd, filename, "404", "Not found",
                    "Tiny couldn't find this file");      // 404 오류 발생
        return; 
    }

    // 정적 콘텐츠 처리 (parse_uri 리턴값이 1 일때)
    if (is_static) {
        // 요청된 파일이 일반 파일이 아니거나, 사용자에게 읽기 권한이 없으면
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
            clienterror(fd, filename, "403", "Forbidden",
                        "Tiny couldn't read the file"); // 403 Forbidden 오류 
            return; 
        }

        // 모든 검사를 통과하면, 정적 파일 서비스 함수 호출
        serve_static(fd, filename, sbuf.st_size);
    }
    
    // 동적 콘텐츠 처리 (parse_uri 리턴값이 0 일때)
    else {
      
        // 요청된 파일이 일반 파일이 아니거나 현재 사용자에게 실행 권한이 없으면
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
            clienterror(fd, filename, "403", "Forbidden",
                        "Tiny couldn't run the CGI program"); // 403 Forbidden 오류 
            return; 
        }

        // 모든 검사를 통과하면, 동적 CGI 프로그램 서비스 함수 호출
        serve_dynamic(fd, filename, cgiargs);
    }
}

// 3.  에처 처리 기능
void clienterror(int fd, char *cause, char *errnum,
    char *shortmsg, char *longmsg)
{
char buf[MAXLINE], body[MAXBUF]; // HTTP 응답 헤더 및 본문 버퍼
int content_len = 0; // 현재 body의 길이를 추적할 변수

/* HTTP 응답 본문 생성 */
// 각 snprintf 호출 시, body + 현재 길이(content_len) 위치에 이어서 쓰고
// 남은 버퍼 공간(MAXBUF - content_len)을 지정하여 안전성 확보
content_len += snprintf(body + content_len, MAXBUF - content_len, 
               "<html><title>Tiny Error</title>\r\n");
content_len += snprintf(body + content_len, MAXBUF - content_len, 
               "<body bgcolor=\"ffffff\">\r\n"); // 배경색 설정
content_len += snprintf(body + content_len, MAXBUF - content_len, 
               "%s: %s\r\n", errnum, shortmsg); // 오류 번호와 짧은 메시지
content_len += snprintf(body + content_len, MAXBUF - content_len, 
               "<p>%s: %s\r\n", longmsg, cause); // 긴 메시지와 원인
content_len += snprintf(body + content_len, MAXBUF - content_len, 
               "<hr><em>The Tiny Web server</em>\r\n"); // 서버 서명

// content_len이 MAXBUF를 초과하지 않도록 보장되므로,
// 마지막에 Rio_writen(fd, body, content_len);을 사용해도 안전합니다.

/* HTTP 응답 헤더 생성 및 전송 */
// 여기서는 buf를 초기화하고 각 헤더를 별도로 Rio_writen으로 전송하는 방식으로 변경
// (또는 serve_static처럼 하나의 buf에 모두 담아 한번에 보낼 수도 있습니다.)

// 응답 라인 전송
snprintf(buf, MAXLINE, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
Rio_writen(fd, buf, strlen(buf));

// Content-type 헤더 전송 (오류 페이지는 HTML이므로 text/html)
snprintf(buf, MAXLINE, "Content-type: text/html\r\n"); 
Rio_writen(fd, buf, strlen(buf));

// Content-length 헤더 전송 (body의 실제 길이 사용)
snprintf(buf, MAXLINE, "Content-length: %d\r\n\r\n", content_len); 
Rio_writen(fd, buf, strlen(buf));

// 완성된 HTTP 응답 본문 전송
Rio_writen(fd, body, content_len);
}

// 4. read_requesthdrs - HTTP 요청 헤더들을 읽기 (여기서는 읽기만 하고 버림)
void read_requesthdrs(rio_t *rp)
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE); // 첫 번째 헤더 라인 읽기
   
    
    while(strcmp(buf, "\r\n" )) { 
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf); // 읽어들인 헤더 라인 출력 (서버 로그용)
    }
    return;
}

// 5. parse_uri - URI를 파싱하여 파일명과 CGI 인자를 분리하고, 정적/동적 여부를 반환
// 정적 콘텐츠면 1, 동적 콘텐츠면 0 
int parse_uri(char *uri, char *filename, char *cgiargs)
{
    char *ptr;

    // URI에 "cgi-bin"이 포함되어 있지 않으면 정적 콘텐츠로 판단
    if (!strstr(uri, "cgi-bin")) {  /* Static content */
        strcpy(cgiargs, "");    /* CGI 인자 스트링은 비움 */
        strcpy(filename, ".");  /* 현재 디렉토리부터 파일 경로 시작 */
        strcat(filename, uri);  /* URI를 파일명 뒤에 덧붙임 */
        // URI가 '/'로 끝나면 기본 파일명(home.html)을 추가
        if (uri[strlen(uri)-1] == '/')
            strcat(filename, "home.html");
        return 1; // 정적 콘텐츠
    }
    // URI에 "cgi-bin"이 포함되어 있으면 동적 콘텐츠로 판단
    else {  /* Dynamic content */
        ptr = index(uri, '?');  /* URI에서 '?' 문자의 위치를 찾음 */
        if (ptr) { // '?'가 있으면
            strcpy(cgiargs, ptr+1); /* '?' 다음부터 CGI 인자를 복사 */
            *ptr = '\0';            /* URI 문자열을 '?'에서 잘라 파일명 부분만 남김 */
        }
        else // '?'가 없으면
            strcpy(cgiargs, "");    /* CGI 인자는 없음 */
        strcpy(filename, ".");  /* 현재 디렉토리부터 파일 경로 시작 */
        strcat(filename, uri);  /* URI를 파일명 뒤에 덧붙임 */
        return 0; // 동적 콘텐츠
    }
}

// // 6. serve_static - 정적 파일을 클라이언트에 복사하여 서비스
// void serve_static(int fd, char *filename, int filesize)
// {
//     int srcfd;
//     char *srcp, filetype[MAXLINE], buf[MAXLINE];
//     int header_len = 0; // 헤더 길이를 추적할 변수

//     get_filetype(filename, filetype);

//     // HTTP 응답 헤더 생성 (snprintf 사용)
//     header_len += snprintf(buf + header_len, MAXLINE - header_len, "HTTP/1.0 200 OK\r\n");
//     header_len += snprintf(buf + header_len, MAXLINE - header_len, "Server: Tiny Web Server\r\n");
//     header_len += snprintf(buf + header_len, MAXLINE - header_len, "Connection: close\r\n");
//     header_len += snprintf(buf + header_len, MAXLINE - header_len, "Content-length: %d\r\n", filesize);
//     header_len += snprintf(buf + header_len, MAXLINE - header_len, "Content-type: %s\r\n\r\n", filetype);

//     Rio_writen(fd, buf, header_len); // 완성된 HTTP 응답 헤더를 클라이언트에 전송
//     printf("Response headers:\n");
//     printf("%s", buf);


//     srcfd = Open(filename, O_RDONLY, 0); // 요청 파일을 읽기 전용으로 열기

//     // 파일을 메모리에 매핑 (mmap 사용): 파일을 메모리처럼 다룰 수 있게 함
//     srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
//     Close(srcfd); // 파일을 메모리에 매핑했으므로 파일 디스크립터는 닫아도 됨
    
//     // 매핑된 메모리에서 클라이언트로 파일 내용 전송 
//     Rio_writen(fd, srcp, filesize);
//     Munmap(srcp, filesize); // 메모리 매핑 해제 (자원 반납)
// }

#define MAXBUF 8192 // 버퍼 크기 정의
// 숙제문제 11.9 : Tiny를 수정해서 정적 컨텐츠를 처리할때 요청한 파일을 mmap 과 rio_readn 대신에
// malloc, rio_writen 을 사용해서 연결 식별자에게 복사하도록 하시오.
void serve_static(int fd, char *filename, int filesize)
{
    int srcfd;
    char filetype[MAXLINE], buf[MAXBUF];
    char *filebuf; // 파일을 읽어올 버퍼 포인터
    ssize_t bytes_read; // 실제로 읽은 바이트 수

    /* HTTP 응답 헤더 생성 */
    // 이 부분은 이전 답변에서 제안드린 대로 snprintf와 header_len을 사용하는 것이 좋습니다.
    // 여기서는 간략하게 작성하며, 이전 수정 내용을 가정합니다.
    int header_len = 0;
    get_filetype(filename, filetype); // 파일 타입 결정

    header_len += snprintf(buf + header_len, MAXBUF - header_len, "HTTP/1.0 200 OK\r\n");
    header_len += snprintf(buf + header_len, MAXBUF - header_len, "Server: Tiny Web Server\r\n");
    header_len += snprintf(buf + header_len, MAXBUF - header_len, "Connection: close\r\n");
    header_len += snprintf(buf + header_len, MAXBUF - header_len, "Content-length: %d\r\n", filesize);
    header_len += snprintf(buf + header_len, MAXBUF - header_len, "Content-type: %s\r\n\r\n", filetype);

    Rio_writen(fd, buf, header_len); // 헤더 전송
    printf("Response headers:\n");
    printf("%s", buf);

    /* 요청된 파일 전송 */
    // 1. 파일 열기
    srcfd = Open(filename, O_RDONLY, 0); // O_RDONLY: 읽기 전용으로 열기

    // 2. malloc으로 파일 내용 저장할 버퍼 할당
    // 파일을 한 번에 읽어 메모리에 올리는 방식 (filesize가 MAXBUF보다 훨씬 클 경우 주의)
    // 작은 파일의 경우 이 방식으로 처리하고, 큰 파일은 청크 방식으로 처리하는 것이 일반적
    // 여기서는 파일을 전부 읽어올 크기로 malloc을 사용합니다.
    filebuf = (char *)Malloc(filesize); 
    
    // 3. Rio_readn을 사용하여 파일 내용을 할당된 버퍼로 읽어옴
    // Rio_readn은 fd로부터 size 바이트를 읽어 buf에 저장합니다.
    bytes_read = Rio_readn(srcfd, filebuf, filesize); 
    
    // 읽은 바이트 수가 filesize와 다를 경우 오류 처리 로직 추가 가능
    if (bytes_read != filesize) {
        fprintf(stderr, "Error reading file %s: expected %d bytes, read %zd bytes\n",
                filename, filesize, bytes_read);
        // 오류 응답을 클라이언트에 보내거나 연결 종료 등 추가 처리 필요
        Free(filebuf);
        Close(srcfd);
        return;
    }

    // 4. Rio_writen을 사용하여 버퍼의 내용을 연결 식별자(소켓)로 복사
    // 읽어온 파일 내용을 소켓에 전송합니다.
    Rio_writen(fd, filebuf, filesize); 

    // 5. 할당된 메모리 해제 및 파일 닫기
    Free(filebuf); // malloc으로 할당한 메모리 해제
    Close(srcfd); // 파일 디스크립터 닫기
}

// 7. get_filetype - 파일명으로부터 파일 타입(MIME)을 유추
void get_filetype(char *filename, char *filetype)
{
    if (strstr(filename, ".html")) // 파일명에 ".html"이 포함되면
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif")) // .gif
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png")) // .png
        strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg")) // .jpg
        strcpy(filetype, "image/jpeg");
    else if (strstr(filename, ".mp4")) // .mp4
        strcpy(filetype, "video/mp4"); 

    else if (strstr(filename, ".mpg")) // 숙제 11.8 : .mpg 또는 .mpeg 파일처리
        strcpy(filetype, "video/mpeg"); 
    else
        strcpy(filetype, "text/plain"); //일반 텍스트 파일로 처리
}

// 8. serve_dynamic - 클라이언트를 대신해 CGI 프로그램을 실행하는 함수
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
    char buf[MAXLINE], *emptylist[] = { NULL }; 

    
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); // 응답 라인
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n"); // Server 헤더
    Rio_writen(fd, buf, strlen(buf));

    // 자식 프로세스 생성: 
    if (Fork() == 0) { 

        /* 실제 서버에서는 모든 CGI 환경 변수를 여기서 설정 */
        setenv("QUERY_STRING", cgiargs, 1); // CGI 환경 변수 QUERY_STRING 설정 (클라이언트 요청 인자)
        
        // 표준 출력을 클라이언트 소켓으로 리디렉션:
        // CGI 프로그램의 printf 결과가 클라이언트에게 직접 전송되도록 함
        Dup2(fd, STDOUT_FILENO);         

        // CGI 프로그램 실행
        // environ: 현재 환경 변수 목록
        Execve(filename, emptylist, environ); 
    }
    Wait(NULL); // 부모 프로세스는 자식이 종료되기를 기다림 -> 자식 프로세스가 좀비가 되는 것을 방지
}
