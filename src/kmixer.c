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

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/buf.h>
#include <sys/kmem.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/condvar.h>
#include <sys/kthread.h>
#include <sys/select.h>
#include <sys/audioio.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/vnode.h>

#include <dev/audiovar.h>
#include <dev/auconv.h>

#include "kmixervar.h"

#define KMIXER_BUFSIZE	AU_RING_SIZE

static int	kmixer_attach(void);
static int	kmixer_detach(void);

static void	kmixer_enum_hw(struct kmixer_softc *);
static void	kmixer_add_hw(struct kmixer_softc *, device_t);
static void	kmixer_del_hw(struct kmixer_softc *, device_t);
static void	kmixer_select_hw(struct kmixer_softc *);
static void	kmixer_devicehook(void *, device_t, int);

static struct kmixer_ch * kmixer_alloc_chan(struct kmixer_softc *);
static void	kmixer_free_chan(struct kmixer_ch *);

dev_type_open(kmixer_open);

const struct cdevsw kmixer_cdevsw = {
	.d_open = kmixer_open,
	.d_close = noclose,
	.d_read = noread,
	.d_write = nowrite,
	.d_ioctl = noioctl,
	.d_stop = nostop,
	.d_tty = notty,
	.d_poll = nopoll,
	.d_mmap = nommap,
	.d_kqfilter = nokqfilter,
	.d_flag = D_OTHER,
};

static int	kmixer_chan_read(struct file *, off_t *, struct uio *,
				 kauth_cred_t, int);
static int	kmixer_chan_write(struct file *, off_t *, struct uio *,
				  kauth_cred_t, int);
static int	kmixer_chan_ioctl(struct file *, u_long, void *);
static int	kmixer_chan_poll(struct file *, int);
static int	kmixer_chan_close(struct file *);

static const struct fileops kmixer_fileops = {
	.fo_read = kmixer_chan_read,
	.fo_write = kmixer_chan_write,
	.fo_ioctl = kmixer_chan_ioctl,
	.fo_fcntl = fnullop_fcntl,
	.fo_poll = kmixer_chan_poll,
	.fo_stat = fbadop_stat,
	.fo_close = kmixer_chan_close,
	.fo_kqfilter = fnullop_kqfilter,
};

extern const struct cdevsw audio_cdevsw;

static struct kmixer_softc *kmixer_softc = NULL;

static int
kmixer_attach(void)
{
	struct kmixer_softc *sc;

	kmixer_softc = sc = kmem_zalloc(sizeof(*sc), KM_SLEEP);
	if (sc == NULL) {
		printf("kmixer: couldn't allocate memory\n");
		return ENOMEM;
	}

	cv_init(&sc->sc_cv, "kmixer");
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	TAILQ_INIT(&sc->sc_hw);
	TAILQ_INIT(&sc->sc_ch);

	mutex_enter(&sc->sc_lock);
	kmixer_enum_hw(sc);
	mutex_exit(&sc->sc_lock);

	return 0;
}

static int
kmixer_detach(void)
{
	struct kmixer_softc *sc = kmixer_softc;
	struct kmixer_hw *hw;
	int cmaj, mn;

	if (sc->sc_hook)
		devicehook_disestablish(sc->sc_hook);

	cmaj = cdevsw_lookup_major(&kmixer_cdevsw);
	mn = 0;
	vdevgone(cmaj, mn, mn, VCHR);

	mutex_destroy(&sc->sc_lock);
	cv_destroy(&sc->sc_cv);

	while (!TAILQ_EMPTY(&sc->sc_hw)) {
		hw = TAILQ_FIRST(&sc->sc_hw);
		kmixer_del_hw(sc, hw->hw_dev);
	}
	kmixer_select_hw(sc);	/* cosmetic */

	kmem_free(sc, sizeof(*sc));
	kmixer_softc = NULL;

	return 0;
}

int
kmixer_open(dev_t dev, int flags, int fmt, struct lwp *l)
{
	struct kmixer_softc *sc = kmixer_softc;
	struct kmixer_ch *ch;
	struct file *fp;
	int err, fd;

	if (sc == NULL)
		return ENXIO;

	ch = kmixer_alloc_chan(sc);
	if (ch == NULL)
		return ENOMEM;

	err = fd_allocfile(&fp, &fd);
	if (err) {
		kmixer_free_chan(ch);
		return err;
	}

	return fd_clone(fp, fd, flags, &kmixer_fileops, ch);
}

static const char *
kmixer_hw_devname(struct kmixer_hw *hw)
{
	return device_xname(hw->hw_dev);
}

static const char *
kmixer_hw_parentname(struct kmixer_hw *hw)
{
	device_t parent;

	parent = device_parent(hw->hw_dev);

	return device_xname(parent);
}

static const char *
kmixer_hw_busname(struct kmixer_hw *hw)
{
	device_t parent, bus;

	parent = device_parent(hw->hw_dev);
	bus = device_parent(parent);

	return bus ? device_xname(bus) : "<none>";
}

static void
kmixer_add_hw(struct kmixer_softc *sc, device_t hw_dev)
{
	struct kmixer_hw *hw;
	device_t pdev, ppdev;

	KASSERT(mutex_owned(&sc->sc_lock));

	hw = kmem_alloc(sizeof(*hw), KM_SLEEP);
	hw->hw_dev = hw_dev;

	pdev = device_parent(hw_dev);
	ppdev = device_parent(pdev);

	printf("kmixer: add dev \"%s\" parent \"%s\" bus \"%s\"\n",
	    kmixer_hw_devname(hw), kmixer_hw_parentname(hw),
	    kmixer_hw_busname(hw));

	TAILQ_INSERT_TAIL(&sc->sc_hw, hw, hw_entry);
}

static void
kmixer_del_hw(struct kmixer_softc *sc, device_t hw_dev)
{
	struct kmixer_hw *hw;

	KASSERT(mutex_owned(&sc->sc_lock));

	TAILQ_FOREACH(hw, &sc->sc_hw, hw_entry) {
		if (hw->hw_dev == hw_dev) {
			TAILQ_REMOVE(&sc->sc_hw, hw, hw_entry);
			kmem_free(hw, sizeof(*hw));
			break;
		}
	}
}

static void
kmixer_enum_hw(struct kmixer_softc *sc)
{
	deviter_t di;
	device_t dev;

	KASSERT(mutex_owned(&sc->sc_lock));

	sc->sc_hook = devicehook_establish(kmixer_devicehook, sc);

	for (dev = deviter_first(&di, DEVITER_F_LEAVES_FIRST); dev != NULL;
	     dev = deviter_next(&di)) {
		if (device_is_a(dev, "audio") &&
		    !device_is_a(device_parent(dev), "pad"))
			kmixer_add_hw(sc, dev);
	}
	deviter_release(&di);

	kmixer_select_hw(sc);
}

static void
kmixer_select_hw(struct kmixer_softc *sc)
{
	const char * const bus_pref [] = { "hdaudio", "pci", "uhub" };
	struct kmixer_hw *hw, *new_hw = NULL;
	unsigned int n;

	KASSERT(mutex_owned(&sc->sc_lock));

	for (n = 0; new_hw == NULL && n < __arraycount(bus_pref); n++) {
		TAILQ_FOREACH(hw, &sc->sc_hw, hw_entry) {
			device_t pdev, ppdev;
		 	pdev = device_parent(hw->hw_dev);
			ppdev = device_parent(pdev);
			if (ppdev && device_is_a(ppdev, bus_pref[n])) {
				new_hw = hw;
				break;
			}
		}
	}
	if (new_hw == NULL)
		new_hw = TAILQ_FIRST(&sc->sc_hw);

	if (new_hw != sc->sc_selhw) {
		sc->sc_selhw = new_hw;
		if (sc->sc_selhw) {
			printf("kmixer: selected dev \"%s\" parent \"%s\" bus \"%s\"\n",
			    kmixer_hw_devname(sc->sc_selhw),
			    kmixer_hw_parentname(sc->sc_selhw),
			    kmixer_hw_busname(sc->sc_selhw));
		} else {
			printf("kmixer: selected dev \"<none>\"\n");
		}
	}
}

static void
kmixer_devicehook(void *arg, device_t dev, int event)
{
	struct kmixer_softc *sc = arg;

	if (!device_is_a(dev, "audio"))
		return;

	mutex_enter(&sc->sc_lock);

	switch (event) {
	case DEVICE_ATTACHED:
		kmixer_add_hw(sc, dev);
		break;
	case DEVICE_DETACHED:
		kmixer_del_hw(sc, dev);
		break;
	}

	kmixer_select_hw(sc);

	mutex_exit(&sc->sc_lock);

}

static struct kmixer_ch *
kmixer_alloc_chan(struct kmixer_softc *sc)
{
	struct kmixer_ch *ch;

	ch = kmem_zalloc(sizeof(*ch), KM_SLEEP);
	if (ch == NULL)
		return NULL;

	ch->ch_softc = sc;
	cv_init(&ch->ch_cv, "kmixerch");
	mutex_init(&ch->ch_lock, MUTEX_DEFAULT, IPL_AUDIO);

	mutex_enter(&sc->sc_lock);
	TAILQ_INSERT_TAIL(&sc->sc_ch, ch, ch_entry);
	mutex_exit(&sc->sc_lock);

	return ch;
}

static void
kmixer_free_chan(struct kmixer_ch *ch)
{
	struct kmixer_softc *sc = ch->ch_softc;

	mutex_enter(&sc->sc_lock);
	TAILQ_REMOVE(&sc->sc_ch, ch, ch_entry);
	mutex_exit(&sc->sc_lock);

	mutex_destroy(&ch->ch_lock);
	cv_destroy(&ch->ch_cv);
	kmem_free(ch, sizeof(*ch));
}

static int
kmixer_chan_read(struct file *fp, off_t *offp, struct uio *uio,
    kauth_cred_t cred, int flags)
{
	return EIO;	/* TODO */
}

static int
kmixer_chan_write(struct file *fp, off_t *offp, struct uio *uio,
    kauth_cred_t cred, int flags)
{
	struct kmixer_ch *ch = fp->f_data;
	//struct kmixer_softc *sc = ch->ch_softc;

	if (uio->uio_resid == 0)
		return 0;

	mutex_enter(&ch->ch_lock);
	while (uio->uio_resid > 0) {
		
	}
	mutex_exit(&ch->ch_lock);

	return 0;
}

static int
kmixer_chan_ioctl(struct file *fp, u_long cmd, void *data)
{
	return ENXIO;	/* TODO */
}

static int
kmixer_chan_poll(struct file *fp, int events)
{
	return 0;	/* TODO */
}

static int
kmixer_chan_close(struct file *fp)
{
	struct kmixer_ch *ch = fp->f_data;

	kmixer_free_chan(ch);

	return 0;
}

MODULE(MODULE_CLASS_DRIVER, kmixer, NULL);

static int
kmixer_modcmd(modcmd_t cmd, void *arg)
{
	switch (cmd) {
	case MODULE_CMD_INIT:
		return kmixer_attach();
	case MODULE_CMD_FINI:
		return kmixer_detach();
	default:
		return ENOTTY;
	}
}
