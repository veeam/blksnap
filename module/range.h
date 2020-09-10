#pragma once

typedef struct range_s
{
	sector_t ofs;
	sector_t cnt;
}range_t;

static inline void range_copy( range_t* dst, range_t* src )
{
	dst->ofs = src->ofs;
	dst->cnt = src->cnt;
}
static inline range_t range_empty( void ){
	range_t rg = { 0 };
	return rg;
};

static inline bool range_is_empty( range_t* rg ){
	return (rg->cnt == 0);
};
