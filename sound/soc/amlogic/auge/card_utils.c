/*
 * sound/soc/amlogic/auge/card_utils.c
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include "card_utils.h"

int aml_card_parse_daifmt(struct device *dev,
				  struct device_node *node,
				  struct device_node *codec,
				  char *prefix,
				  unsigned int *retfmt)
{
	struct device_node *bitclkmaster = NULL;
	struct device_node *framemaster = NULL;
	int prefix_len = prefix ? strlen(prefix) : 0;
	unsigned int daifmt;

	daifmt = snd_soc_of_parse_daifmt(node, prefix,
					 &bitclkmaster, &framemaster);
	daifmt &= ~SND_SOC_DAIFMT_MASTER_MASK;

	if (prefix_len && !bitclkmaster && !framemaster) {
		/*
		 * No dai-link level and master setting was not found from
		 * sound node level, revert back to legacy DT parsing and
		 * take the settings from codec node.
		 */
		dev_dbg(dev, "Revert to legacy daifmt parsing\n");

		daifmt = snd_soc_of_parse_daifmt(codec, NULL, NULL, NULL) |
			(daifmt & ~SND_SOC_DAIFMT_CLOCK_MASK);
	} else {
		if (codec == bitclkmaster)
			daifmt |= (codec == framemaster) ?
				SND_SOC_DAIFMT_CBM_CFM : SND_SOC_DAIFMT_CBM_CFS;
		else
			daifmt |= (codec == framemaster) ?
				SND_SOC_DAIFMT_CBS_CFM : SND_SOC_DAIFMT_CBS_CFS;
	}

	of_node_put(bitclkmaster);
	of_node_put(framemaster);

	*retfmt = daifmt;

	return 0;
}

int aml_card_set_dailink_name(struct device *dev,
				      struct snd_soc_dai_link *dai_link,
				      const char *fmt, ...)
{
	va_list ap;
	char *name = NULL;
	int ret = -ENOMEM;

	va_start(ap, fmt);
	name = devm_kvasprintf(dev, GFP_KERNEL, fmt, ap);
	va_end(ap);

	if (name) {
		ret = 0;

		dai_link->name		= name;
		dai_link->stream_name	= name;
	}

	return ret;
}

int aml_card_parse_card_name(struct snd_soc_card *card,
				     char *prefix)
{
	char prop[128];
	int ret;

	snprintf(prop, sizeof(prop), "%sname", prefix);

	/* Parse the card name from DT */
	ret = snd_soc_of_parse_card_name(card, prop);
	if (ret < 0)
		return ret;

	if (!card->name && card->dai_link)
		card->name = card->dai_link->name;

	return 0;
}

int aml_card_parse_clk(struct device_node *node,
			       struct device_node *dai_of_node,
			       struct aml_dai *aml_dai)
{
	struct clk *clk;
	u32 val;

	/*
	 * Parse dai->sysclk come from "clocks = <&xxx>"
	 * (if system has common clock)
	 *  or "system-clock-frequency = <xxx>"
	 *  or device's module clock.
	 */
	clk = of_clk_get(node, 0);
	if (!IS_ERR(clk)) {
		aml_dai->sysclk = clk_get_rate(clk);
		aml_dai->clk = clk;
	} else if (!of_property_read_u32(node,
			"system-clock-frequency", &val)) {
		aml_dai->sysclk = val;
	} else {
		clk = of_clk_get(dai_of_node, 0);
		if (!IS_ERR(clk))
			aml_dai->sysclk = clk_get_rate(clk);
	}

	return 0;
}

int aml_card_parse_dai(struct device_node *node,
				    struct device_node **dai_of_node,
				    const char **dai_name,
				    const char *list_name,
				    const char *cells_name,
				    int *is_single_link)
{
	struct of_phandle_args args;
	int ret;

	if (!node)
		return 0;

	/*
	 * Get node via "sound-dai = <&phandle port>"
	 * it will be used as xxx_of_node on soc_bind_dai_link()
	 */
	ret = of_parse_phandle_with_args(node, list_name, cells_name, 0, &args);
	if (ret)
		return ret;

	/* Get dai->name */
	if (dai_name) {
		ret = snd_soc_of_get_dai_name(node, dai_name);
		if (ret < 0)
			return ret;
	}

	*dai_of_node = args.np;

	if (is_single_link)
		*is_single_link = !args.args_count;

	return 0;
}

int aml_card_init_dai(struct snd_soc_dai *dai,
			      struct aml_dai *aml_dai)
{
	int ret;

	if (aml_dai->sysclk) {
		ret = snd_soc_dai_set_sysclk(dai, 0, aml_dai->sysclk, 0);
		if (ret && ret != -ENOTSUPP) {
			dev_err(dai->dev, "aml-card: set_sysclk error\n");
			return ret;
		}
	}

	if (aml_dai->slots) {
		ret = snd_soc_dai_set_tdm_slot(dai,
					       aml_dai->tx_slot_mask,
					       aml_dai->rx_slot_mask,
					       aml_dai->slots,
					       aml_dai->slot_width);
		if (ret && ret != -ENOTSUPP) {
			dev_err(dai->dev, "aml-card: set_tdm_slot error\n");
			return ret;
		}
	}

	return 0;
}

int aml_card_canonicalize_dailink(struct snd_soc_dai_link *dai_link)
{
	if (!dai_link->cpu_dai_name ||
			(!dai_link->codec_dai_name && !dai_link->codecs))
		return -EINVAL;

	/* Assumes platform == cpu */
	if (!dai_link->platform_of_node)
		dai_link->platform_of_node = dai_link->cpu_of_node;

	return 0;
}

void aml_card_canonicalize_cpu(struct snd_soc_dai_link *dai_link,
				       int is_single_links)
{
	/*
	 * In soc_bind_dai_link() will check cpu name after
	 * of_node matching if dai_link has cpu_dai_name.
	 * but, it will never match if name was created by
	 * fmt_single_name() remove cpu_dai_name if cpu_args
	 * was 0. See:
	 *	fmt_single_name()
	 *	fmt_multiple_name()
	 */
	if (is_single_links)
		dai_link->cpu_dai_name = NULL;
}

int aml_card_clean_reference(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *dai_link;
	int num_links;

	for (num_links = 0, dai_link = card->dai_link;
	     num_links < card->num_links;
	     num_links++, dai_link++) {
		of_node_put(dai_link->cpu_of_node);
		of_node_put(dai_link->codec_of_node);
	}
	return 0;
}
