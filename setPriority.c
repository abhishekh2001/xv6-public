#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int
main(int argc, char** argv)
{
  if (argc < 2){
    printf(1, "setPriority: 2 arguments expected\n");
    return -1;
  }

  int newpriority, pid;

  newpriority = atoi(argv[1]);
  pid = atoi(argv[2]);

  set_priority(newpriority, pid);
  exit();
}