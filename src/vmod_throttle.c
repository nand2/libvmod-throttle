#include <stdlib.h>

#include "vrt.h"
#include "bin/varnishd/cache.h"

#include "vcc_if.h"

int
init_function(struct vmod_priv *priv, const struct VCL_conf *conf)
{
	return (0);
}

int
vmod_is_allowed(struct sess *sp)
{
	char *p;
	unsigned u, v;

  return 1;
}
