#!/bin/bash
#
# Generate Extended Seed Corpus for CubeSat Peripheral Fuzzing
# Creates comprehensive test cases for GPS and ADCS fuzzers
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GPS_CORPUS="$SCRIPT_DIR/../corpus/gps"
ADCS_CORPUS="$SCRIPT_DIR/../corpus/adcs"

echo "Generating extended seed corpus..."

# ============================================================================
# GPS NMEA Corpus
# ============================================================================

echo "  Creating GPS NMEA seeds..."

# Additional valid GGA sentences
cat > "$GPS_CORPUS/valid_gga_low_alt.nmea" << 'NMEA'
$GPGGA,000001.000,0000.000,N,00000.000,E,1,04,1.0,0.0,M,0.0,M,,*4D
NMEA

cat > "$GPS_CORPUS/valid_gga_high_alt.nmea" << 'NMEA'
$GPGGA,235959.999,4530.000,N,12230.000,W,1,12,0.8,8848.0,M,0.0,M,,*60
NMEA

# RMC variants
cat > "$GPS_CORPUS/valid_rmc_stationary.nmea" << 'NMEA'
$GPRMC,000000.000,A,0000.000,N,00000.000,E,0.0,0.0,010125,0.0,E*7C
NMEA

cat > "$GPS_CORPUS/valid_rmc_moving.nmea" << 'NMEA'
$GPRMC,123456.789,A,4807.038,N,01131.324,E,22.5,084.4,091125,003.1,W*55
NMEA

# GSA sentence
cat > "$GPS_CORPUS/valid_gsa.nmea" << 'NMEA'
$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39
NMEA

# GSV sentence
cat > "$GPS_CORPUS/valid_gsv.nmea" << 'NMEA'
$GPGSV,3,1,12,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45*75
NMEA

# Edge case: Equator crossing
cat > "$GPS_CORPUS/edge_equator.nmea" << 'NMEA'
$GPGGA,120000.000,0000.001,N,00000.000,E,1,08,0.9,100.0,M,0.0,M,,*5A
NMEA

# Edge case: Prime meridian
cat > "$GPS_CORPUS/edge_prime_meridian.nmea" << 'NMEA'
$GPGGA,120000.000,5130.000,N,00000.001,E,1,08,0.9,50.0,M,0.0,M,,*6F
NMEA

# Edge case: International date line
cat > "$GPS_CORPUS/edge_date_line.nmea" << 'NMEA'
$GPGGA,120000.000,0000.000,N,18000.000,E,1,08,0.9,0.0,M,0.0,M,,*41
NMEA

# Edge case: South pole
cat > "$GPS_CORPUS/edge_south_pole.nmea" << 'NMEA'
$GPGGA,120000.000,9000.000,S,00000.000,E,1,04,2.5,2835.0,M,0.0,M,,*46
NMEA

# Malformed: Missing checksum
cat > "$GPS_CORPUS/malformed_no_checksum.nmea" << 'NMEA'
$GPGGA,120000.000,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,
NMEA

# Malformed: Invalid checksum
cat > "$GPS_CORPUS/malformed_bad_checksum.nmea" << 'NMEA'
$GPGGA,120000.000,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*FF
NMEA

# Malformed: Too few fields
cat > "$GPS_CORPUS/malformed_short.nmea" << 'NMEA'
$GPGGA,123519*2E
NMEA

# Malformed: Empty fields
cat > "$GPS_CORPUS/malformed_empty_fields.nmea" << 'NMEA'
$GPGGA,,,,,,,,,,,,,*56
NMEA

# Malformed: Very long sentence
printf '$GPGGA,' > "$GPS_CORPUS/malformed_long.nmea"
for i in {1..100}; do printf "1234567890," >> "$GPS_CORPUS/malformed_long.nmea"; done
printf "*00\n" >> "$GPS_CORPUS/malformed_long.nmea"

# Invalid latitude (triggers bug)
cat > "$GPS_CORPUS/invalid_lat_91.nmea" << 'NMEA'
$GPGGA,000000.000,9100.000,N,00000.000,E,1,04,2.5,100.0,M,0.0,M,,*52
NMEA

cat > "$GPS_CORPUS/invalid_lat_negative.nmea" << 'NMEA'
$GPGGA,000000.000,9000.000,S,00000.000,E,1,04,2.5,100.0,M,0.0,M,,*4A
NMEA

# Invalid longitude
cat > "$GPS_CORPUS/invalid_lon_181.nmea" << 'NMEA'
$GPGGA,000000.000,0000.000,N,18100.000,E,1,04,2.5,100.0,M,0.0,M,,*47
NMEA

# ============================================================================
# ADCS Binary Corpus
# ============================================================================

echo "  Creating ADCS binary packet seeds..."

# Valid magnetometer packets (various field strengths)
printf '\xC5\xAD\x01\x10\x00\x00\x48\x45\x00\x00\x48\x45\x00\x00\x9C\x45\x00\x12\x34' > "$ADCS_CORPUS/valid_mag_weak.bin"
printf '\xC5\xAD\x01\x10\x00\x00\x96\x45\x00\x00\x96\x45\x00\x00\xFA\x45\x00\x12\x34' > "$ADCS_CORPUS/valid_mag_strong.bin"

# Valid sun sensor packets
printf '\xC5\xAD\x03\x30\x00\x00\x80\x3F\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x3F\x00\x00\x00\x00\x00\x00\x00\x00\x12\x34' > "$ADCS_CORPUS/valid_sun_xplus.bin"

# Valid quaternion packets (various attitudes)
# Identity quaternion
printf '\xC5\xAD\x04\x10\x00\x00\x80\x3F\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x12\x34' > "$ADCS_CORPUS/valid_quat_identity.bin"

# 90-degree rotation about Z
printf '\xC5\xAD\x04\x10\x17\xD9\x53\x3F\x00\x00\x00\x00\x00\x00\x00\x00\x17\xD9\x53\x3F\x12\x34' > "$ADCS_CORPUS/valid_quat_z90.bin"

# Small IMU reading (slow rotation)
printf '\xC5\xAD\x02\x20\x00\x00\x00\x3F\x00\x00\x00\x3F\x00\x00\x00\x3F\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x1C\x41\x56\x78' > "$ADCS_CORPUS/valid_imu_slow.bin"

# Fast IMU reading (rapid rotation)
printf '\xC5\xAD\x02\x20\x00\x00\xC8\x42\x00\x00\xC8\x42\x00\x00\xC8\x42\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x1C\x41\x56\x78' > "$ADCS_CORPUS/valid_imu_fast.bin"

# Malformed: Invalid sync word
printf '\xFF\xFF\x01\x10\x00\x00\x70\x45\x00\x00\x70\x45\x00\x00\xA0\x45\x00\x12\x34' > "$ADCS_CORPUS/malformed_bad_sync.bin"

# Malformed: Invalid packet type
printf '\xC5\xAD\xFF\x10\x00\x00\x70\x45\x00\x00\x70\x45\x00\x00\xA0\x45\x00\x12\x34' > "$ADCS_CORPUS/malformed_bad_type.bin"

# Malformed: Wrong length
printf '\xC5\xAD\x01\xFF\x00\x00\x70\x45\x00\x00\x70\x45\x00\x00\xA0\x45\x00\x12\x34' > "$ADCS_CORPUS/malformed_bad_length.bin"

# Malformed: Truncated packet
printf '\xC5\xAD\x01\x10\x00\x00' > "$ADCS_CORPUS/malformed_truncated.bin"

# Edge: NaN values
printf '\xC5\xAD\x01\x10\x00\x00\xC0\x7F\x00\x00\xC0\x7F\x00\x00\xC0\x7F\x00\x12\x34' > "$ADCS_CORPUS/edge_nan_values.bin"

# Edge: Infinity values
printf '\xC5\xAD\x01\x10\x00\x00\x80\x7F\x00\x00\x80\x7F\x00\x00\x80\x7F\x00\x12\x34' > "$ADCS_CORPUS/edge_inf_values.bin"

# Edge: Very small quaternion (near-zero norm)
printf '\xC5\xAD\x04\x10\x00\x00\x80\x38\x00\x00\x80\x38\x00\x00\x80\x38\x00\x00\x80\x38\x12\x34' > "$ADCS_CORPUS/edge_tiny_quat.bin"

# ============================================================================
# Summary
# ============================================================================

GPS_COUNT=$(ls -1 "$GPS_CORPUS" | wc -l)
ADCS_COUNT=$(ls -1 "$ADCS_CORPUS" | wc -l)

echo ""
echo "Extended corpus generation complete!"
echo "  GPS seeds: $GPS_COUNT files"
echo "  ADCS seeds: $ADCS_COUNT files"
echo ""
echo "Corpus ready for AFL++ fuzzing campaigns."

