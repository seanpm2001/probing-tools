/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box Probing Tools distribution.
 *
 *		ISA Plug and Play probing tool.
 *
 *		Heavily based on iPXE drivers/bus/isapnp.c written by some
 *		of the authors identified below.
 *
 *
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *		Timothy Legge, <tlegge@rogers.com>
 *		P.J.H.Fox, <fox@roestock.demon.co.uk>
 *		Michael Brown, <mbrown@fensystems.co.uk>
 *
 *		Copyright 2022 RichardG.
 *		Copyright 2002-2003 Timothy Legge.
 *		Copyright 2001 P.J.H.Fox.
 *		Copyright Michael Brown.
 *
 * ┌──────────────────────────────────────────────────────────────┐
 * │ This file is UTF-8 encoded. If this text is surrounded by    │
 * │ garbage, please tell your editor to open this file as UTF-8. │
 * └──────────────────────────────────────────────────────────────┘
 */
#include <i86.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clib.h"


typedef struct _card_ {
    char id[8];
    struct _card_ *next;
} card_t;


uint8_t buf[256], buf_pos;
uint16_t rd_data;
FILE *f;
card_t *first_card = NULL;


static inline void
io_delay()
{
    delay(1);
}


static uint8_t
lfsr_advance(int lfsr, int bit) {
	register uint8_t next;
	next = lfsr >> 1;
	next |= (((lfsr ^ next) ^ bit)) << 7;
	return next;
}


static void
unlock()
{
    int i, lfsr;

    /* Trigger Wait for Key state. */
    outb(0x279, 0x02);
    outb(0xa79, 0x02);

    /* Delay, then clear address port? */
    io_delay();
    outb(0x279, 0x00);
    outb(0x279, 0x00);

    /* Send key. */
    lfsr = 0x6a;
    for (i = 0; i < 32; i++) {
	outb(0x279, lfsr);
	lfsr = lfsr_advance(lfsr, 0);
    }
}


static int
checksum(uint8_t *identifier)
{
    int i, j, lfsr, byte;

    lfsr = 0x6a;
    for (i = 0; i < 8; i++) {
	byte = identifier[i];
	for (j = 0; j < 8; j++) {
		lfsr = lfsr_advance(lfsr, byte);
		byte >>= 1;
	}
    }
    return lfsr;
}


static void
parse_id(uint8_t *identifier, char *id)
{
    uint16_t vendor;

    vendor = (identifier[0] << 8) | identifier[1];
    sprintf(id, "%c%c%c%02X%02X",
	'@' + ((vendor >> 10) & 0x1f),
	'@' + ((vendor >> 5) & 0x1f),
	'@' + (vendor & 0x1f),
	identifier[2], identifier[3]);
}


static int
read_resource_data(uint8_t *byte)
{
    uint8_t i;

    /* Read byte. */
    for (i = 0; i < 20; i++) {
	/* Read only if ready. */
	outb(0x279, 0x05);
	if (inb(rd_data) & 0x01) {
		outb(0x279, 0x04);
		*byte = inb(rd_data);
		break;
	}
	io_delay();
    }

    /* Return failure if the read timed out. */
    if (i == 20) {
	printf("\n> Read timed out at byte %d", ftell(f) + buf_pos + 1);
	*byte = 0x00;
	return 0;
    }

    /* Add byte to buffer. */
    buf[buf_pos++] = *byte;

    /* Flush buffer if full. */
    if (!buf_pos) {
	if (fwrite(buf, sizeof(buf), 1, f) < 1) {
		printf("\n> File write failed");
		return 0;
	}
    }

    /* Return success. */
    return 1;
}


static int
try_isolate()
{
    uint8_t identifier[9], csn, byte, i, j, seen_55aa, seen_life;
    uint16_t data;
    char id[13];
    card_t *card, *new_card;

    /* Put all cards to Sleep. */
    unlock();

    /* Reset all CSNs. */
    outb(0x279, 0x02);
    outb(0xa79, 0x04);
    io_delay();
    io_delay();

    /* Put all cards to Isolation. */
    unlock();
    outb(0x279, 0x03);
    outb(0xa79, 0x00);

    /* Set the read data port. */
    outb(0x279, 0x00);
    outb(0xa79, rd_data >> 2);
    io_delay();

    /* Isolate cards. */
    csn = 0;
    while (1) {
	/* Put sleeping cards to Isolation. */
	outb(0x279, 0x01);
	io_delay();

	/* Read serial identifier. */
	memset(identifier, 0, sizeof(identifier));
	seen_55aa = seen_life = 0;
	for (i = 0; i < sizeof(identifier); i++) {
		byte = 0;
		for (j = 0; j < 8; j++) {
			data = inb(rd_data);
			io_delay();
			data = (data << 8) | inb(rd_data);
			io_delay();
			byte >>= 1;
			if (data != 0xffff) {
				seen_life++;
				if (data == 0x55aa) {
					byte |= 0x80;
					seen_55aa++;
				}
			}
		}
		identifier[i] = byte;
	}

	/* Stop if we didn't see any 55AA patterns. */
	if (!seen_55aa) {
		if (!csn && seen_life) {
			printf("Read Data Port %04X appears to be busy\n", rd_data);
			csn = -1;
		}
		break;
	}

	/* Output ID and stop if the checksum is invalid. */
	parse_id(identifier, id);
	printf("%s (%02X%02X%02X%02X) on Read Data Port %04X", id, identifier[7], identifier[6], identifier[5], identifier[4], rd_data);
	if (identifier[8] != checksum(identifier)) {
		printf("\n> Bad checksum (expected %02X got %02X), trying another Read Data Port...\n", checksum(identifier), identifier[8]);
		csn = -1;
		break;
	}

	/* Assign a CSN. */
	csn++;
	outb(0x279, 0x06);
	outb(0xa79, csn);
	io_delay();

	/* Wake this card by its CSN to reset the resource data pointer. */
	outb(0x279, 0x03);
	outb(0xa79, csn);
	io_delay();

	/* Sanitize parsed PnP ID for filename purposes. */
	for (i = 0; i < 3; i++) {
		if (id[i] > 'Z')
			id[i] = '_';
	}

	/* Determine this card's unique identifier. */
	i = 0;
	card = first_card;
	while (card) {
		/* Increment unique identifier for each card
		   already found with this sanitized parsed ID. */
		if (!strcmp(card->id, id))
			i++;
		card = card->next;
	}
	new_card = malloc(sizeof(card_t));
	strcpy(new_card->id, id);
	new_card->next = NULL;
	if (!first_card) {
		first_card = new_card;
	} else {
		card = first_card;
		while (card->next)
			card = card->next;
		card->next = new_card;
	}

	/* Generate a dump file name. */
	sprintf(&id[7], "%c.BIN", 'A' + (i % 26));
	printf(" - dumping to %s", id);

	/* Open dump file. */
	f = fopen(id, "wb");
	if (f) {
		/* Dump resource data, starting with the header. */
		buf_pos = 0;
		for (i = 0; i < 9; i++)
			read_resource_data(&byte);

		/* Now dump the resources. */
		j = 0;
		while (read_resource_data(&byte)) {
			/* Determine the amount of bytes to skip depending on resource type. */
			if (byte & 0x80) { /* large resource */
				read_resource_data((uint8_t *) &data);
				read_resource_data(((uint8_t *) &data) + 1);

				/* Handle ANSI strings. */
				byte &= 0x7f;
				if (byte == 0x02) {
					/* Output string. */
					if (!j)
						printf("\n>");
					printf(" \"");
					while (data--) {
						read_resource_data(&byte);
						if ((byte != 0x00) && (byte != '\r') && (byte != '\n'))
							putchar(byte);
					}
					printf("\"");
					data = 0;
				}
			} else { /* small resource */
				data = byte & 0x07;

				/* Handle some resource types. */
				byte = (byte >> 3) & 0x0f;
				if ((byte == 0x02) && (data >= 4)) { /* logical device */
					/* Flag that we're in a logical device for string output purposes. */
					j = 1;

					/* Read logical device ID. */
					read_resource_data(&identifier[0]);
					read_resource_data(&identifier[1]);
					read_resource_data(&identifier[2]);
					read_resource_data(&identifier[3]);
					data -= 4;

					/* Output logical device ID. */
					parse_id(identifier, id);
					printf("\n> %s", id);
				} else if (byte == 0x0f) { /* end tag */
					/* Read the rest of this resource (including the checksum), then stop. */
					if (data == 0) /* just in case the checksum isn't covered */
						data++;
					while (data--)
						read_resource_data(&byte);
					break;
				}
			}

			/* Skip bytes. */
			while (data--)
				read_resource_data(&byte);
		}

		/* Flush buffer if not empty. */
		if (buf_pos) {
			if (fwrite(buf, buf_pos, 1, f) < 1)
				printf("\n> File write failed");
		}

		/* Finish the dump. */
		fclose(f);
		printf("\n");
	} else {
		printf("\n> File creation failed\n");
	}

	/* Put this card to Sleep and other cards to Isolation. */
	outb(0x279, 0x03);
	outb(0xa79, 0x00);
	io_delay();
    }

    /* Put all cards to Wait for Key. */
    outb(0x279, 0x02);
    outb(0xa79, 0x02);

    /* Return card count / maximum CSN. */
    return csn;
}


int
main(int argc, char **argv)
{
    int max_csn;
    card_t *card;

    /* Disable stdout buffering. */
    term_unbuffer_stdout();

    /* Try read data ports until a good one is found. iPXE tries
       [213:3FF] with a hole at [280:380] for whatever safety reason. */
    max_csn = -1;
    for (rd_data = 0x213; rd_data <= 0x3ff; rd_data += 16) {
	if ((rd_data >= 0x280) && (rd_data <= 0x380))
		continue;

	max_csn = try_isolate();
	if (max_csn >= 0)
		break;
    }

    /* Free the card list. */
    while (first_card) {
	card = first_card->next;
	free(first_card);
	first_card = card;
    }

    /* Nothing returned. */
    if (max_csn < 0) {
	printf("Found no good Read Data Ports!\n");
	return 1;
    }

    return 0;
}
