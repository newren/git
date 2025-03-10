#if !defined(XTRACE2_H)
#define XTRACE2_H

void xd_trace2_region_enter(const char *region_name,
			    const char *subregion_name);
void xd_trace2_region_leave(const char *region_name,
			    const char *subregion_name);

#endif /* #if !defined(XTRACE2_H) */
