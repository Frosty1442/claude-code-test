/**
 * GPS NMEA Sentence Parser Fuzzer
 *
 * Target: GPS receiver NMEA 0183 sentence parsing
 * Attack Surface: String parsing with checksums, coordinate conversion, field validation
 * Expected Bugs: Buffer overflows, format string bugs, integer overflows in coordinate math
 *
 * Compile:
 *   gcc -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
 *       gps_nmea_fuzzer.c -o gps_nmea_fuzzer -lm
 *
 * Run with AFL++:
 *   afl-fuzz -i corpus/gps -o findings/gps -- ./gps_nmea_fuzzer @@
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define GPS_MAX_NMEA_LENGTH 256
#define GPS_MAX_FIELDS 32

/* GPS PVT data structure */
typedef struct
{
    double latitude;
    double longitude;
    double altitude_msl;
    uint8_t fix_type;
    uint8_t satellites_used;
    float hdop;
    uint32_t timestamp;
} GPS_PVT_t;

GPS_PVT_t gps_data;

/***********************************************************************/
/* NMEA Checksum Validation */

uint8_t GPS_CalculateNMEAChecksum(const char *sentence)
{
    uint8_t checksum = 0;
    const char *p = sentence;

    /* Find start of sentence (skip $) */
    if (*p == '$')
        p++;

    /* XOR all characters until * */
    while (*p && *p != '*')
    {
        checksum ^= *p;
        p++;
    }

    return checksum;
}

int GPS_ValidateChecksum(const char *sentence)
{
    /* Find checksum position */
    const char *asterisk = strchr(sentence, '*');
    if (!asterisk)
        return 0;  /* No checksum */

    /* Parse checksum from hex */
    uint8_t provided_checksum = 0;
    if (sscanf(asterisk + 1, "%2hhx", &provided_checksum) != 1)
        return 0;

    /* Calculate expected checksum */
    uint8_t calculated_checksum = GPS_CalculateNMEAChecksum(sentence);

    return (provided_checksum == calculated_checksum);
}

/***********************************************************************/
/* NMEA Field Parsing */

int GPS_SplitNMEA(const char *sentence, char fields[][64], int max_fields)
{
    int field_count = 0;
    const char *start = sentence;
    const char *p = sentence;

    /* Skip $ and find sentence type */
    if (*p == '$')
        p++;

    while (*p && field_count < max_fields)
    {
        if (*p == ',' || *p == '*' || *p == '\r' || *p == '\n')
        {
            size_t len = p - start;
            if (len >= 64)
                len = 63;  /* Truncate long fields */

            strncpy(fields[field_count], start, len);
            fields[field_count][len] = '\0';

            field_count++;
            start = p + 1;

            if (*p == '*')
                break;  /* Stop at checksum */
        }
        p++;
    }

    return field_count;
}

/***********************************************************************/
/* Coordinate Conversion - High Risk for Integer Overflow! */

double GPS_NMEAToDecimalDegrees(const char *nmea_coord, char hemisphere)
{
    if (!nmea_coord || strlen(nmea_coord) < 4)
        return 0.0;

    /* NMEA format: DDMM.MMMM or DDDMM.MMMM */
    char degrees_str[4] = {0};
    double minutes = 0.0;

    /* Extract degrees (first 2 or 3 characters) */
    int degree_digits = (strlen(nmea_coord) >= 5 && nmea_coord[4] == '.') ? 2 : 3;

    /* POTENTIAL BUG: No bounds checking on degree_digits */
    strncpy(degrees_str, nmea_coord, degree_digits);
    int degrees = atoi(degrees_str);

    /* Extract minutes */
    minutes = atof(nmea_coord + degree_digits);

    /* Convert to decimal degrees */
    double decimal = degrees + (minutes / 60.0);

    /* Apply hemisphere */
    if (hemisphere == 'S' || hemisphere == 'W')
        decimal = -decimal;

    /* INTENTIONAL BUG: No validation of reasonable range */
    /* Valid latitude: -90 to +90, longitude: -180 to +180 */
    /* Fuzzer should find inputs that create invalid coordinates */

    return decimal;
}

/***********************************************************************/
/* GGA Parser - Position Fix Data */

int GPS_ParseGGA(const char *sentence)
{
    char fields[GPS_MAX_FIELDS][64];
    int field_count = GPS_SplitNMEA(sentence, fields, GPS_MAX_FIELDS);

    if (field_count < 10)
        return -1;  /* Not enough fields */

    /* Field 1: Time (HHMMSS.sss) */
    if (strlen(fields[1]) > 0)
    {
        gps_data.timestamp = (uint32_t)atoi(fields[1]);
    }

    /* Field 2: Latitude (DDMM.MMMM) */
    /* Field 3: N/S */
    if (strlen(fields[2]) > 0 && strlen(fields[3]) > 0)
    {
        char hemisphere = fields[3][0];
        gps_data.latitude = GPS_NMEAToDecimalDegrees(fields[2], hemisphere);

        /* INTENTIONAL BUG: No range validation */
        if (gps_data.latitude > 90.0 || gps_data.latitude < -90.0)
        {
            /* Trigger ASAN crash for demonstration */
            char *crash = NULL;
            *crash = 0x42;  /* Fuzzer will find this! */
        }
    }

    /* Field 4: Longitude (DDDMM.MMMM) */
    /* Field 5: E/W */
    if (strlen(fields[4]) > 0 && strlen(fields[5]) > 0)
    {
        char hemisphere = fields[5][0];
        gps_data.longitude = GPS_NMEAToDecimalDegrees(fields[4], hemisphere);
    }

    /* Field 6: Fix quality (0-9) */
    if (strlen(fields[6]) > 0)
    {
        gps_data.fix_type = (uint8_t)atoi(fields[6]);
    }

    /* Field 7: Number of satellites */
    if (strlen(fields[7]) > 0)
    {
        gps_data.satellites_used = (uint8_t)atoi(fields[7]);

        /* POTENTIAL BUG: Array index out of bounds if used elsewhere */
        /* What if satellites_used > 32? */
    }

    /* Field 8: HDOP */
    if (strlen(fields[8]) > 0)
    {
        gps_data.hdop = (float)atof(fields[8]);
    }

    /* Field 9: Altitude */
    if (strlen(fields[9]) > 0)
    {
        gps_data.altitude_msl = atof(fields[9]);

        /* INTENTIONAL BUG: Integer overflow in altitude conversion */
        if (gps_data.altitude_msl > 1e10)
        {
            /* Simulate overflow vulnerability */
            int32_t altitude_mm = (int32_t)(gps_data.altitude_msl * 1000.0);
            printf("Altitude (mm): %d\n", altitude_mm);  /* Overflow! */
        }
    }

    return 0;  /* Success */
}

/***********************************************************************/
/* RMC Parser - Recommended Minimum Data */

int GPS_ParseRMC(const char *sentence)
{
    char fields[GPS_MAX_FIELDS][64];
    int field_count = GPS_SplitNMEA(sentence, fields, GPS_MAX_FIELDS);

    if (field_count < 12)
        return -1;

    /* Field 2: Status (A=active, V=void) */
    if (strlen(fields[2]) > 0 && fields[2][0] != 'A')
        return -1;  /* Invalid fix */

    /* Field 3/4: Latitude */
    if (strlen(fields[3]) > 0 && strlen(fields[4]) > 0)
    {
        gps_data.latitude = GPS_NMEAToDecimalDegrees(fields[3], fields[4][0]);
    }

    /* Field 5/6: Longitude */
    if (strlen(fields[5]) > 0 && strlen(fields[6]) > 0)
    {
        gps_data.longitude = GPS_NMEAToDecimalDegrees(fields[5], fields[6][0]);
    }

    /* POTENTIAL BUG: No validation of date field (field 9) */
    /* Malformed date could cause issues */

    return 0;
}

/***********************************************************************/
/* Main NMEA Parser - Entry Point */

int GPS_ParseNMEA(const char *sentence, size_t length)
{
    /* Input validation */
    if (!sentence || length == 0)
        return -1;

    if (length > GPS_MAX_NMEA_LENGTH)
        return -1;  /* Too long */

    /* Must start with $ */
    if (sentence[0] != '$')
        return -1;

    /* POTENTIAL BUG: What if length is incorrect? */
    /* Could lead to buffer over-read */
    char buffer[GPS_MAX_NMEA_LENGTH + 1];
    memcpy(buffer, sentence, length);
    buffer[length] = '\0';

    /* Validate checksum (if present) */
    if (strchr(buffer, '*'))
    {
        if (!GPS_ValidateChecksum(buffer))
        {
            /* INTENTIONAL: Allow fuzzing of checksum validation */
            /* Real implementation should reject, but we want to test parser */
        }
    }

    /* Identify sentence type */
    if (strncmp(buffer, "$GPGGA", 6) == 0 || strncmp(buffer, "$GNGGA", 6) == 0)
    {
        return GPS_ParseGGA(buffer);
    }
    else if (strncmp(buffer, "$GPRMC", 6) == 0 || strncmp(buffer, "$GNRMC", 6) == 0)
    {
        return GPS_ParseRMC(buffer);
    }
    else if (strncmp(buffer, "$GPGSA", 6) == 0)
    {
        /* GSA parsing - not implemented, could have bugs */
        return 0;
    }
    else if (strncmp(buffer, "$GPGSV", 6) == 0)
    {
        /* GSV parsing - not implemented, could have bugs */
        return 0;
    }

    return -1;  /* Unknown sentence type */
}

/***********************************************************************/
/* AFL++ Fuzzing Harness */

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    /* Read fuzzing input */
    FILE *fp = fopen(argv[1], "rb");
    if (!fp)
    {
        perror("fopen");
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0 || file_size > GPS_MAX_NMEA_LENGTH)
    {
        fclose(fp);
        return 0;  /* Skip invalid sizes */
    }

    uint8_t buffer[GPS_MAX_NMEA_LENGTH];
    size_t bytes_read = fread(buffer, 1, file_size, fp);
    fclose(fp);

    if (bytes_read != file_size)
        return 0;

    /* Initialize GPS data */
    memset(&gps_data, 0, sizeof(gps_data));

    /* Parse NMEA sentence - this is where bugs will be found! */
    GPS_ParseNMEA((const char *)buffer, bytes_read);

    return 0;
}

/* AFL++ persistent mode variant for 10,000x performance */
#ifdef __AFL_HAVE_MANUAL_CONTROL
__AFL_FUZZ_INIT();

int main_persistent(void)
{
    __AFL_INIT();

    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

    while (__AFL_LOOP(10000))
    {
        int len = __AFL_FUZZ_TESTCASE_LEN;

        if (len > GPS_MAX_NMEA_LENGTH)
            continue;

        memset(&gps_data, 0, sizeof(gps_data));
        GPS_ParseNMEA((const char *)buf, len);
    }

    return 0;
}
#endif
