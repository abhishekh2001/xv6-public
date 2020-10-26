#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int main(int argc, char** argv)
{
  int ch = fork();

  if (ch > 0){  /* parent */
      exit();
  } else if(ch == 0){  /* child */
    sleep(30);
  } else {
      printf(2, "fork failed\n");
  }
  exit();
}