/*
 * soc-dsp.c  --  ALSA SoC Audio DSP
 *
 * Copyright (C) 2010 Texas Instruments Inc.
 *
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#define DEBUG

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/ac97_codec.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/soc-dsp.h>

int soc_pcm_open(struct snd_pcm_substream *);
void soc_pcm_close(struct snd_pcm_substream *);
int soc_pcm_hw_params(struct snd_pcm_substream *, struct snd_pcm_hw_params *);
int soc_pcm_hw_free(struct snd_pcm_substream *);
int soc_pcm_prepare(struct snd_pcm_substream *);
int soc_pcm_trigger(struct snd_pcm_substream *, int);

static int is_be_supported(struct snd_soc_pcm_runtime *rtd, const char *link)
{
	struct snd_soc_dsp_link *dsp = rtd->dai_link->dsp_link;
	int i;

	for (i= 0; i < dsp->num_be; i++) {
		if(!strcmp(dsp->supported_be[i], link))
			return 1;
	}
	return 0;
}

static inline struct snd_pcm_substream *soc_get_be_substream(
		struct snd_soc_pcm_runtime *be, int stream)
{
	return be->pcm->streams[stream].substream;
}

static inline int be_connect(struct snd_soc_pcm_runtime *fe,
		struct snd_soc_pcm_runtime *be, int stream)
{
	struct snd_soc_dsp_params *dsp_params;

	/* only add new dsp_paramss */
	list_for_each_entry(dsp_params, &fe->be_clients[stream], list_be) {
		if (dsp_params->be == be && dsp_params->fe == fe)
			return 0;
	}

	dsp_params = kzalloc(sizeof(struct snd_soc_dsp_params), GFP_KERNEL);
	if (!dsp_params)
		return -ENOMEM;

	dsp_params->be = be;
	dsp_params->fe = fe;
	be->runtime[stream] = fe->runtime[stream];
	dsp_params->state = SND_SOC_DSP_LINK_STATE_NEW;
	list_add(&dsp_params->list_be, &fe->be_clients[stream]);
	list_add(&dsp_params->list_fe, &be->fe_clients[stream]);

	dev_dbg(&fe->dev, "  connected new DSP %s path %s %s %s\n",
			stream ? "capture" : "playback",  fe->dai_link->name,
			stream ? "<-" : "->", be->dai_link->name);
	return 1;
}

static inline void be_disconnect(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_soc_dsp_params *dsp_params, *d;

	list_for_each_entry_safe(dsp_params, d, &fe->be_clients[stream], list_be) {
		if (dsp_params->state == SND_SOC_DSP_LINK_STATE_OLD) {
			dev_dbg(&fe->dev, "  freed DSP %s path %s %s %s\n",
					stream ? "capture" : "playback", fe->dai_link->name,
					stream ? "<-" : "->", dsp_params->be->dai_link->name);
			list_del(&dsp_params->list_be);
			list_del(&dsp_params->list_fe);
			kfree(dsp_params);
		}
	}
}

/*
 * Find the corresponding BE DAIs that source or sink audio to this
 * FE substream.
 */
static int dsp_add_new_paths(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_soc_dai *cpu_dai = fe->cpu_dai;
	struct snd_soc_card *card = fe->card;
	int i, num, count = 0, err;
	const char *fe_aif = NULL, *be_aif;
	enum snd_soc_dapm_type fe_type, be_type;

	dev_dbg(&fe->dev, "scan for new %s %s streams\n", fe->dai_link->name,
			stream ? "capture" : "playback");

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		fe_type = snd_soc_dapm_aif_in;
		be_type = snd_soc_dapm_aif_out;
	} else {
		fe_type = snd_soc_dapm_aif_out;
		be_type = snd_soc_dapm_aif_in;
	}

	/* search card for valid frontend steams */
	for (i = 0; i < card->num_links; i++) {
		struct snd_soc_pcm_runtime *be = &card->rtd[i];

		/* check for frontend */
		if (be->dai_link->dynamic)
			continue;

		fe_aif = snd_soc_dapm_get_aif(&be->platform->dapm,
				cpu_dai->driver->name, fe_type);
		if (fe_aif)
			break;
	}

	if (fe_aif == NULL)
		return 0;

	/* search card for valid backends */
	for (i = 0; i < card->num_links; i++) {
		struct snd_soc_pcm_runtime *be = &card->rtd[i];

		/* check for frontend */
		if (be->dai_link->dynamic)
			continue;

		/* backends must belong to this frontend */
		if (!be->dai_link->no_pcm)
			continue;

		/* backend must be supported by machine driver */
		if (!is_be_supported(fe, be->dai_link->name))
			continue;

		/* get BE AIF */
		be_aif = snd_soc_dapm_get_aif(&be->platform->dapm,
				be->dai_link->stream_name, be_type);
		if (be_aif == NULL)
			continue;

		/* check for valid dsp_params */
		num = snd_soc_dapm_query_path(&be->platform->dapm,
					fe_aif, be_aif, stream);

		dev_dbg(&fe->dev, " scanned %s paths BE %s for stream %s num %d",
				stream ? "capture" : "playback", be_aif,
				be->dai_link->stream_name, num);

		/* add backend if we have space */
		if (num <= 0)
			continue;

		err = be_connect(fe, be, stream);
		if (err < 0)
			break;
		else if (err == 0)
			continue;

		count++;
	}

	/* the number of new paths created */
	return count;
}

/*
 * Find the corresponding BE DAIs that source or sink audio to this
 * FE substream.
 */
static int dsp_prune_old_paths(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_soc_dai *cpu_dai = fe->cpu_dai;
	struct snd_soc_card *card = fe->card;
	struct snd_soc_dsp_params *dsp_params;
	int i, num, count = 0;
	const char *fe_aif = NULL, *be_aif;
	enum snd_soc_dapm_type fe_type, be_type;

	dev_dbg(&fe->dev, "scan for old %s %s streams\n", fe->dai_link->name,
			stream ? "capture" : "playback");

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		fe_type = snd_soc_dapm_aif_in;
		be_type = snd_soc_dapm_aif_out;
	} else {
		fe_type = snd_soc_dapm_aif_out;
		be_type = snd_soc_dapm_aif_in;
	}

	/* search card for valid frontend steams */
	for (i = 0; i < card->num_links; i++) {
		struct snd_soc_pcm_runtime *be = &card->rtd[i];

		/* check for frontend */
		if (be->dai_link->dynamic)
			continue;

		fe_aif = snd_soc_dapm_get_aif(&be->platform->dapm,
				cpu_dai->driver->name, fe_type);
		if (fe_aif)
			break;
	}

	if (fe_aif == NULL)
		return 0;

	/* search card for valid backends */
	list_for_each_entry(dsp_params, &fe->be_clients[stream], list_be) {

		/* get BE AIF */
		be_aif = snd_soc_dapm_get_aif(&dsp_params->be->platform->dapm,
				dsp_params->be->dai_link->stream_name, be_type);
		if (be_aif == NULL) {
			continue;
		}

		/* check for valid dsp_params */
		num = snd_soc_dapm_query_path(&dsp_params->be->platform->dapm,
					fe_aif, be_aif, stream);

		dev_dbg(&fe->dev, " scanned %s paths BE %s for stream %s num %d",
				stream ? "capture" : "playback", be_aif,
				dsp_params->be->dai_link->stream_name, num);

		/* prune backend if no longer used */
		if (num > 0)
			continue;

		dsp_params->state = SND_SOC_DSP_LINK_STATE_OLD;
		count++;
	}

	/* the number of old paths pruned */
	return count;
}

/*
 * Update the state of all BE's with state old to state new.
 */
static void be_state_update(struct snd_soc_pcm_runtime *fe, int stream,
		enum snd_soc_dsp_link_state old, enum snd_soc_dsp_link_state new)
{
	struct snd_soc_dsp_params *dsp_params;

	list_for_each_entry(dsp_params, &fe->be_clients[stream], list_be) {
		if (dsp_params->state == old)
			dsp_params->state = new;
	}
}

/*
 * Update the state of all BE's to new regardless of current state.
 */
static void fe_state_update(struct snd_soc_pcm_runtime *fe, int stream,
		enum snd_soc_dsp_link_state new)
{
	struct snd_soc_dsp_params *dsp_params;

	list_for_each_entry(dsp_params, &fe->be_clients[stream], list_be)
			dsp_params->state = new;
}

/* Unwind the BE startup */
static void soc_dsp_be_dai_startup_unwind(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_soc_dsp_params *dsp_params;

	/* disable any enabled and non active backends */
	list_for_each_entry(dsp_params, &fe->be_clients[stream], list_be) {

		struct snd_pcm_substream *be_substream =
			soc_get_be_substream(dsp_params->be, stream);

		if (dsp_params->state != SND_SOC_DSP_LINK_STATE_NEW)
			continue;

		if (--dsp_params->be->users[stream] != 0)
			continue;

		soc_pcm_close(be_substream);
		be_substream->runtime = NULL;
	}
}

/* Startup all new BE */
static int soc_dsp_be_dai_startup(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_soc_dsp_params *dsp_params;
	int ret = 0;

	/* only startup BE DAIs that are either sinks or sources to this FE DAI */
	list_for_each_entry(dsp_params, &fe->be_clients[stream], list_be) {

		struct snd_pcm_substream *be_substream =
			soc_get_be_substream(dsp_params->be, stream);

		/* only open and ref count new links */
		if (dsp_params->state != SND_SOC_DSP_LINK_STATE_NEW)
			continue;

		/* first time the dsp_params is open ? */
		if (dsp_params->be->users[stream]++ != 0)
			continue;

		dev_dbg(&dsp_params->be->dev, "pcm: open BE %s\n",
				dsp_params->be->dai_link->name);

		be_substream->runtime = dsp_params->be->runtime[stream];
		ret = soc_pcm_open(be_substream);
		if (ret < 0)
			goto unwind;
	}

	/* update BE state */
	be_state_update(fe, stream,
			SND_SOC_DSP_LINK_STATE_NEW, SND_SOC_DSP_LINK_STATE_READY);
	return ret;

unwind:
	/* disable any enabled and non active backends */
	list_for_each_entry_continue_reverse(dsp_params, &fe->be_clients[stream], list_be) {

		struct snd_pcm_substream *be_substream =
			soc_get_be_substream(dsp_params->be, stream);

		if (dsp_params->state != SND_SOC_DSP_LINK_STATE_NEW)
			continue;

		if (--dsp_params->be->users[stream] != 0)
			continue;

		soc_pcm_close(be_substream);
		be_substream->runtime = NULL;
	}

	/* update BE state for disconnect */
	be_state_update(fe, stream,
			SND_SOC_DSP_LINK_STATE_NEW, SND_SOC_DSP_LINK_STATE_OLD);
	return ret;
}

static int soc_dsp_fe_dai_startup(struct snd_pcm_substream *fe_substream)
{
	struct snd_soc_pcm_runtime *fe = fe_substream->private_data;
	int ret = 0;

	mutex_lock(&fe->fe_mutex);

	ret = soc_dsp_be_dai_startup(fe, fe_substream->stream);
	if (ret < 0)
		goto be_err;

	dev_dbg(&fe->dev, "pcm: open FE %s\n", fe->dai_link->name);

	/* start the ABE frontend */
	ret = soc_pcm_open(fe_substream);
	if (ret < 0) {
		dev_err(&fe->dev,"pcm: failed to start FE %d\n", ret);
		goto unwind;
	}

	mutex_unlock(&fe->fe_mutex);
	return 0;

unwind:
	soc_dsp_be_dai_startup_unwind(fe, fe_substream->stream);
be_err:
	mutex_unlock(&fe->fe_mutex);
	return ret;
}

/* BE shutdown - called on DAPM sync updates (i.e. FE is already running)*/
static int soc_dsp_be_dai_shutdown(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_soc_dsp_params *dsp_params;

	/* only shutdown backends that are either sinks or sources to this frontend DAI */
	list_for_each_entry(dsp_params, &fe->be_clients[stream], list_be) {

		struct snd_pcm_substream *be_substream =
			soc_get_be_substream(dsp_params->be, stream);

		if (dsp_params->state != SND_SOC_DSP_LINK_STATE_OLD)
			continue;

		if (--dsp_params->be->users[stream] != 0)
			continue;

		dev_dbg(&dsp_params->be->dev, "pcm: close BE %s\n",
			dsp_params->fe->dai_link->name);

		soc_pcm_close(be_substream);
		be_substream->runtime = NULL;
	}
	return 0;
}

/* FE +BE shutdown - called on FE PCM ops */
static int soc_dsp_fe_dai_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *fe = substream->private_data;
	int stream = substream->stream;
	char stream_name[64];

	mutex_lock(&fe->fe_mutex);

	/* shutdown the BEs */
	soc_dsp_be_dai_shutdown(fe, substream->stream);

	dev_dbg(&fe->dev, "pcm: close FE %s\n", fe->dai_link->name);

	/* now shutdown the frontend */
	soc_pcm_close(substream);

	/* stop the DAPM stream corresponding to this FE */
	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		sprintf(stream_name, "%s %s", fe->dai_link->stream_name, "Playback");
	else
		sprintf(stream_name, "%s %s", fe->dai_link->stream_name, "Capture");
	snd_soc_dapm_stream_event(fe, stream_name, SND_SOC_DAPM_STREAM_STOP);

	mutex_unlock(&fe->fe_mutex);
	return 0;
}

static int soc_dsp_be_dai_hw_params(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_soc_dsp_params *dsp_params;
	int ret;

	list_for_each_entry(dsp_params, &fe->be_clients[stream], list_be) {

		struct snd_pcm_substream *be_substream =
			soc_get_be_substream(dsp_params->be, stream);

		if (dsp_params->state != SND_SOC_DSP_LINK_STATE_READY)
			continue;

		/* first time the dsp_params is open ? */
		if (dsp_params->be->users[stream] != 1)
			continue;

		dev_dbg(&dsp_params->be->dev, "pcm: hw_params BE %s\n",
			dsp_params->fe->dai_link->name);

		/* copy params for each dsp_params */
		memcpy(&dsp_params->params, &fe->params,
				sizeof(struct snd_pcm_hw_params));

		/* perform any hw_params fixups */
		if (dsp_params->be->dai_link->be_hw_params_fixup) {
			ret = dsp_params->be->dai_link->be_hw_params_fixup(dsp_params->be,
					&dsp_params->params);
			if (ret < 0) {
				dev_err(&dsp_params->be->dev,
						"pcm: hw_params BE fixup failed %d\n", ret);
				return ret;
			}
		}

		ret = soc_pcm_hw_params(be_substream, &dsp_params->params);
		if (ret < 0) {
			dev_err(&dsp_params->be->dev, "pcm: hw_params BE failed %d\n", ret);
			return ret;
		}
	}
	return 0;
}

int soc_dsp_fe_dai_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *fe = substream->private_data;
	int ret;

	mutex_lock(&fe->fe_mutex);

	memcpy(&fe->params, params, sizeof(struct snd_pcm_hw_params));
	ret = soc_dsp_be_dai_hw_params(fe, substream->stream);
	if (ret < 0)
		goto out;

	dev_dbg(&fe->dev, "pcm: hw_params FE %s\n", fe->dai_link->name);

	/* call hw_params on the frontend */
	ret = soc_pcm_hw_params(substream, params);
	if (ret < 0)
		dev_err(&fe->dev,"pcm: hw_params FE failed %d\n", ret);

out:
	mutex_unlock(&fe->fe_mutex);
	return ret;
}

int soc_dsp_be_dai_trigger(struct snd_soc_pcm_runtime *fe, int stream, int cmd)
{
	struct snd_soc_dsp_params *dsp_params;
	int ret = 0;

	list_for_each_entry(dsp_params, &fe->be_clients[stream], list_be) {

		struct snd_pcm_substream *be_substream =
			soc_get_be_substream(dsp_params->be, stream);

		/* only trigger ACTIVE or READY BE's */
		if (dsp_params->state == SND_SOC_DSP_LINK_STATE_NEW ||
				dsp_params->state == SND_SOC_DSP_LINK_STATE_OLD)
			continue;

		dev_dbg(&dsp_params->be->dev, "pcm: trigger BE %s cmd %d\n",
			dsp_params->fe->dai_link->name, cmd);

		ret = soc_pcm_trigger(be_substream, cmd);
		if (ret < 0) {
			dev_err(&dsp_params->be->dev,"pcm: trigger BE failed %d\n", ret);
			return ret;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(soc_dsp_be_dai_trigger);

int soc_dsp_fe_dai_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *fe = substream->private_data;
	struct snd_soc_dsp_link *dsp_link = fe->dai_link->dsp_link;
	int stream = substream->stream, ret;

	if (dsp_link->trigger[stream] != SND_SOC_DSP_TRIGGER_PRE)
		goto be_trigger;

	dev_dbg(&fe->dev, "pcm: trigger FE %s cmd %d\n", fe->dai_link->name, cmd);

	/* call trigger on the frontend before the backend. */
	ret = soc_pcm_trigger(substream, cmd);
	if (ret < 0) {
		dev_err(&fe->dev,"pcm: trigger FE failed %d\n", ret);
		return ret;
	}

be_trigger:
	ret = soc_dsp_be_dai_trigger(fe, substream->stream, cmd);

	if (dsp_link->trigger[stream] != SND_SOC_DSP_TRIGGER_POST)
		return 0;

	dev_dbg(&fe->dev, "pcm: trigger FE %s cmd %d\n", fe->dai_link->name, cmd);

	/* call trigger on the frontend after the backend. */
	ret = soc_pcm_trigger(substream, cmd);
	if (ret < 0) {
		dev_err(&fe->dev,"pcm: trigger FE failed %d\n", ret);
		return ret;
	}

	return 0;
}

static int soc_dsp_be_dai_prepare(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_soc_dsp_params *dsp_params;
	int ret = 0;

	list_for_each_entry(dsp_params, &fe->be_clients[stream], list_be) {

		struct snd_pcm_substream *be_substream =
			soc_get_be_substream(dsp_params->be, stream);

		/* only prepare ACTIVE or READY BE's */
		if (dsp_params->state == SND_SOC_DSP_LINK_STATE_NEW ||
				dsp_params->state == SND_SOC_DSP_LINK_STATE_OLD)
			continue;

		dev_dbg(&dsp_params->be->dev, "pcm: prepare BE %s\n",
			dsp_params->fe->dai_link->name);

		ret = soc_pcm_prepare(be_substream);
		if (ret < 0) {
			dev_err(&dsp_params->be->dev,"pcm: backend prepare failed %d\n",
					ret);
			break;
		}

		/* mark the BE as active */
		be_state_update(fe, stream, SND_SOC_DSP_LINK_STATE_READY,
				SND_SOC_DSP_LINK_STATE_ACTIVE);
	}
	return ret;
}

int soc_dsp_fe_dai_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *fe = substream->private_data;
	int stream = substream->stream, ret = 0;

	mutex_lock(&fe->fe_mutex);

	dev_dbg(&fe->dev, "pcm: prepare FE %s\n", fe->dai_link->name);

	ret = soc_dsp_be_dai_prepare(fe, substream->stream);
	if (ret < 0)
		goto out;

	/* mark the BE as active */
	fe_state_update(fe, stream, SND_SOC_DSP_LINK_STATE_ACTIVE);

	/* call prepare on the frontend */
	ret = soc_pcm_prepare(substream);
	if (ret < 0)
		dev_err(&fe->dev,"pcm: prepare FE %s failed\n", fe->dai_link->name);

out:
	mutex_unlock(&fe->fe_mutex);
	return ret;
}

static int soc_dsp_be_dai_hw_free(struct snd_soc_pcm_runtime *fe, int stream)
{
	struct snd_soc_dsp_params *dsp_params;

	/* only hw_params backends that are either sinks or sources
	 * to this frontend DAI */
	list_for_each_entry(dsp_params, &fe->be_clients[stream], list_be) {

		struct snd_pcm_substream *be_substream =
			soc_get_be_substream(dsp_params->be, stream);

		if (dsp_params->state != SND_SOC_DSP_LINK_STATE_OLD)
			continue;

		/* only free hw when no longer used */
		if (dsp_params->be->users[stream] != 1)
			continue;

		dev_dbg(&dsp_params->be->dev, "pcm: hw_free BE %s\n",
			dsp_params->fe->dai_link->name);

		soc_pcm_hw_free(be_substream);
	}

	return 0;
}

int soc_dsp_fe_dai_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *fe = substream->private_data;
	int ret, stream = substream->stream;

	mutex_lock(&fe->fe_mutex);

	fe_state_update(fe, stream, SND_SOC_DSP_LINK_STATE_OLD);

	dev_dbg(&fe->dev, "pcm: hw_free FE %s\n", fe->dai_link->name);

	/* call hw_free on the frontend */
	ret = soc_pcm_hw_free(substream);
	if (ret < 0)
		dev_err(&fe->dev,"pcm: hw_free FE %s failed\n", fe->dai_link->name);

	/* only hw_params backends that are either sinks or sources
	 * to this frontend DAI */
	ret = soc_dsp_be_dai_hw_free(fe, stream);

	mutex_unlock(&fe->fe_mutex);
	return ret;
}

static int dsp_run_update_stop(struct snd_soc_pcm_runtime *fe, int stream)
{
	int ret;

	dev_dbg(&fe->dev, "runtime %s close on FE %s\n",
			stream ? "capture" : "playback", fe->dai_link->name);

	ret = soc_dsp_be_dai_trigger(fe, stream, SNDRV_PCM_TRIGGER_STOP);
	if (ret < 0)
		return ret;

	ret = soc_dsp_be_dai_hw_free(fe, stream);
	if (ret < 0)
		return ret;

	ret = soc_dsp_be_dai_shutdown(fe, stream);
	if (ret < 0)
		return ret;

	return 0;
}

static int dsp_run_update_start(struct snd_soc_pcm_runtime *fe, int stream)
{
	int ret;

	dev_dbg(&fe->dev, "runtime %s open on FE %s\n",
			stream ? "capture" : "playback", fe->dai_link->name);

	ret = soc_dsp_be_dai_startup(fe, stream);
	if (ret < 0)
		return ret;

	ret = soc_dsp_be_dai_hw_params(fe, stream);
	if (ret < 0)
		return ret;

	ret = soc_dsp_be_dai_prepare(fe, stream);
	if (ret < 0)
		return ret;

	ret = soc_dsp_be_dai_trigger(fe, stream, SNDRV_PCM_TRIGGER_START);
	if (ret < 0)
		return ret;

	return 0;
}

static int dsp_run_update(struct snd_soc_pcm_runtime *fe, int stream)
{
	int ret;

	/* close down old BEs first */
	ret = dsp_run_update_stop(fe, stream);
	if (ret < 0)
		dev_err(&fe->dev, "failed to shutdown BEs\n");

	/* startup any new BSs */
	ret = dsp_run_update_start(fe, stream);
	if (ret < 0)
		dev_err(&fe->dev, "failed to startup BEs\n");

	return ret;
}

/* called when any mixer updates change FE -> BE the stream */
int soc_dsp_runtime_update(struct snd_soc_dapm_widget *widget)
{
	struct snd_soc_card *card;
	int i, ret = 0;

	if (widget->codec)
		card = widget->codec->card;
	else if (widget->platform)
		card = widget->platform->card;
	else
		return -EINVAL;

	for (i = 0; i < card->num_rtd; i++) {
		struct snd_soc_pcm_runtime *fe = &card->rtd[i];

		/* make sure link is BE */
		if (!fe->dai_link->dsp_link)
			continue;

		mutex_lock(&fe->fe_mutex);

		/* only check active links */ //lrg - locking
		if (!fe->cpu_dai->active) {
			mutex_unlock(&fe->fe_mutex);
			continue;
		}

		/* DAPM sync will call this to update DSP paths */
		dev_dbg(card->dev, "updates FE %s DSP update\n", fe->dai_link->name);

		/* update any playback paths */
		if (!(dsp_add_new_paths(fe, SNDRV_PCM_STREAM_PLAYBACK) ||
				dsp_prune_old_paths(fe, SNDRV_PCM_STREAM_PLAYBACK)))
			goto capture;

		/* run PCM ops on new/old playback paths */
		ret = dsp_run_update(fe, SNDRV_PCM_STREAM_PLAYBACK);
		if (ret < 0) {
			dev_err(&fe->dev, "failed to update playback FE stream %s\n",
					fe->dai_link->stream_name);
		}

		/* free old playback links */
		be_disconnect(fe, SNDRV_PCM_STREAM_PLAYBACK);

capture:
		/* update any capture paths */
		if (!(dsp_add_new_paths(fe, SNDRV_PCM_STREAM_CAPTURE) ||
				dsp_prune_old_paths(fe, SNDRV_PCM_STREAM_CAPTURE))) {
			mutex_unlock(&fe->fe_mutex);
			continue;
		}

		/* run PCM ops on new/old capture paths */
		ret = dsp_run_update(fe, SNDRV_PCM_STREAM_CAPTURE);
		if (ret < 0) {
			dev_err(&fe->dev, "failed to update capture FE stream %s\n",
					fe->dai_link->stream_name);
		}

		/* free old capture links */
		be_disconnect(fe, SNDRV_PCM_STREAM_CAPTURE);

		mutex_unlock(&fe->fe_mutex);
	}

	return ret;
}

int soc_dsp_be_digital_mute(struct snd_soc_pcm_runtime *fe, int mute)
{
	struct snd_soc_dsp_params *dsp_params;

	list_for_each_entry(dsp_params,
			&fe->be_clients[SNDRV_PCM_STREAM_PLAYBACK], list_be) {

		struct snd_soc_pcm_runtime *be = dsp_params->be;
		struct snd_soc_dai *dai = be->codec_dai;
		struct snd_soc_dai_driver *drv = dai->driver;

		dev_dbg(&be->dev, "BE digital mute %s\n", be->dai_link->name);

		if (be->dai_link->ignore_suspend)
			continue;

		if (drv->ops->digital_mute && dai->playback_active)
				drv->ops->digital_mute(dai, mute);
	}

	return 0;
}

int soc_dsp_be_cpu_dai_suspend(struct snd_soc_pcm_runtime *fe)
{
	struct snd_soc_dsp_params *dsp_params;

	/* suspend for playback */
	list_for_each_entry(dsp_params,
			&fe->be_clients[SNDRV_PCM_STREAM_PLAYBACK], list_be) {

		struct snd_soc_pcm_runtime *be = dsp_params->be;
		struct snd_soc_dai *dai = be->cpu_dai;
		struct snd_soc_dai_driver *drv = dai->driver;

		dev_dbg(&be->dev, "pm: BE CPU DAI playback suspend %s\n",
				be->dai_link->name);

		if (be->dai_link->ignore_suspend)
			continue;

		if (drv->suspend && !drv->ac97_control)
				drv->suspend(dai);
	}

	/* suspend for capture */
	list_for_each_entry(dsp_params,
			&fe->be_clients[SNDRV_PCM_STREAM_CAPTURE], list_be) {

		struct snd_soc_pcm_runtime *be = dsp_params->be;
		struct snd_soc_dai *dai = be->cpu_dai;
		struct snd_soc_dai_driver *drv = dai->driver;

		dev_dbg(&be->dev, "pm: BE CPU DAI capture suspend %s\n",
				be->dai_link->name);

		if (be->dai_link->ignore_suspend)
			continue;

		if (drv->suspend && !drv->ac97_control)
				drv->suspend(dai);
	}

	return 0;
}

int soc_dsp_be_ac97_cpu_dai_suspend(struct snd_soc_pcm_runtime *fe)
{
	struct snd_soc_dsp_params *dsp_params;

	/* suspend for playback */
	list_for_each_entry(dsp_params,
			&fe->be_clients[SNDRV_PCM_STREAM_PLAYBACK], list_be) {

		struct snd_soc_pcm_runtime *be = dsp_params->be;
		struct snd_soc_dai *dai = be->cpu_dai;
		struct snd_soc_dai_driver *drv = dai->driver;

		dev_dbg(&be->dev, "pm: BE CPU DAI playback suspend %s\n",
				be->dai_link->name);

		if (be->dai_link->ignore_suspend)
			continue;

		if (drv->suspend && drv->ac97_control)
				drv->suspend(dai);
	}

	/* suspend for capture */
	list_for_each_entry(dsp_params,
			&fe->be_clients[SNDRV_PCM_STREAM_CAPTURE], list_be) {

		struct snd_soc_pcm_runtime *be = dsp_params->be;
		struct snd_soc_dai *dai = be->cpu_dai;
		struct snd_soc_dai_driver *drv = dai->driver;

		dev_dbg(&be->dev, "pm: BE CPU DAI capture suspend %s\n",
				be->dai_link->name);

		if (be->dai_link->ignore_suspend)
			continue;

		if (drv->suspend && drv->ac97_control)
				drv->suspend(dai);
	}

	return 0;
}

int soc_dsp_be_platform_suspend(struct snd_soc_pcm_runtime *fe)
{
	struct snd_soc_dsp_params *dsp_params;

	/* suspend for playback */
	list_for_each_entry(dsp_params,
			&fe->be_clients[SNDRV_PCM_STREAM_PLAYBACK], list_be) {

		struct snd_soc_pcm_runtime *be = dsp_params->be;
		struct snd_soc_platform *platform = be->platform;
		struct snd_soc_platform_driver *drv = platform->driver;
		struct snd_soc_dai *dai = be->cpu_dai;

		dev_dbg(&be->dev, "pm: BE platform playback suspend %s\n",
				be->dai_link->name);

		if (be->dai_link->ignore_suspend)
			continue;

		if (drv->suspend && !platform->suspended) {
			drv->suspend(dai);
			platform->suspended = 1;
		}
	}

	/* suspend for capture */
	list_for_each_entry(dsp_params,
			&fe->be_clients[SNDRV_PCM_STREAM_CAPTURE], list_be) {

		struct snd_soc_pcm_runtime *be = dsp_params->be;
		struct snd_soc_platform *platform = be->platform;
		struct snd_soc_platform_driver *drv = platform->driver;
		struct snd_soc_dai *dai = be->cpu_dai;

		dev_dbg(&be->dev, "pm: BE platform capture suspend %s\n",
				be->dai_link->name);

		if (be->dai_link->ignore_suspend)
			continue;

		if (drv->suspend && !platform->suspended) {
			drv->suspend(dai);
			platform->suspended = 1;
		}
	}

	return 0;
}

int soc_dsp_be_cpu_dai_resume(struct snd_soc_pcm_runtime *fe)
{
	struct snd_soc_dsp_params *dsp_params;

	/* resume for playback */
	list_for_each_entry(dsp_params,
			&fe->be_clients[SNDRV_PCM_STREAM_PLAYBACK], list_be) {

		struct snd_soc_pcm_runtime *be = dsp_params->be;
		struct snd_soc_dai *dai = be->cpu_dai;
		struct snd_soc_dai_driver *drv = dai->driver;

		dev_dbg(&be->dev, "pm: BE CPU DAI playback resume %s\n",
				be->dai_link->name);

		if (be->dai_link->ignore_suspend)
			continue;

		if (drv->resume && !drv->ac97_control)
				drv->resume(dai);
	}

	/* suspend for capture */
	list_for_each_entry(dsp_params,
			&fe->be_clients[SNDRV_PCM_STREAM_CAPTURE], list_be) {

		struct snd_soc_pcm_runtime *be = dsp_params->be;
		struct snd_soc_dai *dai = be->cpu_dai;
		struct snd_soc_dai_driver *drv = dai->driver;

		dev_dbg(&be->dev, "pm: BE CPU DAI capture resume %s\n",
				be->dai_link->name);

		if (be->dai_link->ignore_suspend)
			continue;

		if (drv->resume && !drv->ac97_control)
				drv->resume(dai);
	}

	return 0;
}

int soc_dsp_be_ac97_cpu_dai_resume(struct snd_soc_pcm_runtime *fe)
{
	struct snd_soc_dsp_params *dsp_params;

	/* resume for playback */
	list_for_each_entry(dsp_params,
			&fe->be_clients[SNDRV_PCM_STREAM_PLAYBACK], list_be) {

		struct snd_soc_pcm_runtime *be = dsp_params->be;
		struct snd_soc_dai *dai = be->cpu_dai;
		struct snd_soc_dai_driver *drv = dai->driver;

		dev_dbg(&be->dev, "pm: BE CPU DAI playback resume %s\n",
				be->dai_link->name);

		if (be->dai_link->ignore_suspend)
			continue;

		if (drv->resume && drv->ac97_control)
				drv->resume(dai);
	}

	/* suspend for capture */
	list_for_each_entry(dsp_params,
			&fe->be_clients[SNDRV_PCM_STREAM_CAPTURE], list_be) {

		struct snd_soc_pcm_runtime *be = dsp_params->be;
		struct snd_soc_dai *dai = be->cpu_dai;
		struct snd_soc_dai_driver *drv = dai->driver;

		dev_dbg(&be->dev, "pm: BE CPU DAI capture resume %s\n",
				be->dai_link->name);

		if (be->dai_link->ignore_suspend)
			continue;

		if (drv->resume && drv->ac97_control)
				drv->resume(dai);
	}

	return 0;
}

int soc_dsp_be_platform_resume(struct snd_soc_pcm_runtime *fe)
{
	struct snd_soc_dsp_params *dsp_params;

	/* resume for playback */
	list_for_each_entry(dsp_params,
			&fe->be_clients[SNDRV_PCM_STREAM_PLAYBACK], list_be) {

		struct snd_soc_pcm_runtime *be = dsp_params->be;
		struct snd_soc_platform *platform = be->platform;
		struct snd_soc_platform_driver *drv = platform->driver;
		struct snd_soc_dai *dai = be->cpu_dai;

		dev_dbg(&be->dev, "pm: BE platform playback resume %s\n",
				be->dai_link->name);

		if (be->dai_link->ignore_suspend)
			continue;

		if (drv->resume && platform->suspended) {
			drv->resume(dai);
			platform->suspended = 0;
		}
	}

	/* resume for capture */
	list_for_each_entry(dsp_params,
			&fe->be_clients[SNDRV_PCM_STREAM_CAPTURE], list_be) {

		struct snd_soc_pcm_runtime *be = dsp_params->be;
		struct snd_soc_platform *platform = be->platform;
		struct snd_soc_platform_driver *drv = platform->driver;
		struct snd_soc_dai *dai = be->cpu_dai;

		dev_dbg(&be->dev, "pm: BE platform capture resume %s\n",
				be->dai_link->name);

		if (be->dai_link->ignore_suspend)
			continue;

		if (drv->resume && platform->suspended) {
			drv->resume(dai);
			platform->suspended = 0;
		}
	}

	return 0;
}

/*
 * FE stream event, send event to all active BEs.
 */
int soc_dsp_dapm_stream_event(struct snd_soc_pcm_runtime *fe,
	int dir, const char *stream, int event)
{
	struct snd_soc_dsp_params *dsp_params;

	/* resume for playback */
	list_for_each_entry(dsp_params, &fe->be_clients[dir], list_be) {

		struct snd_soc_pcm_runtime *be = dsp_params->be;

		/* only send a stream event to ACTIVE or READY BE's */
		if (dsp_params->state == SND_SOC_DSP_LINK_STATE_NEW ||
				dsp_params->state == SND_SOC_DSP_LINK_STATE_OLD)
			continue;

		dev_dbg(&be->dev, "pm: BE stream %s event %d dir %d\n",
				stream, event, dir);

		snd_soc_dapm_stream_event(be, stream, event);
	}

	return 0;
}

/* called when opening FE stream  */
int soc_dsp_fe_dai_open(struct snd_pcm_substream *fe_substream)
{
	struct snd_soc_pcm_runtime *fe = fe_substream->private_data;
	int err;

	fe->runtime[fe_substream->stream] = fe_substream->runtime;

	/* calculate valid and active FE <-> BE dsp_paramss */
	err = dsp_add_new_paths(fe, fe_substream->stream);
	if (err <= 0) {
		dev_err(&fe->dev, "asoc: %s no valid %s route from source to sink\n",
			fe->dai_link->name, fe_substream->stream ? "capture" : "playback");
		return -EINVAL;
	}

	return soc_dsp_fe_dai_startup(fe_substream);
}

/* called when closing FE stream  */
int soc_dsp_fe_dai_close(struct snd_pcm_substream *fe_substream)
{
	struct snd_soc_pcm_runtime *fe = fe_substream->private_data;
	int ret;

	ret = soc_dsp_fe_dai_shutdown(fe_substream);

	be_disconnect(fe, fe_substream->stream);

	return ret;
}

/* Module information */
MODULE_AUTHOR("Liam Girdwood, lrg@slimlogic.co.uk");
MODULE_DESCRIPTION("ALSA SoC DSP Core");
MODULE_LICENSE("GPL");
