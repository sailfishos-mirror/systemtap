#include <linux/module.h>

unsigned long foobar(struct module_sect_attrs *moosas);

unsigned long foobar(struct module_sect_attrs *moosas)
{
  __attribute__((unused)) struct module_sect_attr msa = moosas->attrs[0];
  return msa.address;
}
