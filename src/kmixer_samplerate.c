/*	$NetBSD: aurateconv.c,v 1.9 2003/12/31 13:51:28 bjh21 Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by TAMURA Kent
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: aurateconv.c,v 1.9 2003/12/31 13:51:28 bjh21 Exp $");

#include <sys/systm.h>
#include <sys/types.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/select.h>
#include <sys/audioio.h>

#include <dev/audio_if.h>
#include <dev/audiovar.h>

#include "kmixer_samplerate.h"

#ifdef KMIXER_SAMPLERATE_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

static int kmixer_samplerate_play_slinear16_LE(
	struct kmixer_samplerate_context *,
	const struct audio_params *, const struct audio_params *,
	uint8_t *, const uint8_t *, int);
static int kmixer_samplerate_play_slinear24_LE(
	struct kmixer_samplerate_context *,
	const struct audio_params *, const struct audio_params *,
	uint8_t *, const uint8_t *, int);
static int kmixer_samplerate_play_slinear16_BE(
	struct kmixer_samplerate_context *,
	const struct audio_params *, const struct audio_params *,
	uint8_t *, const uint8_t *, int);
static int kmixer_samplerate_play_slinear24_BE(
	struct kmixer_samplerate_context *,
	const struct audio_params *, const struct audio_params *,
	uint8_t *, const uint8_t *, int);

static int kmixer_samplerate_record_slinear16_LE(
	struct kmixer_samplerate_context *,
	const struct audio_params *, const struct audio_params *,
	uint8_t *, const uint8_t *, int);
static int kmixer_samplerate_record_slinear24_LE(
	struct kmixer_samplerate_context *,
	const struct audio_params *, const struct audio_params *,
	uint8_t *, const uint8_t *, int);
static int kmixer_samplerate_record_slinear16_BE(
	struct kmixer_samplerate_context *,
	const struct audio_params *, const struct audio_params *,
	uint8_t *, const uint8_t *, int);
static int kmixer_samplerate_record_slinear24_BE(
	struct kmixer_samplerate_context *,
	const struct audio_params *, const struct audio_params *,
	uint8_t *, const uint8_t *, int);

int
kmixer_samplerate_check_params(const struct audio_params *from,
    const struct audio_params *to)
{
	DPRINTF(("kmixer_samplerate_check_params: rate=%ld:%ld chan=%d:%d "
		 "prec=%d:%d enc=%d:%d\n,"
		 from->sample_rate, to->sample_rate,
		 from->channels, to->channels, from->precision,
		 to->precision, from->encoding, to->encoding));
	if (to->channels == from->channels
	    && to->sample_rate == from->sample_rate)
		return 0;	/* No conversion */

	if ((to->encoding != AUDIO_ENCODING_SLINEAR_LE
	     && to->encoding != AUDIO_ENCODING_SLINEAR_BE)
	    || (to->precision != 16 && to->precision != 24))
		return (EINVAL);

	if (to->channels != from->channels) {
		if (to->channels == 1 && from->channels == 2) {
			/* Ok */
		} else if (to->channels == 2 && from->channels == 1) {
			/* Ok */
		} else if (to->channels > from->channels) {
			/* Ok */
		} else
			return (EINVAL);
	}
	if (to->channels > AUDIO_MAX_CHANNELS
	    || from->channels > AUDIO_MAX_CHANNELS)
		return (EINVAL);

	if (to->sample_rate != from->sample_rate)
		if (to->sample_rate <= 0 || from->sample_rate <= 0)
			return (EINVAL);
	return 0;
}

void
kmixer_samplerate_init_context(struct kmixer_samplerate_context *context,
    long src_rate, long dst_rate, uint8_t *start, uint8_t *end)
{
	int i;

	context->ring_start = start;
	context->ring_end = end;
	if (dst_rate > src_rate) {
		context->count = src_rate;
	} else {
		context->count = 0;
	}
	for (i = 0; i < AUDIO_MAX_CHANNELS; i++)
		context->prev[i] = 0;
}

/*
 * src is a ring buffer.
 */
int
kmixer_samplerate_record(struct kmixer_samplerate_context *context,
    const struct audio_params *from, const struct audio_params *to,
    uint8_t *dest, const uint8_t *src, int srcsize)
{
	if (to->sample_rate == from->sample_rate
	    && to->channels == from->channels) {
		int n;

		n = context->ring_end - src;
		if (srcsize <= n)
			memcpy(dest, src, srcsize);
		else {
			memcpy(dest, src, n);
			memcpy(dest + n, context->ring_start, srcsize - n);
		}
		return srcsize;
	}

	switch (to->encoding) {
	case AUDIO_ENCODING_SLINEAR_LE:
		switch (to->precision) {
		case 16:
			return kmixer_samplerate_record_slinear16_LE(context,
			    from, to, dest, src, srcsize);
		case 24:
			return kmixer_samplerate_record_slinear24_LE(context,
			    from, to, dest, src, srcsize);
		}
		break;
	case AUDIO_ENCODING_SLINEAR_BE:
		switch (to->precision) {
		case 16:
			return kmixer_samplerate_record_slinear16_BE(context,
			    from, to, dest, src, srcsize);
		case 24:
			return kmixer_samplerate_record_slinear24_BE(context,
			    from, to, dest, src, srcsize);
		}
		break;
	default:
		/* This should be rejected in kmixer_samplerate_check_params */
		printf("kmixer_samplerate_record: unimplemented encoding: %d\n",
		       to->encoding);
		return 0;
	}
	printf("kmixer_samplerate_record: unimplemented precision: %d\n",
	       to->precision);
	return 0;
}

/*
 * dest is a ring buffer.
 */
int
kmixer_samplerate_play(struct kmixer_samplerate_context *context,
    const struct audio_params *from, const struct audio_params *to,
    uint8_t *dest, const uint8_t *src, int srcsize)
{
	int n;

	if (to->sample_rate == from->sample_rate
	    && to->channels == from->channels) {
		n = context->ring_end - dest;
		if (srcsize <= n) {
			memcpy(dest, src, srcsize);
		} else {
			memcpy(dest, src, n);
			memcpy(context->ring_start, src + n, srcsize - n);
		}
		return srcsize;
	}

	switch (to->encoding) {
	case AUDIO_ENCODING_SLINEAR_LE:
		switch (to->precision) {
		case 16:
			return kmixer_samplerate_play_slinear16_LE(context,
			    from, to, dest, src, srcsize);
		case 24:
			return kmixer_samplerate_play_slinear24_LE(context,
			    from, to, dest, src, srcsize);
		}
		break;
	case AUDIO_ENCODING_SLINEAR_BE:
		switch (to->precision) {
		case 16:
			return kmixer_samplerate_play_slinear16_BE(context,
			    from, to, dest, src, srcsize);
		case 24:
			return kmixer_samplerate_play_slinear24_BE(context,
			    from, to, dest, src, srcsize);
		}
		break;
	default:
		/* This should be rejected in kmixer_samplerate_check_params */
		printf("kmixer_samplerate_play: unimplemented encoding: %d\n",
		       to->encoding);
		return 0;
	}
	printf("kmixer_samplerate_play: unimplemented precision: %d\n",
	       to->precision);
	return 0;
}


#define RING_CHECK(C, V)	\
	do { \
		if (V >= (C)->ring_end) \
			V = (C)->ring_start; \
	} while (/*CONSTCOND*/ 0)

#define READ_S8LE(P)		*(const int8_t*)(P)
#define WRITE_S8LE(P, V)	*(int8_t*)(P) = V
#define READ_S8BE(P)		*(const int8_t*)(P)
#define WRITE_S8BE(P, V)	*(int8_t*)(P) = V
#if BYTE_ORDER == LITTLE_ENDIAN
# define READ_S16LE(P)		*(const int16_t*)(P)
# define WRITE_S16LE(P, V)	*(int16_t*)(P) = V
# define READ_S16BE(P)		(int16_t)((P)[0] | ((P)[1]<<8))
# define WRITE_S16BE(P, V)	\
	do { \
		int vv = V; \
		(P)[0] = vv; \
		(P)[1] = vv >> 8; \
	} while (/*CONSTCOND*/ 0)
#else
# define READ_S16LE(P)		(int16_t)((P)[0] | ((P)[1]<<8))
# define WRITE_S16LE(P, V)	\
	do { \
		int vv = V; \
		(P)[0] = vv; \
		(P)[1] = vv >> 8; \
	} while (/*CONSTCOND*/ 0)
# define READ_S16BE(P)		*(const int16_t*)(P)
# define WRITE_S16BE(P, V)	*(int16_t*)(P) = V
#endif
#define READ_S24LE(P)		(int32_t)((P)[0] | ((P)[1]<<8) | (((int8_t)((P)[2]))<<16))
#define WRITE_S24LE(P, V)	\
	do { \
		int vvv = V; \
		(P)[0] = vvv; \
		(P)[1] = vvv >> 8; \
		(P)[2] = vvv >> 16; \
	} while (/*CONSTCOND*/ 0)
#define READ_S24BE(P)		(int32_t)((P)[2] | ((P)[1]<<8) | (((int8_t)((P)[0]))<<16))
#define WRITE_S24BE(P, V)	\
	do { \
		int vvv = V; \
		(P)[0] = vvv >> 16; \
		(P)[1] = vvv >> 8; \
		(P)[2] = vvv; \
	} while (/*CONSTCOND*/ 0)

#define P_READ_Sn(BITS, EN, V, RP, FROM, TO)	\
	do { \
		int j; \
		for (j = 0; j < (FROM)->channels; j++) { \
			(V)[j] = READ_S##BITS##EN(RP); \
			RP += (BITS) / NBBY; \
		} \
	} while (/*CONSTCOND*/ 0)
#define P_WRITE_Sn(BITS, EN, V, WP, FROM, TO, CON, WC)	\
	do { \
		if ((FROM)->channels == 2 && (TO)->channels == 1) { \
			WRITE_S##BITS##EN(WP, ((V)[0] + (V)[1]) / 2); \
			WP += (BITS) / NBBY; \
			RING_CHECK(CON, WP); \
			WC += (BITS) / NBBY; \
		} else { /* channels <= hw_channels */ \
			int j; \
			for (j = 0; j < (FROM)->channels; j++) { \
				WRITE_S##BITS##EN(WP, (V)[j]); \
				WP += (BITS) / NBBY; \
				RING_CHECK(CON, WP); \
			} \
			if (j == 1 && 1 < (TO)->channels) { \
				WRITE_S##BITS##EN(WP, (V)[0]); \
				WP += (BITS) / NBBY; \
				RING_CHECK(CON, WP); \
				j++; \
			} \
			for (; j < (TO)->channels; j++) { \
				WRITE_S##BITS##EN(WP, 0); \
				WP += (BITS) / NBBY; \
				RING_CHECK(CON, WP); \
			} \
			WC += (BITS) / NBBY * j; \
		} \
	} while (/*CONSTCOND*/ 0)

#define R_READ_Sn(BITS, EN, V, RP, FROM, TO, CON, RC)	\
	do { \
		int j; \
		for (j = 0; j < (TO)->channels; j++) { \
			(V)[j] = READ_S##BITS##EN(RP); \
			RP += (BITS) / NBBY; \
			RING_CHECK(CON, RP); \
			RC += (BITS) / NBBY; \
		} \
	} while (/*CONSTCOND*/ 0)
#define R_WRITE_Sn(BITS, EN, V, WP, FROM, TO, WC)	\
	do { \
		if ((FROM)->channels == 2 && (TO)->channels == 1) { \
			WRITE_S##BITS##EN(WP, (V)[0]); \
			WP += (BITS) / NBBY; \
			WRITE_S##BITS##EN(WP, (V)[0]); \
			WP += (BITS) / NBBY; \
			WC += (BITS) / NBBY * 2; \
		} else if ((FROM)->channels == 1 && (TO)->channels >= 2) { \
			WRITE_S##BITS##EN(WP, ((V)[0] + (V)[1]) / 2); \
			WP += (BITS) / NBBY; \
			WC += (BITS) / NBBY; \
		} else {	/* channels <= hw_channels */ \
			int j; \
			for (j = 0; j < (FROM)->channels; j++) { \
				WRITE_S##BITS##EN(WP, (V)[j]); \
				WP += (BITS) / NBBY; \
			} \
			WC += (BITS) / NBBY * j; \
		} \
	} while (/*CONSTCOND*/ 0)

/*
 * Function templates
 *
 *   Source may be 1 sample.  Destination buffer must have space for converted
 *   source.
 *   Don't use them for 32bit data because this linear interpolation overflows
 *   for 32bit data.
 */
#define KMIXER_SAMPLERATE_PLAY_SLINEAR(BITS, EN)	\
static int \
kmixer_samplerate_play_slinear##BITS##_##EN \
			      (struct kmixer_samplerate_context *context, \
			       const struct audio_params *from, \
			       const struct audio_params *to, \
			       uint8_t *dest, const uint8_t *src, \
			       int srcsize) \
{ \
	int wrote; \
	uint8_t *w; \
	const uint8_t *r; \
	const uint8_t *src_end; \
	int32_t v[AUDIO_MAX_CHANNELS]; \
	int32_t prev[AUDIO_MAX_CHANNELS], next[AUDIO_MAX_CHANNELS], c256; \
	int i, values_size; \
 \
	wrote = 0; \
	w = dest; \
	r = src; \
	src_end = src + srcsize; \
	if (from->sample_rate == to->sample_rate) { \
		while (r < src_end) { \
			P_READ_Sn(BITS, EN, v, r, from, to); \
			P_WRITE_Sn(BITS, EN, v, w, from, to, context, wrote); \
		} \
	} else if (to->sample_rate < from->sample_rate) { \
		for (;;) { \
			do { \
				if (r >= src_end) \
					return wrote; \
				P_READ_Sn(BITS, EN, v, r, from, to); \
				context->count += to->sample_rate; \
			} while (context->count < from->sample_rate); \
			context->count -= from->sample_rate; \
			P_WRITE_Sn(BITS, EN, v, w, from, to, context, wrote); \
		} \
	} else { \
		/* Initial value of context->count is from->sample_rate */ \
		values_size = sizeof(int32_t) * from->channels; \
		memcpy(prev, context->prev, values_size); \
		P_READ_Sn(BITS, EN, next, r, from, to); \
		for (;;) { \
			c256 = context->count * 256 / to->sample_rate; \
			for (i = 0; i < from->channels; i++) \
				v[i] = (c256 * next[i] + (256 - c256) * prev[i]) >> 8; \
			P_WRITE_Sn(BITS, EN, v, w, from, to, context, wrote); \
			context->count += from->sample_rate; \
			if (context->count >= to->sample_rate) { \
				context->count -= to->sample_rate; \
				memcpy(prev, next, values_size); \
				if (r >= src_end) \
					break; \
				P_READ_Sn(BITS, EN, next, r, from, to); \
			} \
		} \
		memcpy(context->prev, next, values_size); \
	} \
	return wrote; \
}

#define KMIXER_SAMPLERATE_RECORD_SLINEAR(BITS, EN)	\
static int \
kmixer_samplerate_record_slinear##BITS##_##EN \
				(struct kmixer_samplerate_context *context, \
				 const struct audio_params *from, \
				 const struct audio_params *to, \
				 uint8_t *dest, const uint8_t *src, \
				 int srcsize) \
{ \
	int wrote, rsize; \
	uint8_t *w; \
	const uint8_t *r; \
	int32_t v[AUDIO_MAX_CHANNELS]; \
	int32_t prev[AUDIO_MAX_CHANNELS], next[AUDIO_MAX_CHANNELS], c256; \
	int i, values_size; \
 \
	wrote = 0; \
	rsize = 0; \
	w = dest; \
	r = src; \
	if (from->sample_rate == to->sample_rate) { \
		while (rsize < srcsize) { \
			R_READ_Sn(BITS, EN, v, r, from, to, context, rsize); \
			R_WRITE_Sn(BITS, EN, v, w, from, to, wrote); \
		} \
	} else if (from->sample_rate < to->sample_rate) { \
		for (;;) { \
			do { \
				if (rsize >= srcsize) \
					return wrote; \
				R_READ_Sn(BITS, EN, v, r, from, to, context, rsize); \
				context->count += from->sample_rate; \
			} while (context->count < to->sample_rate); \
			context->count -= to->sample_rate; \
			R_WRITE_Sn(BITS, EN, v, w, from, to, wrote); \
		} \
	} else { \
		/* Initial value of context->count is to->sample_rate */ \
		values_size = sizeof(int32_t) * to->channels; \
		memcpy(prev, context->prev, values_size); \
		R_READ_Sn(BITS, EN, next, r, from, to, context, rsize); \
		for (;;) { \
			c256 = context->count * 256 / from->sample_rate; \
			for (i = 0; i < to->channels; i++) \
				v[i] = (c256 * next[i] + (256 - c256) * prev[i]) >> 8; \
			R_WRITE_Sn(BITS, EN, v, w, from, to, wrote); \
			context->count += to->sample_rate; \
			if (context->count >= from->sample_rate) { \
				context->count -= from->sample_rate; \
				memcpy(prev, next, values_size); \
				if (rsize >= srcsize) \
					break; \
				R_READ_Sn(BITS, EN, next, r, from, to, context, rsize); \
			} \
		} \
		memcpy(context->prev, next, values_size); \
	} \
	return wrote; \
}

KMIXER_SAMPLERATE_PLAY_SLINEAR(16, LE)
KMIXER_SAMPLERATE_PLAY_SLINEAR(24, LE)
KMIXER_SAMPLERATE_PLAY_SLINEAR(16, BE)
KMIXER_SAMPLERATE_PLAY_SLINEAR(24, BE)
KMIXER_SAMPLERATE_RECORD_SLINEAR(16, LE)
KMIXER_SAMPLERATE_RECORD_SLINEAR(24, LE)
KMIXER_SAMPLERATE_RECORD_SLINEAR(16, BE)
KMIXER_SAMPLERATE_RECORD_SLINEAR(24, BE)
