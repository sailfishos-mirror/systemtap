#include "sys/sdt.h"
#include "string.h"
#include <stdio.h>

class ProbeClass
{
private:
  int& ref;
  const char *name;

public:
  ProbeClass(int& v, const char *n) : ref(v), name(n)
  {
    char nm [32];
    strcpy (nm, name);
    STAP_PROBE2(_test_, cons, nm, ref);
  }

  void method(int min)
  {
    char nm [32];
    strcpy (nm, name);
    STAP_PROBE3(_test_, meth, nm, ref, min);
    ref -= min;
  }
  
  ~ProbeClass()
  {
    char nm [32];
    strcpy (nm, name);
    STAP_PROBE2(_test_, dest, nm, ref);
  }
}; 

static void
call()
{
  int i = 64;
  STAP_PROBE1(_test_, call, i);
  ProbeClass inst = ProbeClass(i, "call");
  inst.method(24);
  i += 2;
  // Here the destructor goes out of scope and uses i as ref one last time.
}

static void
call2()
{
  int j = 24;
  STAP_PROBE1(_test_, call2, j);
  ProbeClass inst = ProbeClass(j, "call2");
  inst.method(40);
  j += 58;
  // Here the destructor goes out of scope and uses i as ref one last time.
}

int
main (int argc, char **argv)
{
  STAP_PROBE(_test_, main_enter);
  call();
  call2();
  STAP_PROBE(_test_, main_exit);
  return 0;
}
