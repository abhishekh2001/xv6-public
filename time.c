#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int main(int argc, char** argv)
{
  int ch = fork();

  if (ch > 0){  /* parent */
      int w, r;
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
    }
    exit();
  } else {
      printf(2, "fork failed\n");
  }
  exit();
}
