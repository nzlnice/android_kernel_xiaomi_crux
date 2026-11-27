/*
 * earlycon_ramlog.c - early console writer for a fixed reserved RAM buffer.
 *
 * The buffer is deliberately written through an early ioremap (device mapping)
 * so the data reaches DDR immediately and survives a warm reset.
 *
 * This is a debug aid for boot loops that happen before ramoops/pstore is
 * initialized.  It is not a replacement for pstore.
 */

#include <linux/console.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/serial_core.h>
#include <linux/sizes.h>
#include <linux/string.h>
#include <asm/early_ioremap.h>

#define RAMLOG_PHYS		0x9fe00000UL
#define RAMLOG_SIZE		SZ_256K
#define RAMLOG_MAGIC		0x474c5245	/* "ERLG" */
#define RAMLOG_VERSION		1

struct ramlog_hdr {
	u32 magic;
	u32 version;
	u32 size;
	u32 head;
	u32 tail;
	u32 boots;
	u32 reserved[2];
};

static void __iomem *ramlog_base;
static struct ramlog_hdr __iomem *ramlog_hdr;
static u8 __iomem *ramlog_buf;
static u32 ramlog_ring_size;

static void ramlog_put(struct console *con, const char *s, unsigned int count)
{
	u32 head, tail, i;

	(void)con;

	if (!ramlog_base || !ramlog_ring_size)
		return;

	head = readl_relaxed(&ramlog_hdr->head);
	tail = readl_relaxed(&ramlog_hdr->tail);
	if (head >= ramlog_ring_size || tail >= ramlog_ring_size) {
		head = 0;
		tail = 0;
	}

	for (i = 0; i < count; i++) {
		writeb(s[i], ramlog_buf + head);
		head++;
		if (head >= ramlog_ring_size)
			head = 0;
		if (head == tail) {
			tail++;
			if (tail >= ramlog_ring_size)
				tail = 0;
		}
	}

	writel_relaxed(head, &ramlog_hdr->head);
	writel_relaxed(tail, &ramlog_hdr->tail);
}

static int __init ramlog_setup(struct earlycon_device *dev, const char *options)
{
	u32 boots;
	char marker[64];
	int len;

	(void)options;

	ramlog_base = early_ioremap(RAMLOG_PHYS, RAMLOG_SIZE);
	if (!ramlog_base)
		return -ENOMEM;

	ramlog_hdr = (struct ramlog_hdr __iomem *)ramlog_base;
	ramlog_buf = (u8 __iomem *)ramlog_base + sizeof(*ramlog_hdr);
	ramlog_ring_size = RAMLOG_SIZE - sizeof(*ramlog_hdr);

	if (readl_relaxed(&ramlog_hdr->magic) != RAMLOG_MAGIC ||
	    readl_relaxed(&ramlog_hdr->version) != RAMLOG_VERSION ||
	    readl_relaxed(&ramlog_hdr->size) != ramlog_ring_size) {
		memset_io(ramlog_base, 0, RAMLOG_SIZE);
		writel_relaxed(RAMLOG_MAGIC, &ramlog_hdr->magic);
		writel_relaxed(RAMLOG_VERSION, &ramlog_hdr->version);
		writel_relaxed(ramlog_ring_size, &ramlog_hdr->size);
		writel_relaxed(0, &ramlog_hdr->head);
		writel_relaxed(0, &ramlog_hdr->tail);
		boots = 1;
	} else {
		boots = readl_relaxed(&ramlog_hdr->boots) + 1;
	}
	writel_relaxed(boots, &ramlog_hdr->boots);

	dev->con->write = ramlog_put;
	dev->con->data = dev;

	len = scnprintf(marker, sizeof(marker),
			"\n\n===== early_ramlog boot %u =====\n", boots);
	ramlog_put(dev->con, marker, len);

	return 0;
}
EARLYCON_DECLARE(ramlog, ramlog_setup);
