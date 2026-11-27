/*
 * early_ramlog.c - read the buffer written by earlycon_ramlog.c.
 *
 * The recovery/TWRP kernel boots with a different cmdline (no
 * earlycon=ramlog), so it does not overwrite the buffer.  Userspace reads
 * /proc/early_ramlog after boot.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sizes.h>

#define RAMLOG_PHYS		0x9fe00000UL
#define RAMLOG_SIZE		SZ_256K
#define RAMLOG_MAGIC		0x474c5245	/* "ERLG" */

struct ramlog_hdr {
	u32 magic;
	u32 version;
	u32 size;
	u32 head;
	u32 tail;
	u32 boots;
	u32 reserved[2];
};

static int early_ramlog_show(struct seq_file *m, void *v)
{
	void __iomem *base;
	struct ramlog_hdr __iomem *hdr;
	u8 __iomem *buf;
	u32 head, tail, size, boots, i;
	u8 c;

	base = ioremap(RAMLOG_PHYS, RAMLOG_SIZE);
	if (!base) {
		seq_puts(m, "early_ramlog: ioremap failed\n");
		return 0;
	}

	hdr = (struct ramlog_hdr __iomem *)base;
	buf = (u8 __iomem *)base + sizeof(*hdr);

	if (readl_relaxed(&hdr->magic) != RAMLOG_MAGIC) {
		seq_puts(m, "early_ramlog: no valid buffer (magic mismatch)\n");
		goto out;
	}

	size = readl_relaxed(&hdr->size);
	head = readl_relaxed(&hdr->head);
	tail = readl_relaxed(&hdr->tail);
	boots = readl_relaxed(&hdr->boots);

	seq_printf(m, "early_ramlog: boots=%u size=%u head=%u tail=%u\n",
		   boots, size, head, tail);

	if (!size || size > RAMLOG_SIZE - sizeof(*hdr) ||
	    head >= size || tail >= size) {
		seq_puts(m, "early_ramlog: invalid ring state\n");
		goto out;
	}

	i = tail;
	while (i != head) {
		c = readb(buf + i);
		seq_write(m, &c, 1);
		i++;
		if (i >= size)
			i = 0;
	}

out:
	iounmap(base);
	return 0;
}

static int early_ramlog_open(struct inode *inode, struct file *file)
{
	return single_open(file, early_ramlog_show, NULL);
}

static const struct file_operations early_ramlog_fops = {
	.owner = THIS_MODULE,
	.open = early_ramlog_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init early_ramlog_init(void)
{
	proc_create("early_ramlog", 0444, NULL, &early_ramlog_fops);
	return 0;
}
late_initcall(early_ramlog_init);
