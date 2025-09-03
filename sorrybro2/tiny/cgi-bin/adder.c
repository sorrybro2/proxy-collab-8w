#include <stdio.h>
#include <stdlib.h>
int main(void){
  char *qs = getenv("QUERY_STRING");
  int x=0,y=0; if (qs) sscanf(qs,"x=%d&y=%d",&x,&y);
  printf("Content-Type: text/html\r\n\r\n");
  printf("<html><body>%d + %d = %d</body></html>\n", x,y,x+y);
}
