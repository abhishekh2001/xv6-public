#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int main(int argc, char** argv)
{
  int ch = fork();

  if (ch > 0){  /* parent */
      int w = -1, r = -1;
      ch = waitx(&w, &r);

      printf(1, "child pid = %d\n", ch);
      printf(1, "wait time = %d\n", w);
      printf(1, "run time = %d\n", r);
  } else if(ch == 0){  /* child */
    if (argc > 1){
      if (exec(argv[1], argv+1) < 0){
        printf(2, "(time): %s failed", argv[1]);
        exit();
      }
    } else {
      printf(1, "pid %d\n", getpid());
      int p;
      for (int i = 0; i < 1000000; i++)
        for (int l = 0; l < 10000000; l++)
          for (int z = 0; z < 100000000; z++)
            p = p ^ 1;
      sleep(200);
      ps();
    }
    exit();
  } else {
      printf(2, "fork failed\n");
  }
  exit();
}
