#include "types.h"
#include "stat.h"
#include "user.h"

int main()
{
  printf(1, "waitx test is now starting...\n");

  int ch = fork();

  if (ch > 0){
      int w = 10, r;
      ch = waitx(&w, &r);

      printf(1, "parent: ch = %d returned\n", ch);
      printf(1, "wait = %d, run = %d\n", w, r);
  } else if(ch == 0){
      printf(1, "inside child\n");

      sleep(100);
      for (int i = 0; i < 1000; i++)
        printf(1, "test");
      
      printf(1, "Child exiting...\n");
      exit();
  } else {
      printf(1, "fork failed\n");
  }
  exit();
}