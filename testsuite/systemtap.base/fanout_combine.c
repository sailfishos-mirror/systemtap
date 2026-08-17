/* Unique name so process.function("*tap_fanout_foo") matches only this. */
__attribute__((noinline))
void
tap_fanout_foo (void)
{
}

int
main (void)
{
  tap_fanout_foo ();
  return 0;
}
