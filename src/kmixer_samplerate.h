/*	$NetBSD: aurateconv.h,v 1.1 2002/03/17 11:36:37 kent Exp $	*/

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

#ifndef _KMIXER_SAMPLERATE_H
#define _KMIXER_SAMPLERATE_H

#include <dev/audio_if.h>

struct kmixer_samplerate_context {
	long	count;
	int32_t	prev[AUDIO_MAX_CHANNELS];
	uint8_t	*ring_start;
	uint8_t	*ring_end;
};

int kmixer_samplerate_check_params(const struct audio_params *,
				   const struct audio_params *);
void kmixer_samplerate_init_context(struct kmixer_samplerate_context *,
				    long, long, uint8_t *, uint8_t *);
int kmixer_samplerate_play(struct kmixer_samplerate_context *,
			   const struct audio_params *,
			   const struct audio_params *,
			   uint8_t *, const uint8_t *, int);
int kmixer_samplerate_record(struct kmixer_samplerate_context *,
			     const struct audio_params *,
			     const struct audio_params *,
			     uint8_t *, const uint8_t *, int);

#endif /* _KMIXER_SAMPLERATE_H */
