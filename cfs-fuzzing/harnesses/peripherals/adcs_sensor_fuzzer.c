/**
 * ADCS Sensor Data Fuzzer
 *
 * Target: Attitude sensor data validation and filtering
 * Attack Surface: Binary sensor packets, floating-point math, quaternion operations
 * Expected Bugs: Float overflows, invalid quaternions, division by zero, NaN propagation
 *
 * Compile:
 *   gcc -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
 *       adcs_sensor_fuzzer.c -o adcs_sensor_fuzzer -lm
 *
 * Run with AFL++:
 *   afl-fuzz -i corpus/adcs -o findings/adcs -- ./adcs_sensor_fuzzer @@
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/* Sensor packet format (binary) */
#pragma pack(push, 1)
typedef struct
{
    /* Header */
    uint16_t sync;        /* 0xADC5 (ADCS in hex) */
    uint8_t  packet_type; /* 1=MAG, 2=GYRO, 3=IMU, 4=SUN */
    uint8_t  length;

    /* Sensor data (varies by type) */
    union
    {
        struct  /* Magnetometer */
        {
            float b_x;
            float b_y;
            float b_z;
            uint8_t status;
        } mag;

        struct  /* Gyroscope */
        {
            float gyro_x;
            float gyro_y;
            float gyro_z;
            float accel_x;
            float accel_y;
            float accel_z;
        } imu;

        struct  /* Sun sensor */
        {
            float intensity[6];
            float sun_vector[3];
        } sun;

        struct  /* Quaternion attitude */
        {
            float q0, q1, q2, q3;
        } quat;
    } data;

    uint16_t checksum;
} ADCS_SensorPacket_t;
#pragma pack(pop)

/* Attitude state */
typedef struct
{
    float quaternion[4];      /* q0, q1, q2, q3 */
    float angular_velocity[3];
    float euler_angles[3];    /* roll, pitch, yaw */
} ADCS_AttitudeState_t;

ADCS_AttitudeState_t attitude;

/***********************************************************************/
/* Checksum Validation */

uint16_t ADCS_CalculateChecksum(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint16_t checksum = 0;

    for (size_t i = 0; i < length; i++)
    {
        checksum += bytes[i];
    }

    return checksum;
}

/***********************************************************************/
/* Quaternion Operations - Vulnerable to NaN Propagation! */

void ADCS_NormalizeQuaternion(float *q)
{
    /* POTENTIAL BUG: What if norm is zero or NaN? */
    float norm = sqrtf(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);

    /* INTENTIONAL BUG: No check for division by zero */
    q[0] /= norm;
    q[1] /= norm;
    q[2] /= norm;
    q[3] /= norm;

    /* If inputs are malformed, all components become NaN */
    /* NaN propagates through subsequent calculations */
}

void ADCS_QuaternionMultiply(const float *q1, const float *q2, float *result)
{
    result[0] = q1[0]*q2[0] - q1[1]*q2[1] - q1[2]*q2[2] - q1[3]*q2[3];
    result[1] = q1[0]*q2[1] + q1[1]*q2[0] + q1[2]*q2[3] - q1[3]*q2[2];
    result[2] = q1[0]*q2[2] - q1[1]*q2[3] + q1[2]*q2[0] + q1[3]*q2[1];
    result[3] = q1[0]*q2[3] + q1[1]*q2[2] - q1[2]*q2[1] + q1[3]*q2[0];

    /* POTENTIAL BUG: No NaN checking */
}

void ADCS_QuaternionToEuler(const float *q, float *roll, float *pitch, float *yaw)
{
    /* INTENTIONAL BUG: No input validation */
    /* What if quaternion is not normalized? */

    float q0 = q[0], q1 = q[1], q2 = q[2], q3 = q[3];

    /* POTENTIAL BUG: asinf() requires input in range [-1, 1] */
    /* If input is malformed, this could produce NaN or invalid results */
    *roll = atan2f(2.0f*(q0*q1 + q2*q3), 1.0f - 2.0f*(q1*q1 + q2*q2));
    *pitch = asinf(2.0f*(q0*q2 - q3*q1));  /* Can crash with invalid input! */
    *yaw = atan2f(2.0f*(q0*q3 + q1*q2), 1.0f - 2.0f*(q2*q2 + q3*q3));

    /* Convert to degrees */
    *roll *= (180.0f / M_PI);
    *pitch *= (180.0f / M_PI);
    *yaw *= (180.0f / M_PI);

    /* INTENTIONAL BUG: Trigger crash if pitch is NaN */
    if (isnan(*pitch))
    {
        char *crash = NULL;
        *crash = 0x42;
    }
}

/***********************************************************************/
/* Magnetometer Data Processing */

int ADCS_ProcessMagnetometer(const float *b_field)
{
    /* POTENTIAL BUG: No range validation */
    /* Valid Earth magnetic field: 25,000 - 65,000 nT */

    float magnitude = sqrtf(b_field[0]*b_field[0] +
                            b_field[1]*b_field[1] +
                            b_field[2]*b_field[2]);

    /* INTENTIONAL BUG: Array overflow if magnitude is extreme */
    if (magnitude > 1e10f)
    {
        /* Simulate fixed-point conversion overflow */
        int16_t b_x_fixed = (int16_t)(b_field[0] / 10.0f);
        printf("Mag X (fixed): %d\n", b_x_fixed);
    }

    /* INTENTIONAL BUG: Division by zero if magnitude is zero */
    float normalized[3];
    normalized[0] = b_field[0] / magnitude;
    normalized[1] = b_field[1] / magnitude;
    normalized[2] = b_field[2] / magnitude;

    return 0;
}

/***********************************************************************/
/* IMU Data Processing */

int ADCS_ProcessIMU(const float *gyro, const float *accel)
{
    /* Update angular velocity */
    attitude.angular_velocity[0] = gyro[0] * (M_PI / 180.0f);
    attitude.angular_velocity[1] = gyro[1] * (M_PI / 180.0f);
    attitude.angular_velocity[2] = gyro[2] * (M_PI / 180.0f);

    /* POTENTIAL BUG: What if angular velocity is extreme? */
    /* Could cause overflow in subsequent integration */

    float omega_mag = sqrtf(attitude.angular_velocity[0] * attitude.angular_velocity[0] +
                            attitude.angular_velocity[1] * attitude.angular_velocity[1] +
                            attitude.angular_velocity[2] * attitude.angular_velocity[2]);

    /* INTENTIONAL BUG: Trigger crash if omega is extremely large */
    if (omega_mag > 1000.0f)  /* > 1000 rad/s is physically impossible */
    {
        /* Simulate integer overflow in time calculation */
        int32_t omega_millirads = (int32_t)(omega_mag * 1000.0f);
        printf("Omega (millirad/s): %d\n", omega_millirads);

        /* Trigger crash */
        char *crash = NULL;
        *crash = 0x42;
    }

    /* Integrate to update attitude quaternion */
    float dt = 0.01f;  /* 100 Hz update rate */

    if (omega_mag > 0.001f)
    {
        float half_theta = 0.5f * omega_mag * dt;
        float sin_ht = sinf(half_theta);
        float cos_ht = cosf(half_theta);

        float dq[4];
        dq[0] = cos_ht;
        dq[1] = (attitude.angular_velocity[0] / omega_mag) * sin_ht;
        dq[2] = (attitude.angular_velocity[1] / omega_mag) * sin_ht;
        dq[3] = (attitude.angular_velocity[2] / omega_mag) * sin_ht;

        float q_new[4];
        ADCS_QuaternionMultiply(attitude.quaternion, dq, q_new);

        memcpy(attitude.quaternion, q_new, sizeof(q_new));
        ADCS_NormalizeQuaternion(attitude.quaternion);

        /* Update Euler angles */
        ADCS_QuaternionToEuler(attitude.quaternion,
                               &attitude.euler_angles[0],
                               &attitude.euler_angles[1],
                               &attitude.euler_angles[2]);
    }

    return 0;
}

/***********************************************************************/
/* Sun Sensor Processing */

int ADCS_ProcessSunSensor(const float *intensity, const float *sun_vector)
{
    /* POTENTIAL BUG: No validation of sun vector */
    /* Should be unit vector, but what if it's not? */

    float vec_mag = sqrtf(sun_vector[0]*sun_vector[0] +
                          sun_vector[1]*sun_vector[1] +
                          sun_vector[2]*sun_vector[2]);

    /* INTENTIONAL BUG: No check for zero magnitude */
    float normalized[3];
    normalized[0] = sun_vector[0] / vec_mag;
    normalized[1] = sun_vector[1] / vec_mag;
    normalized[2] = sun_vector[2] / vec_mag;

    /* POTENTIAL BUG: Intensity array access */
    for (int i = 0; i < 6; i++)
    {
        /* What if intensity values are negative? */
        if (intensity[i] < 0.0f)
        {
            /* Simulate unsigned conversion bug */
            uint8_t intensity_u8 = (uint8_t)(intensity[i] * 255.0f);
            printf("Face %d intensity: %u\n", i, intensity_u8);
        }
    }

    return 0;
}

/***********************************************************************/
/* Packet Parser - Main Entry Point */

int ADCS_ParseSensorPacket(const uint8_t *packet_data, size_t length)
{
    if (!packet_data || length < sizeof(ADCS_SensorPacket_t))
        return -1;

    const ADCS_SensorPacket_t *pkt = (const ADCS_SensorPacket_t *)packet_data;

    /* Validate sync word */
    if (pkt->sync != 0xADC5)  /* ADCS magic number */
        return -1;

    /* POTENTIAL BUG: Length field not validated against actual length */
    /* Could lead to buffer over-read */

    /* Verify checksum */
    uint16_t calc_checksum = ADCS_CalculateChecksum(packet_data,
                                                     sizeof(ADCS_SensorPacket_t) - sizeof(uint16_t));

    /* INTENTIONAL: Skip checksum validation to allow fuzzing of data field */

    /* Process based on packet type */
    switch (pkt->packet_type)
    {
        case 1:  /* Magnetometer */
        {
            float b_field[3] = {pkt->data.mag.b_x, pkt->data.mag.b_y, pkt->data.mag.b_z};
            ADCS_ProcessMagnetometer(b_field);
            break;
        }

        case 2:  /* IMU */
        {
            float gyro[3] = {pkt->data.imu.gyro_x, pkt->data.imu.gyro_y, pkt->data.imu.gyro_z};
            float accel[3] = {pkt->data.imu.accel_x, pkt->data.imu.accel_y, pkt->data.imu.accel_z};
            ADCS_ProcessIMU(gyro, accel);
            break;
        }

        case 3:  /* Sun sensor */
        {
            ADCS_ProcessSunSensor(pkt->data.sun.intensity, pkt->data.sun.sun_vector);
            break;
        }

        case 4:  /* Quaternion */
        {
            attitude.quaternion[0] = pkt->data.quat.q0;
            attitude.quaternion[1] = pkt->data.quat.q1;
            attitude.quaternion[2] = pkt->data.quat.q2;
            attitude.quaternion[3] = pkt->data.quat.q3;

            ADCS_NormalizeQuaternion(attitude.quaternion);
            ADCS_QuaternionToEuler(attitude.quaternion,
                                   &attitude.euler_angles[0],
                                   &attitude.euler_angles[1],
                                   &attitude.euler_angles[2]);
            break;
        }

        default:
            return -1;
    }

    return 0;
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

    FILE *fp = fopen(argv[1], "rb");
    if (!fp)
    {
        perror("fopen");
        return 1;
    }

    uint8_t buffer[1024];
    size_t bytes_read = fread(buffer, 1, sizeof(buffer), fp);
    fclose(fp);

    if (bytes_read == 0)
        return 0;

    /* Initialize attitude to identity quaternion */
    memset(&attitude, 0, sizeof(attitude));
    attitude.quaternion[0] = 1.0f;

    /* Parse sensor packet - this is where bugs will be found! */
    ADCS_ParseSensorPacket(buffer, bytes_read);

    return 0;
}
