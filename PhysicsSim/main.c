#include <chipmunk/chipmunk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define LINE_BUFFER_SIZE 256
#define DECRYPTED_CONFIG_FILE "config_decrypted.txt"
#define ENCRYPTED_CONFIG_FILE "config_encrypted.txt"

/* Compatibility helpers for Chipmunk 6.x and 7.x body APIs. */
static void body_set_velocity(cpBody *body, cpVect velocity)
{
#if defined(CP_VERSION_MAJOR) && (CP_VERSION_MAJOR >= 7)
    cpBodySetVelocity(body, velocity);
#else
    cpBodySetVel(body, velocity);
#endif
}

static void body_set_position(cpBody *body, cpVect position)
{
#if defined(CP_VERSION_MAJOR) && (CP_VERSION_MAJOR >= 7)
    cpBodySetPosition(body, position);
#else
    cpBodySetPos(body, position);
#endif
}

/* Caesar cipher: encrypt alphabetic characters by a shift value. */
void caesar_encrypt(char *text, int shift)
{
    int i;
    int normalized_shift = shift % 26;

    if (normalized_shift < 0)
    {
        normalized_shift += 26;
    }

    for (i = 0; text[i] != '\0'; i++)
    {
        char c = text[i];

        if (c >= 'A' && c <= 'Z')
        {
            text[i] = (char)('A' + (c - 'A' + normalized_shift) % 26);
        }
        else if (c >= 'a' && c <= 'z')
        {
            text[i] = (char)('a' + (c - 'a' + normalized_shift) % 26);
        }
    }
}

/* Caesar cipher: decrypt alphabetic characters by reversing the shift. */
void caesar_decrypt(char *text, int shift)
{
    int i;
    int normalized_shift = shift % 26;

    if (normalized_shift < 0)
    {
        normalized_shift += 26;
    }

    for (i = 0; text[i] != '\0'; i++)
    {
        char c = text[i];

        if (c >= 'A' && c <= 'Z')
        {
            text[i] = (char)('A' + (c - 'A' - normalized_shift + 26) % 26);
        }
        else if (c >= 'a' && c <= 'z')
        {
            text[i] = (char)('a' + (c - 'a' - normalized_shift + 26) % 26);
        }
    }
}

/* File encryption helper for config files. */
int encrypt_file(const char *input, const char *output, int shift)
{
    FILE *in_file;
    FILE *out_file;
    char line[LINE_BUFFER_SIZE];

    in_file = fopen(input, "r");
    if (in_file == NULL)
    {
        fprintf(stderr, "Error: Could not open input file '%s' for reading.\n", input);
        return 1;
    }

    out_file = fopen(output, "w");
    if (out_file == NULL)
    {
        fprintf(stderr, "Error: Could not open output file '%s' for writing.\n", output);
        fclose(in_file);
        return 1;
    }

    while (fgets(line, sizeof(line), in_file) != NULL)
    {
        caesar_encrypt(line, shift);
        fputs(line, out_file);
    }

    fclose(out_file);
    fclose(in_file);

    return 0;
}

/* File decryption helper for config files. */
int decrypt_file(const char *input, const char *output, int shift)
{
    FILE *in_file;
    FILE *out_file;
    char line[LINE_BUFFER_SIZE];

    in_file = fopen(input, "r");
    if (in_file == NULL)
    {
        fprintf(stderr, "Error: Could not open input file '%s' for reading.\n", input);
        return 1;
    }

    out_file = fopen(output, "w");
    if (out_file == NULL)
    {
        fprintf(stderr, "Error: Could not open output file '%s' for writing.\n", output);
        fclose(in_file);
        return 1;
    }

    while (fgets(line, sizeof(line), in_file) != NULL)
    {
        caesar_decrypt(line, shift);
        fputs(line, out_file);
    }

    fclose(out_file);
    fclose(in_file);

    return 0;
}

/* Round-trip self-test: decrypt(encrypt(text)) must match original text exactly. */
static int caesar_round_trip_test(void)
{
    char original[] = "Bowling Zz 123 !";
    char buffer[LINE_BUFFER_SIZE];

    strncpy(buffer, original, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    caesar_encrypt(buffer, 7);
    caesar_decrypt(buffer, 7);

    return strcmp(buffer, original) == 0;
}

/* Config parser with defaults and warnings for missing keys. */
int parse_config(const char *filename, double *gravity, double *ball_mass, double *pin_mass, double *friction)
{
    FILE *file;
    char line[LINE_BUFFER_SIZE];
    int gravity_found = 0;
    int ball_mass_found = 0;
    int pin_mass_found = 0;
    int friction_found = 0;

    *gravity = 9.8;
    *ball_mass = 5.0;
    *pin_mass = 1.0;
    *friction = 0.6;

    file = fopen(filename, "r");
    if (file == NULL)
    {
        fprintf(stderr, "Error: Could not open config file '%s'.\n", filename);
        return 1;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char key[64];
        double value;

        if (sscanf(line, " %63[^=]=%lf", key, &value) == 2)
        {
            if (strcmp(key, "gravity") == 0)
            {
                *gravity = value;
                gravity_found = 1;
            }
            else if (strcmp(key, "ball_mass") == 0)
            {
                *ball_mass = value;
                ball_mass_found = 1;
            }
            else if (strcmp(key, "pin_mass") == 0)
            {
                *pin_mass = value;
                pin_mass_found = 1;
            }
            else if (strcmp(key, "friction") == 0)
            {
                *friction = value;
                friction_found = 1;
            }
        }
    }

    fclose(file);

    if (!gravity_found)
    {
        printf("Warning: Missing 'gravity' in config. Using default %.2f\n", *gravity);
    }
    if (!ball_mass_found)
    {
        printf("Warning: Missing 'ball_mass' in config. Using default %.2f\n", *ball_mass);
    }
    if (!pin_mass_found)
    {
        printf("Warning: Missing 'pin_mass' in config. Using default %.2f\n", *pin_mass);
    }
    if (!friction_found)
    {
        printf("Warning: Missing 'friction' in config. Using default %.2f\n", *friction);
    }

    return 0;
}

/* Bowling physics simulation: ball rolls into six triangle-arranged pins. */
void run_simulation(double gravity, double ball_mass, double pin_mass, double friction)
{
    cpSpace *space;
    cpShape *floor_shape = NULL;
    cpBody **ball_body = NULL;
    cpShape **ball_shape = NULL;
    cpBody **pin_bodies = NULL;
    cpShape **pin_shapes = NULL;
    cpVect *pin_start_positions = NULL;

    const int pin_count = 6;
    const int total_steps = 500;
    const cpFloat dt = 1.0 / 60.0;
    const cpFloat floor_y = 40.0;
    const cpFloat ball_radius = 14.0;
    const cpFloat pin_radius = 8.0;
    const cpFloat elasticity = 0.25;

    int i;
    int step;
    int knocked_pins = 0;

    cpFloat pin_x_positions[6] = {560.0, 600.0, 640.0, 580.0, 620.0, 600.0};
    cpFloat pin_y_positions[6] = {70.0, 70.0, 70.0, 102.0, 102.0, 134.0};

    space = cpSpaceNew();
    if (space == NULL)
    {
        fprintf(stderr, "Error: Failed to create Chipmunk space.\n");
        return;
    }

    cpSpaceSetGravity(space, cpv(0.0, -(cpFloat)fabs(gravity)));

    floor_shape = cpSegmentShapeNew(cpSpaceGetStaticBody(space), cpv(0.0, floor_y), cpv(900.0, floor_y), 2.0);
    if (floor_shape == NULL)
    {
        fprintf(stderr, "Error: Failed to create floor shape.\n");
        cpSpaceFree(space);
        return;
    }
    cpShapeSetFriction(floor_shape, (cpFloat)friction);
    cpShapeSetElasticity(floor_shape, elasticity);
    cpSpaceAddShape(space, floor_shape);

    ball_body = (cpBody **)calloc(1, sizeof(cpBody *));
    ball_shape = (cpShape **)calloc(1, sizeof(cpShape *));
    pin_bodies = (cpBody **)calloc(pin_count, sizeof(cpBody *));
    pin_shapes = (cpShape **)calloc(pin_count, sizeof(cpShape *));
    pin_start_positions = (cpVect *)calloc(pin_count, sizeof(cpVect));

    if (ball_body == NULL || ball_shape == NULL || pin_bodies == NULL || pin_shapes == NULL || pin_start_positions == NULL)
    {
        fprintf(stderr, "Error: Memory allocation failed for simulation objects.\n");
        free(ball_body);
        free(ball_shape);
        free(pin_bodies);
        free(pin_shapes);
        free(pin_start_positions);
        cpSpaceRemoveShape(space, floor_shape);
        cpShapeFree(floor_shape);
        cpSpaceFree(space);
        return;
    }

    ball_body[0] = cpBodyNew((cpFloat)ball_mass, cpMomentForCircle((cpFloat)ball_mass, 0.0, ball_radius, cpvzero));
    if (ball_body[0] == NULL)
    {
        fprintf(stderr, "Error: Failed to create bowling ball body.\n");
        goto cleanup;
    }

    body_set_position(ball_body[0], cpv(120.0, floor_y + ball_radius + 2.0));
    cpSpaceAddBody(space, ball_body[0]);

    ball_shape[0] = cpCircleShapeNew(ball_body[0], ball_radius, cpvzero);
    if (ball_shape[0] == NULL)
    {
        fprintf(stderr, "Error: Failed to create bowling ball shape.\n");
        goto cleanup;
    }
    cpShapeSetFriction(ball_shape[0], (cpFloat)friction);
    cpShapeSetElasticity(ball_shape[0], elasticity);
    cpSpaceAddShape(space, ball_shape[0]);

    body_set_velocity(ball_body[0], cpv(260.0, 0.0));

    for (i = 0; i < pin_count; i++)
    {
        pin_bodies[i] = cpBodyNew((cpFloat)pin_mass, cpMomentForCircle((cpFloat)pin_mass, 0.0, pin_radius, cpvzero));
        if (pin_bodies[i] == NULL)
        {
            fprintf(stderr, "Error: Failed to create pin body %d.\n", i + 1);
            goto cleanup;
        }

        pin_start_positions[i] = cpv(pin_x_positions[i], pin_y_positions[i]);
        body_set_position(pin_bodies[i], pin_start_positions[i]);
        cpSpaceAddBody(space, pin_bodies[i]);

        pin_shapes[i] = cpCircleShapeNew(pin_bodies[i], pin_radius, cpvzero);
        if (pin_shapes[i] == NULL)
        {
            fprintf(stderr, "Error: Failed to create pin shape %d.\n", i + 1);
            goto cleanup;
        }

        cpShapeSetFriction(pin_shapes[i], (cpFloat)friction);
        cpShapeSetElasticity(pin_shapes[i], elasticity);
        cpSpaceAddShape(space, pin_shapes[i]);
    }

    printf("Starting bowling simulation...\n");
    printf("gravity=%.2f, ball_mass=%.2f, pin_mass=%.2f, friction=%.2f\n", gravity, ball_mass, pin_mass, friction);

    for (step = 0; step < total_steps; step++)
    {
        cpSpaceStep(space, dt);

        if (step % 50 == 0)
        {
#if defined(CP_VERSION_MAJOR) && (CP_VERSION_MAJOR >= 7)
            cpVect ball_pos = cpBodyGetPosition(ball_body[0]);
            cpVect ball_vel = cpBodyGetVelocity(ball_body[0]);
#else
            cpVect ball_pos = cpBodyGetPos(ball_body[0]);
            cpVect ball_vel = cpBodyGetVel(ball_body[0]);
#endif
            printf("\nStep %d\n", step);
            printf("Ball: pos=(%.2f, %.2f), vel=(%.2f, %.2f)\n", ball_pos.x, ball_pos.y, ball_vel.x, ball_vel.y);

            for (i = 0; i < pin_count; i++)
            {
#if defined(CP_VERSION_MAJOR) && (CP_VERSION_MAJOR >= 7)
                cpVect pin_pos = cpBodyGetPosition(pin_bodies[i]);
                cpVect pin_vel = cpBodyGetVelocity(pin_bodies[i]);
#else
                cpVect pin_pos = cpBodyGetPos(pin_bodies[i]);
                cpVect pin_vel = cpBodyGetVel(pin_bodies[i]);
#endif
                printf("Pin %d: pos=(%.2f, %.2f), vel=(%.2f, %.2f)\n", i + 1, pin_pos.x, pin_pos.y, pin_vel.x, pin_vel.y);
            }
        }
    }

    for (i = 0; i < pin_count; i++)
    {
#if defined(CP_VERSION_MAJOR) && (CP_VERSION_MAJOR >= 7)
        cpVect final_pos = cpBodyGetPosition(pin_bodies[i]);
#else
        cpVect final_pos = cpBodyGetPos(pin_bodies[i]);
#endif
        cpFloat dx = final_pos.x - pin_start_positions[i].x;
        cpFloat dy = final_pos.y - pin_start_positions[i].y;
        cpFloat distance = (cpFloat)sqrt((double)(dx * dx + dy * dy));

        if (distance > 15.0)
        {
            knocked_pins++;
        }
    }

    printf("\nSimulation complete. Pins knocked over (significant movement): %d/%d\n", knocked_pins, pin_count);

cleanup:
    if (pin_shapes != NULL)
    {
        for (i = 0; i < pin_count; i++)
        {
            if (pin_shapes[i] != NULL)
            {
                cpSpaceRemoveShape(space, pin_shapes[i]);
                cpShapeFree(pin_shapes[i]);
            }
        }
    }

    if (pin_bodies != NULL)
    {
        for (i = 0; i < pin_count; i++)
        {
            if (pin_bodies[i] != NULL)
            {
                cpSpaceRemoveBody(space, pin_bodies[i]);
                cpBodyFree(pin_bodies[i]);
            }
        }
    }

    if (ball_shape != NULL && ball_shape[0] != NULL)
    {
        cpSpaceRemoveShape(space, ball_shape[0]);
        cpShapeFree(ball_shape[0]);
    }

    if (ball_body != NULL && ball_body[0] != NULL)
    {
        cpSpaceRemoveBody(space, ball_body[0]);
        cpBodyFree(ball_body[0]);
    }

    if (floor_shape != NULL)
    {
        cpSpaceRemoveShape(space, floor_shape);
        cpShapeFree(floor_shape);
    }

    if (space != NULL)
    {
        cpSpaceFree(space);
    }

    free(pin_start_positions);
    free(pin_shapes);
    free(pin_bodies);
    free(ball_shape);
    free(ball_body);
}

int main(int argc, char *argv[])
{
    int shift;
    char *endptr;

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "-help") == 0))
    {
        printf("Bowling Physics Simulator - Help\n");
        printf("=================================\n");
        printf("Available commands:\n\n");
        printf("  -help\n");
        printf("      Shows this help menu\n\n");
        printf("  -encrypt <input_file> <shift>\n");
        printf("      Encrypts a config file using Caesar Cipher\n");
        printf("      Example: -encrypt config.txt 3\n\n");
        printf("  -decrypt <input_file> <shift>\n");
        printf("      Decrypts a file using Caesar Cipher\n");
        printf("      Example: -decrypt config_encrypted.txt 3\n\n");
        printf("  -runsim <encrypted_file> <shift>\n");
        printf("      Decrypts the config file and runs the bowling simulation\n");
        printf("      Example: -runsim config_encrypted.txt 3\n\n");
        printf("Config file must contain (fully customisable):\n");
        printf("  gravity=<value> (9.8 for Earth, 1.6 for Moon, 20.0 for heavy gravity)\n");
        printf("  ball_mass=<value> (e.g. 5.0 - heavier ball hits pins harder)\n");
        printf("  pin_mass=<value> (e.g. 1.0 - lighter pins scatter more)\n");
        printf("  friction=<value> (e.g. 0.6 - lower = slippery lane, higher = grippy lane)\n");
        return 0;
    }

    if (argc != 4)
    {
        fprintf(stderr, "Error: Invalid number of arguments. Use -help for usage.\n");
        return 1;
    }

    shift = (int)strtol(argv[3], &endptr, 10);
    if (*endptr != '\0')
    {
        fprintf(stderr, "Error: Shift must be an integer.\n");
        return 1;
    }

    if (!caesar_round_trip_test())
    {
        fprintf(stderr, "Error: Caesar cipher round-trip self-test failed.\n");
        return 1;
    }

    if (strcmp(argv[1], "-encrypt") == 0)
    {
        if (encrypt_file(argv[2], ENCRYPTED_CONFIG_FILE, shift) != 0)
        {
            return 1;
        }
        printf("Encryption successful. Output written to '%s'.\n", ENCRYPTED_CONFIG_FILE);
        return 0;
    }

    if (strcmp(argv[1], "-decrypt") == 0)
    {
        if (decrypt_file(argv[2], DECRYPTED_CONFIG_FILE, shift) != 0)
        {
            return 1;
        }
        printf("Decryption successful. Output written to '%s'.\n", DECRYPTED_CONFIG_FILE);
        return 0;
    }

    if (strcmp(argv[1], "-runsim") == 0)
    {
        double gravity;
        double ball_mass;
        double pin_mass;
        double friction;

        if (decrypt_file(argv[2], DECRYPTED_CONFIG_FILE, shift) != 0)
        {
            return 1;
        }

        if (parse_config(DECRYPTED_CONFIG_FILE, &gravity, &ball_mass, &pin_mass, &friction) != 0)
        {
            return 1;
        }

        run_simulation(gravity, ball_mass, pin_mass, friction);
        return 0;
    }

    fprintf(stderr, "Error: Unknown mode '%s'. Use -encrypt, -decrypt, or -runsim.\n", argv[1]);
    return 1;
}
