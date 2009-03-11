/* 
 * Driver for AIS/AIVDM messages.
 *
 * The relevant standard is "ITU Recommendation M.1371":
 * <http://www.itu.int/rec/R-REC-M.1371-2-200603-I/en.
 * Alas, the distribution terms are evil.
 *
 * Also see "IALA Technical Clarifications on Recommendation ITU-R
 * M.1371-1", at 
 * <http://www.navcen.uscg.gov/enav/ais/ITU-R_M1371-1_IALA_Tech_Note1.3.pdf>
 *
 * The page http://www.bosunsmate.org/ais/ reveals part of what's going on.
 *
 * There's a sentence decoder we can test with at http://rl.se/aivdm
 *
 * Open-source code at http://sourceforge.net/projects/gnuais
 */
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>

#include "gpsd_config.h"
#include "gpsd.h"
#if defined(AIVDM_ENABLE)

#include "bits.h"

struct aivdm_t
{
    int	id;
    union {
	struct {
	    unsigned int mmsi;
	    double latitude, longitude, course;
	    unsigned int heading;
	    double sog;
	    unsigned int turn;
	} ais01;
    };
};


/**
 * Parse the data from the device
 */
/*@ +charint @*/
gps_mask_t aivdm_parse(struct gps_device_t *session)
{
    char *sixbits[64] = {
	"000000", "000001", "000010", "000011", "000100",
	"000101", "000110", "000111", "001000", "001001",
	"001010", "001011", "001100", "001101",	"001110",
	"001111", "010000", "010001", "010010", "010011",
	"010100", "010101", "010110", "010111",	"011000",
	"011001", "011010", "011011", "011100",	"011101",
	"011110", "011111", "100000", "100001",	"100010",
	"100011", "100100", "100101", "100110",	"100111",
	"101000", "101001", "101010", "101011",	"101100",
	"101101", "101110", "101111", "110000",	"110001",
	"110010", "110011", "110100", "110101",	"110110",
	"110111", "111000", "111001", "111010",	"111011",
	"111100", "111101", "111110", "111111",    
    };

    gps_mask_t mask = ONLINE_SET;    
    int nfields = 0;    
    unsigned char *data, *cp = session->driver.aivdm.fieldcopy;    
    unsigned char ch;
    int i;
    struct aivdm_t ais; 

    if (session->packet.outbuflen == 0)
	return 0;

    /* we may need to dump the raw packet */
    gpsd_report(LOG_RAW, "raw AIVDM packet length %ld: %s", 
		session->packet.outbuflen, session->packet.outbuffer);

    /* extract packet fields */
    (void)strlcpy((char *)session->driver.aivdm.fieldcopy, 
		  (char*)session->packet.outbuffer,
		  session->packet.outbuflen);
    session->driver.aivdm.field[nfields++] = session->packet.outbuffer;
    for (cp = session->driver.aivdm.fieldcopy; 
	 cp < session->driver.aivdm.fieldcopy + session->packet.outbuflen;
	 cp++)
	if (*cp == ',') {
	    *cp = '\0';
	    session->driver.aivdm.field[nfields++] = cp + 1;
	}
    session->driver.aivdm.part = atoi((char *)session->driver.aivdm.field[1]);
    session->driver.aivdm.await = atoi((char *)session->driver.aivdm.field[2]);
    data = session->driver.aivdm.field[5];
    gpsd_report(LOG_PROG, "part=%d, awaiting=%d, data=%s\n",
		session->driver.aivdm.part, session->driver.aivdm.await,
		data);

    /* assemble the binary data */
    if (session->driver.aivdm.part == 1) {
	(void)memset(session->driver.aivdm.bits, '\0', sizeof((char *)session->driver.aivdm.bits));
	session->driver.aivdm.bitlen = 0;
    }

    /* wacky 6-bit encoding, shades of FIELDATA */
    /*@ +charint @*/
    for (cp = data; cp < data + strlen((char *)data); cp++) {
	ch = *cp;
	if (ch < 87)
	    ch = ch - 48;
	else
	    ch = ch - 56;
	gpsd_report(LOG_RAW+1, "%c: %s\n", *cp, sixbits[ch]);
	for (i = 5; i >= 0; i--) {
	    if ((ch >> i) & 0x01) {
		session->driver.aivdm.bits[session->driver.aivdm.bitlen / 8] |= (1 << (7 - session->driver.aivdm.bitlen % 8));
	    }
	    session->driver.aivdm.bitlen++;
	}
    }
    /*@ -charint @*/

    /* time to pass buffered-up data to where it's actually processed? */
    if (session->driver.aivdm.part == session->driver.aivdm.await) {
	gpsd_report(LOG_IO, "AIVDM payload %ld: %s\n",
		    session->driver.aivdm.bitlen,
		    gpsd_hexdump_wrapper(session->driver.aivdm.bits,
					 (session->driver.aivdm.bitlen + 7)/8, LOG_IO));

#define ugrab(start, width)	ubits((char *)session->driver.aivdm.bits, start, width)
#define sgrab(start, width)	sbits((char *)session->driver.aivdm.bits, start, width)
	ais.id = ugrab(0, 6);
	gpsd_report(LOG_ERROR, "AIVDM message type %d.\n", ais.id);
	switch (ais.id) {
	case 1:	/* Position Report with SOTDMA */ {
	    ais.ais01.mmsi = ugrab(8, 30);
	    ais.ais01.longitude = ugrab(61, 28) / 600000.0;
	    ais.ais01.latitude = sgrab(89, 27) / 600000.0;
	    ais.ais01.course = ugrab(116, 12) / 10.0;
	    ais.ais01.heading = ugrab(128, 9);
	    ais.ais01.sog = ugrab(50, 10) / 10.0;
	    ais.ais01.turn = ugrab(40, 8);
	    gpsd_report(LOG_INF,
			"MMSI: %09d  Latitude: %.7f  Longitude: %.7f  Speed: %f  Coarse:%.5f  Heading: %f  Turn: %d\n",
			ais.ais01.mmsi, 
			ais.ais01.latitude, ais.ais01.longitude, 
			ais.ais01.sog, ais.ais01.course, 
			ais.ais01.heading, ais.ais01.turn);
	    break;
	}
	default:
	    gpsd_report(LOG_ERROR, "Unknown AIVDM message type.\n");
	    break;
	} 
#undef sgrab
#undef ugrab
    }

   /*
    * XXX The tag field is only 8 bytes; be careful you do not overflow.
    * XXX Using an abbreviation (eg. "italk" -> "itk") may be useful.
    */
    (void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag),
	"AIVDM");

    /* FIXME: actual driver code goes here */

    /* not posting any data yet */
    return mask;
}
/*@ -charint @*/

/* This is everything we export */
const struct gps_type_t aivdm = {
    /* Full name of type */
    .type_name        = "AIVDM",
    /* associated lexer packet type */
    .packet_type    = AIVDM_PACKET,
    /* Response string that identifies device (not active) */
    .trigger          = NULL,
    /* Number of satellite channels supported by the device */
    .channels         = 0,
    /* Startup-time device detector */
    .probe_detect     = NULL,
    /* Wakeup to be done before each baud hunt */
    .probe_wakeup     = NULL,
    /* Initialize the device and get subtype */
    .probe_subtype    = NULL,
    /* Packet getter (using default routine) */
    .get_packet       = generic_get,
    /* Parse message packets */
    .parse_packet     = aivdm_parse,
    /* RTCM handler (using default routine) */
    .rtcm_writer      = NULL,
#ifdef ALLOW_CONTROLSEND
    /* Control string sender - should provide checksum and headers/trailer */
    .control_send   = NULL,
#endif /* ALLOW_CONTROLSEND */
#ifdef ALLOW_RECONFIGURE
    /* Enable what reports we need */
    .configurator     = NULL,
    /* Speed (baudrate) switch */
    .speed_switcher   = NULL,
    /* Switch to NMEA mode */
    .mode_switcher    = NULL,
    /* Message delivery rate switcher (not active) */
    .rate_switcher    = NULL,
    /* Minimum cycle time of the device */
    .min_cycle        = 1,
    /* Undo the actions of .configurator */
    .revert           = NULL,
#endif /* ALLOW_RECONFIGURE */
    /* Puts device back to original settings */
    .wrapup           = NULL,
};
#endif /* defined(AIVDM_ENABLE) */
