/*
 * Simple MPEG/DVB parser to achieve network/service information without initial tuning data
 *
 * Copyright (C) 2006, 2007, 2008, 2009 Winfried Koehler 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 * Or, point your browser to http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

/******************************************************************************
 * reading dvbscan initial_tuning_data.
 * 
 * NOTE: READBACK IS *ONLY* GUARANTEED IF FILES ARE GENERATED BY w_scan2 AND
 * NOT MODIFIED. WORKING WITHOUT INIT TRANSPONDER FILES IS MAIN PHILOSOPHY.
 *
 * readback not tested yet. - added wk 20090303 -
 *****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "extended_frontend.h"
#include "scan.h"
#include "parse-dvbscan.h"
#include "dvbscan.h"
#include "satellites.h"
#include "dump-vdr.h"

#define MAX_LINE_LENGTH 1024	// paranoia, but still possible
#define DELIMITERS    " \r\n\t"

enum __dvbscan_args {
	sat_frequency,
	sat_polarization,
	sat_symbol_rate,
	sat_fec_inner,
	sat_rolloff,		// optional
	sat_modulation,		// optional
	sat_END_READING,
	cable_frequency,
	cable_symbol_rate,
	cable_fec_inner,
	cable_modulation,
	cable_END_READING,
	terr_plp_id,
	terr_system_id,
	terr_frequency,
	terr_bandwidth,
	terr_fec_high_priority,
	terr_fec_low_priority,
	terr_modulation,
	terr_transmission_mode,
	terr_guard_interval,
	terr_hierarchy,
	terr_END_READING,
	atsc_frequency,
	atsc_modulation,
	atsc_END_READING,
	end_of_line,
	STOP,
};

enum __extflags {
	ignore,
	wscan_version,
	tuning_timeout,
	filter_timeout,
	fe_type,
	list_idx
};

void parse_w_scan_flags(const char *input_buffer, struct w_scan_flags *flags)
{
	char *copy = (char *)malloc(strlen(input_buffer) + 1);
	char *token = strtok(copy, DELIMITERS);
	enum __extflags arg = ignore;

	strcpy(copy, input_buffer);
	if (NULL == token) {
		free(copy);
		return;
	}
	while (NULL != (token = strtok(0, DELIMITERS))) {
		if (0 == strcasecmp(token, "<w_scan>")) {
			arg = wscan_version;
			continue;
		}
		if (0 == strcasecmp(token, "</w_scan>")) {
			arg = ignore;
			continue;
		}
		switch (arg++) {
		case wscan_version:
			flags->version = (char *)malloc(strlen(token) + 1);
			if (flags->version == NULL) {
				error("malloc() returned NULL\n");
				exit(1);
			}
			strncpy(flags->version, token, strlen(token));
			break;
		case tuning_timeout:
			flags->tuning_timeout = strtoul(token, NULL, 10);
			break;
		case filter_timeout:
			flags->filter_timeout = strtoul(token, NULL, 10);
			break;
		case fe_type:
			flags->scantype = txt_to_scantype(token);
			break;
		case list_idx:
			flags->list_id = strtoul(token, NULL, 10);
			break;
		case ignore:
		default:
			continue;
		}
	}
	free(copy);
}

int dvbscan_parse_tuningdata(const char *tuningdata, struct w_scan_flags *flags)
{
	FILE *initdata = NULL;
	char *buf = (char *)calloc(sizeof(char), MAX_LINE_LENGTH);
	enum __dvbscan_args arg;
	struct transponder *tn;
	int count = 0;

	if (tuningdata == NULL) {
		free(buf);
		error
		    ("could not open initial tuning data: file name is NULL\n");
		return 0;	// err
	}
	info("parsing initial tuning data \"%s\"..\n", tuningdata);
	initdata = fopen(tuningdata, "r");
	if (initdata == NULL) {
		free(buf);
		error("cannot open '%s': error %d %s\n", tuningdata, errno,
		      strerror(errno));
		return 0;	// err
	}

	while (fgets(buf, MAX_LINE_LENGTH, initdata) != NULL) {
		char *copy = (char *)calloc(sizeof(char), strlen(buf) + 1);
		char *token;

		if (copy == NULL) {
			fatal("Could not allocate memory.\n");
		}
		memset(&tn, 0, sizeof(tn));
		/* strtok will modify it's first argument, but working 
		 * on a copy is safe. Be really careful here -
		 * 'copy' should NOT be referred to after usage of strtok()
		 * -wk-
		 */
		strcpy(copy, buf);
		token = strtok(copy, DELIMITERS);
		if (NULL == token)
			continue;
		switch (toupper(token[0])) {
		case 'A':
			tn = alloc_transponder(0, SCAN_TERRCABLE_ATSC, 0);
			tn->type = SCAN_TERRCABLE_ATSC;
			break;
		case 'C':
			tn = alloc_transponder(0, SCAN_CABLE, 0);
			tn->type = SCAN_CABLE;
			break;
		case 'S':
			tn = alloc_transponder(0, SCAN_SATELLITE, 0);
			tn->type = SCAN_SATELLITE;
			break;
		case 'T':
			tn = alloc_transponder(0, SCAN_TERRESTRIAL, 0);
			tn->type = SCAN_TERRESTRIAL;
			break;
		case '#':
			if (strlen(token) > 2)
				switch (token[1]) {
				case '!':
					parse_w_scan_flags(buf, flags);
					continue;
				default:;
				}
			continue;
		default:
			free(buf);
			error("could not parse %s\n", tuningdata);
			return 0;	// err
		}
		flags->scantype = tn->type;
		switch (tn->type) {
		case SCAN_SATELLITE:
			tn->delsys = SYS_DVBS;
			if (strlen(token) >= 2)
				if (token[1] == '2') {
					flags->need_2g_fe = 1;
					tn->delsys = SYS_DVBS2;
				}
			arg = sat_frequency;
			tn->inversion = INVERSION_AUTO;
			tn->coderate = FEC_AUTO;
			tn->pilot = PILOT_AUTO;
			tn->modulation = QPSK;
			tn->rolloff = ROLLOFF_35;
			break;
		case SCAN_CABLE:
			tn->delsys = SYS_DVBC_ANNEX_AC;
			if (strlen(token) >= 2)
				if (token[1] == '2') {
					flags->need_2g_fe = 1;
				}
			arg = cable_frequency;
			tn->inversion = INVERSION_AUTO;
			tn->modulation = QAM_AUTO;
			tn->symbolrate = 6900000;
			tn->coderate = FEC_NONE;
			break;
		case SCAN_TERRESTRIAL:
			tn->delsys = SYS_DVBT;
			arg = terr_frequency;
			if (strlen(token) >= 2)
				if (token[1] == '2') {
					flags->need_2g_fe = 1;
					tn->delsys = SYS_DVBT2;
					arg = terr_plp_id;
				}
			tn->inversion = INVERSION_AUTO;
			tn->bandwidth = 8000000;
			tn->coderate = FEC_AUTO;
			tn->coderate_LP = FEC_NONE;
			tn->modulation = QAM_AUTO;
			tn->transmission = TRANSMISSION_MODE_AUTO;
			tn->guard = GUARD_INTERVAL_AUTO;
			tn->hierarchy = HIERARCHY_AUTO;
			break;
		case SCAN_TERRCABLE_ATSC:
			tn->delsys = SYS_ATSC;
			if (strlen(token) >= 2)
				if (token[1] == '2') {
					flags->need_2g_fe = 1;
				}
			arg = atsc_frequency;
			tn->inversion = INVERSION_AUTO;
			tn->modulation = VSB_8;
			break;
		default:
			free(buf);
			error("could not parse '%s' - undefined fe_type\n",
			      buf);
			return 0;	// err
		}

		while (NULL != (token = strtok(0, DELIMITERS))) {
			switch (arg++) {
			case sat_frequency:
			case cable_frequency:
			case terr_frequency:
			case atsc_frequency:
				tn->frequency = strtoul(token, NULL, 10);
				break;
			case sat_polarization:
				tn->polarization = txt_to_sat_pol(token);
				break;
			case sat_symbol_rate:
				tn->symbolrate = strtoul(token, NULL, 10);
				break;
			case sat_fec_inner:
				tn->coderate = txt_to_sat_fec(token);
				count++;
				break;
			case sat_rolloff:
				tn->rolloff = txt_to_sat_rolloff(token);
				break;
			case sat_modulation:
				tn->modulation = txt_to_sat_mod(token);
				break;
			case cable_symbol_rate:
				tn->symbolrate = strtoul(token, NULL, 0);
				break;
			case cable_fec_inner:
				tn->coderate = txt_to_cable_fec(token);
				break;
			case cable_modulation:
				tn->modulation = txt_to_cable_mod(token);
				count++;
				break;
			case terr_plp_id:
				tn->plp_id = strtoul(token, NULL, 10);
				break;
			case terr_system_id:
				tn->system_id = strtoul(token, NULL, 10);
				break;
			case terr_bandwidth:
				tn->bandwidth = txt_to_terr_bw(token);
				break;
			case terr_fec_high_priority:
				tn->coderate = txt_to_terr_fec(token);
				break;
			case terr_fec_low_priority:
				tn->coderate_LP = txt_to_terr_fec(token);
				break;
			case terr_modulation:
				tn->modulation = txt_to_terr_mod(token);
				break;
			case terr_transmission_mode:
				tn->transmission =
				    txt_to_terr_transmission(token);
				break;
			case terr_guard_interval:
				tn->guard = txt_to_terr_guard(token);
				break;
			case terr_hierarchy:
				tn->hierarchy = txt_to_terr_hierarchy(token);
				count++;
				break;
			case atsc_modulation:
				tn->modulation = txt_to_atsc_mod(token);
				count++;
				break;
			case cable_END_READING:
			case sat_END_READING:
			case terr_END_READING:
			case atsc_END_READING:
			case end_of_line:
			case STOP:
			default:
				arg = end_of_line;
				break;
			}
			if ((arg == STOP) || (arg == end_of_line))
				break;
		}
		free(copy);
		copy = NULL;
		memset(buf, 0, sizeof(char) * MAX_LINE_LENGTH);
		print_transponder(buf, tn);
		info("\ttransponder %s\n", buf);
		memset(buf, 0, sizeof(char) * MAX_LINE_LENGTH);
	}
	free(buf);
	fclose(initdata);
	if (count == 0) {
		info("Unexpected end of file..\n");
		return 0;
	}
	return 1;		// success
}

/*****************************************************************************/

/******************************************************************************
 * reading rotor configuration.
 * 
 * NOTE: READING IS *ONLY* GUARANTEED IF FILES ARE USING FILE FORMAT AS
 * DESCRIBED IN doc/README.file_formats
 *
 * NOTE: VDR plugin 'rotor' rotor.conf file format is inofficially supported
 * too, but reading is not guaranteed. So don't bother me is some files in this
 * file format are not accepted. wk 20090501 -
 *****************************************************************************/

enum __rotor_args {
	rotor_position,
	sat_id,
	end_of_line_rotor,
	READ_STOP,
	plug_rotor_sat_id,
	plug_rotor_rotor_position,
	plug_rotor_eol,
};

enum __rotor_file_formats {
	rotor_fileformat_undef,
	rotor_fileformat_wscan,
	rotor_fileformat_vdrpluginrotor
};

struct pos_item {
	int position;
	char *id;
};

#define ROTOR_DELIMITERS    " \t=\r\n"

/* 
 * needs to be kept in sync with vdr's source enum (sat only)
 */
enum vdr_source_types {
	stNone = 0x0000,
	stSat = 0x8000,
	st_Mask = 0xC000,
	st_Neg = 0x0800,
	st_Pos = 0x07FF,
};

/*
 * convert a string token to source-id used by vdr (sat only)
 */
int vdr_code_from_token(const char *token)
{
	int code = -1;
	int pos = 0;
	int dot = 0;
	int neg = 0;

	switch (toupper(*token)) {
	case 'S':
		code = stSat;
		break;
	default:
		fatal("%s: could not parse %s\n", __FUNCTION__, token);
	}

	while (*++token) {
		switch (toupper(*token)) {
		case '0' ... '9':
			pos *= 10;
			pos += *token - '0';
			break;
		case '.':
			dot++;
			break;
		case 'E':
			neg++;	// no break, fall through
		case 'W':
			if (!dot)
				pos *= 10;
			break;
		default:
			fatal("%s: unknown source character '%c'",
			      __FUNCTION__, *token);
		}
	}
	if (neg)
		pos |= st_Neg;
	code |= pos;
	return code;
}

/*
 * convert a source-id used by vdr to the matching string used in channels.conf
 * (sat only). returned value is copied to buffer.
 */
void vdr_source_to_str(int id, char *buffer, int bufsize)
{
	char *q = buffer;
	switch (id & st_Mask) {
	case stSat:
		*q++ = 'S';
		{
			int pos = id & ~st_Mask;
			if ((pos & ~st_Neg) % 10)
				q += snprintf(q, bufsize - 2, "%u.%u",
					      (pos & ~st_Neg) / 10,
					      (pos & ~st_Neg) % 10);
			else
				q += snprintf(q, bufsize - 2, "%u",
					      (pos & ~st_Neg) / 10);
			*q++ = (id & st_Neg) ? 'E' : 'W';
		}
		break;
	default:
		*q++ = id + '0';
	}
	*q = 0;
}

/*
 * parse a rotor.conf in w_scan2's file format.
 *
 * file format as described in doc/README.file_formats
 * # R <position>       <satellite id>  [# comment]
 * R    1               S4E8
 * R    7               S19E2
 *
 * - satellite_id has to match w_scan2's identifiers, see option -s
 * - rotor position 1..255 (positions < 1 are ignored)
 * -------------------------------------------------------------
 * NOTE: the rotor.conf from vdr 'rotor' plugin is also accepted,
 * but not officially supported:
 * S4.8E = 1
 * S19.2E = 7
 * -------------------------------------------------------------
 */
int dvbscan_parse_rotor_positions(const char *positiondata)
{
	FILE *data = NULL;
	char *buf = (char *)calloc(sizeof(char), MAX_LINE_LENGTH);
	enum __rotor_args arg = end_of_line_rotor;
	//enum __rotor_file_formats fformat = rotor_fileformat_undef;
	struct pos_item item = { 0, NULL };
	int count = 0;

	if (positiondata == NULL) {
		free(buf);
		error
		    ("could not open rotor position tuning data: file name is NULL\n");
		return 0;	// err
	}
	info("parsing rotor data \"%s\"..\n", positiondata);
	data = fopen(positiondata, "r");
	if (data == NULL) {
		free(buf);
		error("cannot open '%s': error %d %s\n", positiondata, errno,
		      strerror(errno));
		return 0;	// err
	}

	while (fgets(buf, MAX_LINE_LENGTH, data) != NULL) {
		char *copy = (char *)calloc(sizeof(char), strlen(buf) + 1);
		char *token;

		if (copy == NULL)
			fatal("Could not allocate memory.\n");

		strcpy(copy, buf);
		token = strtok(copy, ROTOR_DELIMITERS);
		if (NULL == token)
			continue;

		switch (toupper(token[0])) {
		case 'R':
			arg = rotor_position;
			//fformat = rotor_fileformat_wscan;
			break;
		case 'S':
			{
				char buffer[16];
				int code = vdr_code_from_token(token);
				vdr_source_to_str(code, buffer, sizeof(buffer));
				item.id = (char *)
				    malloc(strlen
					   (vdr_name_to_short_name(buffer)) +
					   1);
				strcpy(item.id, vdr_name_to_short_name(buffer));
				arg = plug_rotor_rotor_position;
				//fformat = rotor_fileformat_vdrpluginrotor;
				break;
			}
		case '#':
			continue;
		default:
			error("could not parse %s\n", positiondata);
			goto err;
		}

		while (NULL != (token = strtok(0, DELIMITERS))) {
			switch (arg++) {
			case rotor_position:
				switch (toupper(token[0])) {
				case '0' ... '9':
					item.position =
					    strtoul(token, NULL, 10);
					if (item.position < 1)
						continue;
					break;
				case '-':
					item.position = -1;
					arg = end_of_line_rotor;
					continue;
				default:
					error("could not parse %s\n",
					      positiondata);
					goto err;
				}
			case sat_id:
				if (item.id)
					free(item.id);
				item.id = (char *)malloc(strlen(token) + 1);
				strcpy(item.id, token);
				break;
			case plug_rotor_rotor_position:
				if (!strcmp("=", token)) {
					// fixme: we hit a "=" 
					arg--;
					break;
				}
				switch (toupper(token[0])) {
				case '0' ... '9':
					item.position =
					    strtoul(token, NULL, 10);
					if (item.position < 1)
						continue;
					break;
				case '-':
					item.position = -1;
					arg = end_of_line_rotor;
					continue;
				default:
					error("could not parse %s\n",
					      positiondata);
					goto err;
				}
			case end_of_line_rotor:
			case READ_STOP:
			default:
				arg = end_of_line_rotor;
				break;
			}
			if ((arg == READ_STOP) || (arg == end_of_line_rotor))
				break;
		}
		free(copy);
		copy = NULL;
		if (item.position > 0) {
			if (txt_to_satellite(item.id) < 0) {
				info("Satellite ID \"%s\" not defined.\n",
				     item.id);
				goto err;
			}
			if (item.position > 255) {
				info("Invalid satellite position %d.\n",
				     item.position);
				goto err;
			}
			sat_list[txt_to_satellite(item.id)].rotor_position =
			    item.position;
			count++;
			info("\trotor position %3d = %6s\n", item.position,
			     item.id);
		}
		memset(buf, 0, sizeof(char) * MAX_LINE_LENGTH);
	}
	if (item.id)
		free(item.id);
	free(buf);
	fclose(data);
	if (count == 0) {
		info("Unexpected end of file..\n");
		return 0;
	}
	return 1;		// success
err:
	// clean up and leave with error.
	info("line causing error was \n\"%s\n", buf);
	if (item.id)
		free(item.id);
	free(buf);
	fclose(data);
	return 0;		//err
}
