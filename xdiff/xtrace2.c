#define USE_THE_REPOSITORY_VARIABLE

#include "xinclude.h"
#include "xtrace2.h"
#include "../repository.h"
#include "../trace2.h"


void xd_trace2_region_enter(const char *region_name,
			    const char *subregion_name)
{
	trace2_region_enter(region_name, subregion_name, the_repository);
}

void xd_trace2_region_leave(const char *region_name,
			    const char *subregion_name)
{
	trace2_region_leave(region_name, subregion_name, the_repository);
}
