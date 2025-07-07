// 두개의 숫자를 더하는 CGI 프로그램
/*
0. 디렉토리 이동 
cd webproxy-lab/tiny

1. 컴파일
gcc -o cgi-bin/adder cgi-bin/adder.c

2. 서버 실행
./tiny 8000

3 <cgi-bin/adder 에 실행 권한 부여>
chmod +x cgi-bin/adder

4. 웹 브라우저 접속
http://localhost:8000
http://localhost:8000/cgi-bin/adder?first=100&second=9999

*/

//#include "csapp.h" 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXLINE 1024
int main(void)
{
  char *buf, *p; // 문자열 버퍼와 포인터 선언
  char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE]; // 인자 및 응답 내용을 저장할 배열 선언 
  int n1 = 0, n2 = 0; // 두 숫자를 저장할 변수 초기화


  //  "QUERY_STRING"에서 요청 인자("first=10&second=20") 추출
  // 웹 서버는 GET 요청의 쿼리 스트링을 QUERY_STRING 환경 변수에 넣어 CGI 프로그램에 전달
  if ((buf = getenv("QUERY_STRING")) != NULL)
  {
    // buf에서 '&' 문자를 찾습니다. '&'는 두 인자를 구분하는 역할을 합니다.
    p = strchr(buf, '&');

    // 찾은 '&' 위치에 널 문자('\0')를 넣어 buf 문자열을 첫 번째 인자까지만 유효하게 만듦
    // "first=10&second=20" -> "first=10\0second=20"
    *p = '\0';

    // 첫 번째 인자("first=10")를 arg1 복사
    strcpy(arg1, buf);

    // 두 번째 인자("second=20")를 arg2 복사 (p+1은 '\0' 다음 위치를 가리킴)
    strcpy(arg2, p + 1);

    // arg1, arg2 에서 '=' 문자를 찾고, 그 다음 문자부터 숫자로 변환하여 n1에 저장
    // 예를 들어, "first=10" -> "10"을 atoi로 변환
    // strchr : 특정 문자가 가리키는 위치의 포인터 반환.
    n1 = atoi(strchr(arg1, '=') + 1);

    n2 = atoi(strchr(arg2, '=') + 1);
  }

  // 브라우저로 보낼 응답 생성
  
  sprintf(content, "QUERY_STRING=%s\r\n<p>", buf);
  sprintf(content + strlen(content), "Welcome to add.com: ");
  sprintf(content + strlen(content), "THE Internet addition portal.\r\n<p>");
  sprintf(content + strlen(content), "The answer is: %d + %d = %d\r\n<p>",
          n1, n2, n1 + n2);
  sprintf(content + strlen(content), "Thanks for visiting!\r\n");
  printf("Content-type: text/html\r\n");
  printf("Content-length: %d\r\n", (int)strlen(content));

  // 헤더와 본문 사이에 빈 줄 출력-HTTP 프로토콜의 필수 요소
  printf("\r\n");
  printf("%s", content);
  fflush(stdout);

  exit(0);
}
