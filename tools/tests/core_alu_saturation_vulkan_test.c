/* Saturated destination views and unsaturated scalar history. MSVC C89. */
#include "alu_continuation_address_fixture.h"
#ifndef XE_ALU_RESUME_SATURATION
#error Saturation capability is required
#endif
#include "shader_saturation_fixture.h"
int main(void)
{
 static const address_fixture test={"saturation","SATURATION_DEVICE",160,640,640,0,
  saturation_program,saturation_context,saturation_status,saturation_golden,
  saturation_constant,saturation_window};
 return address_fixture_run(&test);
}
