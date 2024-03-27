/*
 * pcm_data.h - arrays of audio samples for buddhabox
 * 09-28-23 E. Brombaugh
 */
#include "tdf_raw.h"
#include "shriek_alu_raw.h"
#include "td_tangram1_raw.h"
#include "tt_lwymi_raw.h"

/*
 * pointers to sample records
 */
const uint8_t *pcm_data[4] = 
{
	tdf_raw,
	shriek_alu_raw,
	td_tangram1_raw,
	tt_lwymi_raw,
};

/*
 * pointers to record lengths
 */
const int *pcm_len[4] =
{
	&tdf_raw_len,
	&shriek_alu_raw_len,
	&td_tangram1_raw_len,
	&tt_lwymi_raw_len,
};
	