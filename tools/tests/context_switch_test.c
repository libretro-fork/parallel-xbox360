/* Check the owned libco ABI with two suspends/resumes. MSVC C89. */
#include <libco.h>
#include <stdlib.h>
#include <stdio.h>
static cothread_t parent,child;
static int phase;
static void entry(void)
{
   phase=1;
   co_switch(parent);
   if(phase!=1) abort();
   phase=2;
   co_switch(parent);
   abort();
}
int main(void)
{
   int result;
   parent=co_active();
   child=co_create(65536,entry);
   if(!child) return 1;
   co_switch(child);
   result=phase!=1 || co_active()!=parent;
   if(!result)
   {
      co_switch(child);
      result=phase!=2 || co_active()!=parent;
   }
   co_delete(child);
   if(!result) puts("Context switch: two resumes matched.");
   return result;
}