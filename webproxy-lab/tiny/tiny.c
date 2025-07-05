/*
1. 컴파일
gcc -o tiny tiny.c csapp.c -lpthread -Wall

2. 서버 실행
./tiny 8000

3. 웹 브라우저 접속
http://localhost:8000

*/

#include "csapp.h" 

// 함수선언
void doit(int fd); // HTTP 트랜잭션 처리
void read_requesthdrs(rio_t *rp); // 요청 헤더 읽기 (무시)
int parse_uri(char *uri, char *filename, char *cgiargs); // URI 파싱 (파일명, CGI 인자)
void serve_static(int fd, char *filename, int filesize); // 정적 파일 서비스
void get_filetype(char *filename, char *filetype); // 파일 타입(MIME) 얻기
void serve_dynamic(int fd, char *filename, char *cgiargs); // 동적 CGI 프로그램 서비스
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg); // 클라이언트 오류 응답

// 1. tiny main 
int main(int argc, char **argv)
{
    int listenfd, connfd; // 듣기 소켓, 연결 소켓
    char hostname[MAXLINE], port[MAXLINE]; // 클라이언트 호스트명, 포트명 저장 버퍼
    socklen_t clientlen; // 클라이언트 주소 구조체 크기
    struct sockaddr_storage clientaddr; // 클라이언트 소켓 주소 (IPv4/IPv6 겸용)

    // 명령줄 인자 개수 확인 (포트 번호 하나만 받는지)
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]); // 사용법 안내
        exit(1); // 프로그램 종료
    }

    // 듣기 소켓 생성 및 초기화 (socket, bind, listen 포함)
    // argv[1]로 받은 포트 번호 사용
    listenfd = Open_listenfd(argv[1]);

    // 무한 루프: 클라이언트 연결 계속 처리
    while (1) {
        clientlen = sizeof(clientaddr); // 클라이언트 주소 구조체 크기 초기화
        
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

    rio_t rio; // 견고한 I/O(Robust I/O)를 위한 버퍼 구조체

    // 요청 라인과 헤더를 읽기 
    Rio_readinitb(&rio, fd); 
    Rio_readlineb(&rio, buf, MAXLINE); // 클라이언트로부터 HTTP 요청의 첫 번째 줄(요청 라인)을 읽어 buf에 저장
    printf("Request headers:\n"); 
    printf("%s", buf); // 읽어들인 요청 라인 출력 

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

    // 정적 콘텐츠 처리
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
    
    // 동적 콘텐츠 (CGI 프로그램) 요청 처리
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

    /* HTTP 응답 본문 생성 */
    sprintf(body, "<html><title>Tiny Error</title>"); // HTML 시작
    sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body); // 배경색 설정
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg); // 오류 번호와 짧은 메시지
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause); // 긴 메시지와 원인
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body); // 서버 서명
    
    /* HTTP 응답 헤더 생성 및 전송 */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg); // 응답 라인
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n"); 
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body)); 
    Rio_writen(fd, buf, strlen(buf));
    
    Rio_writen(fd, body, strlen(body)); // 완성된 HTTP 응답 본문 전송
}

// 4. read_requesthdrs - HTTP 요청 헤더들을 읽기 (여기서는 읽기만 하고 버림)
void read_requesthdrs(rio_t *rp)
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE); // 첫 번째 헤더 라인 읽기
    while(strcmp(buf, "\r\n")) { // CRLF(빈 줄)가 나올 때까지 반복해서 모든 헤더 라인 읽음
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

// 6. serve_static - 정적 파일을 클라이언트에 복사하여 서비스
void serve_static(int fd, char *filename, int filesize)
{
    int srcfd; // 소스 파일 디스크립터
    char *srcp, filetype[MAXLINE], buf[MAXLINE]; // 파일 내용을 매핑할 포인터, 파일 타입, 응답 버퍼

    /* 클라이언트에 응답 헤더 전송 */
    get_filetype(filename, filetype); // 파일명으로부터 파일 타입(MIME) 결정 
    
    // HTTP 응답 헤더 생성
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); // 응답 라인
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf); // Server 헤더 추가
    sprintf(buf, "%sConnection: close\r\n", buf); // Connection 헤더: 연결 종료를 알림
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize); // Content-Length 헤더: 파일 크기
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype); // Content-Type 헤더와 헤더 끝을 알리는 빈 줄
    
    Rio_writen(fd, buf, strlen(buf)); // 완성된 HTTP 응답 헤더를 클라이언트에 전송
    printf("Response headers:\n"); // 서버 터미널에 로그 출력
    printf("%s", buf); // 전송된 응답 헤더 출력


    srcfd = Open(filename, O_RDONLY, 0); // 요청 파일을 읽기 전용으로 열기

    // 파일을 메모리에 매핑 (mmap 사용): 파일을 메모리처럼 다룰 수 있게 함
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    Close(srcfd); // 파일을 메모리에 매핑했으므로 파일 디스크립터는 닫아도 됨
    
    // 매핑된 메모리에서 클라이언트로 파일 내용 전송 
    Rio_writen(fd, srcp, filesize);
    Munmap(srcp, filesize); // 메모리 매핑 해제 (자원 반납)
}

// 7. get_filetype - 파일명으로부터 파일 타입(MIME)을 유추
void get_filetype(char *filename, char *filetype)
{
    if (strstr(filename, ".html")) // 파일명에 ".html"이 포함되면
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif")) // .gif면
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png")) // .png면
        strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg")) // .jpg면
        strcpy(filetype, "image/jpeg");
    else if (strstr(filename, ".mp4")) // .mp4면
        strcpy(filetype, "video/mp4"); 
    else
        strcpy(filetype, "text/plain"); //일반 텍스트 파일로 처리
}

// 8. serve_dynamic - 클라이언트를 대신해 CGI 프로그램을 실행하는 함수
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
    char buf[MAXLINE], *emptylist[] = { NULL }; // 응답 버퍼, execve에 전달할 인자 리스트 (비어 있음)

    /* HTTP 응답의 첫 부분 (상태 라인 및 Server 헤더) 클라이언트에 반환 */
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); // 응답 라인
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n"); // Server 헤더
    Rio_writen(fd, buf, strlen(buf));

    // 자식 프로세스 생성: CGI 프로그램을 실행하기 위해 새로운 프로세스를 생성
    if (Fork() == 0) { /* 자식 프로세스 */
        /* 실제 서버에서는 모든 CGI 환경 변수를 여기서 설정 */
        setenv("QUERY_STRING", cgiargs, 1); // CGI 환경 변수 QUERY_STRING 설정 (클라이언트 요청 인자)
        
        // 표준 출력을 클라이언트 소켓으로 리다이렉션:
        // CGI 프로그램의 printf 결과가 클라이언트에게 직접 전송되도록 함
        Dup2(fd, STDOUT_FILENO);         
        Execve(filename, emptylist, environ); /* CGI 프로그램 실행 */ // environ: 현재 환경 변수 목록
    }
    Wait(NULL); /* 부모 프로세스는 자식이 종료되기를 기다림 */ // 자식 프로세스가 좀비가 되는 것을 방지
}
