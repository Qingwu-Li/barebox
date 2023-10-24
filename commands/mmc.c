// SPDX-License-Identifier: GPL-2.0-only

#include <command.h>
#include <mci.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>

static int mmc_enh_area_setmax(struct mci *mci, u8 *ext_csd)
{
	unsigned i;
	struct {
		unsigned index;
		unsigned value;
	} regval[] = {
		{
			.index = EXT_CSD_ERASE_GROUP_DEF,
			.value = 1,
		}, {
			.index = EXT_CSD_ENH_START_ADDR,
			.value = 0,
		}, {
			.index = EXT_CSD_ENH_START_ADDR + 1,
			.value = 0,
		}, {
			.index = EXT_CSD_ENH_START_ADDR + 2,
			.value = 0,
		}, {
			.index = EXT_CSD_ENH_START_ADDR + 3,
			.value = 0,
		}, {
			.index = EXT_CSD_ENH_SIZE_MULT,
			.value = ext_csd[EXT_CSD_MAX_ENH_SIZE_MULT],
		}, {
			.index = EXT_CSD_ENH_SIZE_MULT + 1,
			.value = ext_csd[EXT_CSD_MAX_ENH_SIZE_MULT + 1],
		}, {
			.index = EXT_CSD_ENH_SIZE_MULT + 2,
			.value = ext_csd[EXT_CSD_MAX_ENH_SIZE_MULT + 2],
		}, {
			.index = EXT_CSD_PARTITIONS_ATTRIBUTE,
			.value = ext_csd[EXT_CSD_PARTITIONS_ATTRIBUTE] | EXT_CSD_ENH_USR_MASK,
		}
	};

	for (i = 0; i < ARRAY_SIZE(regval); ++i) {
		int ret = mci_switch(mci, regval[i].index, regval[i].value);
		if (ret) {
			printf("Failure to write to register %u", regval[i].index);
			return ret;
		}
	}

	return 0;
}

static int mmc_partitioning_complete(struct mci *mci)
{
	int ret;

	ret = mci_switch(mci, EXT_CSD_PARTITION_SETTING_COMPLETED, 1);
	if (ret)
		printf("Failure to write to EXT_CSD_PARTITION_SETTING_COMPLETED\n");

	return ret;
}

static u8 *mci_get_ext_csd(struct mci *mci)
{
	u8 *ext_csd;
	int ret;

	ext_csd = xmalloc(512);

	ret = mci_send_ext_csd(mci, ext_csd);
	if (ret) {
		printf("Failure to read EXT_CSD register\n");
		free(ext_csd);
		return ERR_PTR(-EIO);
	}

	return ext_csd;
}

/* enh_area [-c] /dev/mmcX */
static int do_mmc_enh_area(int argc, char *argv[])
{
	const char *devpath;
	struct mci *mci;
	u8 *ext_csd;
	int set_completed = 0;
	int opt;
	int ret;

	while ((opt = getopt(argc, argv, "c")) > 0) {
		switch (opt) {
		case 'c':
			set_completed = 1;
			break;
		}
	}

	if (argc - optind != 1) {
		printf("Usage: mmc enh_area [-c] /dev/mmcX\n");
		return COMMAND_ERROR_USAGE;
	}

	devpath = argv[optind];

	mci = mci_get_device_by_devpath(devpath);
	if (!mci) {
		printf("Failure to open %s as mci device\n", devpath);
		return COMMAND_ERROR;
	}

	ext_csd = mci_get_ext_csd(mci);
	if (IS_ERR(ext_csd))
		return COMMAND_ERROR;

	if (!(ext_csd[EXT_CSD_PARTITIONING_SUPPORT] & EXT_CSD_ENH_ATTRIBUTE_EN_MASK)) {
		printf("Device doesn't support enhanced area\n");
		goto error;
	}

	if (ext_csd[EXT_CSD_PARTITION_SETTING_COMPLETED]) {
		printf("Partitioning already finalized\n");
		goto error;
	}

	ret = mmc_enh_area_setmax(mci, ext_csd);
	if (ret)
		goto error;

	free(ext_csd);

	if (set_completed) {
		ret = mmc_partitioning_complete(mci);
		if (ret)
			return COMMAND_ERROR;
		printf("Now power cycle the device to let it reconfigure itself.\n");
	}

	return COMMAND_SUCCESS;

error:
	free(ext_csd);
	return COMMAND_ERROR;
}

/* write_reliability /dev/mmcX */
static int do_mmc_write_reliability_set(int argc, char *argv[])
{
	const char *devpath;
	struct mci *mci;
	u8 *ext_csd;

	if (argc - optind != 1) {
		printf("Usage: mmc write_reliability /dev/mmcX\n");
		return COMMAND_ERROR_USAGE;
	}

	devpath = argv[optind];

	mci = mci_get_device_by_devpath(devpath);
	if (!mci) {
		printf("Failure to open %s as mci device\n", devpath);
		return COMMAND_ERROR;
	}

	ext_csd = mci_get_ext_csd(mci);
	if (IS_ERR(ext_csd))
		return COMMAND_ERROR;

	if (ext_csd[EXT_CSD_PARTITION_SETTING_COMPLETED]) {
		printf("Partitioning already finalized\n");
		goto error;
	}

	if (!(ext_csd[EXT_CSD_WR_REL_PARAM] & EXT_CSD_HS_CTRL_REL)) {
		printf("parameters in the WR_REL_SET registers are not R/W\n");
		goto error;
	}

	if (!(ext_csd[EXT_CSD_WR_REL_PARAM] & EXT_CSD_EN_REL_WR)) {
		printf("device does not support the enhanced definition of reliable write\n");
		goto error;
	}

	/*
	 * host has one opportunity to write all of the bits
	 * separate writes to individual bits are not permitted
	 * so set all bits here
	 */
	if (ext_csd[EXT_CSD_WR_REL_SET] & 0x1f != 0x1f) {
		if (mci_switch(mci, EXT_CSD_WR_REL_SET, 0x1f)) {
			printf("Failure to write to EXT_CSD_WR_REL_SET\n");
			goto error;
		}
		printf("Done setting EXT_CSD_WR_REL_SET to 0x1F\n");
	}

	free(ext_csd);

	return COMMAND_SUCCESS;

error:
	free(ext_csd);
	return COMMAND_ERROR;
}

/* set_partition_complete /dev/mmcX */
static int do_mmc_set_partition_complete(int argc, char *argv[])
{
	const char *devpath;
	struct mci *mci;
	int ret;

	if (argc - optind != 1) {
		printf("Usage: mmc set_partition_complete /dev/mmcX\n");
		return COMMAND_ERROR_USAGE;
	}

	devpath = argv[optind];

	mci = mci_get_device_by_devpath(devpath);
	if (!mci) {
		printf("Failure to open %s as mci device\n", devpath);
		return COMMAND_ERROR_USAGE;
	}

	return mmc_partitioning_complete(mci);
}

/* get_partition_complete /dev/mmcX */
static int do_mmc_get_partition_complete(int argc, char *argv[])
{
	const char *devpath;
	struct mci *mci;
	u8 *ext_csd;
	int ret;

	if (argc - optind != 1) {
		printf("Usage: mmc set_partition_complete /dev/mmcX\n");
		return COMMAND_ERROR_USAGE;
	}

	devpath = argv[optind];

	mci = mci_get_device_by_devpath(devpath);
	if (!mci) {
		printf("Failure to open %s as mci device\n", devpath);
		return COMMAND_ERROR_USAGE;
	}

	ext_csd = mci_get_ext_csd(mci);
	if (IS_ERR(ext_csd))
		return -1;

	ret = ext_csd[EXT_CSD_PARTITION_SETTING_COMPLETED];

	free(ext_csd);

	return ret;
}

static struct {
	const char *cmd;
	int (*func)(int argc, char *argv[]);
} mmc_subcmds[] = {
	{
		.cmd = "enh_area",
		.func = do_mmc_enh_area,
	},
	{
		.cmd = "write_reliability",
		.func = do_mmc_write_reliability_set,
	},
	{
		.cmd = "set_partition_complete",
		.func = do_mmc_set_partition_complete,
	},
	{
		.cmd = "get_partition_complete",
		.func = do_mmc_get_partition_complete,
	}
};

static int do_mmc(int argc, char *argv[])
{
	size_t i;
	int (*func)(int argc, char *argv[]) = NULL;

	if (argc < 2) {
		printf("mmc: required subcommand missing\n");
		return 1;
	}

	for (i = 0; i < ARRAY_SIZE(mmc_subcmds); ++i) {
		if (strcmp(mmc_subcmds[i].cmd, argv[1]) == 0) {
			func = mmc_subcmds[i].func;
			break;
		}
	}

	if (func) {
		return func(argc - 1, argv + 1);
	} else {
		printf("mmc: subcommand \"%s\" not found\n", argv[1]);
		return COMMAND_ERROR_USAGE;
	}
}

BAREBOX_CMD_HELP_START(mmc)
BAREBOX_CMD_HELP_TEXT("Modifies mmc properties.")
BAREBOX_CMD_HELP_TEXT("")
BAREBOX_CMD_HELP_TEXT("The subcommand enh_area creates an enhanced area of")
BAREBOX_CMD_HELP_TEXT("maximal size.")
BAREBOX_CMD_HELP_TEXT("Note, with -c this is an irreversible action.")
BAREBOX_CMD_HELP_OPT("-c", "complete partitioning")
BAREBOX_CMD_HELP_TEXT("The subcommand write_reliability enable write reliability")
BAREBOX_CMD_HELP_TEXT("The subcommand set_partition_complete set PARTITION_SETTING_COMPLETED")
BAREBOX_CMD_HELP_TEXT("The subcommand get_partition_complete get PARTITION_SETTING_COMPLETED")
BAREBOX_CMD_HELP_END

BAREBOX_CMD_START(mmc)
	.cmd = do_mmc,
	BAREBOX_CMD_OPTS("enh_area [-c] /dev/mmcX")
	BAREBOX_CMD_OPTS("write_reliability /dev/mmcX")
	BAREBOX_CMD_OPTS("set_partition_complete /dev/mmcX")
	BAREBOX_CMD_OPTS("get_partition_complete /dev/mmcX")
	BAREBOX_CMD_GROUP(CMD_GRP_HWMANIP)
	BAREBOX_CMD_HELP(cmd_mmc_help)
BAREBOX_CMD_END
