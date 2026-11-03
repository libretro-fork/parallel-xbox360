/* Raw tessellation stage selection. MSVC C89; BSD license. */
#include "guest_vertex_stage.h"
int xe_dxil_guest_domain_config(unsigned host, unsigned *domain, unsigned *points) {
  if (!domain || !points) return 0;
  switch (host) {
    case 3: *domain = 2; *points = 3; break;
    case 4: *domain = 2; *points = 1; break;
    case 5: *domain = 3; *points = 4; break;
    case 6: *domain = 3; *points = 1; break;
    default: return 0;
  }
  return 1;
}
int xe_dxil_guest_tessellation_hosts(unsigned mode, unsigned host, unsigned *vs, unsigned *hs) {
  unsigned domain, points;
  if (!vs || !hs || mode > 2 || !xe_dxil_guest_domain_config(host,&domain,&points)) return 0;
  if (mode == 2) {
    if (points != 1) return 0;
    *vs = 1; *hs = domain == 2 ? 10 : 11;
  } else {
    *vs = 0; *hs = 2+mode*4+(domain == 3 ? 2 : 0)+(points != 1);
  }
  return 1;
}
