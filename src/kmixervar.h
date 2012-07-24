/* $NetBSD$ */

/*-
 * Copyright (c) 2010-2012 Jared D. McNeill <jmcneill@invisible.ca>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#ifndef _KMIXERVAR_H
#define _KMIXERVAR_H

struct kmixer_softc;
struct kmixer_hw;
struct kmixer_ch;

TAILQ_HEAD(kmixer_hw_list, kmixer_hw);
TAILQ_HEAD(kmixer_ch_list, kmixer_ch);

/* hardware state */
struct kmixer_hw {
	device_t		hw_dev;
	dev_t			hw_audiodev;
	struct kmixer_ch_list	hw_act_ch;	/* active channel list */
	TAILQ_ENTRY(kmixer_hw)	hw_entry;
};

/* channel state */
struct kmixer_ch {
	kmutex_t		ch_lock;
	kcondvar_t		ch_cv;
	struct kmixer_softc	*ch_softc;
	struct kmixer_hw	*ch_selhw;

	audio_params_t		ch_pparams;

	TAILQ_ENTRY(kmixer_ch) ch_entry;
};

/* kmixer state */
struct kmixer_softc {
	kmutex_t		sc_lock;
	kcondvar_t		sc_cv;

	struct kmixer_hw_list	sc_hw;		/* hardware list */
	struct kmixer_hw	*sc_selhw;	/* selected hw device */

	struct kmixer_ch_list	sc_inact_ch;	/* inactive channel list */

	void			*sc_hook;	/* devicehook handle */

	struct audio_softc	sc_audiosc;	/* fake audio softc */
};

#endif /* !_KMIXERVAR_H */
